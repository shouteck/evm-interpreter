#include "evm/hex.hpp"

#include <stdexcept>

namespace evm {

static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("from_hex: bad character");
}

Bytes from_hex(std::string_view s) {
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);

    Bytes out;
    out.reserve((s.size() + 1) / 2);

    std::size_t i = 0;
    if (s.size() % 2 == 1) {
        out.push_back(static_cast<Byte>(nibble(s[0])));
        i = 1;
    }
    for (; i + 1 < s.size(); i += 2)
        out.push_back(static_cast<Byte>((nibble(s[i]) << 4) | nibble(s[i + 1])));
    return out;
}

std::string to_hex(const Bytes& b) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "0x";
    out.reserve(2 + b.size() * 2);
    for (Byte x : b) {
        out += digits[x >> 4];
        out += digits[x & 0x0f];
    }
    return out;
}

} // namespace evm
