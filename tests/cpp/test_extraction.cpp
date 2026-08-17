/**
 * @file test_extraction.cpp
 * @brief Tests for Gen value extraction with validation (Feature 004)
 *
 * Tests for REQ-C20..C34, REQ-C40..C62: Value extraction with error handling
 */

#include "giac_impl.h"
#include <iostream>
#include <cassert>
#include <string>
#include <stdexcept>

using namespace giac_julia;

// Simple test framework macros
#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { test_##name(); std::cout << "PASSED" << std::endl; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << std::endl; return 1; } \
} while(0)

// ============================================================================
// REQ-C20: gen_to_int - extract integer value
// REQ-C21: gen_to_int throws on non-integer
// ============================================================================

TEST(to_int64_valid) {
    Gen g(static_cast<int64_t>(42));
    int64_t val = g.to_int64();
    assert(val == 42);
    std::cout << "to_int64(42)=42 ";
}

TEST(to_int64_negative) {
    Gen g(static_cast<int64_t>(-123));
    int64_t val = g.to_int64();
    assert(val == -123);
    std::cout << "to_int64(-123)=-123 ";
}

TEST(to_int64_throws_on_double) {
    Gen g(3.14);
    bool threw = false;
    try {
        g.to_int64();
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        assert(msg.find("not an integer") != std::string::npos ||
               msg.find("not of type") != std::string::npos);
    }
    assert(threw);
    std::cout << "to_int64(3.14) throws ";
}

TEST(to_int64_throws_on_symbolic) {
    Gen g = giac_eval("sin(x)");
    bool threw = false;
    try {
        g.to_int64();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "to_int64(sin(x)) throws ";
}

// ============================================================================
// REQ-C22, C23: gen_to_double - extract double value
// REQ-C24: gen_to_double throws on non-numeric
// ============================================================================

TEST(to_double_from_double) {
    Gen g(3.14);
    double val = g.to_double();
    assert(val > 3.13 && val < 3.15);
    std::cout << "to_double(3.14)=" << val << " ";
}

TEST(to_double_from_int) {
    Gen g(static_cast<int64_t>(42));
    double val = g.to_double();
    assert(val == 42.0);
    std::cout << "to_double(42)=42.0 ";
}

TEST(to_double_throws_on_symbolic) {
    Gen g = giac_eval("sin(x)");
    bool threw = false;
    try {
        g.to_double();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "to_double(sin(x)) throws ";
}

// ============================================================================
// REQ-C30, C31: gen_vector_size - get vector size
// ============================================================================

TEST(vect_size_valid) {
    Gen g = giac_eval("[1, 2, 3, 4, 5]");
    int32_t size = g.vect_size();
    assert(size == 5);
    std::cout << "vect_size([1..5])=5 ";
}

TEST(vect_size_empty) {
    Gen g = giac_eval("[]");
    int32_t size = g.vect_size();
    assert(size == 0);
    std::cout << "vect_size([])=0 ";
}

TEST(vect_size_throws_on_int) {
    Gen g(static_cast<int64_t>(42));
    bool threw = false;
    try {
        g.vect_size();
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        assert(msg.find("not a vector") != std::string::npos ||
               msg.find("not of type") != std::string::npos);
    }
    assert(threw);
    std::cout << "vect_size(42) throws ";
}

// ============================================================================
// REQ-C32, C33, C34: gen_vector_getindex - get element at index
// ============================================================================

TEST(vect_at_valid) {
    Gen g = giac_eval("[10, 20, 30]");
    Gen first = g.vect_at(0);
    Gen last = g.vect_at(2);
    assert(first.to_string() == "10");
    assert(last.to_string() == "30");
    std::cout << "vect_at(0)=10, vect_at(2)=30 ";
}

TEST(vect_at_throws_on_bounds) {
    Gen g = giac_eval("[1, 2, 3]");
    bool threw = false;
    try {
        g.vect_at(10);  // Out of bounds
    } catch (const std::exception& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "vect_at(10) throws on [1,2,3] ";
}

TEST(vect_at_throws_on_negative) {
    Gen g = giac_eval("[1, 2, 3]");
    bool threw = false;
    try {
        g.vect_at(-1);  // Negative index
    } catch (const std::exception& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "vect_at(-1) throws ";
}

TEST(vect_at_throws_on_non_vector) {
    Gen g(static_cast<int64_t>(42));
    bool threw = false;
    try {
        g.vect_at(0);
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "vect_at(0) on int throws ";
}

// ============================================================================
// REQ-C40..C44: Fraction accessors
// ============================================================================

TEST(frac_num_valid) {
    Gen g = giac_eval("3/7");
    Gen num = g.frac_num();
    assert(num.to_string() == "3");
    std::cout << "frac_num(3/7)=3 ";
}

TEST(frac_den_valid) {
    Gen g = giac_eval("3/7");
    Gen den = g.frac_den();
    assert(den.to_string() == "7");
    std::cout << "frac_den(3/7)=7 ";
}

TEST(frac_num_on_int) {
    // REQ-C41: For integers, frac_num returns the integer itself
    Gen g(static_cast<int64_t>(5));
    Gen num = g.frac_num();
    assert(num.to_string() == "5");
    std::cout << "frac_num(5)=5 ";
}

TEST(frac_den_on_int) {
    // REQ-C43: For integers, frac_den returns 1
    Gen g(static_cast<int64_t>(5));
    Gen den = g.frac_den();
    assert(den.to_string() == "1");
    std::cout << "frac_den(5)=1 ";
}

TEST(frac_accessors_throw_on_incompatible) {
    // REQ-C44: Throws on incompatible types (not _FRAC_, _INT_, _ZINT_)
    Gen g = giac_eval("sin(x)");
    bool threw = false;
    try {
        g.frac_num();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "frac_num(sin(x)) throws ";
}

// ============================================================================
// REQ-C50..C53: Complex accessors
// ============================================================================

TEST(cplx_re_valid) {
    Gen g = giac_eval("2+3*i");
    Gen re = g.cplx_re();
    assert(re.to_string() == "2");
    std::cout << "cplx_re(2+3i)=2 ";
}

TEST(cplx_im_valid) {
    Gen g = giac_eval("2+3*i");
    Gen im = g.cplx_im();
    assert(im.to_string() == "3");
    std::cout << "cplx_im(2+3i)=3 ";
}

TEST(cplx_re_on_real) {
    // REQ-C51: For non-complex, cplx_re returns the value itself
    Gen g(static_cast<int64_t>(5));
    Gen re = g.cplx_re();
    assert(re.to_string() == "5");
    std::cout << "cplx_re(5)=5 ";
}

TEST(cplx_im_on_real) {
    // REQ-C53: For non-complex, cplx_im returns 0
    Gen g(static_cast<int64_t>(5));
    Gen im = g.cplx_im();
    assert(im.to_string() == "0" || im.is_zero());
    std::cout << "cplx_im(5)=0 ";
}

// ============================================================================
// REQ-C60..C62: Symbolic expression accessors
// ============================================================================

TEST(symb_funcname_valid) {
    Gen g = giac_eval("sin(x)");
    std::string name = g.symb_sommet_name();
    assert(name.find("sin") != std::string::npos);
    std::cout << "symb_sommet_name(sin(x))=" << name << " ";
}

TEST(symb_feuille_valid) {
    Gen g = giac_eval("sin(x)");
    Gen arg = g.symb_feuille();
    assert(arg.to_string() == "x");
    std::cout << "symb_feuille(sin(x))=x ";
}

TEST(symb_funcname_throws_on_non_symbolic) {
    Gen g(static_cast<int64_t>(42));
    bool threw = false;
    try {
        g.symb_sommet_name();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "symb_sommet_name(42) throws ";
}

TEST(symb_feuille_throws_on_non_symbolic) {
    Gen g(static_cast<int64_t>(42));
    bool threw = false;
    try {
        g.symb_feuille();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "symb_feuille(42) throws ";
}

// ============================================================================
// String accessor
// ============================================================================

TEST(strng_value_valid) {
    Gen g = giac_eval("\"hello world\"");
    std::string val = g.strng_value();
    assert(val == "hello world");
    std::cout << "strng_value(\"hello world\")=\"hello world\" ";
}

TEST(strng_value_throws_on_int) {
    Gen g(static_cast<int64_t>(42));
    bool threw = false;
    try {
        g.strng_value();
    } catch (const std::runtime_error& e) {
        threw = true;
    }
    assert(threw);
    std::cout << "strng_value(42) throws ";
}

// ============================================================================
// Map accessors
// ============================================================================

TEST(map_accessors_throw_on_int) {
    Gen g(static_cast<int64_t>(42));
    bool threw_size = false, threw_keys = false, threw_values = false;
    try {
        g.map_size();
    } catch (const std::runtime_error& e) {
        threw_size = true;
    }
    try {
        g.map_keys();
    } catch (const std::runtime_error& e) {
        threw_keys = true;
    }
    try {
        g.map_values();
    } catch (const std::runtime_error& e) {
        threw_values = true;
    }
    assert(threw_size);
    assert(threw_keys);
    assert(threw_values);
    std::cout << "map_size/map_keys/map_values(42) throw ";
}

int main() {
    std::cout << "=== GIAC Wrapper Value Extraction Tests ===" << std::endl;

    // Integer extraction (REQ-C20, C21)
    RUN_TEST(to_int64_valid);
    RUN_TEST(to_int64_negative);
    RUN_TEST(to_int64_throws_on_double);
    RUN_TEST(to_int64_throws_on_symbolic);

    // Double extraction (REQ-C22, C23, C24)
    RUN_TEST(to_double_from_double);
    RUN_TEST(to_double_from_int);
    RUN_TEST(to_double_throws_on_symbolic);

    // Vector size (REQ-C30, C31)
    RUN_TEST(vect_size_valid);
    RUN_TEST(vect_size_empty);
    RUN_TEST(vect_size_throws_on_int);

    // Vector indexing (REQ-C32, C33, C34)
    RUN_TEST(vect_at_valid);
    RUN_TEST(vect_at_throws_on_bounds);
    RUN_TEST(vect_at_throws_on_negative);
    RUN_TEST(vect_at_throws_on_non_vector);

    // Fraction accessors (REQ-C40..C44)
    RUN_TEST(frac_num_valid);
    RUN_TEST(frac_den_valid);
    RUN_TEST(frac_num_on_int);
    RUN_TEST(frac_den_on_int);
    RUN_TEST(frac_accessors_throw_on_incompatible);

    // Complex accessors (REQ-C50..C53)
    RUN_TEST(cplx_re_valid);
    RUN_TEST(cplx_im_valid);
    RUN_TEST(cplx_re_on_real);
    RUN_TEST(cplx_im_on_real);

    // Symbolic accessors (REQ-C60..C62)
    RUN_TEST(symb_funcname_valid);
    RUN_TEST(symb_feuille_valid);
    RUN_TEST(symb_funcname_throws_on_non_symbolic);
    RUN_TEST(symb_feuille_throws_on_non_symbolic);

    // String accessor (REQ-C70, C74)
    RUN_TEST(strng_value_valid);
    RUN_TEST(strng_value_throws_on_int);

    // Map accessors (REQ-C71..C74)
    RUN_TEST(map_accessors_throw_on_int);

    std::cout << "=== All extraction tests passed ===" << std::endl;
    return 0;
}
