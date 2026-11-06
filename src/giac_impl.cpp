/**
 * @file giac_impl.cpp
 * @brief Implementation of GIAC wrapper, includes GIAC headers
 *
 * This translation unit includes GIAC headers and implements the
 * interface defined in giac_impl.h. It is kept separate from jlcxx
 * to avoid header conflicts.
 */

// Include GIAC headers first, before any potential conflicts
// For GIAC 2.0.0, headers are directly in include path (not in giac/ subdirectory)
#include <config.h>
#include <giac.h>
#include <input_lexer.h>
#include <algorithm>
#include <set>
#include <limits>

#include "giac_impl.h"
#include <mutex>
#include <cstring>
#include <unordered_map>

namespace giac_julia {

// ============================================================================
// Implementation structs (hidden from header)
// ============================================================================

struct GiacContextImpl {
    // Context is never freed to avoid destruction order issues with GIAC's
    // internal reference counting. This is an intentional leak to prevent crashes.
    giac::context* ctx;
    std::function<void(const std::string&)> warning_handler;
    // Wrapper-internal per-context timeout. GIAC has no per-context timeout
    // accessor (only the process-global caseval_maxtime), so this lives on
    // GiacContextImpl. Currently round-trip only — no evaluation interruption.
    double timeout_seconds_;

    GiacContextImpl() : ctx(new giac::context()),
                        warning_handler(nullptr),
                        timeout_seconds_(30.0) {}
    // Destructor intentionally does NOT delete ctx
};

struct GenImpl {
    giac::gen g;

    GenImpl() : g() {}
    explicit GenImpl(const giac::gen& gen) : g(gen) {}
    explicit GenImpl(giac::gen&& gen) : g(std::move(gen)) {}
};

// ============================================================================
// Thread-local global context (fixes context lifetime issues)
// ============================================================================
// GIAC expects contexts to outlive giac::gen objects that reference them.
// The context is intentionally never freed to avoid destruction order issues
// during process/thread exit. This is a deliberate memory leak that prevents
// use-after-free crashes when giac::gen destructors run after context cleanup.
//
// Thread-local storage ensures:
// 1. Context persists for the lifetime of the program (never freed)
// 2. Thread-safe operation without explicit locking

namespace {
    giac::context& get_thread_local_context() {
        // Use pointer with intentional leak - never deleted
        // This prevents crashes during static destruction order
        thread_local giac::context* ctx = new giac::context();
        return *ctx;
    }
}

// ============================================================================
// Series flags
// ============================================================================
// GIAC's limit and series set bit 3 of series_flags while they work and clear it again only on a
// normal return: when the computation throws, the bit stays set, and with it atan stops rewriting
// its argument through tan, for the rest of the session. Each call into GIAC restores the bit to
// what it was when the call began.

namespace {
    class SeriesFlagsGuard {
    public:
        explicit SeriesFlagsGuard(giac::context* ctx) : ctx_(ctx), bit_(giac::series_flags(ctx) & 8) {}
        ~SeriesFlagsGuard() {
            int& flags = giac::series_flags(ctx_);
            flags = (flags & ~8) | bit_;
        }
        SeriesFlagsGuard(const SeriesFlagsGuard&) = delete;
        SeriesFlagsGuard& operator=(const SeriesFlagsGuard&) = delete;

    private:
        giac::context* ctx_;
        int bit_;
    };
}

int get_series_flags() {
    return giac::series_flags(&get_thread_local_context());
}

// ============================================================================
// Library initialization
// ============================================================================

namespace {
    std::once_flag giac_init_flag;
    bool giac_initialized = false;

    void initialize_giac_library() {
        std::call_once(giac_init_flag, []() {
            // Initialize the thread-local context for the main thread
            (void)get_thread_local_context();
            giac_initialized = true;
        });
    }
}

// ============================================================================
// Version Functions
// ============================================================================

std::string get_giac_version() {
#ifdef GIAC_VERSION
    return GIAC_VERSION;
#elif defined(VERSION)
    return VERSION;
#else
    return "unknown";
#endif
}

std::string get_wrapper_version() {
#ifdef WRAPPER_VERSION
    return WRAPPER_VERSION;
#else
    return "unknown";
#endif
}

bool check_giac_available() {
    initialize_giac_library();
    return giac_initialized;
}

// ============================================================================
// Configuration Functions
// ============================================================================

void set_xcasroot(const std::string& path) {
    // giac::xcasroot() returns a reference to a static string
    giac::xcasroot() = path;
}

std::string get_xcasroot() {
    return giac::xcasroot();
}

bool init_help(const std::string& aide_cas_path) {
    // Pre-initialize the help database with the correct path
    // This prevents GIAC from trying fallback paths that print error messages
    int helpitems = 0;
    if (!giac::vector_aide_ptr()) {
        giac::vector_aide_ptr() = new std::vector<giac::aide>;
    }
    *giac::vector_aide_ptr() = giac::readhelp(aide_cas_path.c_str(), helpitems, false);
    return helpitems > 0;
}

std::string list_commands() {
    std::string result;
    if (giac::vector_aide_ptr() && !giac::vector_aide_ptr()->empty()) {
        const auto& aides = *giac::vector_aide_ptr();
        for (const auto& a : aides) {
            if (!a.cmd_name.empty()) {
                if (!result.empty()) result += '\n';
                result += a.cmd_name;
            }
        }
    }
    return result;
}

int help_count() {
    if (giac::vector_aide_ptr()) {
        return static_cast<int>(giac::vector_aide_ptr()->size());
    }
    return 0;
}

// ============================================================================
// Expression Evaluation
// ============================================================================

Gen giac_eval(const std::string& expr) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen parsed = giac::gen(expr, &ctx);
    giac::gen result = giac::eval(parsed, &ctx);
    return Gen(std::make_unique<GenImpl>(result));
}

Gen giac_eval(const std::string& expr, GiacContext& ctx) {
    initialize_giac_library();
    giac::context* gctx = ctx.impl_->ctx;
    SeriesFlagsGuard series_guard(gctx);
    try {
        giac::gen parsed = giac::gen(expr, gctx);
        giac::gen result = giac::eval(parsed, gctx);
        return Gen(std::make_unique<GenImpl>(result));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("GIAC evaluation error: ") + e.what());
    }
}

// ============================================================================
// Generic Dispatch Implementation
// ============================================================================

namespace {
    // A command's function, looked up by name once: parsing the name on every call costs more
    // than many commands. Only built-in functions are kept, since a user function can change.
    giac::gen lookup_function(const std::string& name, giac::context* ctx) {
        static std::mutex lock;
        static std::unordered_map<std::string, giac::gen> functions;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto it = functions.find(name);
            if (it != functions.end()) return it->second;
        }
        giac::gen f(name, ctx);
        if (f.type == giac::_FUNC) {
            std::lock_guard<std::mutex> guard(lock);
            functions.emplace(name, f);
        }
        return f;
    }
}

Gen apply_func0(const std::string& name) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen func_gen = lookup_function(name, &ctx);

    if (func_gen.type == giac::_FUNC) {
        // Direct symbolic construction with no arguments
        giac::gen expr = giac::symbolic(*func_gen._FUNCptr, giac::gen(giac::vecteur(), giac::_SEQ__VECT));
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx)));
    } else {
        // Fallback: string-based evaluation
        std::string expr_str = name + "()";
        giac::gen parsed(expr_str, &ctx);
        return Gen(std::make_unique<GenImpl>(giac::eval(parsed, &ctx)));
    }
}

Gen apply_func1(const std::string& name, const Gen& arg) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen func_gen = lookup_function(name, &ctx);

    if (func_gen.type == giac::_FUNC) {
        // Direct symbolic construction - no serialization
        giac::gen expr = giac::symbolic(*func_gen._FUNCptr, arg.impl_->g);
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx)));
    } else {
        // Fallback: string-based evaluation
        std::string expr_str = name + "(" + arg.to_string() + ")";
        giac::gen parsed(expr_str, &ctx);
        return Gen(std::make_unique<GenImpl>(giac::eval(parsed, &ctx)));
    }
}

Gen apply_func2(const std::string& name, const Gen& arg1, const Gen& arg2) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen func_gen = lookup_function(name, &ctx);

    if (func_gen.type == giac::_FUNC) {
        // Create sequence with two arguments
        giac::vecteur args;
        args.push_back(arg1.impl_->g);
        args.push_back(arg2.impl_->g);
        giac::gen seq = giac::gen(args, giac::_SEQ__VECT);
        giac::gen expr = giac::symbolic(*func_gen._FUNCptr, seq);
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx)));
    } else {
        // Fallback
        std::string expr_str = name + "(" + arg1.to_string() + "," + arg2.to_string() + ")";
        giac::gen parsed(expr_str, &ctx);
        return Gen(std::make_unique<GenImpl>(giac::eval(parsed, &ctx)));
    }
}

Gen apply_func3(const std::string& name, const Gen& arg1, const Gen& arg2, const Gen& arg3) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen func_gen = lookup_function(name, &ctx);

    if (func_gen.type == giac::_FUNC) {
        // Create sequence with three arguments
        giac::vecteur args;
        args.push_back(arg1.impl_->g);
        args.push_back(arg2.impl_->g);
        args.push_back(arg3.impl_->g);
        giac::gen seq = giac::gen(args, giac::_SEQ__VECT);
        giac::gen expr = giac::symbolic(*func_gen._FUNCptr, seq);
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx)));
    } else {
        // Fallback
        std::string expr_str = name + "(" + arg1.to_string() + "," + arg2.to_string() + "," + arg3.to_string() + ")";
        giac::gen parsed(expr_str, &ctx);
        return Gen(std::make_unique<GenImpl>(giac::eval(parsed, &ctx)));
    }
}

Gen apply_funcN(const std::string& name, const std::vector<Gen>& args) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen func_gen = lookup_function(name, &ctx);

    if (func_gen.type == giac::_FUNC) {
        // Create sequence with N arguments
        giac::vecteur giac_args;
        for (const auto& arg : args) {
            giac_args.push_back(arg.impl_->g);
        }
        giac::gen seq = giac::gen(giac_args, giac::_SEQ__VECT);
        giac::gen expr = giac::symbolic(*func_gen._FUNCptr, seq);
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx)));
    } else {
        // Fallback: string concatenation
        std::string expr_str = name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) expr_str += ",";
            expr_str += args[i].to_string();
        }
        expr_str += ")";
        giac::gen parsed(expr_str, &ctx);
        return Gen(std::make_unique<GenImpl>(giac::eval(parsed, &ctx)));
    }
}

// ============================================================================
// Interruption
// ============================================================================

void giac_interrupt() {
    giac::ctrl_c = true;
}

void giac_clear_interrupt() {
    giac::ctrl_c = false;
    giac::interrupted = false;
}

bool giac_interrupt_pending() {
    return giac::ctrl_c || giac::interrupted;
}

// ============================================================================
// Batched export
// ============================================================================

namespace {
    thread_local std::vector<giac::gen> export_opaque;

    void put_u8(std::string& o, uint8_t b) { o.push_back(static_cast<char>(b)); }
    template <typename T> void put(std::string& o, T v) { o.append(reinterpret_cast<const char*>(&v), sizeof(T)); }
    void put_str(std::string& o, const std::string& s) { put<uint32_t>(o, static_cast<uint32_t>(s.size())); o += s; }

    void put_opaque(std::string& o, const giac::gen& g) {
        put_u8(o, 9);
        put<uint32_t>(o, static_cast<uint32_t>(export_opaque.size()));
        export_opaque.push_back(g);
    }

    bool is_integer_gen(const giac::gen& g) { return g.type == giac::_INT_ || g.type == giac::_ZINT; }
    bool is_number_gen(const giac::gen& g) {
        return is_integer_gen(g) || g.type == giac::_DOUBLE_ ||
               (g.type == giac::_FRAC && is_integer_gen(g._FRACptr->num) && is_integer_gen(g._FRACptr->den));
    }

    bool is_identifier_like(const std::string& h) {
        if (h.empty() || !(std::isalpha(static_cast<unsigned char>(h[0])) || h[0] == '_')) return false;
        for (char c : h)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
        return true;
    }

    std::string head_name(const giac::gen& g, giac::context* ctx) { return g._SYMBptr->sommet.ptr()->print(ctx); }

    // a product of powers of integers, as ifactor writes 2^3*3^2*5: a form, kept as GIAC has it
    bool is_integer_power(const giac::gen& g, giac::context* ctx) {
        if (g.type != giac::_SYMB || head_name(g, ctx) != "^") return false;
        const giac::gen& f = g._SYMBptr->feuille;
        return f.type == giac::_VECT && f._VECTptr->size() == 2 && is_integer_gen((*f._VECTptr)[0]) && is_integer_gen((*f._VECTptr)[1]);
    }
    bool is_factorization(const giac::gen& g, giac::context* ctx) {
        if (is_integer_power(g, ctx)) return true;
        if (g.type != giac::_SYMB || head_name(g, ctx) != "*") return false;
        const giac::gen& f = g._SYMBptr->feuille;
        if (f.type != giac::_VECT) return false;
        bool power = false;
        for (const giac::gen& x : *f._VECTptr) {
            if (is_integer_power(x, ctx)) power = true;
            else if (!is_integer_gen(x)) return false;
        }
        return power;
    }

    // the arguments of a symbolic node: a sequence spliced in, recursively
    void push_args(std::vector<const giac::gen*>& out, const giac::gen& g) {
        if (g.type == giac::_VECT && g.subtype == giac::_SEQ__VECT) {
            for (const giac::gen& x : *g._VECTptr) push_args(out, x);
        } else {
            out.push_back(&g);
        }
    }

    void write_gen(std::string& o, const giac::gen& g, const std::set<std::string>& heads, giac::context* ctx) {
        switch (g.type) {
        case giac::_INT_:
            put_u8(o, 1);
            put<int64_t>(o, static_cast<int64_t>(g.val));
            return;
        case giac::_ZINT: {
            put_u8(o, 2);
            put<int32_t>(o, mpz_sgn(*g._ZINTptr));
            size_t count = 0;
            void* data = mpz_sgn(*g._ZINTptr) == 0 ? nullptr : mpz_export(nullptr, &count, 1, 1, 1, 0, *g._ZINTptr);
            put<uint32_t>(o, static_cast<uint32_t>(count));
            if (data) {
                o.append(static_cast<const char*>(data), count);
                void (*free_fn)(void*, size_t);
                mp_get_memory_functions(nullptr, nullptr, &free_fn);
                free_fn(data, count);
            }
            return;
        }
        case giac::_DOUBLE_:
            put_u8(o, 3);
            put<double>(o, g._DOUBLE_val);
            return;
        case giac::_IDNT:
            put_u8(o, 4);
            put_str(o, g.print(ctx));
            return;
        case giac::_FRAC:
            if (is_integer_gen(g._FRACptr->num) && is_integer_gen(g._FRACptr->den)) {
                put_u8(o, 5);
                write_gen(o, g._FRACptr->num, heads, ctx);
                write_gen(o, g._FRACptr->den, heads, ctx);
                return;
            }
            break;
        case giac::_CPLX:
            if (is_number_gen(*g._CPLXptr) && is_number_gen(*(g._CPLXptr + 1))) {
                put_u8(o, 6);
                write_gen(o, *g._CPLXptr, heads, ctx);
                write_gen(o, *(g._CPLXptr + 1), heads, ctx);
                return;
            }
            break;
        case giac::_SYMB: {
            std::string h = head_name(g, ctx);
            bool arithmetic = h == "+" || h == "*" || h == "-" || h == "inv" || h == "^" || h == "/";
            if ((arithmetic || heads.count(h) || is_identifier_like(h)) && !is_factorization(g, ctx)) {
                const giac::gen& f = g._SYMBptr->feuille;
                std::vector<const giac::gen*> args;
                if (f.type == giac::_VECT && (f.subtype == giac::_SEQ__VECT || f.subtype == giac::_SORTED__VECT)) {
                    for (const giac::gen& x : *f._VECTptr) push_args(args, x);
                } else {
                    args.push_back(&f);
                }
                put_u8(o, 7);
                put_str(o, h);
                put<uint32_t>(o, static_cast<uint32_t>(args.size()));
                for (const giac::gen* a : args) write_gen(o, *a, heads, ctx);
                return;
            }
            break;
        }
        default:
            break;
        }
        put_opaque(o, g);
    }
}

std::string gen_export(const Gen& g, const std::string& heads) {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    std::set<std::string> accepted;
    size_t start = 0;
    while (start <= heads.size()) {
        size_t end = heads.find('\n', start);
        if (end == std::string::npos) end = heads.size();
        if (end > start) accepted.insert(heads.substr(start, end - start));
        start = end + 1;
    }
    export_opaque.clear();
    std::string out;
    write_gen(out, g.impl_->g, accepted, &ctx);
    return out;
}

Gen gen_export_opaque(int32_t i) {
    if (i < 0 || static_cast<size_t>(i) >= export_opaque.size()) {
        throw std::out_of_range("no such opaque object in the last export");
    }
    return Gen(std::make_unique<GenImpl>(export_opaque[static_cast<size_t>(i)]));
}

// ============================================================================
// Batched import
// ============================================================================

namespace {
    struct ProgramReader {
        const std::string& p;
        size_t pos = 0;
        template <typename T> T get() {
            if (pos + sizeof(T) > p.size()) throw std::runtime_error("gen_import: program ends early");
            T v;
            std::memcpy(&v, p.data() + pos, sizeof(T));
            pos += sizeof(T);
            return v;
        }
        std::string str() {
            uint32_t n = get<uint32_t>();
            if (pos + n > p.size()) throw std::runtime_error("gen_import: program ends early");
            std::string s = p.substr(pos, n);
            pos += n;
            return s;
        }
    };

    giac::gen apply_unevaluated(const std::string& name, const giac::vecteur& args, giac::context* ctx) {
        const giac::unary_function_ptr* f = nullptr;
        if (name == "+") f = giac::at_plus;
        else if (name == "-") f = giac::at_neg;
        else if (name == "*") f = giac::at_prod;
        else if (name == "/") f = giac::at_division;
        else if (name == "^") f = giac::at_pow;
        else {
            giac::gen g = lookup_function(name, ctx);
            if (g.type != giac::_FUNC) throw std::runtime_error("Unknown function or operator: " + name);
            f = g._FUNCptr;
        }
        if (args.size() == 1) return giac::symbolic(*f, args[0]);
        return giac::symbolic(*f, giac::gen(args, giac::_SEQ__VECT));
    }

    giac::gen read_gen(ProgramReader& r, const std::vector<giac::gen>& opaque, giac::context* ctx) {
        uint8_t tag = r.get<uint8_t>();
        switch (tag) {
        case 1: {
            int64_t v = r.get<int64_t>();
            if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) return giac::gen(static_cast<int>(v));
            return giac::gen(static_cast<long long>(v));
        }
        case 2: {
            int32_t sign = r.get<int32_t>();
            uint32_t n = r.get<uint32_t>();
            if (r.pos + n > r.p.size()) throw std::runtime_error("gen_import: program ends early");
            if (n == 0 || sign == 0) { r.pos += n; return giac::gen(0); }
            mpz_t z;
            mpz_init(z);
            mpz_import(z, n, 1, 1, 1, 0, r.p.data() + r.pos);
            r.pos += n;
            if (sign < 0) mpz_neg(z, z);
            giac::gen g(z);
            mpz_clear(z);
            return g;
        }
        case 3: return giac::gen(r.get<double>());
        case 4: return giac::gen(giac::identificateur(r.str()));
        case 10: return giac::eval(giac::gen(r.str(), ctx), ctx);
        case 5: {
            giac::gen num = read_gen(r, opaque, ctx);
            giac::gen den = read_gen(r, opaque, ctx);
            return giac::fraction(num, den);
        }
        case 6: {
            giac::gen re = read_gen(r, opaque, ctx);
            giac::gen im = read_gen(r, opaque, ctx);
            return giac::gen(re, im);
        }
        case 9: {
            uint32_t i = r.get<uint32_t>();
            if (i >= opaque.size()) throw std::runtime_error("gen_import: no such opaque object");
            return opaque[i];
        }
        case 11: {
            // the terms added at once: adding one at a time rebuilds the sum at every step
            uint32_t k = r.get<uint32_t>();
            if (k == 0) return giac::gen(0);
            giac::vecteur terms;
            terms.reserve(k);
            for (uint32_t i = 0; i < k; ++i) terms.push_back(read_gen(r, opaque, ctx));
            if (k == 1) return terms[0];
            return giac::_plus(giac::gen(terms, giac::_SEQ__VECT), ctx);
        }
        case 13: {
            giac::gen b = read_gen(r, opaque, ctx);
            giac::gen e = read_gen(r, opaque, ctx);
            giac::vecteur args;
            args.push_back(b);
            args.push_back(e);
            return giac::eval(giac::symbolic(giac::at_pow, giac::gen(args, giac::_SEQ__VECT)), ctx);
        }
        case 14: {
            giac::gen a = read_gen(r, opaque, ctx);
            giac::gen b = read_gen(r, opaque, ctx);
            return a * b;
        }
        case 15: {
            giac::gen a = read_gen(r, opaque, ctx);
            giac::gen b = read_gen(r, opaque, ctx);
            return a / b;
        }
        case 16: return -read_gen(r, opaque, ctx);
        case 7: {
            std::string name = r.str();
            uint32_t k = r.get<uint32_t>();
            giac::vecteur args;
            args.reserve(k);
            for (uint32_t i = 0; i < k; ++i) args.push_back(read_gen(r, opaque, ctx));
            return apply_unevaluated(name, args, ctx);
        }
        case 17: {
            uint32_t k = r.get<uint32_t>();
            giac::vecteur items;
            items.reserve(k);
            for (uint32_t i = 0; i < k; ++i) items.push_back(read_gen(r, opaque, ctx));
            return giac::gen(items, giac::_SEQ__VECT);
        }
        default:
            throw std::runtime_error("gen_import: unknown tag " + std::to_string(tag));
        }
    }
}

Gen gen_import(const std::string& program, const std::vector<Gen>& opaque) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    std::vector<giac::gen> objects;
    objects.reserve(opaque.size());
    for (const Gen& o : opaque) objects.push_back(o.impl_->g);
    ProgramReader r{program};
    giac::gen g = read_gen(r, objects, &ctx);
    if (r.pos != program.size()) throw std::runtime_error("gen_import: program has trailing bytes");
    return Gen(std::make_unique<GenImpl>(g));
}

// ============================================================================
// Function Listing Implementation
// ============================================================================

std::string list_builtin_functions() {
    initialize_giac_library();
    std::string result;
    for (auto it = giac::builtin_lexer_functions_begin();
         it != giac::builtin_lexer_functions_end(); ++it) {
        if (!result.empty()) result += '\n';
        result += it->first;
    }
    return result;
}

int builtin_function_count() {
    return giac::builtin_lexer_functions_number;
}

std::string list_all_functions() {
    initialize_giac_library();

    // Use a set for deduplication and automatic sorting
    std::set<std::string> all_funcs;

    // Add documented commands from help database
    if (giac::vector_aide_ptr() && !giac::vector_aide_ptr()->empty()) {
        const auto& aides = *giac::vector_aide_ptr();
        for (const auto& a : aides) {
            if (!a.cmd_name.empty()) {
                all_funcs.insert(a.cmd_name);
            }
        }
    }

    // Add builtin lexer functions
    for (auto it = giac::builtin_lexer_functions_begin();
         it != giac::builtin_lexer_functions_end(); ++it) {
        all_funcs.insert(it->first);
    }

    // Convert to newline-separated string
    std::string result;
    for (const auto& func : all_funcs) {
        if (!result.empty()) result += '\n';
        result += func;
    }
    return result;
}

// ============================================================================
// Tier 1 Direct Wrappers (High Performance - No Name Lookup)
// ============================================================================

// Helper macro for single-argument Tier 1 wrappers
#define TIER1_SINGLE_ARG(name, at_symbol) \
    Gen giac_##name(const Gen& arg) { \
        initialize_giac_library(); \
        giac::context& ctx = get_thread_local_context(); \
        SeriesFlagsGuard series_guard(&ctx); \
        giac::gen expr = giac::symbolic(giac::at_symbol, arg.impl_->g); \
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx))); \
    }

// Helper macro for two-argument Tier 1 wrappers
#define TIER1_TWO_ARG(name, at_symbol) \
    Gen giac_##name(const Gen& arg1, const Gen& arg2) { \
        initialize_giac_library(); \
        giac::context& ctx = get_thread_local_context(); \
        SeriesFlagsGuard series_guard(&ctx); \
        giac::vecteur args; \
        args.push_back(arg1.impl_->g); \
        args.push_back(arg2.impl_->g); \
        giac::gen seq = giac::gen(args, giac::_SEQ__VECT); \
        giac::gen expr = giac::symbolic(giac::at_symbol, seq); \
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx))); \
    }

// Helper macro for three-argument Tier 1 wrappers
#define TIER1_THREE_ARG(name, at_symbol) \
    Gen giac_##name(const Gen& arg1, const Gen& arg2, const Gen& arg3) { \
        initialize_giac_library(); \
        giac::context& ctx = get_thread_local_context(); \
        SeriesFlagsGuard series_guard(&ctx); \
        giac::vecteur args; \
        args.push_back(arg1.impl_->g); \
        args.push_back(arg2.impl_->g); \
        args.push_back(arg3.impl_->g); \
        giac::gen seq = giac::gen(args, giac::_SEQ__VECT); \
        giac::gen expr = giac::symbolic(giac::at_symbol, seq); \
        return Gen(std::make_unique<GenImpl>(giac::eval(expr, &ctx))); \
    }

// Trigonometry
TIER1_SINGLE_ARG(sin, at_sin)
TIER1_SINGLE_ARG(cos, at_cos)
TIER1_SINGLE_ARG(tan, at_tan)
TIER1_SINGLE_ARG(asin, at_asin)
TIER1_SINGLE_ARG(acos, at_acos)
TIER1_SINGLE_ARG(atan, at_atan)

// Exponential / Logarithm
TIER1_SINGLE_ARG(exp, at_exp)
TIER1_SINGLE_ARG(ln, at_ln)
TIER1_SINGLE_ARG(log10, at_log10)
TIER1_SINGLE_ARG(sqrt, at_sqrt)

// Arithmetic
TIER1_SINGLE_ARG(abs, at_abs)
TIER1_SINGLE_ARG(sign, at_sign)
TIER1_SINGLE_ARG(floor, at_floor)
TIER1_SINGLE_ARG(ceil, at_ceil)

// Complex
TIER1_SINGLE_ARG(re, at_re)
TIER1_SINGLE_ARG(im, at_im)
TIER1_SINGLE_ARG(conj, at_conj)

// Algebra
TIER1_SINGLE_ARG(normal, at_normal)
TIER1_SINGLE_ARG(evalf, at_evalf)

// Calculus (multi-argument) - note: differentiation uses at_derive
TIER1_TWO_ARG(diff, at_derive)
TIER1_TWO_ARG(integrate, at_integrate)
TIER1_THREE_ARG(subst, at_subst)
TIER1_TWO_ARG(solve, at_solve)
TIER1_THREE_ARG(limit, at_limit)
TIER1_THREE_ARG(series, at_series)

// Arithmetic (multi-argument)
TIER1_TWO_ARG(gcd, at_gcd)
TIER1_TWO_ARG(lcm, at_lcm)

// Power
TIER1_TWO_ARG(pow, at_pow)

#undef TIER1_SINGLE_ARG
#undef TIER1_TWO_ARG
#undef TIER1_THREE_ARG

// ============================================================================
// GiacContext Implementation
// ============================================================================

GiacContext::GiacContext() : impl_(std::make_unique<GiacContextImpl>()) {
    initialize_giac_library();
}

GiacContext::~GiacContext() = default;

GiacContext::GiacContext(GiacContext&& other) noexcept = default;
GiacContext& GiacContext::operator=(GiacContext&& other) noexcept = default;

std::string GiacContext::eval(const std::string& input) {
    SeriesFlagsGuard series_guard(impl_->ctx);
    try {
        giac::gen result = giac::eval(giac::gen(input, impl_->ctx), impl_->ctx);
        return result.print(impl_->ctx);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("GIAC evaluation error: ") + e.what());
    }
}

void GiacContext::set_variable(const std::string& name, const std::string& value) {
    SeriesFlagsGuard series_guard(impl_->ctx);
    try {
        giac::gen val(value, impl_->ctx);
        giac::sto(val, giac::gen(name, impl_->ctx), impl_->ctx);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to set variable: ") + e.what());
    }
}

std::string GiacContext::get_variable(const std::string& name) {
    SeriesFlagsGuard series_guard(impl_->ctx);
    try {
        giac::gen result = giac::eval(giac::gen(name, impl_->ctx), impl_->ctx);
        return result.print(impl_->ctx);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to get variable: ") + e.what());
    }
}

void GiacContext::set_timeout(double seconds) {
    impl_->timeout_seconds_ = seconds;
}

double GiacContext::get_timeout() const {
    return impl_->timeout_seconds_;
}

void GiacContext::set_precision(int digits) {
    giac::decimal_digits(digits, impl_->ctx);
}

int GiacContext::get_precision() const {
    return giac::decimal_digits(impl_->ctx);
}

bool GiacContext::is_complex_mode() const {
    return giac::complex_mode(impl_->ctx);
}

void GiacContext::set_complex_mode(bool enable) {
    giac::complex_mode(enable, impl_->ctx);
}

void GiacContext::set_warning_handler(std::function<void(const std::string&)> handler) {
    impl_->warning_handler = std::move(handler);
}

void GiacContext::clear_warning_handler() {
    impl_->warning_handler = nullptr;
}

// ============================================================================
// Gen Implementation
// ============================================================================

Gen::Gen() : impl_(std::make_unique<GenImpl>()) {}

Gen::Gen(const std::string& expr) : impl_(std::make_unique<GenImpl>()) {
    initialize_giac_library();  // Ensure GIAC is initialized
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    impl_->g = giac::gen(expr, &ctx);
}

Gen::Gen(int64_t value) : impl_(std::make_unique<GenImpl>()) {
    initialize_giac_library();
    // Use int constructor for values that fit in int to preserve _INT_ type
    // GIAC's gen(int) creates type _INT_, while gen(longlong) may create _DOUBLE_
    if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()) {
        impl_->g = giac::gen(static_cast<int>(value));
    } else {
        // For larger values, use longlong (may become ZINT or DOUBLE depending on GIAC)
        impl_->g = giac::gen(static_cast<long long>(value));
    }
}

Gen::Gen(double value) : impl_(std::make_unique<GenImpl>()) {
    initialize_giac_library();
    impl_->g = giac::gen(value);
}

Gen::~Gen() = default;

Gen::Gen(const Gen& other) : impl_(std::make_unique<GenImpl>(other.impl_->g)) {}

Gen& Gen::operator=(const Gen& other) {
    if (this != &other) {
        impl_ = std::make_unique<GenImpl>(other.impl_->g);
    }
    return *this;
}

Gen::Gen(Gen&& other) noexcept = default;
Gen& Gen::operator=(Gen&& other) noexcept = default;

Gen::Gen(std::unique_ptr<GenImpl> impl) : impl_(std::move(impl)) {}

std::string Gen::to_string() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return impl_->g.print(&ctx);
}

int Gen::type() const {
    return impl_->g.type;
}

int32_t Gen::subtype() const {
    return impl_->g.subtype;
}

std::string Gen::type_name() const {
    switch (impl_->g.type) {
        case giac::_INT_: return "integer";
        case giac::_DOUBLE_: return "double";
        case giac::_ZINT: return "bigint";
        case giac::_REAL: return "real";
        case giac::_CPLX: return "complex";
        case giac::_IDNT: return "identifier";
        case giac::_SYMB: return "symbolic";
        case giac::_VECT: return "vector";
        case giac::_POLY: return "polynomial";
        case giac::_FRAC: return "fraction";
        case giac::_STRNG: return "string";
        case giac::_FUNC: return "function";
        case giac::_MAP: return "map";
        default: return "unknown";
    }
}

// ============================================================================
// Typed Accessors
// ============================================================================

int64_t Gen::to_int64() const {
    // REQ-C21: Throws if not an integer type
    if (impl_->g.type != giac::_INT_) {
        throw std::runtime_error("gen is not an integer");
    }
    return static_cast<int64_t>(impl_->g.val);
}

int32_t Gen::to_int32() const {
    // REQ-C21: Throws if not an integer type
    if (impl_->g.type != giac::_INT_) {
        throw std::runtime_error("gen is not an integer");
    }
    return static_cast<int32_t>(impl_->g.val);
}

double Gen::to_double() const {
    // REQ-C22, C23, C24: Returns double for _INT_ or _DOUBLE_, throws otherwise
    if (impl_->g.type == giac::_DOUBLE_) {
        return impl_->g._DOUBLE_val;
    } else if (impl_->g.type == giac::_INT_) {
        return static_cast<double>(impl_->g.val);
    } else {
        throw std::runtime_error("gen is not a numeric type");
    }
}

std::string Gen::zint_to_string() const {
    // Convert GMP big integer to string
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    giac::gen bigint_gen = impl_->g;
    return bigint_gen.print(&ctx);
}

int Gen::zint_sign() const {
    // Return sign of ZINT: -1 (negative), 0 (zero), 1 (positive)
    if (impl_->g.type != giac::_ZINT) {
        throw std::runtime_error("gen is not a ZINT");
    }
    return mpz_sgn(*impl_->g._ZINTptr);
}

std::vector<uint8_t> Gen::zint_to_bytes() const {
    // Export ZINT as big-endian byte array (absolute value only)
    // Sign must be obtained separately via zint_sign()
    if (impl_->g.type != giac::_ZINT) {
        throw std::runtime_error("gen is not a ZINT");
    }

    mpz_t* z = impl_->g._ZINTptr;
    if (mpz_sgn(*z) == 0) {
        return {};  // Empty vector for zero
    }

    size_t count;
    // mpz_export(rop, countp, order=1 (MSB first), size=1 (byte), endian=1 (big), nails=0, op)
    void* data = mpz_export(NULL, &count, 1, 1, 1, 0, *z);

    std::vector<uint8_t> result(static_cast<uint8_t*>(data),
                                 static_cast<uint8_t*>(data) + count);
    free(data);  // mpz_export allocates with malloc when rop is NULL
    return result;
}

Gen Gen::cplx_re() const {
    // REQ-C50, C51: Returns real part for _CPLX_, self for non-complex
    if (impl_->g.type == giac::_CPLX) {
        return Gen(std::make_unique<GenImpl>(*impl_->g._CPLXptr));
    } else {
        // REQ-C51: Return self for non-complex
        return Gen(std::make_unique<GenImpl>(impl_->g));
    }
}

Gen Gen::cplx_im() const {
    // REQ-C52, C53: Returns imaginary part for _CPLX_, 0 for non-complex
    if (impl_->g.type == giac::_CPLX) {
        return Gen(std::make_unique<GenImpl>(*(impl_->g._CPLXptr + 1)));
    } else {
        // REQ-C53: Return 0 for non-complex
        return Gen(std::make_unique<GenImpl>(giac::gen(0)));
    }
}

Gen Gen::frac_num() const {
    // REQ-C40, C41, C44: Returns numerator for _FRAC_, self for _INT_/_ZINT_, throws otherwise
    int t = impl_->g.type;
    if (t == giac::_FRAC) {
        return Gen(std::make_unique<GenImpl>(impl_->g._FRACptr->num));
    } else if (t == giac::_INT_ || t == giac::_ZINT) {
        // REQ-C41: Return self for integers
        return Gen(std::make_unique<GenImpl>(impl_->g));
    } else {
        throw std::runtime_error("gen is not a fraction or integer");
    }
}

Gen Gen::frac_den() const {
    // REQ-C42, C43, C44: Returns denominator for _FRAC_, 1 for _INT_/_ZINT_, throws otherwise
    int t = impl_->g.type;
    if (t == giac::_FRAC) {
        return Gen(std::make_unique<GenImpl>(impl_->g._FRACptr->den));
    } else if (t == giac::_INT_ || t == giac::_ZINT) {
        // REQ-C43: Return 1 for integers
        return Gen(std::make_unique<GenImpl>(giac::gen(1)));
    } else {
        throw std::runtime_error("gen is not a fraction or integer");
    }
}

int32_t Gen::vect_size() const {
    // REQ-C31: Throws if not a vector type
    if (impl_->g.type != giac::_VECT) {
        throw std::runtime_error("gen is not a vector");
    }
    return static_cast<int32_t>(impl_->g._VECTptr->size());
}

Gen Gen::vect_at(int32_t i) const {
    // REQ-C34: Throws if not a vector type
    if (impl_->g.type != giac::_VECT) {
        throw std::runtime_error("gen is not a vector");
    }
    const giac::vecteur& v = *impl_->g._VECTptr;
    // REQ-C33: Throws if index out of bounds
    if (i < 0 || static_cast<size_t>(i) >= v.size()) {
        throw std::runtime_error("index out of bounds");
    }
    return Gen(std::make_unique<GenImpl>(v[i]));
}

std::string Gen::symb_sommet_name() const {
    // REQ-C60, C62: Returns function name for _SYMB_, throws otherwise
    if (impl_->g.type != giac::_SYMB) {
        throw std::runtime_error("gen is not symbolic");
    }
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return impl_->g._SYMBptr->sommet.ptr()->print(&ctx);
}

Gen Gen::symb_feuille() const {
    // REQ-C61, C62: Returns argument for _SYMB_, throws otherwise
    if (impl_->g.type != giac::_SYMB) {
        throw std::runtime_error("gen is not symbolic");
    }
    return Gen(std::make_unique<GenImpl>(impl_->g._SYMBptr->feuille));
}

std::string Gen::idnt_name() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return impl_->g.print(&ctx);  // For identifiers, print gives the name
}

std::string Gen::strng_value() const {
    // REQ-C70, C74: Returns the string for _STRNG, throws otherwise
    if (impl_->g.type != giac::_STRNG) {
        throw std::runtime_error("gen is not a string");
    }
    return *impl_->g._STRNGptr;
}

int32_t Gen::map_size() const {
    // REQ-C71, C74: Returns entry count for _MAP, throws otherwise
    if (impl_->g.type != giac::_MAP) {
        throw std::runtime_error("gen is not a map");
    }
    return static_cast<int32_t>(impl_->g._MAPptr->size());
}

Gen Gen::map_keys() const {
    // REQ-C72, C74: Returns the keys for _MAP, throws otherwise
    if (impl_->g.type != giac::_MAP) {
        throw std::runtime_error("gen is not a map");
    }
    giac::vecteur keys;
    for (const auto& pair : *impl_->g._MAPptr) {
        keys.push_back(pair.first);
    }
    return Gen(std::make_unique<GenImpl>(giac::gen(keys)));
}

Gen Gen::map_values() const {
    // REQ-C73, C74: Returns the values for _MAP, throws otherwise
    if (impl_->g.type != giac::_MAP) {
        throw std::runtime_error("gen is not a map");
    }
    giac::vecteur values;
    for (const auto& pair : *impl_->g._MAPptr) {
        values.push_back(pair.second);
    }
    return Gen(std::make_unique<GenImpl>(giac::gen(values)));
}

// ============================================================================
// Predicates
// ============================================================================

bool Gen::is_zero() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return giac::is_zero(impl_->g, &ctx);
}

bool Gen::is_one() const {
    // Check if value equals 1
    if (impl_->g.type == giac::_INT_) {
        return impl_->g.val == 1;
    }
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return giac::is_zero(impl_->g - giac::gen(1), &ctx);
}

bool Gen::is_integer() const {
    return impl_->g.type == giac::_INT_ || impl_->g.type == giac::_ZINT;
}

bool Gen::is_approx() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return giac::has_evalf(impl_->g, impl_->g, 1, &ctx);
}

// ============================================================================
// Type Predicates (Feature 004: REQ-C10..C17)
// ============================================================================

bool Gen::is_numeric() const {
    // REQ-C11: true iff type is _INT_, _DOUBLE_, _ZINT_, or _REAL_
    int t = impl_->g.type;
    return t == giac::_INT_ || t == giac::_DOUBLE_ ||
           t == giac::_ZINT || t == giac::_REAL;
}

bool Gen::is_vector() const {
    // REQ-C12: true iff type is _VECT_
    return impl_->g.type == giac::_VECT;
}

bool Gen::is_symbolic() const {
    // REQ-C13: true iff type is _SYMB_
    return impl_->g.type == giac::_SYMB;
}

bool Gen::is_identifier() const {
    // REQ-C14: true iff type is _IDNT_
    return impl_->g.type == giac::_IDNT;
}

bool Gen::is_fraction() const {
    // REQ-C15: true iff type is _FRAC_
    return impl_->g.type == giac::_FRAC;
}

bool Gen::is_complex() const {
    // REQ-C16: true iff type is _CPLX_
    return impl_->g.type == giac::_CPLX;
}

bool Gen::is_string() const {
    // REQ-C17: true iff type is _STRNG_
    return impl_->g.type == giac::_STRNG;
}

Gen Gen::eval() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return Gen(std::make_unique<GenImpl>(giac::eval(impl_->g, &ctx)));
}

Gen Gen::simplify() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return Gen(std::make_unique<GenImpl>(giac::simplify(impl_->g, &ctx)));
}

Gen Gen::expand() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    return Gen(std::make_unique<GenImpl>(giac::expand(impl_->g, &ctx)));
}

Gen Gen::factor() const {
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);
    // Use _factor which is available in most GIAC versions
    giac::gen result = giac::eval(giac::symbolic(giac::at_factor, impl_->g), &ctx);
    return Gen(std::make_unique<GenImpl>(result));
}

Gen Gen::operator+(const Gen& other) const {
    return Gen(std::make_unique<GenImpl>(impl_->g + other.impl_->g));
}

Gen Gen::operator-(const Gen& other) const {
    return Gen(std::make_unique<GenImpl>(impl_->g - other.impl_->g));
}

Gen Gen::operator*(const Gen& other) const {
    return Gen(std::make_unique<GenImpl>(impl_->g * other.impl_->g));
}

Gen Gen::operator/(const Gen& other) const {
    return Gen(std::make_unique<GenImpl>(impl_->g / other.impl_->g));
}

Gen Gen::operator-() const {
    return Gen(std::make_unique<GenImpl>(-impl_->g));
}

bool Gen::operator==(const Gen& other) const {
    return impl_->g == other.impl_->g;
}

bool Gen::operator!=(const Gen& other) const {
    return impl_->g != other.impl_->g;
}

void* Gen::get_impl() const {
    return impl_.get();
}

Gen Gen::from_impl(void* impl) {
    auto* genImpl = static_cast<GenImpl*>(impl);
    return Gen(std::make_unique<GenImpl>(genImpl->g));
}

// ============================================================================
// Gen Construction Functions (Feature 051: Direct to_giac)
// ============================================================================

Gen make_identifier(const std::string& name) {
    initialize_giac_library();
    // Create an identifier using giac::identificateur
    giac::gen idnt = giac::identificateur(name);
    return Gen(std::make_unique<GenImpl>(idnt));
}

Gen make_zint_from_bytes(const std::vector<uint8_t>& bytes, int32_t sign) {
    initialize_giac_library();

    if (bytes.empty() || sign == 0) {
        return Gen(std::make_unique<GenImpl>(giac::gen(0)));
    }

    mpz_t z;
    mpz_init(z);

    // mpz_import(rop, count, order=1 (MSB first), size=1 (byte), endian=1 (big), nails=0, op)
    mpz_import(z, bytes.size(), 1, 1, 1, 0, bytes.data());

    if (sign < 0) {
        mpz_neg(z, z);
    }

    giac::gen result(z);
    mpz_clear(z);

    return Gen(std::make_unique<GenImpl>(result));
}

Gen make_symbolic_unevaluated(const std::string& op_name, const std::vector<Gen>& args) {
    initialize_giac_library();
    giac::context& ctx = get_thread_local_context();
    SeriesFlagsGuard series_guard(&ctx);

    // Get the function pointer
    const giac::unary_function_ptr* func_ptr = nullptr;

    // Handle special operators that are not in the function lookup table
    if (op_name == "+") {
        func_ptr = giac::at_plus;
    } else if (op_name == "-") {
        func_ptr = giac::at_neg;  // For unary minus; binary minus is handled as + with negated arg
    } else if (op_name == "*") {
        func_ptr = giac::at_prod;
    } else if (op_name == "/") {
        func_ptr = giac::at_division;
    } else if (op_name == "^") {
        func_ptr = giac::at_pow;
    } else {
        // Lookup function by name
        giac::gen func_gen(op_name, &ctx);

        if (func_gen.type != giac::_FUNC) {
            throw std::runtime_error("Unknown function or operator: " + op_name);
        }
        func_ptr = func_gen._FUNCptr;
    }

    // Handle single argument vs multiple arguments
    if (args.size() == 1) {
        // Single argument - pass directly
        giac::gen expr = giac::symbolic(*func_ptr, args[0].impl_->g);
        return Gen(std::make_unique<GenImpl>(expr));
    } else {
        // Multiple arguments - create sequence
        giac::vecteur giac_args;
        for (const auto& arg : args) {
            giac_args.push_back(arg.impl_->g);
        }
        giac::gen seq = giac::gen(giac_args, giac::_SEQ__VECT);
        giac::gen expr = giac::symbolic(*func_ptr, seq);
        return Gen(std::make_unique<GenImpl>(expr));
    }
}

Gen make_complex(const Gen& re, const Gen& im) {
    initialize_giac_library();
    // GIAC constructor for complex: gen(re, im) creates _CPLX type
    giac::gen result(re.impl_->g, im.impl_->g);
    return Gen(std::make_unique<GenImpl>(result));
}

Gen make_fraction(const Gen& num, const Gen& den) {
    initialize_giac_library();
    // Create fraction using GIAC's fraction type
    giac::gen result = giac::fraction(num.impl_->g, den.impl_->g);
    return Gen(std::make_unique<GenImpl>(result));
}

Gen make_vect(const std::vector<Gen>& elements, int32_t subtype) {
    initialize_giac_library();
    // Create vector with specified subtype
    giac::vecteur v;
    for (const auto& elem : elements) {
        v.push_back(elem.impl_->g);
    }
    giac::gen result(v, static_cast<short>(subtype));
    return Gen(std::make_unique<GenImpl>(result));
}

// ============================================================================
// Gen Pointer Management (Feature 051: Direct to_giac without strings)
// ============================================================================

void* gen_to_heap_ptr(const Gen& gen) {
    // Allocate a new giac::gen on the heap, copy from Gen's internal impl
    return new giac::gen(gen.impl_->g);
}

void free_gen_ptr(void* ptr) {
    if (ptr != nullptr) {
        delete static_cast<giac::gen*>(ptr);
    }
}

std::string gen_ptr_to_string(void* ptr) {
    if (ptr == nullptr) {
        return "<null>";
    }
    giac::gen* g = static_cast<giac::gen*>(ptr);
    return g->print(nullptr);
}

int gen_ptr_type(void* ptr) {
    if (ptr == nullptr) {
        return -1;
    }
    giac::gen* g = static_cast<giac::gen*>(ptr);
    return static_cast<int>(g->type);
}

// ============================================================================
// Gen Pointer Reconstruction (Feature 052: Direct to_symbolics)
// ============================================================================

Gen gen_from_heap_ptr(void* ptr) {
    if (ptr == nullptr) {
        throw std::runtime_error("Cannot create Gen from null pointer");
    }
    giac::gen* g = static_cast<giac::gen*>(ptr);
    // Create a COPY of the giac::gen, leaving original unchanged
    return Gen(std::make_unique<GenImpl>(*g));
}

} // namespace giac_julia
