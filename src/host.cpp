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
    journal_.push_back({JournalEntry::Store, a, key, sload(a, key)});  // receipt first
    storage_[a][limbs_of(key)] = limbs_of(value);
}

void InMemoryHost::log(const Address& a, Bytes data, std::vector<U256> topics) {
    logs_.push_back({a, std::move(data), std::move(topics)});
    journal_.push_back({JournalEntry::Log, a, {}, {}});                // a post is undoable too
}

const Bytes& InMemoryHost::code(const Address& a) const {
    static const Bytes empty;
    if (auto it = code_.find(a); it != code_.end()) return it->second;
    return empty;
}

bool InMemoryHost::exists(const Address& a) const {
    return balances_.count(a) || code_.count(a) || storage_.count(a) || nonces_.count(a);
}

U256 InMemoryHost::blockhash(std::uint64_t n) const {
    if (auto it = hashes_.find(n); it != hashes_.end()) return it->second;
    return U256();
}

bool InMemoryHost::transfer(const Address& from, const Address& to, const U256& value) {
    U256 fb = balance(from);
    if (fb < value) return false;                       // not enough cash — call fails
    journal_.push_back({JournalEntry::Balance, from, {}, fb});
    journal_.push_back({JournalEntry::Balance, to,   {}, balance(to)});
    balances_[from] = limbs_of(fb - value);
    balances_[to]   = limbs_of(balance(to) + value);
    return true;
}

std::uint64_t InMemoryHost::nonce(const Address& a) const {
    if (auto it = nonces_.find(a); it != nonces_.end()) return it->second;
    return 0;
}

void InMemoryHost::bump_nonce(const Address& a) {
    journal_.push_back({JournalEntry::Nonce, a, {}, U256(nonce(a))});
    ++nonces_[a];
}

void InMemoryHost::create_account(const Address& a) {
    journal_.push_back({JournalEntry::Account, a, {}, {}});
    nonces_.emplace(a, 0);
}

void InMemoryHost::install_code(const Address& a, Bytes code) {
    code_[a] = std::move(code);   // journaled by the surrounding Account entry
}

void InMemoryHost::kill(const Address& self, const Address& beneficiary) {
    transfer(self, beneficiary, balance(self));   // sweep the cash; journalled
}

// Pop receipts back down to `cp`, each restoring what it recorded.
// Reverse order matters: a slot written twice unwinds newest-first.
void InMemoryHost::revert(std::size_t cp) {
    while (journal_.size() > cp) {
        JournalEntry e = std::move(journal_.back());
        journal_.pop_back();
        switch (e.kind) {
            case JournalEntry::Store: {
                auto& slots = storage_[e.addr];
                auto k = limbs_of(e.key);
                if (e.old == U256()) slots.erase(k);
                else                 slots[k] = limbs_of(e.old);
                break;
            }
            case JournalEntry::Balance:
                if (e.old == U256()) balances_.erase(e.addr);
                else                 balances_[e.addr] = limbs_of(e.old);
                break;
            case JournalEntry::Log:
                logs_.pop_back();
                break;
            case JournalEntry::Nonce:
                nonces_[e.addr] = e.old.low64();
                break;
            case JournalEntry::Account:          // business un-registers
                nonces_.erase(e.addr);
                code_.erase(e.addr);
                storage_.erase(e.addr);
                break;
        }
    }
}

} // namespace evm
