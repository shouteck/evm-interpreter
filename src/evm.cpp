#include "evm/evm.hpp"

#include <stdexcept>
#include <utility>

namespace evm {

Evm::Evm(Bytes code, Host& host, CallContext call, Gas gas)
    : code_(std::move(code)), host_(host), call_(std::move(call)), gas_(gas) {}

ExecResult Evm::run() {
    while (step()) {}
    return result_;
}

bool Evm::step() {
    if (halted_) return false;

    // M2 is yours: fetch code_[pc_], charge gas, dispatch, mutate
    // stack_/memory_/pc_, and set halted_ + result_ on STOP/RETURN/
    // REVERT/halt. Until then:
    throw std::logic_error("Evm::step() not implemented — that's M2, and it's yours");
}

} // namespace evm
