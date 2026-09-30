#include "evm/host.hpp"

#include <algorithm>

namespace evm {

U256 InMemoryHost::balance(const Address& a) const {
    U256 out;
    if (auto it = balances_.find(a); it != balances_.end())
        std::copy(it->second.begin(), it->second.end(), out.l);
    return out;
}

U256 InMemoryHost::sload(const Address& a, const U256& key) const {
    U256 out;
    if (auto it = storage_.find(a); it != storage_.end())
        if (auto jt = it->second.find(limbs_of(key)); jt != it->second.end())
            std::copy(jt->second.begin(), jt->second.end(), out.l);
    return out;
}

void InMemoryHost::sstore(const Address& a, const U256& key, const U256& value) {
    storage_[a][limbs_of(key)] = limbs_of(value);
}

void InMemoryHost::log(const Address& a, Bytes data, std::vector<U256> topics) {
    logs_.push_back({a, std::move(data), std::move(topics)});
}

} // namespace evm
