#pragma once

#include <cstddef>
#include "evm/types.hpp"
#include "evm/uint256.hpp"

namespace evm {

// The seal press: keccak-256.
//
// NB: the EVM's SHA3 opcode is *keccak*-256 — the pre-FIPS variant.
// FIPS SHA3-256 differs only in the padding suffix (0x06 vs keccak's
// 0x01); same sponge, different seal. Watch for that when checking
// against generic SHA3 test vectors.
void keccak256(const Byte* data, std::size_t n, Byte out[32]);

// Convenience: digest as a pile-ready word (the 32 bytes read big-endian).
U256 keccak256(const Byte* data, std::size_t n);

} // namespace evm
