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
    Address  address{};     // this office (ADDRESS)
    Address  caller{};      // who sent us (CALLER)
    Address  origin{};      // who started the whole chain (ORIGIN)
    U256     call_value;    // wei attached (CALLVALUE)
    Bytes    calldata;      // the letter (CALLDATA*)
    unsigned depth = 0;     // how deep in the call chain (limit 1024)
    bool     is_static = false;  // read-only frame: SSTORE/LOG/etc. halt inside
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

    // --- CALL support ---
    virtual const Bytes& code(const Address&) const = 0;                 // callee's wall
    virtual bool transfer(const Address& from, const Address& to,
                          const U256& value) = 0;                        // cash move; false if broke
    virtual bool exists(const Address&) const = 0;                       // touched account?
    virtual U256 blockhash(std::uint64_t n) const = 0;                   // recent-block lookup (0 outside window)

    // --- CREATE/SELFDESTRUCT support ---
    virtual std::uint64_t nonce(const Address&) const = 0;               // create-count, feeds addr derivation
    virtual void bump_nonce(const Address&) = 0;
    virtual void create_account(const Address&) = 0;                     // register a fresh business
    virtual void install_code(const Address&, Bytes) = 0;                // init code's return becomes the wall
    virtual void kill(const Address& self, const Address& beneficiary) = 0;

    // Speculative state: every write pushes an undo receipt.
    // checkpoint() = current journal height; revert() replays backwards.
    // Commit is implicit — entries stay, merging into the parent's speculation.
    virtual std::size_t checkpoint() = 0;
    virtual void revert(std::size_t cp) = 0;
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

    const Bytes& code(const Address& a) const override;
    bool transfer(const Address& from, const Address& to, const U256& value) override;
    bool exists(const Address& a) const override;
    U256 blockhash(std::uint64_t n) const override;
    std::uint64_t nonce(const Address& a) const override;
    void bump_nonce(const Address& a) override;
    void create_account(const Address& a) override;
    void install_code(const Address& a, Bytes code) override;
    void kill(const Address& self, const Address& beneficiary) override;
    std::size_t checkpoint() override { return journal_.size(); }
    void revert(std::size_t cp) override;

    void set_balance(const Address& a, const U256& v) { balances_[a] = limbs_of(v); } // setup: unjournaled
    void set_block(const BlockContext& b) { block_ = b; }
    void deploy(const Address& a, Bytes code) { code_[a] = std::move(code); }
    void set_blockhash(std::uint64_t n, const U256& h) { hashes_[n] = h; }
    const std::vector<LogRecord>& logs() const { return logs_; }

private:
    using Limbs = std::array<std::uint64_t, 4>;

    static Limbs limbs_of(const U256& v) { return {v.l[0], v.l[1], v.l[2], v.l[3]}; }

    // One undo receipt — records what a write *was* so revert() can put it back.
    struct JournalEntry {
        enum Kind { Store, Balance, Log, Nonce, Account } kind;
        Address addr;
        U256    key;       // cabinet slot (Store only)
        U256    old;       // previous value (Store/Balance); prev nonce low64 (Nonce)
    };

    std::map<Address, Limbs> balances_;
    std::map<Address, std::map<Limbs, Limbs>> storage_;
    std::map<Address, Bytes> code_;
    std::map<Address, std::uint64_t> nonces_;
    std::map<std::uint64_t, U256> hashes_;
    std::vector<LogRecord> logs_;
    std::vector<JournalEntry> journal_;
    BlockContext block_{};
};

} // namespace evm
