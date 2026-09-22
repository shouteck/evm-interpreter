#include "evm/uint256.hpp"

#include <ostream>
#include <stdexcept>

namespace evm {

// ------------------------------------------------------------------
// M1 is yours: implement these. Everything is currently a stub that
// throws — tests will tell you when they're real.
//
// Semantics to hit (Yellow Paper / execution spec):
//   - all arithmetic wraps mod 2^256; there is no overflow
//   - division/modulo by zero yields 0 (not a trap)
//   - sdiv/smod interpret operands as two's-complement 256-bit
//   - shifts by >= 256 yield 0
//   - from_bytes / MLOAD-style reads are BIG-endian
// ------------------------------------------------------------------

[[noreturn]] static void todo() {
    throw std::logic_error("U256 op not implemented — that's M1, and it's yours");
}

bool operator<(const U256&, const U256&)  { todo(); }
bool operator<=(const U256&, const U256&) { todo(); }
bool operator>(const U256&, const U256&)  { todo(); }
bool operator>=(const U256&, const U256&) { todo(); }

U256 operator+(const U256&, const U256&) { todo(); }
U256 operator-(const U256&, const U256&) { todo(); }
U256 operator*(const U256&, const U256&) { todo(); }
U256 operator/(const U256&, const U256&) { todo(); }
U256 operator%(const U256&, const U256&) { todo(); }

U256 sdiv(const U256&, const U256&) { todo(); }
U256 smod(const U256&, const U256&) { todo(); }
bool slt(const U256&, const U256&)  { todo(); }
bool sgt(const U256&, const U256&)  { todo(); }

U256 addmod(const U256&, const U256&, const U256&) { todo(); }
U256 mulmod(const U256&, const U256&, const U256&) { todo(); }

U256 operator&(const U256&, const U256&) { todo(); }
U256 operator|(const U256&, const U256&) { todo(); }
U256 operator^(const U256&, const U256&) { todo(); }
U256 operator~(const U256&)              { todo(); }

U256 operator<<(const U256&, unsigned) { todo(); }
U256 operator>>(const U256&, unsigned) { todo(); }

unsigned bit(const U256&, unsigned)   { todo(); }
Byte byte_at(const U256&, unsigned)   { todo(); }
int bit_length(const U256&)           { todo(); }

U256 from_bytes(const Byte*, std::size_t)   { todo(); }
void to_bytes32(const U256&, Byte[32])      { todo(); }
U256 U256::from_hex(std::string_view)       { todo(); }
std::string U256::to_hex() const            { todo(); }

std::ostream& operator<<(std::ostream& os, const U256& v) {
    return os << v.to_hex();
}

} // namespace evm
