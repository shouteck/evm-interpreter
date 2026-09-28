// keccak-256 vectors — the seal press exam.
// References: keccak (0x01 padding), NOT FIPS sha3-256 (0x06) — a generic
// sha3 tool will disagree with every value here.
#include "test_util.hpp"

#include "evm/keccak.hpp"
#include "evm/hex.hpp"

#include <cstring>

using namespace evm;

static U256 hash_str(const char* s) {
    return keccak256(reinterpret_cast<const Byte*>(s), std::strlen(s));
}

static U256 hash_n(Byte b, std::size_t n) {
    Bytes buf(n, b);
    return keccak256(buf.data(), buf.size());
}

TEST(keccak_empty) {
    Byte dummy = 0;                                  // need *some* pointer
    CHECK(keccak256(&dummy, 0) ==
          U256::from_hex("c5d2460186f7233c927e7db2dcc703c0e500b653"
                         "ca82273b7bfad8045d85a470"));
}

TEST(keccak_abc) {
    CHECK(hash_str("abc") ==
          U256::from_hex("4e03657aea45a94fc7d47ba826c8d667c0d1e6e3"
                         "3a64a036ec44f58fa12d6c45"));
}

TEST(keccak_transfer_selector) {
    // The envelope's first four bytes for a transfer call are this
    // digest's high word — the function selector.
    U256 h = hash_str("transfer(address,uint256)");
    CHECK(h == U256::from_hex("a9059cbb2ab09eb219583f4a59a5d0623ade346d"
                              "962bcd4e46b11da047c9049b"));
    CHECK_EQ(h.l[3] >> 32, 0xa9059cbbull);           // selector = first 4 bytes
}

TEST(keccak_rate_boundaries) {
    // 135 bytes: pad byte and final 0x80 merge into 0x81 in one block.
    CHECK(hash_n('a', 135) ==
          U256::from_hex("34367dc248bbd832f4e3e69dfaac2f92638bd0bbd18f"
                         "2912ba4ef454919cf446"));
    // 136 bytes: input fills the rate exactly; padding needs a whole
    // extra block (0x01 ... 0x80 in an otherwise-zero block).
    CHECK(hash_n('a', 136) ==
          U256::from_hex("a6c4d403279fe3e0af03729caada8374b5ca54d8065329"
                         "a3ebcaeb4b60aa386e"));
    // 137 bytes: first real multi-block absorb.
    CHECK(hash_n('a', 137) ==
          U256::from_hex("d869f639c7046b4929fc92a4d988a8b22c55fbadb802"
                         "c0c66ebcd484f1915f39"));
    // 200 bytes: two full blocks.
    CHECK(hash_n('a', 200) ==
          U256::from_hex("96ea54061def936c4be90b518992fdc6f12f535068a256"
                         "229aca54267b4d084d"));
}
