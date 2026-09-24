#include "evm/uint256.hpp"
#include "evm/hex.hpp"

#include <ostream>
#include <stdexcept>
#include <intrin.h>

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

bool operator<(const U256& a, const U256& b)  { 

    for (int i = 3; i >= 0; --i) {
        if (a.l[i] < b.l[i]) return true;
        if (a.l[i] > b.l[i]) return false;
    }

    return false;

}

bool operator<=(const U256& a, const U256& b) { 

    return !(b < a);

}

bool operator>(const U256& a, const U256& b)  { 

    return !(a <= b);

}

bool operator>=(const U256& a, const U256& b) { 

    return !(a < b);

}

U256 operator+(const U256& a, const U256& b) { 

    U256 r;
    uint64_t carry = 0;

    for (int i = 0; i < 4; ++i) {

        uint64_t s = a.l[i] + carry;
        uint64_t c = (s < a.l[i]);
        s += b.l[i];
        c |= (s < b.l[i]);
        r.l[i] = s; carry = c;

    }

    return r;

}

U256 operator-(const U256& a, const U256& b) { 

    U256 r;
    uint64_t borrow = 0;

    for (int i = 0; i < 4; ++i) {
        
        uint64_t c = (a.l[i] < borrow);
        uint64_t s = a.l[i] - borrow;
        c |= (s < b.l[i]);
        s -= b.l[i];
        r.l[i] = s; borrow = c;

    }

    return r;

}

U256 operator*(const U256& a, const U256& b) { 

    U256 r;
    for (int i = 0; i < 4; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j + i < 4; ++j) {
            uint64_t hi;
            uint64_t lo = _umul128(a.l[i], b.l[j], &hi);
            uint64_t s = r.l[i + j] + lo;
            uint64_t c = (s < r.l[i + j]);
            s += carry;
            c += (s < carry);
            r.l[i + j] = s;
            carry = hi + c; // hi <= 2^64-2 always, so this never overflows
        }
    }

    return r;

}

static void divmod(const U256& a, const U256& b, U256& q, U256& r) {  
    for (int i = 255; i >= 0; --i) {
        r = r << 1;
        if (bit(a, i)) r.l[0] |= 1;
        if (r >= b) {
            r = r - b;
            q.l[i / 64] |= (1ull << (i % 64));
        }
    }
    // when the dust settles: q = quotient, r = remainder
}

U256 operator/(const U256& a, const U256& b) { 
    if (b.is_zero()) return U256();
    if (a < b) return U256();  
    U256 q, r; 
    divmod(a, b, q, r);
    return q;

}

U256 operator%(const U256& a, const U256& b) { 
    if (b.is_zero()) return U256();
    U256 q, r; 
    divmod(a, b, q, r);
    return r;

}

U256 sdiv(const U256& a, const U256& b) { 

    /*

    0 - x  =  0 - (2^256 - 5)
        =  wraps to 2^256 - (2^256 - 5)
        =  5

    */

    bool a_neg = bit(a, 255);
    bool b_neg = bit(b, 255);

    U256 mag_a = a_neg ? (U256() - a) : a;
    U256 mag_b = b_neg ? (U256() - b) : b;

    U256 q = mag_a / mag_b;

    if (a_neg != b_neg) {
        q = U256() - q;
    }

    return q;

}

U256 smod(const U256& a, const U256& b) { 

    bool a_neg = bit(a, 255);
    bool b_neg = bit(b, 255);

    U256 mag_a = a_neg ? (U256() - a) : a;
    U256 mag_b = b_neg ? (U256() - b) : b;

    U256 r = mag_a % mag_b;

    if (a_neg) {
        r = U256() - r;
    }

    return r;

}

bool slt(const U256& a, const U256& b)  { 
    
    bool a_neg = bit(a, 255);
    bool b_neg = bit(b, 255);

    if (a_neg != b_neg) return a_neg;
    return a < b;

}

bool sgt(const U256& a, const U256& b)  { 

    bool a_neg = bit(a, 255);
    bool b_neg = bit(b, 255);
    
    if (a_neg != b_neg) return b_neg;
    return a > b;

}

U256 addmod(const U256& a, const U256& b, const U256& n) { 

    if (n.is_zero()) return U256();

    U256 x = a % n; U256 y = b % n;

    if (x >= n - y) {
        return x - (n - y);
    }

    return x + y;

}

U256 mulmod(const U256& a, const U256& b, const U256& n) { 

    if (n.is_zero()) return U256();

    U256 r;
    U256 pile = a % n;

    for (int i = 0; i < 256; ++i) {

        if (bit(b, i)) {
            r = addmod(r, pile, n);
        }

        pile = addmod(pile, pile, n);

    }

    return r;

}

U256 operator&(const U256& a, const U256& b) { 

    U256 r;
    for (int i = 0; i < 4; ++i) {
        r.l[i] = a.l[i] & b.l[i];
    }
    return r;

}

U256 operator|(const U256& a, const U256& b) { 

    U256 r;
    for (int i = 0; i < 4; ++i) {
        r.l[i] = a.l[i] | b.l[i];
    }
    return r;
    
}

U256 operator^(const U256& a, const U256& b) { 
    
    U256 r;
    for (int i = 0; i < 4; ++i) {
        r.l[i] = a.l[i] ^ b.l[i];
    }
    return r;

}

U256 operator~(const U256& a) { 
    
    U256 r;
    for (int i = 0; i < 4; ++i) {
        r.l[i] = ~a.l[i];
    }
    return r;

}

U256 operator<<(const U256& a, unsigned n) { 

    if (n >= 256) return U256();
    int limb_shift = n / 64;
    int bit_shift = n % 64;

    U256 r;
    for (int i = 3; i >= 0; --i) {
        int src = i - limb_shift;
        if (src < 0) continue;
        r.l[i] = a.l[src] << bit_shift;
        if (bit_shift && src - 1 >= 0) {
            r.l[i] |= a.l[src - 1] >> (64 - bit_shift);
        }
    }

    return r;

}

U256 operator>>(const U256& a, unsigned n) { 

    if (n >= 256) return U256();
    int limb_shift = n / 64;
    int bit_shift = n % 64;

    U256 r;
    for (int i = 0; i < 4; ++i) {
        int src = i + limb_shift;
        if (src > 3) continue;
        r.l[i] = a.l[src] >> bit_shift;
        if (bit_shift && src + 1 < 4) {
            r.l[i] |= a.l[src + 1] << (64 - bit_shift);
        }
    }

    return r;    

}

unsigned bit(const U256& a, unsigned i)   { 

    return (a.l[i / 64] >> (i % 64)) & 1;

}

Byte byte_at(const U256& a, unsigned i)   { 

    unsigned j = 31 - i;
    return (a.l[j / 8] >> ((j % 8) * 8)) & 0xff;

}

int bit_length(const U256& a) { 

    for (int i = 3; i >= 0; --i) {
        uint64_t x = a.l[i];
        if (x == 0) continue;
        
        unsigned long idx;
        _BitScanReverse64(&idx, a.l[i]);
        return i * 64 + idx + 1;
    }
    
    return 0;

}

U256 from_bytes(const Byte* p, std::size_t n)   { 

    U256 r;

    if (n > 32) {
        p += n - 32;    // skip the first (n - 32) bytes - the most significant
        n = 32;         // now pretend the array is 32 bytes long
    }

    for (std::size_t k = 0; k < n; ++k) {
        std::size_t j  = n - 1 - k;
        r.l[j / 8] |= (uint64_t)p[k] << ((j % 8) * 8);
    }

    return r;

}

void to_bytes32(const U256& v, Byte out[32]) {  

    for (int i = 0; i < 32; ++i) {
        out[i] = byte_at(v, i);
    }

}


U256 U256::from_hex(std::string_view s) { 

    Bytes b = evm::from_hex(s);
    return from_bytes(b.data(), b.size());

}

std::string U256::to_hex() const { 

    Byte buf[32];
    to_bytes32(*this, buf); // machine to paper
 
    std::string s = evm::to_hex(Bytes(buf, buf + 32)); // "0x0000...00ff"
 
    std::size_t i = s.find_first_not_of('0', 2); // skip leading zeros
    if (i == std::string::npos) return "0x0"; // all zeros
    return "0x" + s.substr(i);

}

std::ostream& operator<<(std::ostream& os, const U256& v) {
    return os << v.to_hex();
}

} // namespace evm
