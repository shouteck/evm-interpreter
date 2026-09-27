#pragma once

#include <array>
#include <cstddef>
#include "evm/types.hpp"
#include "evm/uint256.hpp"
#include "evm/host.hpp"

namespace evm {

// The clerk. Executes one contract's bytecode against a Host.
//
// Owns the machine state: the stack of paper (<=1024 words), the desk
// (memory, byte-addressed, zero-initialized on expansion), the program
// counter into the code on the wall, and the stamp budget (gas).
//
// The clerk's whole job is step(): read one instruction, charge its gas,
// do what it says, advance pc. run() just calls step() until halted.
class Evm {
public:
    static constexpr std::size_t MAX_STACK = 1024;

    Evm(Bytes code, Host& host, CallContext call, Gas gas);

    ExecResult run();              // step() until halted, return final result
    bool step();                   // execute one instruction; false once halted

    bool halted() const { return halted_; }
    const ExecResult& result() const { return result_; }

    // --- inspection (tests + future tracer) ---
    std::size_t pc() const { return pc_; }
    std::size_t sp() const { return sp_; }
    Gas gas() const { return gas_; }
    const Bytes& memory() const { return memory_; }
    const Bytes& code() const { return code_; }
    const U256& peek(std::size_t i) const { return stack_[sp_ - 1 - i]; } // 0 = top

private:
    Bytes       code_;     // the code on the wall
    Host&       host_;     // phone line to the country
    CallContext call_;     // the visitor + their letter
    Gas         gas_;      // stamp budget

    std::array<U256, MAX_STACK> stack_;
    std::size_t sp_ = 0;         // first empty slot; stack grows upward
    Bytes       memory_;         // the desk — grows lazily, word-aligned
    std::size_t pc_ = 0;         // next instruction

    ExecResult  result_{};
    bool        halted_ = false;

    void push(const U256& v);
    U256 pop();
    void halt(StopReason r, Error e = Error::None);
    void mem_expand(const U256& off, std::size_t len);
    void jump(const U256& dest);
};

} // namespace evm
