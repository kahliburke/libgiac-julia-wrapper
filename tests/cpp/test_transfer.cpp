/**
 * @file test_transfer.cpp
 * @brief Tests for batched tree transfer (gen_export, gen_import) and interruption
 */

#include "giac_impl.h"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace giac_julia;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " #name "... "; \
    try { test_##name(); std::cout << "PASSED" << std::endl; } \
    catch (const std::exception& e) { std::cout << "FAILED: " << e.what() << std::endl; return 1; } \
} while(0)

#define ASSERT_STR(expected, actual) do { \
    std::string e_ = (expected), a_ = (actual); \
    if (e_ != a_) throw std::runtime_error("Expected: " + e_ + ", Got: " + a_); \
} while(0)

#define ASSERT_THROWS(expr) do { \
    bool threw_ = false; \
    try { (void)(expr); } catch (const std::exception&) { threw_ = true; } \
    if (!threw_) throw std::runtime_error("Expected an exception from " #expr); \
} while(0)

// A program or an export, written the way the other side reads it: little-endian, with
// length-prefixed strings
struct Bytes {
    std::string s;
    template <typename T> Bytes& put(T v) {
        s.append(reinterpret_cast<const char*>(&v), sizeof(T));
        return *this;
    }
    Bytes& tag(uint8_t t) { return put(t); }
    Bytes& str(const std::string& x) {
        put(static_cast<uint32_t>(x.size()));
        s += x;
        return *this;
    }
    Bytes& raw(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) s.push_back(static_cast<char>(b));
        return *this;
    }
};

static std::string evaluated(const std::string& expr) { return giac_eval(expr).to_string(); }

// ---------------------------------------------------------------------------------------------
// gen_import
// ---------------------------------------------------------------------------------------------

TEST(import_sum_of_product) {
    Bytes p;
    p.tag(11).put<uint32_t>(2)
        .tag(4).str("x")
        .tag(14).tag(1).put<int64_t>(2).tag(4).str("y");
    ASSERT_STR(evaluated("x+2*y"), gen_import(p.s, {}).to_string());
}

TEST(import_big_integer) {
    // -2^64: nine magnitude bytes, most significant first
    Bytes p;
    p.tag(2).put<int32_t>(-1).put<uint32_t>(9).raw({1, 0, 0, 0, 0, 0, 0, 0, 0});
    ASSERT_STR("-18446744073709551616", gen_import(p.s, {}).to_string());
}

TEST(import_fraction_and_complex) {
    Bytes p;
    p.tag(6)
        .tag(5).tag(1).put<int64_t>(1).tag(1).put<int64_t>(3)
        .tag(1).put<int64_t>(2);
    ASSERT_STR(evaluated("1/3+2*i"), gen_import(p.s, {}).to_string());
}

TEST(import_call_unevaluated) {
    Bytes p;
    p.tag(7).str("sin").put<uint32_t>(1).tag(1).put<int64_t>(0);
    ASSERT_STR("sin(0)", gen_import(p.s, {}).to_string());
}

TEST(import_power_and_quotient) {
    Bytes p;
    p.tag(15)
        .tag(13).tag(4).str("x").tag(1).put<int64_t>(2)
        .tag(16).tag(4).str("y");
    ASSERT_STR(evaluated("x^2/(-y)"), gen_import(p.s, {}).to_string());
}

TEST(import_opaque) {
    Bytes p;
    p.tag(9).put<uint32_t>(0);
    std::vector<Gen> objects;
    objects.push_back(giac_eval("[1,2]"));
    ASSERT_STR("[1,2]", gen_import(p.s, objects).to_string());
}

TEST(import_rejects_malformed) {
    Bytes unknown;
    unknown.tag(99);
    ASSERT_THROWS(gen_import(unknown.s, {}));

    Bytes trailing;
    trailing.tag(1).put<int64_t>(1).tag(1);
    ASSERT_THROWS(gen_import(trailing.s, {}));

    Bytes short_;
    short_.tag(4).put<uint32_t>(10).raw({'x'});
    ASSERT_THROWS(gen_import(short_.s, {}));

    Bytes missing;
    missing.tag(9).put<uint32_t>(0);
    ASSERT_THROWS(gen_import(missing.s, {}));
}

// ---------------------------------------------------------------------------------------------
// gen_export
// ---------------------------------------------------------------------------------------------

TEST(export_integer) {
    Bytes expected;
    expected.tag(1).put<int64_t>(5);
    if (gen_export(giac_eval("5"), "") != expected.s) throw std::runtime_error("export of 5");
}

TEST(export_big_integer) {
    // 2^70 = 0x40 followed by eight zero bytes
    Bytes expected;
    expected.tag(2).put<int32_t>(1).put<uint32_t>(9).raw({0x40, 0, 0, 0, 0, 0, 0, 0, 0});
    if (gen_export(giac_eval("2^70"), "") != expected.s) throw std::runtime_error("export of 2^70");
}

TEST(export_sum) {
    Bytes expected;
    expected.tag(7).str("+").put<uint32_t>(2).tag(4).str("x").tag(4).str("y");
    if (gen_export(giac_eval("x+y"), "") != expected.s) throw std::runtime_error("export of x+y");
}

TEST(export_heads) {
    // a head written out only when accepted; otherwise the call is opaque
    Gen g = giac_eval("sin(x)");
    Bytes written;
    written.tag(7).str("sin").put<uint32_t>(1).tag(4).str("x");
    if (gen_export(g, "cos\nsin") != written.s) throw std::runtime_error("export of sin(x) with sin accepted");
}

TEST(export_opaque) {
    Bytes expected;
    expected.tag(9).put<uint32_t>(0);
    if (gen_export(giac_eval("[1,2]"), "") != expected.s) throw std::runtime_error("export of [1,2]");
    ASSERT_STR("[1,2]", gen_export_opaque(0).to_string());
    ASSERT_THROWS(gen_export_opaque(1));
}

// ---------------------------------------------------------------------------------------------
// Interruption
// ---------------------------------------------------------------------------------------------

TEST(interrupt_flags) {
    giac_clear_interrupt();
    if (giac_interrupt_pending()) throw std::runtime_error("pending after clearing");
    giac_interrupt();
    if (!giac_interrupt_pending()) throw std::runtime_error("not pending after interrupting");
    giac_clear_interrupt();
    if (giac_interrupt_pending()) throw std::runtime_error("pending after clearing again");
    ASSERT_STR("2", evaluated("1+1"));
}

int main() {
    std::cout << "=== Transfer and Interruption Tests ===" << std::endl;
    RUN_TEST(import_sum_of_product);
    RUN_TEST(import_big_integer);
    RUN_TEST(import_fraction_and_complex);
    RUN_TEST(import_call_unevaluated);
    RUN_TEST(import_power_and_quotient);
    RUN_TEST(import_opaque);
    RUN_TEST(import_rejects_malformed);
    RUN_TEST(export_integer);
    RUN_TEST(export_big_integer);
    RUN_TEST(export_sum);
    RUN_TEST(export_heads);
    RUN_TEST(export_opaque);
    RUN_TEST(interrupt_flags);
    std::cout << "All transfer tests passed." << std::endl;
    return 0;
}
