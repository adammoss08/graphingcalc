#pragma once
// Minimal test registry: TEST(name) { CHECK(...); CHECK_NEAR(a, b, eps); }
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
inline void fail(const char* file, int line, const std::string& msg) {
    std::printf("  FAIL %s:%d: %s\n", file, line, msg.c_str());
    std::fflush(stdout);
    ++failures();
}
} // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name) \
    static void name(); \
    static test::Reg TEST_CAT(reg_, name)(#name, name); \
    static void name()
#define CHECK(cond) do { if (!(cond)) test::fail(__FILE__, __LINE__, #cond); } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) \
    test::fail(__FILE__, __LINE__, std::string(#a " == " #b "  [") + test::str(_a) + " vs " + test::str(_b) + "]"); } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); if (!(std::fabs(_a - _b) <= (eps))) { \
    char _buf[160]; std::snprintf(_buf, sizeof _buf, "%s ~= %s  [%.12g vs %.12g]", #a, #b, _a, _b); \
    test::fail(__FILE__, __LINE__, _buf); } } while (0)

namespace test {
inline std::string str(const std::string& s) { return s; }
inline std::string str(const char* s) { return s; }
template <class T> std::string str(const T& v) { return std::to_string(v); }
}
