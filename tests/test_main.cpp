#include "test_util.hpp"

int main() {
    int failed = 0;
    for (auto& [name, fn] : tst::reg().tests) {
        try {
            fn();
            std::printf("PASS %s\n", name.c_str());
        } catch (const std::exception&) {
            ++failed;
            std::printf("FAIL %s\n", name.c_str());
        }
    }
    std::printf("%zu tests, %d failed\n", tst::reg().tests.size(), failed);
    return failed == 0 ? 0 : 1;
}
