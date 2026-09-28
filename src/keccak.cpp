// The seal press: keccak-256.
//
// State: 25 lanes of uint64_t, indexed a[x + 5*y] (a 5x5 waffle).
// Sponge: XOR input into the low RATE bytes of the state (little-endian
// per lane), stir with keccak_f after each full block, pad10*1 the tail,
// then squeeze the first 32 bytes back out (little-endian per lane).
//
//   soak -> stir -> soak -> stir ... -> squeeze
//
// Verified against known-answer vectors in tests/test_keccak.cpp.
// Do not tweak constants or steps — a single wrong bit poisons every output.
#include "evm/keccak.hpp"

#include <cstring>
#include <stdexcept>

namespace evm {
namespace {

// keccak-256: rate 1088 bits = 136 bytes (capacity 512 = the security margin).
constexpr std::size_t RATE = 136;

// rho: rotation offset for lane (x,y), flat as a[x + 5*y].
constexpr unsigned RHO[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14,
};

// iota: one round constant per round, XORed into lane (0,0).
constexpr std::uint64_t RC[24] = {
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
    0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
    0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
    0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
    0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
    0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
    0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
};

std::uint64_t rotl64(std::uint64_t x, unsigned s) {
    return s == 0 ? x : (x << s) | (x >> (64 - s));  // guard: >>64 is UB
}

// The stir: keccak-f[1600], 24 rounds of five steps.
// Lane (x,y) = a[x + 5*y].
void keccak_f(std::uint64_t a[25]) {
    std::uint64_t b[25], C[5], D[5];
    for (unsigned r = 0; r < 24; ++r) {
        // ---- theta: column parities ----
        // squeezes column's rows into one 64 bit summary
        for (int x = 0; x < 5; ++x) 
            C[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        
        // C[(x + 4) % 5] - left neighbor checksum
        // rotl64(C[(x + 1) % 5], 1) - right neighbor checksum, rotated
        // without rotation, every bit-plane is a sealed compartment
        // bit 37 of lane A can only ever touch bit 37 of lanes it influences
        for (int x = 0; x < 5; ++x)
            D[x] = C[(x + 4) % 5] ^ rotl64(C[(x + 1) % 5], 1);

        // write D[x] disturbance into every lane in column x
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                a[x + 5*y] ^= D[x];

        // ---- rho + pi: rotate each lane, then shuffle positions ----
        //   lane (x,y) rotates by RHO, then moves to (y, 2x+3y mod 5)
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 5; ++y)
                b[y + 5*((2*x + 3*y) % 5)] = rotl64(a[x + 5*y], RHO[x + 5*y]);

        // ---- chi: the only nonlinear step; reads b, writes a ----
        //   each lane gets its row-neighbors' bits folded in nonlinearly;
        //   must read from b — writing a in place would corrupt the reads
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                a[x + 5*y] = b[x + 5*y]
                           ^ (~b[(x+1)%5 + 5*y] & b[(x+2)%5 + 5*y]);

        // ---- iota: round constant into lane (0,0); breaks symmetry so
        //    the 24 rounds aren't interchangeable ----
        a[0] ^= RC[r];
    }
}

} // namespace

void keccak256(const Byte* data, std::size_t n, Byte out[32]) {
    std::uint64_t a[25] = {};

    // absorb: XOR data into the low RATE bytes of the state, one block at a
    // time; stir after each full block. Lane packing is little-endian:
    // input byte i lands in lane i/8 at byte i%8.
    while (n >= RATE) {
        for (std::size_t i = 0; i < RATE; ++i)
            a[i / 8] ^= std::uint64_t(data[i]) << (8 * (i % 8));
        keccak_f(a);
        data += RATE;
        n -= RATE;
    }

    // final block: remaining bytes + pad10*1 (keccak suffix 0x01, NOT
    // FIPS sha3's 0x06), high bit set on the last byte of the rate.
    Byte block[RATE] = {};
    std::memcpy(block, data, n);
    block[n] ^= 0x01;          // n < RATE here, so in-bounds
    block[RATE - 1] ^= 0x80;
    for (std::size_t i = 0; i < RATE; ++i)
        a[i / 8] ^= std::uint64_t(block[i]) << (8 * (i % 8));
    keccak_f(a);

    // squeeze: the first 32 bytes of state is the digest (rate > 32, so one
    // stir suffices). Same little-endian lane order on the way out.
    for (unsigned i = 0; i < 32; ++i)
        out[i] = static_cast<Byte>(a[i / 8] >> (8 * (i % 8)));
}

U256 keccak256(const Byte* data, std::size_t n) {
    Byte out[32];
    keccak256(data, n, out);
    return from_bytes(out, 32);   // paper order -> slip
}

} // namespace evm
