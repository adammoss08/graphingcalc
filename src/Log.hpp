#pragma once
// Diagnostics on stderr. Errors always print; info lines only when verbose (CALC_VERBOSE=1 in the
// environment, or --verbose; scripts/run.sh turns it on). Each line carries ms since launch.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace calc::log {

inline bool& verbose() {
    static bool v = [] {
        const char* e = std::getenv("CALC_VERBOSE");
        return e && *e && *e != '0';
    }();
    return v;
}

inline double sinceStartMs() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration<double, std::milli>(steady_clock::now() - t0).count();
}

inline void vwrite(const char* level, const char* fmt, va_list ap) {
    std::fprintf(stderr, "[%9.1f ms] %s ", sinceStartMs(), level);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
}

#if defined(__GNUC__) || defined(__clang__)
#define CALC_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define CALC_PRINTF(a, b)
#endif

CALC_PRINTF(1, 2) inline void info(const char* fmt, ...) {
    if (!verbose()) return;
    va_list ap;
    va_start(ap, fmt);
    vwrite("info ", fmt, ap);
    va_end(ap);
}

CALC_PRINTF(1, 2) inline void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vwrite("ERROR", fmt, ap);
    va_end(ap);
}

} // namespace calc::log
