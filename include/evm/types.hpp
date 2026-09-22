#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace evm {

using Byte    = std::uint8_t;
using Bytes   = std::vector<Byte>;
using Gas     = std::int64_t;               // signed: "out of gas" is detected as gas < 0
using Address = std::array<Byte, 20>;       // 160-bit account address

// Why a run stopped.
enum class StopReason {
    Running,      // step() may continue
    Stop,         // STOP opcode
    Return,       // RETURN — output holds the return data
    Revert,       // REVERT — output holds the revert data
    Halt,         // exceptional halt — see ExecResult::error
};

// Machine error codes, meaningful when StopReason == Halt.
enum class Error {
    None,
    StackUnderflow,
    StackOverflow,
    OutOfGas,
    InvalidOpcode,
    InvalidJump,     // JUMP/JUMPI to a non-JUMPDEST
    OutOfBounds,     // memory offset doesn't fit in size_t
};

struct ExecResult {
    StopReason reason   = StopReason::Halt;
    Error      error    = Error::None;
    Gas        gas_left = 0;
    Bytes      output;
};

inline const char* to_string(StopReason r) {
    switch (r) {
        case StopReason::Running: return "RUNNING";
        case StopReason::Stop:    return "STOP";
        case StopReason::Return:  return "RETURN";
        case StopReason::Revert:  return "REVERT";
        case StopReason::Halt:    return "HALT";
    }
    return "?";
}

inline const char* to_string(Error e) {
    switch (e) {
        case Error::None:           return "none";
        case Error::StackUnderflow: return "stack underflow";
        case Error::StackOverflow:  return "stack overflow";
        case Error::OutOfGas:       return "out of gas";
        case Error::InvalidOpcode:  return "invalid opcode";
        case Error::InvalidJump:    return "invalid jump destination";
        case Error::OutOfBounds:    return "offset out of bounds";
    }
    return "?";
}

} // namespace evm
