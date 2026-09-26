// M0 smoke tests: the scaffold compiles, the harness works, and the
// stubs fail loudly. Real coverage arrives with each milestone.
#include "test_util.hpp"

#include "evm/evm.hpp"
#include "evm/hex.hpp"
#include "evm/opcode.hpp"

using namespace evm;

TEST(hex_roundtrip) {
    Bytes b = from_hex("0x6001600101");
    CHECK_EQ(b.size(), 5u);
    CHECK_EQ(b[0], 0x60);
    CHECK_EQ(b[4], 0x01);
    CHECK(to_hex(b) == "0x6001600101");
}

TEST(hex_edge_cases) {
    // no prefix, odd digit count -> left-padded nibble
    Bytes b = from_hex("123");
    CHECK_EQ(b.size(), 2u);
    CHECK_EQ(b[0], 0x01);
    CHECK_EQ(b[1], 0x23);

    bool threw = false;
    try { from_hex("0xzz"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(opcode_table) {
    CHECK(opcode_name(op::ADD) == "ADD");
    CHECK(opcode_name(0x5f) == "PUSH0");
    CHECK(opcode_name(0x60) == "PUSH1");
    CHECK(opcode_name(0x7f) == "PUSH32");
    CHECK(opcode_name(0x8f) == "DUP16");
    CHECK(opcode_name(0x9f) == "SWAP16");
    CHECK(opcode_name(0xfe) == "INVALID");
    CHECK(opcode_name(0x0c) == "UNDEFINED");

    CHECK(is_push(0x5f) && is_push(0x7f) && !is_push(0x80));
    CHECK_EQ(push_size(0x5f), 0u);
    CHECK_EQ(push_size(0x63), 4u);
    CHECK(is_dup(0x80) && is_swap(0x9f) && is_log(0xa4));
}

TEST(u256_scaffold) {
    U256 z;
    CHECK(z.is_zero());
    U256 v(42);
    CHECK_EQ(v.low64(), 42ull);
    CHECK(v == U256(42));
    CHECK(U256::max() != v);
}

TEST(host_storage_roundtrip) {
    InMemoryHost host;
    Address a{};
    U256 key(7), value(0xdead);

    // untouched slot reads as zero
    CHECK(host.sload(a, key).is_zero());

    host.sstore(a, key, value);
    U256 got = host.sload(a, key);
    CHECK_EQ(got.l[0], 0xdeadull);

    // a different account sees nothing
    Address b{};
    b[0] = 1;
    CHECK(host.sload(b, key).is_zero());
}

TEST(evm_scaffold) {
    InMemoryHost host;
    Evm vm(from_hex("0x00"), host, CallContext{}, 1000);
    CHECK_EQ(vm.gas(), 1000);
    CHECK_EQ(vm.sp(), 0u);
    CHECK_EQ(vm.pc(), 0u);
    CHECK(!vm.halted());
}

TEST(evm_step_stop) {
    InMemoryHost host;
    Evm vm(from_hex("0x00"), host, CallContext{}, 1000);
    CHECK(!vm.step());                                 // STOP ends the run
    CHECK(vm.result().reason == StopReason::Stop);
    CHECK(vm.halted());
}
