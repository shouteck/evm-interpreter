// The M1 exam. Tests run in the suggested implementation order:
//   comparisons -> add/sub -> mul -> div/mod -> bitwise -> shifts
//   -> bit/byte access -> signed ops -> addmod/mulmod -> conversions
// Everything is currently red — implement src/uint256.cpp top to bottom
// and watch them go green.
//
// Conventions: U(l0,l1,l2,l3) builds a word (little-endian limbs).
// U256 equality uses == (inline); CHECK_U256 prints limbs on mismatch.
#include "test_util.hpp"

#include "evm/uint256.hpp"

using namespace evm;

static constexpr std::uint64_t M = ~0ull;  // all-ones limb

static U256 U(std::uint64_t l0, std::uint64_t l1 = 0,
              std::uint64_t l2 = 0, std::uint64_t l3 = 0) {
    return U256(l0, l1, l2, l3);
}

// Limb-printing equality check (to_hex isn't implemented yet either).
static void check_u256(const U256& want, const U256& got,
                       const char* file, int line) {
    if (want == got) return;
    std::printf("    want %016llx %016llx %016llx %016llx\n"
                "    got  %016llx %016llx %016llx %016llx\n",
                (unsigned long long)want.l[3], (unsigned long long)want.l[2],
                (unsigned long long)want.l[1], (unsigned long long)want.l[0],
                (unsigned long long)got.l[3],  (unsigned long long)got.l[2],
                (unsigned long long)got.l[1],  (unsigned long long)got.l[0]);
    ::tst::fail(file, line, "U256 mismatch");
}
#define CHECK_U256(want, got) check_u256((want), (got), __FILE__, __LINE__)

// ------------------------------------------------------------------
// 1. Comparisons — scan from the most significant limb; first
//    difference decides.
// ------------------------------------------------------------------

TEST(cmp_less_than) {
    CHECK(U(5) < U(9));
    CHECK(!(U(9) < U(5)));
    CHECK(!(U(7) < U(7)));          // equal is not less

    CHECK(U(M, M, M, 0) < U(0, 0, 0, 1));   // top limb dominates
    CHECK(!(U(0, 0, 0, 1) < U(M, M, M, 0)));
    CHECK(U(5, 0, 1, 0) < U(5, 0, 2, 0));   // decided in l[2]
    CHECK(U(5, 1, 0, 0) < U(5, 2, 0, 0));   // decided in l[1]

    CHECK(U(0) < U256::max());
    CHECK(!(U256::max() < U(0)));
    CHECK(!(U256::max() < U256::max()));
}

TEST(cmp_family) {
    CHECK(U(3) <= U(3));
    CHECK(U(3) <= U(4));
    CHECK(!(U(4) <= U(3)));
    CHECK(U(9) > U(5));
    CHECK(!(U(5) > U(5)));
    CHECK(U(9) >= U(9));
    CHECK(U(9) >= U(5));
    CHECK(!(U(4) >= U(5)));

    // trichotomy on a boundary pair
    U256 a = U(M, M, M, 0), b = U(0, 0, 0, 1);
    CHECK((a < b) + (b < a) + (a == b) == 1);
}

// ------------------------------------------------------------------
// 2. Add/sub — carry chains, borrow chains, wraparound at the page edge.
// ------------------------------------------------------------------

TEST(add_basic) {
    CHECK_U256(U(3), U(1) + U(2));
    CHECK_U256(U(M), U(5) + U(M - 5));
}

TEST(add_carry_chain) {
    CHECK_U256(U(0, 1, 0, 0), U(M, 0, 0, 0) + U(1));  // carry l0 -> l1
    CHECK_U256(U(0, 0, 1, 0), U(M, M, 0, 0) + U(1));  // l0 -> l1 -> l2
    CHECK_U256(U(0, 0, 0, 1), U(M, M, M, 0) + U(1));  // ripples to l3
}

TEST(add_wraps_mod_2_256) {
    CHECK_U256(U(0), U256::max() + U(1));                 // carry falls off the page
    CHECK_U256(U(M - 1, M, M, M), U256::max() + U256::max()); // 2^257 - 2 -> low 256 bits
}

TEST(sub_basic) {
    CHECK_U256(U(2), U(5) - U(3));
    CHECK_U256(U(0), U(7) - U(7));
}

TEST(sub_borrow_chain) {
    CHECK_U256(U(M, 0, 0, 0), U(0, 1, 0, 0) - U(1));  // borrow from l1
    CHECK_U256(U(M, M, 0, 0), U(0, 0, 1, 0) - U(1));  // l2 -> l1 -> l0
}

TEST(sub_wraps_mod_2_256) {
    CHECK_U256(U256::max(), U(0) - U(1));                 // 0 - 1 = 2^256 - 1
    CHECK_U256(U(M - 1, M, M, M), U(0) - U(2));        // -2
}

// ------------------------------------------------------------------
// 3. Multiply — schoolbook partial products, keep the low 256 bits.
// ------------------------------------------------------------------

TEST(mul_basic) {
    CHECK_U256(U(12), U(3) * U(4));
    CHECK_U256(U(0), U(0) * U256::max());
    CHECK_U256(U256::max(), U256::max() * U(1));
}

TEST(mul_across_limbs) {
    CHECK_U256(U(0, 0, 1, 0), U(0, 1, 0, 0) * U(0, 1, 0, 0));   // 2^64 * 2^64 = 2^128
    CHECK_U256(U(0, 0, 0, 1), U(0, 1, 0, 0) * U(0, 0, 1, 0));   // 2^64 * 2^128 = 2^192
}

TEST(mul_truncates_high_bits) {
    CHECK_U256(U(M - 1, M, M, M), U256::max() * U(2));             // 2^257-2 -> low half
    CHECK_U256(U(1), U256::max() * U256::max());                      // (2^256-1)^2 mod 2^256 = 1
    CHECK_U256(U(0), U(0, 0, 1, 0) * U(0, 0, 1, 0));            // 2^256 -> 0
}

// ------------------------------------------------------------------
// 4. Division/modulo — the slow chore. x/0 == x%0 == 0, per the rulebook.
// ------------------------------------------------------------------

TEST(div_basic) {
    CHECK_U256(U(4), U(20) / U(5));
    CHECK_U256(U(6), U(20) / U(3));
    CHECK_U256(U(1), U(7) / U(7));
    CHECK_U256(U(0), U(3) / U(5));       // smaller / larger = 0
    CHECK_U256(U(0), U(0) / U(9));
    CHECK_U256(U(7), U(7) / U(1));
}

TEST(div_by_zero_is_zero) {
    CHECK_U256(U(0), U(5) / U(0));
    CHECK_U256(U(0), U256::max() / U(0));
}

TEST(mod_basic) {
    CHECK_U256(U(0), U(20) % U(5));
    CHECK_U256(U(2), U(20) % U(3));
    CHECK_U256(U(3), U(3) % U(5));       // a % b = a when a < b
    CHECK_U256(U(0), U(7) % U(7));
}

TEST(mod_by_zero_is_zero) {
    CHECK_U256(U(0), U(5) % U(0));
}

TEST(div_mod_wide) {
    CHECK_U256(U(M, M, M, 0x7fffffffffffffffull), U256::max() / U(2));  // = max >> 1
    CHECK_U256(U(0, 1, 0, 0), U(0, 0, 1, 0) / U(0, 1, 0, 0));        // 2^128 / 2^64 = 2^64
    CHECK_U256(U(0), U(0, 0, 1, 0) % U(0, 1, 0, 0));                 // 2^128 % 2^64 = 0
}

// ------------------------------------------------------------------
// 5. Bitwise — per-limb ops.
// ------------------------------------------------------------------

TEST(bitwise_ops) {
    CHECK_U256(U(0x00ff00ff00ff00ffull, 0x0f0f0f0f0f0f0f0full),
               U(0x00ff00ff00ff00ffull, 0xffffffffffffffffull) &
               U(0xffffffffffffffffull, 0x0f0f0f0f0f0f0f0full));

    CHECK_U256(U(M), U(0xff00ff00ff00ff00ull) | U(0x00ff00ff00ff00ffull));
    CHECK_U256(U(0), U(M) ^ U(M));
    CHECK_U256(U(M), ~U(0));
    CHECK_U256(U(0), ~U256::max());
}

// ------------------------------------------------------------------
// 6. Shifts — slide the chunks; n >= 256 pushes it off the page.
// ------------------------------------------------------------------

TEST(shl_basic) {
    CHECK_U256(U(6), U(3) << 1);
    CHECK_U256(U(0, 1, 0, 0), U(1) << 64);
    CHECK_U256(U(0, 0, 1, 0), U(1) << 128);
    CHECK_U256(U(0, 0, 0, 0x8000000000000000ull), U(1) << 255);
}

TEST(shl_boundaries) {
    CHECK_U256(U(0), U(1) << 256);
    CHECK_U256(U(0), U256::max() << 256);
    CHECK_U256(U(5), U(5) << 0);
    // bits crossing a limb boundary mid-shift
    CHECK_U256(U(0, M, 0, 0), U(M, 0, 0, 0) << 64);
    CHECK_U256(U(0x8000000000000000ull, 0, 0, 0), U(1) << 63);
}

TEST(shr_basic) {
    CHECK_U256(U(3), U(6) >> 1);
    CHECK_U256(U(1), U(0, 1, 0, 0) >> 64);
    CHECK_U256(U(1), U256::max() >> 255);
    CHECK_U256(U(M, M, 0, 0), U256::max() >> 128);
}

TEST(shr_boundaries) {
    CHECK_U256(U(0), U(1) >> 256);
    CHECK_U256(U(5), U(5) >> 0);
    CHECK_U256(U(1), U(0x8000000000000000ull, 0, 0, 0) >> 63);
    CHECK_U256(U(1), U(0, 1, 0, 0) >> 64);   // roundtrip with shl
}

// ------------------------------------------------------------------
// 7. Bit / byte access — the indexing conventions matter here.
//    bit(i): i=0 is the LEAST significant bit.
//    byte_at(i): i=0 is the MOST significant byte (BYTE opcode order).
// ------------------------------------------------------------------

TEST(bit_access) {
    CHECK_EQ(bit(U(1), 0), 1u);
    CHECK_EQ(bit(U(1), 1), 0u);
    CHECK_EQ(bit(U(0, 1, 0, 0), 64), 1u);
    CHECK_EQ(bit(U(0, 1, 0, 0), 65), 0u);
    CHECK_EQ(bit(U256::max(), 255), 1u);
    CHECK_EQ(bit(U256::max(), 0), 1u);
}

TEST(byte_access) {
    // l[3] = 0x0102030405060708 occupies bytes 0..7 (most significant first),
    // l[0] = 0x8877665544332211 occupies bytes 24..31.
    U256 v = U(0x8877665544332211ull, 0, 0, 0x0102030405060708ull);
    CHECK_EQ(byte_at(v, 0), 0x01);
    CHECK_EQ(byte_at(v, 7), 0x08);
    CHECK_EQ(byte_at(v, 24), 0x88);
    CHECK_EQ(byte_at(v, 31), 0x11);
    CHECK_EQ(byte_at(v, 8), 0x00);          // l[2] is zero
}

TEST(bit_len) {
    CHECK_EQ(bit_length(U(0)), 0);
    CHECK_EQ(bit_length(U(1)), 1);
    CHECK_EQ(bit_length(U(2)), 2);
    CHECK_EQ(bit_length(U(3)), 2);
    CHECK_EQ(bit_length(U(M)), 64);
    CHECK_EQ(bit_length(U(0, 1, 0, 0)), 65);
    CHECK_EQ(bit_length(U256::max()), 256);
}

// ------------------------------------------------------------------
// 8. Signed ops — same ink, different reading. Top bit of l[3] = sign.
//    -1 is U256::max(), -2 is U(M-1, M, M, M).
// ------------------------------------------------------------------

TEST(signed_compare) {
    CHECK(slt(U256::max(), U(1)));              // -1 < 1
    CHECK(!slt(U(1), U256::max()));             // 1 > -1
    CHECK(!slt(U256::max(), U256::max()));         // -1 !< -1
    CHECK(!slt(U(0), U256::max()));             // 0 > -1
    CHECK(sgt(U(1), U256::max()));              // 1 > -1
    CHECK(!sgt(U256::max(), U(1)));             // -1 < 1
    CHECK(slt(U(M - 1, M, M, M), U(5)));     // -2 < 5
    CHECK(slt(U(M - 1, M, M, M), U256::max())); // -2 < -1
}

TEST(sdiv_basic) {
    CHECK_U256(U(4), sdiv(U(20), U(5)));
    CHECK_U256(U(0), sdiv(U(5), U(0)));

    // -4 / 2 = -2            (-4 = U(~0-3,...), -2 = U(~0-1,...))
    CHECK_U256(U(M - 1, M, M, M), sdiv(U(M - 3, M, M, M), U(2)));
    // 5 / -3 = -1 (truncate toward zero), -5 / -3 = 1, -5 / 3 = -1
    CHECK_U256(U256::max(), sdiv(U(5), U(M - 2, M, M, M)));
    CHECK_U256(U(1), sdiv(U(M - 4, M, M, M), U(M - 2, M, M, M)));
    CHECK_U256(U256::max(), sdiv(U(M - 4, M, M, M), U(3)));
}

TEST(smod_basic) {
    CHECK_U256(U(2), smod(U(20), U(3)));
    CHECK_U256(U(0), smod(U(5), U(0)));

    // sign follows the DIVIDEND: smod(-5,3) = -2, smod(5,-3) = 2
    CHECK_U256(U(M - 1, M, M, M), smod(U(M - 4, M, M, M), U(3)));
    CHECK_U256(U(2), smod(U(5), U(M - 2, M, M, M)));
    CHECK_U256(U(M - 1, M, M, M), smod(U(M - 4, M, M, M), U(M - 2, M, M, M)));
}

TEST(sdiv_overflow_edge) {
    // min_int / -1 = -2^255 / -1 = +2^255, which doesn't fit signed ->
    // wraps back to 2^255 unsigned. U(0,0,0,0x8000...) both ways.
    U256 min_int = U(0, 0, 0, 0x8000000000000000ull);
    CHECK_U256(min_int, sdiv(min_int, U256::max()));
}

// ------------------------------------------------------------------
// 9. addmod/mulmod — the trap: the intermediate doesn't fit on the page.
//    (a op b) mod n must be computed EXACTLY, not via wrapped op then mod.
// ------------------------------------------------------------------

TEST(addmod_basic) {
    CHECK_U256(U(0), addmod(U(2), U(3), U(5)));
    CHECK_U256(U(5), addmod(U(2), U(3), U(7)));
    CHECK_U256(U(3), addmod(U(10), U(0), U(7)));
    CHECK_U256(U(0), addmod(U(2), U(3), U(0)));   // n=0 -> 0
}

TEST(addmod_no_wrap_trap) {
    // max + max = 2^257 - 2. True answer mod 3 is 0; the naive
    // (a+b)%n computes (2^256-2) % 3 = 2. Watch for it.
    CHECK_U256(U(0), addmod(U256::max(), U256::max(), U(3)));
    // same trap, mod 5: true = 0, naive = 4
    CHECK_U256(U(0), addmod(U256::max(), U256::max(), U(5)));
    // max + 1 = 2^256 ≡ 0 mod 4 (and naive wrapped value 0 also gives 0 —
    // so don't stop at cases where both agree)
    CHECK_U256(U(0), addmod(U256::max(), U(1), U(4)));
}

TEST(mulmod_basic) {
    CHECK_U256(U(2), mulmod(U(3), U(4), U(5)));
    CHECK_U256(U(4), mulmod(U(5), U(5), U(7)));
    CHECK_U256(U(3), mulmod(U(10), U(1), U(7)));
    CHECK_U256(U(0), mulmod(U(3), U(4), U(0)));   // n=0 -> 0
}

TEST(mulmod_no_wrap_trap) {
    // 2^128 * 2^128 = 2^256 ≡ 1 mod 3; naive (a*b)%n wraps to 0 first.
    CHECK_U256(U(1), mulmod(U(0, 0, 1, 0), U(0, 0, 1, 0), U(3)));
    // max * 2 = 2^257 - 2 ≡ 0 mod 10; naive computes (2^256-2) % 10 = 4.
    CHECK_U256(U(0), mulmod(U256::max(), U(2), U(10)));
    // sanity where both agree: (2^256-1)^2 mod 3 = 1 (and naive also gets 1)
    CHECK_U256(U(1), mulmod(U256::max(), U256::max(), U(3)));
}

// ------------------------------------------------------------------
// 10. Conversions — wire format is big-endian; limbs are little-endian.
//     from_bytes takes the LOW 256 bits if n > 32 (rightmost bytes win).
// ------------------------------------------------------------------

TEST(from_bytes_be) {
    Byte one[] = {0x01};
    CHECK_U256(U(1), from_bytes(one, 1));

    Byte two[] = {0x01, 0x00};
    CHECK_U256(U(256), from_bytes(two, 2));

    Byte wide[32]{};
    wide[31] = 0x01;
    CHECK_U256(U(1), from_bytes(wide, 32));
    wide[31] = 0;
    wide[0] = 0x01;
    CHECK_U256(U(0, 0, 0, 0x0100000000000000ull), from_bytes(wide, 32));

    Byte over[33]{};
    over[0] = 0xff;                        // falls off: only rightmost 32 count
    CHECK_U256(U(0), from_bytes(over, 33));
}

TEST(to_bytes32_roundtrip) {
    Byte out[32];
    to_bytes32(U(1), out);
    CHECK_EQ(out[31], 0x01);
    CHECK_EQ(out[0], 0x00);

    U256 v = U(0x8877665544332211ull, 0, 0, 0x0102030405060708ull);
    to_bytes32(v, out);
    CHECK_U256(v, from_bytes(out, 32));
}

TEST(hex_io) {
    CHECK_U256(U(255), U256::from_hex("0xff"));
    CHECK_U256(U(255), U256::from_hex("ff"));
    CHECK_U256(U(0), U256::from_hex("0x0"));
    CHECK_U256(U256::max(), U256::from_hex(
        "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    CHECK(U(0).to_hex() == "0x0");
    CHECK(U(255).to_hex() == "0xff");
    CHECK(U(256).to_hex() == "0x100");

    U256 v = U(M, M, 0xdead, 0xbeef);
    CHECK(U256::from_hex(v.to_hex()) == v);
}

// ------------------------------------------------------------------
// 11. Property checks — deterministic RNG; invariants that must hold
//     for ANY words, plus the uint64 oracle where values fit.
// ------------------------------------------------------------------

static std::uint64_t rng_s = 0x9e3779b97f4a7c15ull;
static std::uint64_t nextrand() {
    rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
    return rng_s;
}
static U256 rand_u256() { return U(nextrand(), nextrand(), nextrand(), nextrand()); }

TEST(prop_ring_invariants) {
    for (int i = 0; i < 500; ++i) {
        U256 a = rand_u256(), b = rand_u256();

        CHECK_U256(a, (a + b) - b);          // ring: add then sub roundtrips
        CHECK_U256(U(0), a ^ a);
        CHECK_U256(a, ~(~a));
        CHECK((a < b) + (b < a) + (a == b) == 1);   // trichotomy

        if (!b.is_zero()) {
            CHECK_U256(a, (a / b) * b + (a % b));   // division identity
            CHECK(a % b < b);
        }
    }
}

TEST(prop_u64_oracle) {
    // for values that fit in 64 bits, plain uint64 math is ground truth
    for (int i = 0; i < 500; ++i) {
        std::uint64_t x = nextrand(), y = nextrand() | 1;  // y != 0
        CHECK_EQ(U(x).low64() + 0, x);                     // sanity
        CHECK((U(x) < U(y)) == (x < y));
        CHECK_U256(U(x + y), U(x) + U(y));
        CHECK_U256(U(x - y), U(x) - U(y));
        CHECK_U256(U(x / y), U(x) / U(y));
        CHECK_U256(U(x % y), U(x) % U(y));
        if (x < (1ull << 32) && y < (1ull << 32))
            CHECK_U256(U(x * y), U(x) * U(y));
    }
}
