// M4 harness: run the legacy ethereum/tests VMTests corpus.
// Fixture format: { name: { env, exec, pre, post, gas, out, logs } }
//   exec = the frame to run (code, caller, origin, value, calldata, gas)
//   pre/post = account maps {addr: {balance, code, nonce, storage}}
//   gas/out = expected remaining gas / return data
//   logs = keccak256(rlp([log records])) — the bloom-less receipts hash
//
// Usage: evm_vectors [fixtures_dir]  (default tests/fixtures/vmtests)

#include "evm/evm.hpp"
#include "evm/hex.hpp"
#include "evm/host.hpp"
#include "evm/keccak.hpp"
#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace evm;

// ---- decoding helpers -------------------------------------------------------

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

// ---- rlp for the logs hash --------------------------------------------------

static void rlp_len(Bytes& out, Byte short_base, std::size_t len) {
    if (len <= 55) { out.push_back((Byte)(short_base + len)); return; }
    Bytes lb;                                        // big-endian length bytes
    for (std::size_t n = len; n; n >>= 8) lb.insert(lb.begin(), (Byte)(n & 0xff));
    out.push_back((Byte)(short_base + 55 + lb.size()));
    out.insert(out.end(), lb.begin(), lb.end());
}

static Bytes rlp_str(const Bytes& b) {
    Bytes out;
    if (b.size() == 1 && b[0] < 0x80) { out.push_back(b[0]); return out; }
    rlp_len(out, 0x80, b.size());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

static Bytes rlp_list(const Bytes& payload) {
    Bytes out;
    rlp_len(out, 0xc0, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static Bytes rlp_logs(const std::vector<LogRecord>& logs) {
    Bytes items;
    for (auto& lg : logs) {
        Bytes entry;
        Bytes a(lg.address.begin(), lg.address.end());
        auto enc = rlp_str(a);
        entry.insert(entry.end(), enc.begin(), enc.end());
        Bytes topics;
        for (auto& t : lg.topics) {
            Byte w[32]; to_bytes32(t, w);
            Bytes tw(w, w + 32);
            auto te = rlp_str(tw);
            topics.insert(topics.end(), te.begin(), te.end());
        }
        auto tl = rlp_list(topics);
        entry.insert(entry.end(), tl.begin(), tl.end());
        auto de = rlp_str(lg.data);
        entry.insert(entry.end(), de.begin(), de.end());
        auto ee = rlp_list(entry);
        items.insert(items.end(), ee.begin(), ee.end());
    }
    return rlp_list(items);
}

// ---- one fixture case -------------------------------------------------------

struct Tally { int pass = 0, fail = 0, gas_diff = 0; };

static void run_case(const std::string& name, const Json& t, Tally& tal,
                     std::ostream& diag) {
    InMemoryHost host;

    // pre-state -> the country
    for (auto& [addr, acc] : t["pre"].o) {
        Address a = to_addr(addr);
        host.set_balance(a, to_u256(acc["balance"].s));
        host.set_nonce(a, (std::uint64_t)to_u256(acc["nonce"].s).low64());
        Bytes c = from_hex(acc["code"].s);
        if (!c.empty()) host.deploy(a, std::move(c));
        for (auto& [k, v] : acc["storage"].o)
            host.sstore(a, to_u256(k), to_u256(v.s));
    }

    // env -> the gazette
    BlockContext bc;
    auto& e = t["env"];
    bc.coinbase   = from_bytes(to_addr(e["currentCoinbase"].s).data(), 20);
    bc.difficulty = to_u256(e["currentDifficulty"].s);
    bc.gas_limit  = to_u256(e["currentGasLimit"].s);
    bc.number     = to_u256(e["currentNumber"].s).low64();
    bc.timestamp  = to_u256(e["currentTimestamp"].s).low64();
    bc.chain_id   = U256(1);

    // exec -> the envelope
    auto& x = t["exec"];
    CallContext c;
    c.address    = to_addr(x["address"].s);
    c.caller     = to_addr(x["caller"].s);
    c.origin     = to_addr(x["origin"].s);
    c.call_value = to_u256(x["value"].s);
    c.calldata   = from_hex(x["data"].s);
    bc.gas_price = to_u256(x["gasPrice"].s);    // VMTests: gas price lives in exec
    host.set_block(bc);
    Bytes code   = from_hex(x["code"].s);
    Gas gas      = to_gas(x["gas"].s);

    Evm vm(std::move(code), host, c, gas);
    // pathological fixtures carry near-infinite gas budgets; cap the sweep
    ExecResult r = vm.run(20'000'000);
    if (!vm.halted()) { ++tal.fail; diag << "  STEP-CAP " << name << "\n"; return; }

    // compare
    std::ostringstream bad;
    Bytes want_out = from_hex(t["out"].s);
    if (r.output != want_out) bad << " out";

    Gas want_gas = to_gas(t["gas"].s);
    if (r.gas_left != want_gas) { bad << " gas(" << want_gas << "vs" << r.gas_left << ")"; tal.gas_diff++; }

    for (auto& [addr, acc] : t["post"].o) {
        Address a = to_addr(addr);
        if (host.balance(a) != to_u256(acc["balance"].s)) bad << " balance";
        for (auto& [k, v] : acc["storage"].o)
            if (host.sload(a, to_u256(k)) != to_u256(v.s)) bad << " storage";
        if (acc.has("code") && acc["code"].s != "0x")
            if (host.code(a) != from_hex(acc["code"].s)) bad << " code";
    }

    if (t.has("logs")) {
        Byte lh[32]; keccak256(rlp_logs(host.logs()).data(),
                               rlp_logs(host.logs()).size(), lh);
        Bytes want = from_hex(t["logs"].s);
        if (!std::equal(want.begin(), want.end(), lh, lh + 32)) bad << " logs";
    }

    if (bad.str().empty()) ++tal.pass;
    else { ++tal.fail; diag << "  FAIL " << name << ":" << bad.str() << "\n"; }
}

// ---- driver -----------------------------------------------------------------

int main(int argc, char** argv) {
    fs::path root = argc > 1 ? argv[1] : "tests/fixtures/vmtests";
    if (!fs::exists(root)) {
        std::cerr << "fixtures dir not found: " << root << "\n";
        return 2;
    }

    Tally total;
    std::ostringstream diag;

    for (auto& dir : fs::directory_iterator(root)) {
        if (!dir.is_directory()) continue;
        // benchmarks, not correctness — minutes of interpreter loops per file
        auto dn = dir.path().filename().string();
        if (dn == "vmPerformance" || dn == "vmRandomTest") continue;
        Tally suite;
        for (auto& f : fs::directory_iterator(dir.path())) {
            if (f.path().extension() != ".json") continue;
            std::cerr << "  file " << f.path().filename() << "\n";   // unbuffered progress
            std::ifstream in(f.path());
            std::stringstream ss; ss << in.rdbuf();
            Json file;
            try { file = parse_json(ss.str()); }
            catch (...) { diag << "  PARSE-FAIL " << f.path().filename() << "\n"; ++suite.fail; continue; }
            for (auto& [name, t] : file.o)
                try { run_case(name, t, suite, diag); }
                catch (const std::exception& e) {
                    ++suite.fail; diag << "  CRASH " << name << ": " << e.what() << "\n";
                }
        }
        std::printf("%-28s %4d pass  %4d fail\n",
                    dir.path().filename().string().c_str(), suite.pass, suite.fail);
        total.pass += suite.pass; total.fail += suite.fail; total.gas_diff += suite.gas_diff;
    }
    std::printf("--------------------------------------------------------------\n");
    std::printf("%-28s %4d pass  %4d fail  (%d gas diffs)\n",
                "TOTAL", total.pass, total.fail, total.gas_diff);
    std::cout << diag.str();
    return 0;
}
