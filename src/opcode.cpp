#include "evm/opcode.hpp"

#include <array>

namespace evm {

bool is_push(std::uint8_t op) { return op >= 0x5f && op <= 0x7f; }
bool is_dup(std::uint8_t op)  { return op >= 0x80 && op <= 0x8f; }
bool is_swap(std::uint8_t op) { return op >= 0x90 && op <= 0x9f; }
bool is_log(std::uint8_t op)  { return op >= 0xa0 && op <= 0xa4; }

unsigned push_size(std::uint8_t op) {
    return is_push(op) ? (op == 0x5f ? 0u : op - 0x5f) : 0u;
}

static constexpr auto NAMES = [] {
    std::array<std::string_view, 256> t{};
    t.fill("UNDEFINED");

    t[0x00] = "STOP";
    t[0x01] = "ADD";       t[0x02] = "MUL";        t[0x03] = "SUB";
    t[0x04] = "DIV";       t[0x05] = "SDIV";       t[0x06] = "MOD";
    t[0x07] = "SMOD";      t[0x08] = "ADDMOD";     t[0x09] = "MULMOD";
    t[0x0a] = "EXP";       t[0x0b] = "SIGNEXTEND";

    t[0x10] = "LT";        t[0x11] = "GT";         t[0x12] = "SLT";
    t[0x13] = "SGT";       t[0x14] = "EQ";         t[0x15] = "ISZERO";
    t[0x16] = "AND";       t[0x17] = "OR";         t[0x18] = "XOR";
    t[0x19] = "NOT";       t[0x1a] = "BYTE";       t[0x1b] = "SHL";
    t[0x1c] = "SHR";       t[0x1d] = "SAR";

    t[0x20] = "SHA3";

    t[0x30] = "ADDRESS";        t[0x31] = "BALANCE";      t[0x32] = "ORIGIN";
    t[0x33] = "CALLER";         t[0x34] = "CALLVALUE";    t[0x35] = "CALLDATALOAD";
    t[0x36] = "CALLDATASIZE";   t[0x37] = "CALLDATACOPY"; t[0x38] = "CODESIZE";
    t[0x39] = "CODECOPY";       t[0x3a] = "GASPRICE";     t[0x3b] = "EXTCODESIZE";
    t[0x3c] = "EXTCODECOPY";    t[0x3d] = "RETURNDATASIZE";
    t[0x3e] = "RETURNDATACOPY"; t[0x3f] = "EXTCODEHASH";

    t[0x40] = "BLOCKHASH";  t[0x41] = "COINBASE";   t[0x42] = "TIMESTAMP";
    t[0x43] = "NUMBER";     t[0x44] = "PREVRANDAO"; t[0x45] = "GASLIMIT";
    t[0x46] = "CHAINID";    t[0x47] = "SELFBALANCE";t[0x48] = "BASEFEE";
    t[0x49] = "BLOBHASH";   t[0x4a] = "BLOBBASEFEE";

    t[0x50] = "POP";      t[0x51] = "MLOAD";   t[0x52] = "MSTORE";
    t[0x53] = "MSTORE8";  t[0x54] = "SLOAD";   t[0x55] = "SSTORE";
    t[0x56] = "JUMP";     t[0x57] = "JUMPI";   t[0x58] = "PC";
    t[0x59] = "MSIZE";    t[0x5a] = "GAS";     t[0x5b] = "JUMPDEST";
    t[0x5c] = "TLOAD";    t[0x5d] = "TSTORE";  t[0x5e] = "MCOPY";
    t[0x5f] = "PUSH0";

    t[0x60]="PUSH1";  t[0x61]="PUSH2";  t[0x62]="PUSH3";  t[0x63]="PUSH4";
    t[0x64]="PUSH5";  t[0x65]="PUSH6";  t[0x66]="PUSH7";  t[0x67]="PUSH8";
    t[0x68]="PUSH9";  t[0x69]="PUSH10"; t[0x6a]="PUSH11"; t[0x6b]="PUSH12";
    t[0x6c]="PUSH13"; t[0x6d]="PUSH14"; t[0x6e]="PUSH15"; t[0x6f]="PUSH16";
    t[0x70]="PUSH17"; t[0x71]="PUSH18"; t[0x72]="PUSH19"; t[0x73]="PUSH20";
    t[0x74]="PUSH21"; t[0x75]="PUSH22"; t[0x76]="PUSH23"; t[0x77]="PUSH24";
    t[0x78]="PUSH25"; t[0x79]="PUSH26"; t[0x7a]="PUSH27"; t[0x7b]="PUSH28";
    t[0x7c]="PUSH29"; t[0x7d]="PUSH30"; t[0x7e]="PUSH31"; t[0x7f]="PUSH32";

    t[0x80]="DUP1";   t[0x81]="DUP2";   t[0x82]="DUP3";   t[0x83]="DUP4";
    t[0x84]="DUP5";   t[0x85]="DUP6";   t[0x86]="DUP7";   t[0x87]="DUP8";
    t[0x88]="DUP9";   t[0x89]="DUP10";  t[0x8a]="DUP11";  t[0x8b]="DUP12";
    t[0x8c]="DUP13";  t[0x8d]="DUP14";  t[0x8e]="DUP15";  t[0x8f]="DUP16";

    t[0x90]="SWAP1";  t[0x91]="SWAP2";  t[0x92]="SWAP3";  t[0x93]="SWAP4";
    t[0x94]="SWAP5";  t[0x95]="SWAP6";  t[0x96]="SWAP7";  t[0x97]="SWAP8";
    t[0x98]="SWAP9";  t[0x99]="SWAP10"; t[0x9a]="SWAP11"; t[0x9b]="SWAP12";
    t[0x9c]="SWAP13"; t[0x9d]="SWAP14"; t[0x9e]="SWAP15"; t[0x9f]="SWAP16";

    t[0xa0]="LOG0"; t[0xa1]="LOG1"; t[0xa2]="LOG2"; t[0xa3]="LOG3"; t[0xa4]="LOG4";

    t[0xf0] = "CREATE";       t[0xf1] = "CALL";      t[0xf2] = "CALLCODE";
    t[0xf3] = "RETURN";       t[0xf4] = "DELEGATECALL"; t[0xf5] = "CREATE2";
    t[0xfa] = "STATICCALL";   t[0xfd] = "REVERT";    t[0xfe] = "INVALID";
    t[0xff] = "SELFDESTRUCT";

    return t;
}();

std::string_view opcode_name(std::uint8_t op) { return NAMES[op]; }

} // namespace evm
