#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Minimal test harness: TEST(name) registers, CHECK/CHECK_EQ abort the test
// on first failure, test_main runs everything and reports.
namespace tst {

struct Registry {
    std::vector<std::pair<std::string, std::function<void()>>> tests;
};

inline Registry& reg() {
    static Registry r;
    return r;
}

struct Add {
    Add(std::string name, std::function<void()> fn) {
        reg().tests.push_back({std::move(name), std::move(fn)});
    }
};

[[noreturn]] inline void fail(const char* file, int line, const std::string& msg) {
    std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
    throw std::runtime_error("check failed");
}

} // namespace tst

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

#define TEST(name)                                                        \
    static void name();                                                   \
    static ::tst::Add CONCAT(_reg_, name)(#name, name);                   \
    static void name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) ::tst::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        auto _va = (a);                                                   \
        auto _vb = (b);                                                   \
        if (!(_va == _vb)) {                                              \
            std::ostringstream _os;                                       \
            _os << "CHECK_EQ(" #a ", " #b ") -> " << _va << " vs " << _vb;\
            ::tst::fail(__FILE__, __LINE__, _os.str());                   \
        }                                                                 \
    } while (0)
