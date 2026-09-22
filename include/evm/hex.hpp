#pragma once

#include <string>
#include <string_view>
#include "evm/types.hpp"

namespace evm {

// "0x6001" / "6001" -> {0x60, 0x01}. Odd digit counts left-pad a zero
// nibble ("0x123" -> {0x01, 0x23}). Throws std::invalid_argument on bad chars.
Bytes from_hex(std::string_view s);

// {0x60, 0x01} -> "0x6001" (lowercase, always even digits).
std::string to_hex(const Bytes& b);

} // namespace evm
