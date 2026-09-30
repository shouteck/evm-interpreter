#pragma once

#include <cstdint>
#include <string_view>

namespace evm {

// Byte values for the Shanghai-era opcode set.
namespace op {

inline constexpr std::uint8_t STOP = 0x00;

inline constexpr std::uint8_t ADD = 0x01, MUL = 0x02, SUB = 0x03, DIV = 0x04,
                              SDIV = 0x05, MOD = 0x06, SMOD = 0x07,
                              ADDMOD = 0x08, MULMOD = 0x09, EXP = 0x0a,
                              SIGNEXTEND = 0x0b;

inline constexpr std::uint8_t LT = 0x10, GT = 0x11, SLT = 0x12, SGT = 0x13,
                              EQ = 0x14, ISZERO = 0x15, AND = 0x16, OR = 0x17,
                              XOR = 0x18, NOT = 0x19, BYTE = 0x1a,
                              SHL = 0x1b, SHR = 0x1c, SAR = 0x1d;

inline constexpr std::uint8_t SHA3 = 0x20;   // keccak256

inline constexpr std::uint8_t ADDRESS = 0x30, BALANCE = 0x31, ORIGIN = 0x32,
                              CALLER = 0x33, CALLVALUE = 0x34,
                              CALLDATALOAD = 0x35, CALLDATASIZE = 0x36,
                              CALLDATACOPY = 0x37, CODESIZE = 0x38,
                              CODECOPY = 0x39, GASPRICE = 0x3a,
                              EXTCODESIZE = 0x3b, EXTCODECOPY = 0x3c,
                              RETURNDATASIZE = 0x3d, RETURNDATACOPY = 0x3e,
                              EXTCODEHASH = 0x3f;

inline constexpr std::uint8_t BLOCKHASH = 0x40, COINBASE = 0x41,
                              TIMESTAMP = 0x42, NUMBER = 0x43,
                              PREVRANDAO = 0x44, GASLIMIT = 0x45,
                              CHAINID = 0x46, SELFBALANCE = 0x47,
                              BASEFEE = 0x48, BLOBHASH = 0x49,
                              BLOBBASEFEE = 0x4a;

inline constexpr std::uint8_t POP = 0x50, MLOAD = 0x51, MSTORE = 0x52,
                              MSTORE8 = 0x53, SLOAD = 0x54, SSTORE = 0x55,
                              JUMP = 0x56, JUMPI = 0x57, PC = 0x58,
                              MSIZE = 0x59, GAS = 0x5a, JUMPDEST = 0x5b,
                              TLOAD = 0x5c, TSTORE = 0x5d, MCOPY = 0x5e,
                              PUSH0 = 0x5f;

inline constexpr std::uint8_t PUSH1 = 0x60;   // .. 0x7f = PUSH32
inline constexpr std::uint8_t DUP1  = 0x80;   // .. 0x8f = DUP16
inline constexpr std::uint8_t SWAP1 = 0x90;   // .. 0x9f = SWAP16
inline constexpr std::uint8_t LOG0 = 0xa0, LOG1 = 0xa1, LOG2 = 0xa2,
                              LOG3 = 0xa3, LOG4 = 0xa4;

inline constexpr std::uint8_t CREATE = 0xf0, CALL = 0xf1, CALLCODE = 0xf2,
                              RETURN = 0xf3, DELEGATECALL = 0xf4,
                              CREATE2 = 0xf5, STATICCALL = 0xfa,
                              REVERT = 0xfd, INVALID = 0xfe,
                              SELFDESTRUCT = 0xff;

} // namespace op

// --- classification helpers ---
bool is_push(std::uint8_t op);              // PUSH0..PUSH32 (0x5f..0x7f)
bool is_dup(std::uint8_t op);               // DUP1..16  (0x80..0x8f)
bool is_swap(std::uint8_t op);              // SWAP1..16 (0x90..0x9f)
bool is_log(std::uint8_t op);               // LOG0..4   (0xa0..0xa4)
unsigned push_size(std::uint8_t op);        // PUSHn -> n (PUSH0 -> 0)

// Canonical name, e.g. opcode_name(0x01) == "ADD".
// Gaps in the encoding -> "UNDEFINED"; 0xfe -> "INVALID".
std::string_view opcode_name(std::uint8_t op);

} // namespace evm
