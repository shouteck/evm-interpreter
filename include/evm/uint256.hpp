#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include "evm/types.hpp"

namespace evm {

// 256-bit unsigned integer: the EVM's machine word.
//
// Four 64-bit limbs, little-endian limb order (l[0] is least significant).
// All arithmetic wraps mod 2^256 — EVM words have no overflow.
//
// Provided for free (harness): constructors, ==, !=, is_zero, low64,
// ostream<< (once to_hex works).
// Yours to implement in uint256.cpp (M1): everything else.
struct U256 {
    std::uint64_t l[4];

    constexpr U256() : l{0, 0, 0, 0} {}
    constexpr U256(std::uint64_t v) : l{v, 0, 0, 0} {}
    constexpr U256(std::uint64_t l0, std::uint64_t l1,
                   std::uint64_t l2, std::uint64_t l3) : l{l0, l1, l2, l3} {}

    bool is_zero() const { return (l[0] | l[1] | l[2] | l[3]) == 0; }
    std::uint64_t low64() const { return l[0]; }

    static U256 max() { return {~0ull, ~0ull, ~0ull, ~0ull}; }

    static U256 from_hex(std::string_view s);   // optional 0x prefix
    std::string to_hex() const;                 // "0x"-prefixed, "0x0" for zero
};

// --- comparisons (== and != are free; ordering is yours) ---
constexpr bool operator==(const U256& a, const U256& b) {
    return a.l[0] == b.l[0] && a.l[1] == b.l[1] &&
           a.l[2] == b.l[2] && a.l[3] == b.l[3];
}
constexpr bool operator!=(const U256& a, const U256& b) { return !(a == b); }

bool operator<(const U256&, const U256&);
bool operator<=(const U256&, const U256&);
bool operator>(const U256&, const U256&);
bool operator>=(const U256&, const U256&);

// --- arithmetic: all wrap mod 2^256 ---
U256 operator+(const U256&, const U256&);
U256 operator-(const U256&, const U256&);
U256 operator*(const U256&, const U256&);   // low 256 bits of the product
U256 operator/(const U256&, const U256&);   // x / 0 == 0  (EVM semantics)
U256 operator%(const U256&, const U256&);   // x % 0 == 0

// Signed variants: operands interpreted as two's-complement 256-bit.
// (a/b, a%b with division-by-zero -> 0, per Yellow Paper SDIV/SMOD.)
U256 sdiv(const U256&, const U256&);
U256 smod(const U256&, const U256&);

// Signed comparisons (SLT / SGT opcodes).
bool slt(const U256&, const U256&);
bool sgt(const U256&, const U256&);

// Modular arithmetic (ADDMOD / MULMOD): (a op b) mod n, computed exactly —
// no wraparound allowed even when a+b or a*b exceeds 256 bits. n==0 -> 0.
U256 addmod(const U256& a, const U256& b, const U256& n);
U256 mulmod(const U256& a, const U256& b, const U256& n);

// --- bitwise ---
U256 operator&(const U256&, const U256&);
U256 operator|(const U256&, const U256&);
U256 operator^(const U256&, const U256&);
U256 operator~(const U256&);

// Shift by n bits; n >= 256 yields 0 (matches SHL/SHR semantics).
U256 operator<<(const U256&, unsigned n);
U256 operator>>(const U256&, unsigned n);

// --- bit / byte access ---
unsigned bit(const U256&, unsigned i);   // i in [0,256), 0 = least significant
Byte byte_at(const U256&, unsigned i);   // i in [0,32), 0 = MOST significant (BYTE opcode)
int bit_length(const U256&);             // 0 for zero, else index of MSB + 1

// --- conversions ---
// Big-endian byte input (EVM convention): p[0] is the most significant byte.
U256 from_bytes(const Byte* p, std::size_t n);
void to_bytes32(const U256&, Byte out[32]);        // big-endian output

std::ostream& operator<<(std::ostream&, const U256&);

} // namespace evm
