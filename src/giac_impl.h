/**
 * @file giac_impl.h
 * @brief Pure C++ interface to GIAC, isolated from jlcxx
 *
 * This header provides a clean interface that doesn't require
 * including GIAC headers, avoiding macro conflicts with Julia/jlcxx.
 */

#ifndef GIAC_IMPL_H
#define GIAC_IMPL_H

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace giac_julia {

// Forward declaration of opaque types
struct GiacContextImpl;
struct GenImpl;
class Gen;           // Forward declaration for free functions
class GiacContext;   // Forward declaration for free functions taking a context

// ============================================================================
// Version Functions
// ============================================================================

std::string get_giac_version();
std::string get_wrapper_version();
bool check_giac_available();

// ============================================================================
// Configuration Functions
// ============================================================================

void set_xcasroot(const std::string& path);
std::string get_xcasroot();
bool init_help(const std::string& aide_cas_path);
std::string list_commands();
int help_count();

// ============================================================================
// Expression Evaluation
// ============================================================================

/**
 * @brief Parse and evaluate a Giac expression string
 * @param expr Expression string (e.g., "sin(x)+1", "[[1,2],[3,4]]")
 * @return Evaluated Gen
 * @note This is the preferred entry point for string expressions
 * @note Uses a process-wide singleton context; use the (expr, ctx)
 *       overload below for per-context isolation.
 */
Gen giac_eval(const std::string& expr);

/**
 * @brief Parse and evaluate a Giac expression string in a specific context
 * @param expr Expression string
 * @param ctx Context whose state (variable bindings, configuration) is
 *            used and updated by this evaluation. Two distinct GiacContext
 *            instances do not share `:=` bindings, enabling true isolation.
 * @return Evaluated Gen
 */
Gen giac_eval(const std::string& expr, GiacContext& ctx);

// ============================================================================
// Generic Dispatch (Tier 2)
// ============================================================================

/**
 * @brief Apply a Giac function by name with zero arguments
 * @param name Function name (e.g., "rand")
 * @return Result of function application
 */
Gen apply_func0(const std::string& name);

/**
 * @brief Apply a Giac function by name to a single argument
 * @param name Function name (e.g., "ifactor", "sin")
 * @param arg Argument Gen
 * @return Result of function application
 * @note Uses gen(name, &ctx) lookup; falls back to string eval if not _FUNC
 */
Gen apply_func1(const std::string& name, const Gen& arg);

/**
 * @brief Apply a Giac function by name to two arguments
 */
Gen apply_func2(const std::string& name, const Gen& arg1, const Gen& arg2);

/**
 * @brief Apply a Giac function by name to three arguments
 */
Gen apply_func3(const std::string& name, const Gen& arg1, const Gen& arg2, const Gen& arg3);

/**
 * @brief Apply a Giac function by name to N arguments
 * @param args Vector of arguments
 */
Gen apply_funcN(const std::string& name, const std::vector<Gen>& args);

// ============================================================================
// Interruption
// ============================================================================

/// Ask the running GIAC computation to stop. Safe to call from any thread: it sets the flag
/// GIAC checks in its long loops, and the computation ends with an error.
void giac_interrupt();

/// Clear GIAC's interruption flags, for the next computation.
void giac_clear_interrupt();

/// Whether an interruption is pending or happened in the last computation.
bool giac_interrupt_pending();

// ============================================================================
// Batched export of an expression tree
// ============================================================================

/// The tree of `g` serialized in one call, for Julia to read without a call per node. Preorder,
/// little-endian, each node a tag byte then its payload:
///   1 INT     int64
///   2 ZINT    int32 sign, uint32 n, n bytes of magnitude (big-endian)
///   3 DOUBLE  float64
///   4 IDNT    uint32 n, n bytes of name
///   5 FRAC    numerator node, denominator node (both integers)
///   6 CPLX    real node, imaginary node (both numbers)
///   7 SYMB    uint32 n, n bytes of head, uint32 k, then k argument nodes
///   9 OPAQUE  uint32 index of the object, fetched with gen_export_opaque
/// A symbolic node is written out when its head is an arithmetic operator, an identifier-like
/// name, or one of the newline-separated `heads`; other objects (vectors, strings, products of
/// integer powers such as ifactor's) are opaque.
std::string gen_export(const Gen& g, const std::string& heads);

/// The i-th opaque object (0-based) of the last gen_export on this thread.
Gen gen_export_opaque(int32_t i);

// ============================================================================
// Function Listing
// ============================================================================

/**
 * @brief List all builtin functions from the lexer table
 * @return Newline-separated list of function names
 */
std::string list_builtin_functions();

/**
 * @brief Count of builtin lexer functions
 * @return Number of functions in builtin_lexer_functions table
 */
int builtin_function_count();

/**
 * @brief Union of documented commands and builtin functions
 * @return Newline-separated, deduplicated, sorted list of all function names
 * @note Requires init_help() to have been called for documented commands
 */
std::string list_all_functions();

// ============================================================================
// Tier 1 Direct Wrappers (High Performance - No Name Lookup)
// ============================================================================

// Trigonometry
Gen giac_sin(const Gen& arg);
Gen giac_cos(const Gen& arg);
Gen giac_tan(const Gen& arg);
Gen giac_asin(const Gen& arg);
Gen giac_acos(const Gen& arg);
Gen giac_atan(const Gen& arg);

// Exponential / Logarithm
Gen giac_exp(const Gen& arg);
Gen giac_ln(const Gen& arg);
Gen giac_log10(const Gen& arg);
Gen giac_sqrt(const Gen& arg);

// Arithmetic
Gen giac_abs(const Gen& arg);
Gen giac_sign(const Gen& arg);
Gen giac_floor(const Gen& arg);
Gen giac_ceil(const Gen& arg);

// Complex
Gen giac_re(const Gen& arg);
Gen giac_im(const Gen& arg);
Gen giac_conj(const Gen& arg);

// Algebra
Gen giac_normal(const Gen& arg);
Gen giac_evalf(const Gen& arg);

// Calculus (multi-argument)
Gen giac_diff(const Gen& expr, const Gen& var);
Gen giac_integrate(const Gen& expr, const Gen& var);
Gen giac_subst(const Gen& expr, const Gen& var, const Gen& val);
Gen giac_solve(const Gen& expr, const Gen& var);
Gen giac_limit(const Gen& expr, const Gen& var, const Gen& val);
Gen giac_series(const Gen& expr, const Gen& var, const Gen& order);

// Arithmetic (multi-argument)
Gen giac_gcd(const Gen& a, const Gen& b);
Gen giac_lcm(const Gen& a, const Gen& b);

// Power
Gen giac_pow(const Gen& base, const Gen& exp);

// ============================================================================
// Gen Construction Functions (Feature 051: Direct to_giac)
// ============================================================================

/**
 * @brief Create an identifier (symbolic variable) from a name
 * @param name Variable name (e.g., "x", "alpha")
 * @return Gen of type _IDNT
 */
Gen make_identifier(const std::string& name);

/**
 * @brief Create a big integer (ZINT) from raw bytes
 * @param bytes Big-endian byte array (absolute value)
 * @param sign Sign: -1 (negative), 0 (zero), 1 (positive)
 * @return Gen of type _ZINT (or _INT_ if small enough)
 * @note Reverse of zint_to_bytes() / zint_sign()
 */
Gen make_zint_from_bytes(const std::vector<uint8_t>& bytes, int32_t sign);

/**
 * @brief Create an unevaluated symbolic expression
 * @param op_name Operator/function name (e.g., "+", "sin")
 * @param args Vector of argument Gen objects
 * @return Gen of type _SYMB (unevaluated)
 * @note Unlike apply_funcN, does NOT evaluate the result
 */
Gen make_symbolic_unevaluated(const std::string& op_name, const std::vector<Gen>& args);

/**
 * @brief Create a complex number from real and imaginary parts
 * @param re Real part Gen
 * @param im Imaginary part Gen
 * @return Gen of type _CPLX
 */
Gen make_complex(const Gen& re, const Gen& im);

/**
 * @brief Create a fraction from numerator and denominator
 * @param num Numerator Gen
 * @param den Denominator Gen
 * @return Gen of type _FRAC
 */
Gen make_fraction(const Gen& num, const Gen& den);

/**
 * @brief Create a vector/sequence from elements
 * @param elements Vector of Gen elements
 * @param subtype Vector subtype (0=list, 1=seq, 2=set, 11=matrix, etc.)
 * @return Gen of type _VECT with specified subtype
 */
Gen make_vect(const std::vector<Gen>& elements, int32_t subtype);

// ============================================================================
// Gen Pointer Management (Feature 051: Direct to_giac without strings)
// ============================================================================

/**
 * @brief Allocate a copy of a Gen on the heap
 * @param gen Source Gen to copy
 * @return Opaque pointer to heap-allocated Gen (caller owns)
 * @note Use free_gen_ptr() to deallocate
 */
void* gen_to_heap_ptr(const Gen& gen);

/**
 * @brief Free a heap-allocated Gen
 * @param ptr Pointer returned by gen_to_heap_ptr()
 */
void free_gen_ptr(void* ptr);

/**
 * @brief Get string representation of a heap Gen
 * @param ptr Pointer returned by gen_to_heap_ptr()
 * @return String representation
 */
std::string gen_ptr_to_string(void* ptr);

/**
 * @brief Get type of a heap Gen
 * @param ptr Pointer returned by gen_to_heap_ptr()
 * @return Gen type code
 */
int gen_ptr_type(void* ptr);

/**
 * @brief Reconstruct a Gen from a heap pointer (Feature 052: Direct to_symbolics)
 * @param ptr Pointer to heap-allocated giac::gen (from gen_to_heap_ptr or _giac_eval_string)
 * @return Gen object containing a COPY of the pointed-to giac::gen
 * @throws std::runtime_error if ptr is null
 * @note Non-destructive: original pointer remains valid and unchanged
 */
Gen gen_from_heap_ptr(void* ptr);

// ============================================================================
// GiacContext - Opaque wrapper around giac::context
// ============================================================================

class GiacContext {
public:
    GiacContext();
    ~GiacContext();

    // Non-copyable
    GiacContext(const GiacContext&) = delete;
    GiacContext& operator=(const GiacContext&) = delete;

    // Movable
    GiacContext(GiacContext&& other) noexcept;
    GiacContext& operator=(GiacContext&& other) noexcept;

    // Evaluation
    std::string eval(const std::string& input);

    // Variable management
    void set_variable(const std::string& name, const std::string& value);
    std::string get_variable(const std::string& name);

    // Configuration
    void set_timeout(double seconds);
    double get_timeout() const;
    void set_precision(int digits);
    int get_precision() const;
    bool is_complex_mode() const;
    void set_complex_mode(bool enable);

    // Warning handler
    void set_warning_handler(std::function<void(const std::string&)> handler);
    void clear_warning_handler();

private:
    std::unique_ptr<GiacContextImpl> impl_;

    // Free function that needs access to the underlying giac::context*.
    friend Gen giac_eval(const std::string& expr, GiacContext& ctx);
};

// ============================================================================
// Gen - Opaque wrapper around giac::gen
// ============================================================================

class Gen {
public:
    Gen();
    explicit Gen(const std::string& expr);
    explicit Gen(int64_t value);
    explicit Gen(double value);
    ~Gen();

    // Copyable
    Gen(const Gen& other);
    Gen& operator=(const Gen& other);

    // Movable
    Gen(Gen&& other) noexcept;
    Gen& operator=(Gen&& other) noexcept;

    // String conversion
    std::string to_string() const;

    // Type information
    int type() const;
    int32_t subtype() const;
    std::string type_name() const;

    // Typed accessors (caller must verify type() first!)
    // _INT_ type
    int64_t to_int64() const;
    int32_t to_int32() const;

    // _DOUBLE_ type
    double to_double() const;

    // _ZINT type (big integers)
    std::string zint_to_string() const;
    int zint_sign() const;
    std::vector<uint8_t> zint_to_bytes() const;

    // _CPLX type
    Gen cplx_re() const;
    Gen cplx_im() const;

    // _FRAC type
    Gen frac_num() const;
    Gen frac_den() const;

    // _VECT type
    int32_t vect_size() const;
    Gen vect_at(int32_t i) const;  // throws std::out_of_range if i out of bounds

    // _SYMB type
    std::string symb_sommet_name() const;
    Gen symb_feuille() const;

    // _IDNT type
    std::string idnt_name() const;

    // _STRNG type
    std::string strng_value() const;

    // _MAP type
    int32_t map_size() const;
    Gen map_keys() const;
    Gen map_values() const;

    // Predicates
    bool is_zero() const;
    bool is_one() const;
    bool is_integer() const;
    bool is_approx() const;

    // Type predicates (Feature 004: REQ-C10..C17)
    bool is_numeric() const;      // REQ-C11: _INT_, _DOUBLE_, _ZINT_, or _REAL_
    bool is_vector() const;       // REQ-C12: _VECT_
    bool is_symbolic() const;     // REQ-C13: _SYMB_
    bool is_identifier() const;   // REQ-C14: _IDNT_
    bool is_fraction() const;     // REQ-C15: _FRAC_
    bool is_complex() const;      // REQ-C16: _CPLX_
    bool is_string() const;       // REQ-C17: _STRNG_

    // Operations
    Gen eval() const;
    Gen simplify() const;
    Gen expand() const;
    Gen factor() const;

    // Arithmetic operators
    Gen operator+(const Gen& other) const;
    Gen operator-(const Gen& other) const;
    Gen operator*(const Gen& other) const;
    Gen operator/(const Gen& other) const;
    Gen operator-() const;

    // Comparison
    bool operator==(const Gen& other) const;
    bool operator!=(const Gen& other) const;

    // Internal use
    void* get_impl() const;
    static Gen from_impl(void* impl);

private:
    std::unique_ptr<GenImpl> impl_;
    explicit Gen(std::unique_ptr<GenImpl> impl);

    // Friend functions that need access to private constructor
    friend Gen giac_eval(const std::string& expr);
    friend Gen giac_eval(const std::string& expr, GiacContext& ctx);
    friend std::string gen_export(const Gen& g, const std::string& heads);
    friend Gen gen_export_opaque(int32_t i);
    friend Gen apply_func0(const std::string& name);
    friend Gen apply_func1(const std::string& name, const Gen& arg);
    friend Gen apply_func2(const std::string& name, const Gen& arg1, const Gen& arg2);
    friend Gen apply_func3(const std::string& name, const Gen& arg1, const Gen& arg2, const Gen& arg3);
    friend Gen apply_funcN(const std::string& name, const std::vector<Gen>& args);

    // Tier 1 direct wrapper friends (single-arg)
    friend Gen giac_sin(const Gen& arg);
    friend Gen giac_cos(const Gen& arg);
    friend Gen giac_tan(const Gen& arg);
    friend Gen giac_asin(const Gen& arg);
    friend Gen giac_acos(const Gen& arg);
    friend Gen giac_atan(const Gen& arg);
    friend Gen giac_exp(const Gen& arg);
    friend Gen giac_ln(const Gen& arg);
    friend Gen giac_log10(const Gen& arg);
    friend Gen giac_sqrt(const Gen& arg);
    friend Gen giac_abs(const Gen& arg);
    friend Gen giac_sign(const Gen& arg);
    friend Gen giac_floor(const Gen& arg);
    friend Gen giac_ceil(const Gen& arg);
    friend Gen giac_re(const Gen& arg);
    friend Gen giac_im(const Gen& arg);
    friend Gen giac_conj(const Gen& arg);
    friend Gen giac_normal(const Gen& arg);
    friend Gen giac_evalf(const Gen& arg);

    // Tier 1 direct wrapper friends (multi-arg)
    friend Gen giac_diff(const Gen& expr, const Gen& var);
    friend Gen giac_integrate(const Gen& expr, const Gen& var);
    friend Gen giac_subst(const Gen& expr, const Gen& var, const Gen& val);
    friend Gen giac_solve(const Gen& expr, const Gen& var);
    friend Gen giac_limit(const Gen& expr, const Gen& var, const Gen& val);
    friend Gen giac_series(const Gen& expr, const Gen& var, const Gen& order);
    friend Gen giac_gcd(const Gen& a, const Gen& b);
    friend Gen giac_lcm(const Gen& a, const Gen& b);
    friend Gen giac_pow(const Gen& base, const Gen& exp);

    // Gen construction functions (Feature 051)
    friend Gen make_identifier(const std::string& name);
    friend Gen make_zint_from_bytes(const std::vector<uint8_t>& bytes, int32_t sign);
    friend Gen make_symbolic_unevaluated(const std::string& op_name, const std::vector<Gen>& args);
    friend Gen make_complex(const Gen& re, const Gen& im);
    friend Gen make_fraction(const Gen& num, const Gen& den);
    friend Gen make_vect(const std::vector<Gen>& elements, int32_t subtype);

    // Gen pointer management (Feature 051: direct to_giac)
    friend void* gen_to_heap_ptr(const Gen& gen);

    // Gen pointer reconstruction (Feature 052: direct to_symbolics)
    friend Gen gen_from_heap_ptr(void* ptr);
};

} // namespace giac_julia

#endif // GIAC_IMPL_H
