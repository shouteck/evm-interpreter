// Scenario runner for the GitHub Pages demo — dual-mode:
//   native:   evm_scenario <file.json> -> trace JSON on stdout
//   wasm:     evm_run_scenario(json)   -> trace JSON string (emscripten)
//
// Scenario JSON:
//   { "accounts": { addr: {balance, nonce, code, storage:{k:v}} },
//     "storage_auto": [{acct, holder, slot, value}],     // keccak(holder||slot)
//     "block": {coinbase, timestamp, number, difficulty, gasLimit},
//     "exec": {address, caller, origin, value, data, gas, gasPrice} }
//
// Output JSON:
//   { "trace": [{d, a, pc, op, name, gas, sp, stack[], mem, jn, ln, ev[]}],
//     "result": {reason, error, gas_left, output},
//     "accounts": [{addr, balance, nonce, storage:[{k,v}]}],
//     "logs": [{addr, topics[], data}] }

#include "evm/evm.hpp"
#include "evm/hex.hpp"
#include "evm/host.hpp"
#include "evm/keccak.hpp"
#include "evm/opcode.hpp"
#include "json.hpp"

#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace evm;

// ---- json escaping / hex helpers --------------------------------------------

static std::string hex_addr(const Address& a) { return to_hex(Bytes(a.begin(), a.end())); }

static std::string hex_u(const U256& v) {
    Byte b[32]; to_bytes32(v, b);
    // trim leading zero bytes, keep at least one
    int i = 0; while (i < 31 && b[i] == 0) ++i;
    return to_hex(Bytes(b + i, b + 32));
}

static Address to_addr(const std::string& h) {
    Bytes b = from_hex(h);
    Address a{};
    if (b.size() <= 20) std::copy(b.begin(), b.end(), a.end() - b.size());
    else                std::copy(b.end() - 20, b.end(), a.begin());
    return a;
}

static U256 to_u256(const std::string& h) {
    if (h.empty() || h == "0x") return U256();
    return U256::from_hex(h);
}

static Gas to_gas(const std::string& h) {
    U256 v = to_u256(h);
    return v.l[1] || v.l[2] || v.l[3] ? (Gas)INT64_MAX : (Gas)v.low64();
}

static U256 limbs_to_u256(const std::array<std::uint64_t,4>& l) {
    U256 v; v.l[0]=l[0]; v.l[1]=l[1]; v.l[2]=l[2]; v.l[3]=l[3]; return v;
}

// keccak(pad32(holder) || pad32(slot)) — where a mapping entry actually lives
static U256 mapping_slot(const Address& holder, const U256& slot) {
    Byte buf[64] = {};
    std::copy(holder.begin(), holder.end(), buf + 12);
    Byte sb[32]; to_bytes32(slot, sb);
    std::memcpy(buf + 32, sb, 32);
    Byte h[32]; keccak256(buf, 64, h);
    return from_bytes(h, 32);
}

// ---- the run -----------------------------------------------------------------

std::string run_scenario_json(const std::string& input) {
    Json t = parse_json(input);
    InMemoryHost host;
    std::set<Address> known;

    for (auto& [a, acc] : t["accounts"].o) {
        Address addr = to_addr(a);
        known.insert(addr);
        if (acc.has("balance")) host.set_balance(addr, to_u256(acc["balance"].s));
        if (acc.has("nonce"))   host.set_nonce(addr, (std::uint64_t)to_u256(acc["nonce"].s).low64());
        if (acc.has("code"))    { Bytes c = from_hex(acc["code"].s); if (!c.empty()) host.deploy(addr, std::move(c)); }
        for (auto& [k, v] : acc["storage"].o)
            host.sstore(addr, to_u256(k), to_u256(v.s));
    }
    // display metadata: readable names for storage slots.
    //   storage_auto {..., "name":"balanceOf[alice]"}   — computed mapping key
    //   watch_slots  [{acct, holder, slot, name}]      — label only, no write
    //   slot_names   [{acct, k, name}]                 — label a literal slot
    std::map<std::string, std::string> slot_names;

    for (auto& e : t["storage_auto"].a) {
        Address acct = to_addr(e["acct"].s);
        known.insert(acct);
        U256 key = mapping_slot(to_addr(e["holder"].s), to_u256(e["slot"].s));
        host.sstore(acct, key, to_u256(e["value"].s));
        if (e.has("name")) slot_names[hex_addr(acct) + ":" + hex_u(key)] = e["name"].s;
    }
    for (auto& e : t["watch_slots"].a) {
        U256 key = mapping_slot(to_addr(e["holder"].s), to_u256(e["slot"].s));
        slot_names[hex_addr(to_addr(e["acct"].s)) + ":" + hex_u(key)] = e["name"].s;
    }
    for (auto& e : t["slot_names"].a)
        slot_names[hex_addr(to_addr(e["acct"].s)) + ":" + hex_u(to_u256(e["k"].s))] = e["name"].s;

    if (t.has("block")) {
        auto& b = t["block"];
        BlockContext bc;
        bc.coinbase   = b.has("coinbase")   ? to_u256(b["coinbase"].s)   : U256();
        bc.timestamp  = b.has("timestamp")  ? to_u256(b["timestamp"].s).low64() : 0;
        bc.number     = b.has("number")     ? to_u256(b["number"].s).low64()    : 0;
        bc.difficulty = b.has("difficulty") ? to_u256(b["difficulty"].s) : U256();
        bc.gas_limit  = b.has("gasLimit")   ? to_u256(b["gasLimit"].s)   : U256();
        host.set_block(bc);
    }

    auto& x = t["exec"];
    CallContext c;
    c.address    = to_addr(x["address"].s);
    c.caller     = to_addr(x["caller"].s);
    c.origin     = to_addr(x.has("origin") ? x["origin"].s : x["caller"].s);
    c.call_value = to_u256(x.has("value") ? x["value"].s : "");
    c.calldata   = from_hex(x.has("data") ? x["data"].s : "");
    known.insert(c.address);
    if (t.has("block") && t["block"].has("gasPrice")) {
        BlockContext bc = host.block(); bc.gas_price = to_u256(t["block"]["gasPrice"].s);
        host.set_block(bc);
    }
    Bytes code(host.code(c.address));
    if (x.has("code")) code = from_hex(x["code"].s);     // ad-hoc code override

    // ---- trace collection ----
    std::ostringstream tr; tr << "[";
    bool first = true;
    std::size_t prev_j = host.journal().size(), prev_l = host.logs().size();

    auto ev_str = [](const InMemoryHost& h, std::size_t& pj, std::size_t& pl) {
        std::ostringstream ev; ev << "[";
        bool ef = true;
        auto& j = h.journal();
        if (j.size() > pj) {
            for (std::size_t i = pj; i < j.size(); ++i) {
                auto& e = j[i];
                // a LOG files a journal receipt AND lands on the bulletin
                // board — render it once, via the "posted" event below
                if (e.kind == InMemoryHost::JournalEntry::Log) continue;
                if (!ef) ev << ",";
                ef = false;
                switch (e.kind) {
                case InMemoryHost::JournalEntry::Store:
                    ev << "{\"t\":\"sstore\",\"a\":\"" << hex_addr(e.addr)
                       << "\",\"k\":\"" << hex_u(e.key)
                       << "\",\"old\":\"" << hex_u(e.old)
                       << "\",\"v\":\"" << hex_u(h.sload(e.addr, e.key)) << "\"}";
                    break;
                case InMemoryHost::JournalEntry::Balance:
                    ev << "{\"t\":\"balance\",\"a\":\"" << hex_addr(e.addr)
                       << "\",\"old\":\"" << hex_u(e.old)
                       << "\",\"v\":\"" << hex_u(h.balance(e.addr)) << "\"}";
                    break;
                case InMemoryHost::JournalEntry::Log:
                    break;  // unreachable — filtered above
                case InMemoryHost::JournalEntry::Nonce:
                    ev << "{\"t\":\"nonce\",\"a\":\"" << hex_addr(e.addr) << "\"}";
                    break;
                case InMemoryHost::JournalEntry::Account:
                    ev << "{\"t\":\"account\",\"a\":\"" << hex_addr(e.addr) << "\"}";
                    break;
                }
            }
        } else if (j.size() < pj) {
            ev << "{\"t\":\"revert\",\"n\":" << (pj - j.size()) << "}";
            ef = false;
        }
        auto& lg = h.logs();
        for (std::size_t i = pl; i < lg.size(); ++i) {
            if (!ef) ev << ",";
            ef = false;
            ev << "{\"t\":\"posted\",\"a\":\"" << hex_addr(lg[i].address)
               << "\",\"data\":\"" << to_hex(lg[i].data) << "\",\"topics\":[";
            for (std::size_t k = 0; k < lg[i].topics.size(); ++k) {
                if (k) ev << ",";
                ev << "\"" << hex_u(lg[i].topics[k]) << "\"";
            }
            ev << "]}";
        }
        pj = j.size(); pl = lg.size();
        ev << "]";
        return ev.str();
    };

    // snapshot the world before execution (for before→after display)
    std::ostringstream init; init << "[";
    {
        bool af = true;
        for (auto& a : known) {
            if (!af) init << ",";
            af = false;
            init << "{\"addr\":\"" << hex_addr(a)
                 << "\",\"balance\":\"" << hex_u(host.balance(a))
                 << "\",\"storage\":[";
            bool sf = true;
            auto sit = host.storage().find(a);
            if (sit != host.storage().end())
                for (auto& [k, v] : sit->second) {
                    if (!sf) init << ",";
                    sf = false;
                    init << "{\"k\":\"" << hex_u(limbs_to_u256(k))
                         << "\",\"v\":\"" << hex_u(limbs_to_u256(v)) << "\"}";
                }
            init << "]}";
        }
    }
    init << "]";

    // dedup the walls being executed — a delegatecall frame runs *another*
    // business's wall, so records carry a code id, not just the office address
    std::vector<Bytes> code_blobs;
    std::map<Bytes, int>  code_ids;

    Evm vm(std::move(code), host, c, to_gas(x["gas"].s));
    vm.on_step = [&](const Evm& f, std::size_t opc, Byte o) {
        auto [cit, newcode] = code_ids.try_emplace(f.code(), (int)code_blobs.size());
        if (newcode) code_blobs.push_back(f.code());
        if (!first) tr << ",";
        first = false;
        tr << "{\"d\":" << f.call().depth
           << ",\"a\":\"" << hex_addr(f.call().address)
           << "\",\"c\":" << cit->second
           << ",\"pc\":" << opc
           << ",\"op\":" << (unsigned)o
           << ",\"name\":\"" << opcode_name(o)
           << "\",\"gas\":" << f.gas()
           << ",\"sp\":" << f.sp()
           << ",\"stack\":[";
        std::size_t n = f.sp() < 8 ? f.sp() : 8;
        for (std::size_t i = 0; i < n; ++i) {
            if (i) tr << ",";
            tr << "\"" << hex_u(f.peek(i)) << "\"";
        }
        tr << "],\"mem\":" << f.memory().size()
           << ",\"ev\":" << ev_str(host, prev_j, prev_l)
           << "}";
    };
    ExecResult r = vm.run(2'000'000);
    tr << "]";

    // ---- result + final state ----
    std::ostringstream out;
    out << "{\"trace\":" << tr.str()
        << ",\"result\":{\"reason\":\"" << to_string(r.reason)
        << "\",\"error\":\"" << to_string(r.error)
        << "\",\"gas_left\":" << r.gas_left
        << ",\"output\":\"" << to_hex(r.output) << "\"}";

    // every distinct wall that executed, in first-seen order
    out << ",\"codes\":[";
    for (std::size_t i = 0; i < code_blobs.size(); ++i) {
        if (i) out << ",";
        out << "\"" << to_hex(code_blobs[i]) << "\"";
    }
    out << "]";

    // union of scenario accounts + every address the journal touched
    for (auto& e : host.journal()) known.insert(e.addr);
    for (auto& lg : host.logs()) known.insert(lg.address);
    out << ",\"accounts\":[";
    bool af = true;
    for (auto& a : known) {
        if (!af) out << ",";
        af = false;
        out << "{\"addr\":\"" << hex_addr(a)
            << "\",\"balance\":\"" << hex_u(host.balance(a))
            << "\",\"nonce\":" << host.nonce(a)
            << ",\"code_size\":" << host.code(a).size()
            << ",\"storage\":[";
        bool sf = true;
        auto sit = host.storage().find(a);
        if (sit != host.storage().end())
            for (auto& [k, v] : sit->second) {
                if (!sf) out << ",";
                sf = false;
                out << "{\"k\":\"" << hex_u(limbs_to_u256(k))
                    << "\",\"v\":\"" << hex_u(limbs_to_u256(v)) << "\"}";
            }
        out << "]}";
    }
    out << "],\"initial\":" << init.str();

    out << ",\"slots\":{";
    bool nf = true;
    for (auto& [k, v] : slot_names) {
        if (!nf) out << ",";
        nf = false;
        out << "\"" << k << "\":\"" << v << "\"";
    }

    out << "},\"logs\":[";
    for (std::size_t i = 0; i < host.logs().size(); ++i) {
        if (i) out << ",";
        auto& lg = host.logs()[i];
        out << "{\"a\":\"" << hex_addr(lg.address) << "\",\"data\":\""
            << to_hex(lg.data) << "\",\"topics\":[";
        for (std::size_t k = 0; k < lg.topics.size(); ++k) {
            if (k) out << ",";
            out << "\"" << hex_u(lg.topics[k]) << "\"";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE
const char* evm_run_scenario(const char* s) {
    static std::string out;
    try { out = run_scenario_json(s); }
    catch (const std::exception& e) { out = std::string("{\"error\":\"") + e.what() + "\"}"; }
    return out.c_str();
}
#else
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: evm_scenario <file.json>\n"; return 2; }
    std::ifstream in(argv[1]);
    std::stringstream ss; ss << in.rdbuf();
    try { std::cout << run_scenario_json(ss.str()) << "\n"; }
    catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
    return 0;
}
#endif
