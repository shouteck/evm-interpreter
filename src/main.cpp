// evm_run — run a bytecode blob and print the outcome.
//
//   evm_run 0x6001600101        bytecode as a hex literal
//   evm_run path/to/code.hex    bytecode from a file

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "evm/evm.hpp"
#include "evm/hex.hpp"
#include "evm/opcode.hpp"

using namespace evm;

static void dump(const Evm& vm) {
    const ExecResult& r = vm.result();
    std::printf("reason   : %s\n", to_string(r.reason));
    if (r.error != Error::None)
        std::printf("error    : %s\n", to_string(r.error));
    std::printf("gas left : %lld\n", static_cast<long long>(r.gas_left));
    if (!r.output.empty())
        std::printf("output   : %s\n", to_hex(r.output).c_str());
    std::printf("stack    : %zu word(s)%s\n", vm.sp(), vm.sp() ? ", top first:" : "");
    for (std::size_t i = 0; i < vm.sp() && i < 16; ++i) {
        std::printf("  [%zu] ", i);
        std::cout << vm.peek(i) << "\n";
    }
    if (vm.sp() > 16) std::printf("  ... (%zu more)\n", vm.sp() - 16);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: evm_run <0xHEX | file.hex>\n");
        return 2;
    }

    Bytes code;
    try {
        std::string arg = argv[1];
        std::ifstream f(arg);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            code = from_hex(ss.str());
        } else {
            code = from_hex(arg);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bad bytecode: %s\n", e.what());
        return 2;
    }

    InMemoryHost host;
    Evm vm(code, host, CallContext{}, /*gas*/ 10'000'000);

    try {
        vm.run();
    } catch (const std::exception& e) {
        std::printf("threw    : %s\n", e.what());
        return 1;
    }
    dump(vm);
    return 0;
}
