#include "Test.hpp"

#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = test::failures();
        c.fn();
        ++run;
        if (test::failures() != before) std::printf("FAIL %s\n", c.name);
    }
    std::printf("%d tests, %d failures\n", run, test::failures());
    return test::failures() ? 1 : 0;
}
