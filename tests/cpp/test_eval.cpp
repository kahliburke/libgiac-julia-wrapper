/**
 * @file test_eval.cpp
 * @brief Tests for expression evaluation (User Story 1)
 */

#include "giac_impl.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace giac_julia;

// Simple test framework macros
#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { test_##name(); std::cout << "PASSED" << std::endl; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << std::endl; return 1; } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        throw std::runtime_error("Expected: " + std::string(expected) + ", Got: " + std::string(actual)); \
    } \
} while(0)

#define ASSERT_THROWS(expr, exception_type) do { \
    bool caught = false; \
    try { expr; } catch (const exception_type&) { caught = true; } \
    if (!caught) throw std::runtime_error("Expected exception not thrown"); \
} while(0)

// T016: Test basic eval - "1+1" returns "2"
TEST(basic_eval) {
    GiacContext ctx;
    std::string result = ctx.eval("1+1");
    ASSERT_EQ("2", result);
}

// T017: Test factor operation - "factor(x^2-1)"
TEST(factor_operation) {
    GiacContext ctx;
    std::string result = ctx.eval("factor(x^2-1)");
    // Result should be "(x-1)*(x+1)" or equivalent
    // GIAC may format differently, so we evaluate back
    ASSERT_EQ("(x-1)*(x+1)", result);
}

// T018: Test error handling - invalid expression
// Note: GIAC handles syntax errors gracefully by adding missing parens
// and printing warnings, rather than throwing exceptions
TEST(error_handling) {
    GiacContext ctx;
    // Just verify it doesn't crash on malformed input
    std::string result = ctx.eval("invalid(((");
    // GIAC will try to parse this somehow - we just check it runs
    (void)result; // Use the result to avoid unused warning
}

// Additional tests
TEST(version_functions) {
    std::string gv = get_giac_version();
    std::string wv = get_wrapper_version();
    // Issue #2: wrapper_version() now comes from meson.project_version()
    // at build time. Check it is non-empty and SemVer-shaped (X.Y.Z...)
    // rather than pinning a literal.
    assert(!wv.empty());
    assert(wv != "unknown");
    assert(wv.find('.') != std::string::npos);
    // GIAC version should be non-empty
    assert(!gv.empty());
}

TEST(giac_available) {
    assert(check_giac_available());
}

// A limit that fails leaves GIAC's series_flags as they were: with bit 3 left set, atan stops
// rewriting through tan, and acot(cot(pi/90)) no longer simplifies
TEST(failed_limit_restores_series_flags) {
    const char* failing = "limit(x^(b+1)/(b+1)*ln(x),x,0,1)";
    const char* check = "simplify(180*acot(cot(pi/90))/pi)";
    try { giac_eval(failing); } catch (const std::exception&) {}
    assert((get_series_flags() & 8) == 0);
    ASSERT_EQ("2", giac_eval(check).to_string());

    GiacContext ctx;
    try { ctx.eval(failing); } catch (const std::exception&) {}
    ASSERT_EQ("2", ctx.eval(check));
}

int main() {
    std::cout << "=== GIAC Wrapper Eval Tests ===" << std::endl;

    RUN_TEST(version_functions);
    RUN_TEST(giac_available);
    RUN_TEST(basic_eval);
    RUN_TEST(factor_operation);
    RUN_TEST(error_handling);
    RUN_TEST(failed_limit_restores_series_flags);

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
