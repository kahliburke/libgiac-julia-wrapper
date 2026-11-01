/**
 * @file giac_wrapper.cpp
 * @brief JLCXX module definition for Julia bindings
 *
 * This file only includes jlcxx headers and the opaque giac_impl.h
 * interface. It does NOT include GIAC headers directly to avoid
 * macro conflicts between GIAC and Julia.
 */

#include <jlcxx/jlcxx.hpp>
#include <jlcxx/stl.hpp>

#include "giac_impl.h"
#include "gc_safe.h"

JLCXX_MODULE define_julia_module(jlcxx::Module& mod)
{
    using namespace giac_julia;

    // GIAC's computations run GC-safe, so GMP must not allocate through Julia's counted functions
    use_plain_gmp_allocation();

    // ========================================================================
    // Type Constants (EARS-CW-010–013)
    // ========================================================================
    // Note: Using flat constant names prefixed with type category
    // Julia side will create module wrappers if needed

    // GenType constants - gen type constants
    mod.set_const("GENTYPE_INT", static_cast<int32_t>(0));      // _INT_
    mod.set_const("GENTYPE_DOUBLE", static_cast<int32_t>(1));   // _DOUBLE_
    mod.set_const("GENTYPE_ZINT", static_cast<int32_t>(2));     // _ZINT
    mod.set_const("GENTYPE_REAL", static_cast<int32_t>(3));     // _REAL
    mod.set_const("GENTYPE_CPLX", static_cast<int32_t>(4));     // _CPLX
    mod.set_const("GENTYPE_POLY", static_cast<int32_t>(5));     // _POLY
    mod.set_const("GENTYPE_IDNT", static_cast<int32_t>(6));     // _IDNT
    mod.set_const("GENTYPE_VECT", static_cast<int32_t>(7));     // _VECT
    mod.set_const("GENTYPE_SYMB", static_cast<int32_t>(8));     // _SYMB
    mod.set_const("GENTYPE_SPOL1", static_cast<int32_t>(9));    // _SPOL1
    mod.set_const("GENTYPE_FRAC", static_cast<int32_t>(10));    // _FRAC
    mod.set_const("GENTYPE_EXT", static_cast<int32_t>(11));     // _EXT
    mod.set_const("GENTYPE_STRNG", static_cast<int32_t>(12));   // _STRNG
    mod.set_const("GENTYPE_FUNC", static_cast<int32_t>(13));    // _FUNC
    mod.set_const("GENTYPE_MOD", static_cast<int32_t>(15));     // _MOD
    mod.set_const("GENTYPE_USER", static_cast<int32_t>(16));    // _USER
    mod.set_const("GENTYPE_MAP", static_cast<int32_t>(17));     // _MAP
    mod.set_const("GENTYPE_FLOAT", static_cast<int32_t>(21));   // _FLOAT_

    // VectSubtype constants
    mod.set_const("VECTSUBTYPE_LIST", static_cast<int32_t>(0));
    mod.set_const("VECTSUBTYPE_SEQ", static_cast<int32_t>(1));
    mod.set_const("VECTSUBTYPE_SET", static_cast<int32_t>(2));
    mod.set_const("VECTSUBTYPE_RPN", static_cast<int32_t>(4));
    mod.set_const("VECTSUBTYPE_GROUP", static_cast<int32_t>(5));
    mod.set_const("VECTSUBTYPE_LINE", static_cast<int32_t>(6));
    mod.set_const("VECTSUBTYPE_VECTOR", static_cast<int32_t>(7));
    mod.set_const("VECTSUBTYPE_PNT", static_cast<int32_t>(8));
    mod.set_const("VECTSUBTYPE_POLY1", static_cast<int32_t>(10));
    mod.set_const("VECTSUBTYPE_MATRIX", static_cast<int32_t>(11));
    mod.set_const("VECTSUBTYPE_ASSUME", static_cast<int32_t>(13));
    mod.set_const("VECTSUBTYPE_SPREAD", static_cast<int32_t>(14));
    mod.set_const("VECTSUBTYPE_POINT", static_cast<int32_t>(20));

    // IntSubtype constants
    mod.set_const("INTSUBTYPE_INT", static_cast<int32_t>(0));
    mod.set_const("INTSUBTYPE_TYPE", static_cast<int32_t>(1));
    mod.set_const("INTSUBTYPE_CHAR", static_cast<int32_t>(2));
    mod.set_const("INTSUBTYPE_COLOR", static_cast<int32_t>(5));
    mod.set_const("INTSUBTYPE_BOOLEAN", static_cast<int32_t>(6));
    mod.set_const("INTSUBTYPE_PLOT", static_cast<int32_t>(7));

    // Register version functions
    mod.method("giac_version", &get_giac_version);
    mod.method("wrapper_version", &get_wrapper_version);
    mod.method("is_giac_available", &check_giac_available);

    // Register configuration functions
    mod.method("set_xcasroot", &set_xcasroot);
    mod.method("get_xcasroot", &get_xcasroot);
    mod.method("init_help", &init_help);
    mod.method("list_commands", &list_commands);
    mod.method("help_count", &help_count);

    // Register GiacContext type
    mod.add_type<GiacContext>("GiacContext")
        .constructor<>()
        .method("giac_eval", gc_safe(&GiacContext::eval))
        .method("set_variable", gc_safe(&GiacContext::set_variable))
        .method("get_variable", gc_safe(&GiacContext::get_variable))
        .method("set_timeout", &GiacContext::set_timeout)
        .method("get_timeout", &GiacContext::get_timeout)
        .method("set_precision", &GiacContext::set_precision)
        .method("get_precision", &GiacContext::get_precision)
        .method("is_complex_mode", &GiacContext::is_complex_mode)
        .method("set_complex_mode", &GiacContext::set_complex_mode);

    // Register Gen type
    mod.add_type<Gen>("Gen")
        .constructor<>()
        .constructor<const std::string&>()
        .constructor<int64_t>()
        .constructor<double>()
        // String conversion
        .method("to_string", gc_safe(&Gen::to_string))
        // Type information
        .method("type", &Gen::type)
        .method("subtype", &Gen::subtype)
        .method("type_name", &Gen::type_name)
        // Typed accessors
        .method("to_int64", &Gen::to_int64)
        .method("to_int32", &Gen::to_int32)
        .method("to_double", &Gen::to_double)
        .method("zint_to_string", &Gen::zint_to_string)
        .method("zint_sign", &Gen::zint_sign)
        .method("zint_to_bytes", &Gen::zint_to_bytes)
        .method("cplx_re", &Gen::cplx_re)
        .method("cplx_im", &Gen::cplx_im)
        .method("frac_num", &Gen::frac_num)
        .method("frac_den", &Gen::frac_den)
        .method("vect_size", &Gen::vect_size)
        .method("vect_at", &Gen::vect_at)
        .method("symb_sommet_name", &Gen::symb_sommet_name)
        .method("symb_feuille", &Gen::symb_feuille)
        .method("idnt_name", &Gen::idnt_name)
        .method("strng_value", &Gen::strng_value)
        .method("map_size", &Gen::map_size)
        .method("map_keys", &Gen::map_keys)
        .method("map_values", &Gen::map_values)
        // Predicates
        .method("is_zero", &Gen::is_zero)
        .method("is_one", &Gen::is_one)
        .method("is_integer", &Gen::is_integer)
        .method("is_approx", &Gen::is_approx)
        // Type predicates (Feature 004: REQ-C10..C17)
        .method("is_numeric", &Gen::is_numeric)
        .method("is_vector", &Gen::is_vector)
        .method("is_symbolic", &Gen::is_symbolic)
        .method("is_identifier", &Gen::is_identifier)
        .method("is_fraction", &Gen::is_fraction)
        .method("is_complex", &Gen::is_complex)
        .method("is_string", &Gen::is_string)
        // Operations
        .method("gen_eval", gc_safe(&Gen::eval))  // Renamed to avoid conflict with giac_eval
        .method("simplify", gc_safe(&Gen::simplify))
        .method("expand", gc_safe(&Gen::expand))
        .method("factor", gc_safe(&Gen::factor));

    // Register giac_eval free function (both overloads).
    // The (expr) overload uses the singleton thread-local context; the
    // (expr, ctx) overload provides per-context isolation (issue #3).
    mod.method("giac_eval",
        gc_safe(static_cast<Gen(*)(const std::string&)>(&giac_eval)));
    mod.method("giac_eval",
        gc_safe(static_cast<Gen(*)(const std::string&, GiacContext&)>(&giac_eval)));

    // Register generic dispatch functions (Tier 2)
    mod.method("apply_func0", gc_safe(&apply_func0));
    mod.method("apply_func1", gc_safe(&apply_func1));
    mod.method("apply_func2", gc_safe(&apply_func2));
    mod.method("apply_func3", gc_safe(&apply_func3));
    mod.method("apply_funcN", gc_safe(&apply_funcN));

    // Interruption: callable from any thread, while a computation runs on another
    mod.method("giac_interrupt", &giac_interrupt);
    mod.method("giac_clear_interrupt", &giac_clear_interrupt);
    mod.method("giac_interrupt_pending", &giac_interrupt_pending);

    // Batched export of an expression tree (one call instead of one per node)
    mod.method("gen_export", gc_safe(&gen_export));
    mod.method("gen_export_opaque", &gen_export_opaque);
    mod.method("gen_import", gc_safe(&gen_import));

    // Register function listing
    mod.method("list_builtin_functions", &list_builtin_functions);
    mod.method("builtin_function_count", &builtin_function_count);
    mod.method("list_all_functions", &list_all_functions);

    // Register Tier 1 direct wrappers (high-performance, no name lookup)
    // Trigonometry
    mod.method("giac_sin", gc_safe(&giac_sin));
    mod.method("giac_cos", gc_safe(&giac_cos));
    mod.method("giac_tan", gc_safe(&giac_tan));
    mod.method("giac_asin", gc_safe(&giac_asin));
    mod.method("giac_acos", gc_safe(&giac_acos));
    mod.method("giac_atan", gc_safe(&giac_atan));

    // Exponential / Logarithm
    mod.method("giac_exp", gc_safe(&giac_exp));
    mod.method("giac_ln", gc_safe(&giac_ln));
    mod.method("giac_log10", gc_safe(&giac_log10));
    mod.method("giac_sqrt", gc_safe(&giac_sqrt));

    // Arithmetic
    mod.method("giac_abs", gc_safe(&giac_abs));
    mod.method("giac_sign", gc_safe(&giac_sign));
    mod.method("giac_floor", gc_safe(&giac_floor));
    mod.method("giac_ceil", gc_safe(&giac_ceil));

    // Complex
    mod.method("giac_re", gc_safe(&giac_re));
    mod.method("giac_im", gc_safe(&giac_im));
    mod.method("giac_conj", gc_safe(&giac_conj));

    // Algebra
    mod.method("giac_normal", gc_safe(&giac_normal));
    mod.method("giac_evalf", gc_safe(&giac_evalf));

    // Calculus (multi-argument)
    mod.method("giac_diff", gc_safe(&giac_diff));
    mod.method("giac_integrate", gc_safe(&giac_integrate));
    mod.method("giac_subst", gc_safe(&giac_subst));
    mod.method("giac_solve", gc_safe(&giac_solve));
    mod.method("giac_limit", gc_safe(&giac_limit));
    mod.method("giac_series", gc_safe(&giac_series));

    // Arithmetic (multi-argument)
    mod.method("giac_gcd", gc_safe(&giac_gcd));
    mod.method("giac_lcm", gc_safe(&giac_lcm));

    // Power
    mod.method("giac_pow", gc_safe(&giac_pow));

    // ========================================================================
    // Gen Construction Functions (Feature 051: Direct to_giac)
    // ========================================================================
    mod.method("make_identifier", &make_identifier);
    mod.method("make_zint_from_bytes", &make_zint_from_bytes);
    mod.method("make_symbolic_unevaluated", &make_symbolic_unevaluated);
    mod.method("make_complex", &make_complex);
    mod.method("make_fraction", &make_fraction);
    mod.method("make_vect", &make_vect);

    // ========================================================================
    // Gen Pointer Management (Feature 051: Direct to_giac without strings)
    // ========================================================================
    mod.method("gen_to_heap_ptr", &gen_to_heap_ptr);
    mod.method("free_gen_ptr", &free_gen_ptr);
    mod.method("gen_ptr_to_string", gc_safe(&gen_ptr_to_string));
    mod.method("gen_ptr_type", &gen_ptr_type);

    // ========================================================================
    // Gen Pointer Reconstruction (Feature 052: Direct to_symbolics)
    // ========================================================================
    mod.method("gen_from_heap_ptr", &gen_from_heap_ptr);

    // Register Gen operators
    mod.set_override_module(jl_base_module);
    mod.method("+", [](const Gen& a, const Gen& b) { GCSafeRegion region; return a + b; });
    mod.method("-", [](const Gen& a, const Gen& b) { GCSafeRegion region; return a - b; });
    mod.method("*", [](const Gen& a, const Gen& b) { GCSafeRegion region; return a * b; });
    mod.method("/", [](const Gen& a, const Gen& b) { GCSafeRegion region; return a / b; });
    mod.method("-", [](const Gen& a) { GCSafeRegion region; return -a; });
    mod.method("==", [](const Gen& a, const Gen& b) { return a == b; });
    mod.method("!=", [](const Gen& a, const Gen& b) { return a != b; });

    // Mixed-type operators: Gen × int64_t
    mod.method("+", [](const Gen& a, int64_t b) { GCSafeRegion region; return a + Gen(b); });
    mod.method("+", [](int64_t a, const Gen& b) { GCSafeRegion region; return Gen(a) + b; });
    mod.method("-", [](const Gen& a, int64_t b) { GCSafeRegion region; return a - Gen(b); });
    mod.method("-", [](int64_t a, const Gen& b) { GCSafeRegion region; return Gen(a) - b; });
    mod.method("*", [](const Gen& a, int64_t b) { GCSafeRegion region; return a * Gen(b); });
    mod.method("*", [](int64_t a, const Gen& b) { GCSafeRegion region; return Gen(a) * b; });
    mod.method("/", [](const Gen& a, int64_t b) { GCSafeRegion region; return a / Gen(b); });
    mod.method("/", [](int64_t a, const Gen& b) { GCSafeRegion region; return Gen(a) / b; });

    // Mixed-type operators: Gen × double
    mod.method("+", [](const Gen& a, double b) { GCSafeRegion region; return a + Gen(b); });
    mod.method("+", [](double a, const Gen& b) { GCSafeRegion region; return Gen(a) + b; });
    mod.method("-", [](const Gen& a, double b) { GCSafeRegion region; return a - Gen(b); });
    mod.method("-", [](double a, const Gen& b) { GCSafeRegion region; return Gen(a) - b; });
    mod.method("*", [](const Gen& a, double b) { GCSafeRegion region; return a * Gen(b); });
    mod.method("*", [](double a, const Gen& b) { GCSafeRegion region; return Gen(a) * b; });
    mod.method("/", [](const Gen& a, double b) { GCSafeRegion region; return a / Gen(b); });
    mod.method("/", [](double a, const Gen& b) { GCSafeRegion region; return Gen(a) / b; });
    mod.unset_override_module();
}
