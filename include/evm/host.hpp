#pragma once

#include <map>
#include <vector>
#include "evm/types.hpp"
#include "evm/uint256.hpp"

namespace evm {

// --- The country outside the office walls ---

// What the block/chain looks like right now (the "government's" answers
// to BLOCKHASH-adjacent questions).
struct BlockContext {
    U256          coinbase;
    std::uint64_t timestamp = 0;
    std::uint64_t number    = 0;
    U256          difficulty;    // PREVRANDAO post-merge
    U256          gas_limit;
    U256          base_fee;
    U256          chain_id;
    U256          gas_price;
};

// The visitor: who arrived at this office, carrying what.
struct CallContext {
    Address address{};      // this office (ADDRESS)
    Address caller{};       // who sent us (CALLER)
    Address origin{};       // who started the whole chain (ORIGIN)
    U256    call_value;     // wei attached (CALLVALUE)
    Bytes   calldata;       // the letter (CALLDATA*)
};

// One notice on the country's bulletin board (LOG0..LOG4).
// Write-only: contracts can post, never read back — observers (explorers,
// indexers) scan these off-chain. Topics are the searchable labels; data
// is the raw body. Cheap storage: the board doesn't live in world state.
struct LogRecord {
    Address           address;   // the business that posted
    Bytes             data;      // the notice body (desk slice)
    std::vector<U256> topics;    // up to 4 labels
};

// The phone line. The interpreter never touches world state directly —
// it asks the Host. Implementations can be a flat map (tests) or a real
// state backend (never, in this project's scope).
class Host {
public:
    virtual ~Host() = default;

    virtual U256 balance(const Address&) const = 0;
    virtual U256 sload(const Address&, const U256& key) const = 0;
    virtual void sstore(const Address&, const U256& key, const U256& value) = 0;
    virtual void log(const Address&, Bytes data, std::vector<U256> topics) = 0;
    virtual BlockContext block() const = 0;
};

// Flat in-memory world — enough for execution-only scope.
// Keyed on raw limbs, so it works before U256 arithmetic exists.
class InMemoryHost : public Host {
public:
    U256 balance(const Address& a) const override;
    U256 sload(const Address& a, const U256& key) const override;
    void sstore(const Address& a, const U256& key, const U256& value) override;
    void log(const Address& a, Bytes data, std::vector<U256> topics) override;
    BlockContext block() const override { return block_; }

    void set_balance(const Address& a, const U256& v) { balances_[a] = limbs_of(v); }
    void set_block(const BlockContext& b) { block_ = b; }
    const std::vector<LogRecord>& logs() const { return logs_; }

private:
    using Limbs = std::array<std::uint64_t, 4>;

    static Limbs limbs_of(const U256& v) { return {v.l[0], v.l[1], v.l[2], v.l[3]}; }

    std::map<Address, Limbs> balances_;
    std::map<Address, std::map<Limbs, Limbs>> storage_;
    std::vector<LogRecord> logs_;
    BlockContext block_{};
};

} // namespace evm
