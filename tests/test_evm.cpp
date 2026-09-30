// M2 interpreter tests: the step() loop, stack discipline, halts.
#include "test_util.hpp"

#include "evm/evm.hpp"
#include "evm/hex.hpp"

using namespace evm;

TEST(evm_push_stop) {
    InMemoryHost host;
    Evm vm(from_hex("0x6005600300"), host, CallContext{}, 1000);  // PUSH1 5; PUSH1 3; STOP
    vm.run();
    CHECK(vm.result().reason == StopReason::Stop);
    CHECK_EQ(vm.sp(), 2u);
    CHECK(vm.peek(0) == U256(3));        // top
    CHECK(vm.peek(1) == U256(5));
}

TEST(evm_push32_wide) {
    // PUSH32 max-value immediate lands as a full-width word on the pile.
    InMemoryHost host;
    Evm vm(from_hex("0x7f" + std::string(64, 'f') + "00"), host, CallContext{}, 1000);
    vm.run();
    CHECK(vm.result().reason == StopReason::Stop);
    CHECK(vm.peek(0) == U256::max());
}

TEST(evm_push_truncated) {
    // PUSH2 at end-of-code: missing low byte reads as zero -> 0xaa00.
    InMemoryHost host;
    Evm vm(from_hex("0x61aa"), host, CallContext{}, 1000);
    vm.run();
    CHECK(vm.result().reason == StopReason::Stop);
    CHECK_EQ(vm.peek(0).low64(), 0xaa00ull);
}

TEST(evm_pop_underflow) {
    InMemoryHost host;
    Evm vm(from_hex("0x50"), host, CallContext{}, 1000);          // POP on empty pile
    vm.run();
    CHECK(vm.result().reason == StopReason::Halt);
    CHECK(vm.result().error == Error::StackUnderflow);
}

TEST(evm_pop_discards) {
    InMemoryHost host;
    Evm vm(from_hex("0x600160025000"), host, CallContext{}, 1000); // PUSH1 1; PUSH1 2; POP; STOP
    vm.run();
    CHECK_EQ(vm.sp(), 1u);
    CHECK(vm.peek(0) == U256(1));
}

TEST(evm_invalid_opcode) {
    InMemoryHost host;
    Evm vm(from_hex("0x0c"), host, CallContext{}, 1000);          // undefined byte
    vm.run();
    CHECK(vm.result().reason == StopReason::Halt);
    CHECK(vm.result().error == Error::InvalidOpcode);
}

TEST(evm_empty_code) {
    InMemoryHost host;
    Evm vm(Bytes{}, host, CallContext{}, 1000);
    vm.run();
    CHECK(vm.result().reason == StopReason::Stop);   // implicit STOP
}

// ------------------------------------------------------------------
// Arithmetic: operands pop top-first, so PUSH x; PUSH y; OP -> y OP x
// (top is the left operand per the Yellow Paper).
// ------------------------------------------------------------------

TEST(evm_arith_binary) {
    InMemoryHost host;

    {   // PUSH1 5; PUSH1 3; ADD -> 8
        Evm vm(from_hex("0x60056003" "01"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(8));
    }
    {   // PUSH1 3; PUSH1 5; SUB -> 5 - 3 = 2
        Evm vm(from_hex("0x60036005" "03"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(2));
    }
    {   // PUSH1 5; PUSH1 3; SUB -> 3 - 5 wraps to MAX-1
        Evm vm(from_hex("0x60056003" "03"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256::max() - U256(1));
    }
    {   // PUSH1 5; PUSH1 3; MUL -> 15
        Evm vm(from_hex("0x60056003" "02"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(15));
    }
    {   // PUSH1 3; PUSH1 15; DIV -> 15 / 3 = 5
        Evm vm(from_hex("0x6003600f" "04"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(5));
    }
    {   // PUSH1 0; PUSH1 5; DIV -> 5 / 0 = 0 (EVM rule, no trap)
        Evm vm(from_hex("0x60006005" "04"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // PUSH1 2; PUSH1 7; MOD -> 7 % 2 = 1
        Evm vm(from_hex("0x60026007" "06"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
    }
}

TEST(evm_arith_signed) {
    InMemoryHost host;

    {   // -14 / 5 = -2:  PUSH 5; build -14 on top via 0-14; SDIV
        Evm vm(from_hex("0x6005" "600e6000" "03" "05"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256() - U256(2));   // -2 wrapped
    }
    {   // -7 % 2 = -1:  PUSH 2; build -7 on top; SMOD
        Evm vm(from_hex("0x6002" "60076000" "03" "07"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256::max());        // -1 wrapped
    }
    {   // SLT: -1 < 1 -> 1.  push 1; build -1 on top; SLT
        Evm vm(from_hex("0x6001" "60016000" "03" "12"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
    }
    {   // SGT: -1 > 1 -> 0
        Evm vm(from_hex("0x6001" "60016000" "03" "13"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
}

TEST(evm_comparisons_bitwise) {
    InMemoryHost host;

    {   // LT: 3 < 5 -> 1
        Evm vm(from_hex("0x60056003" "10"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
    }
    {   // LT: 5 < 3 -> 0
        Evm vm(from_hex("0x60036005" "10"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // GT: 3 > 5 -> 0
        Evm vm(from_hex("0x60056003" "11"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // EQ: equal -> 1
        Evm vm(from_hex("0x60056005" "14"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
    }
    {   // AND: 0xff & 0x0f = 0x0f
        Evm vm(from_hex("0x60ff600f" "16"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0x0full);
    }
    {   // OR: 0xf0 | 0x0f = 0xff
        Evm vm(from_hex("0x60f0600f" "17"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0xffull);
    }
    {   // XOR: 0xff ^ 0x0f = 0xf0
        Evm vm(from_hex("0x60ff600f" "18"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0xf0ull);
    }
}

TEST(evm_dup_swap) {
    InMemoryHost host;

    {   // PUSH1 5; DUP1 -> [5, 5]
        Evm vm(from_hex("0x6005" "80"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.sp(), 2u);
        CHECK(vm.peek(0) == U256(5));
        CHECK(vm.peek(1) == U256(5));
    }
    {   // PUSH1 5; PUSH1 3; DUP2 -> [5, 3, 5]  (top-first)
        Evm vm(from_hex("0x60056003" "81"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.sp(), 3u);
        CHECK(vm.peek(0) == U256(5));
        CHECK(vm.peek(1) == U256(3));
        CHECK(vm.peek(2) == U256(5));
    }
    {   // PUSH1 5; PUSH1 3; SWAP1 -> [5, 3]
        Evm vm(from_hex("0x60056003" "90"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(5));
        CHECK(vm.peek(1) == U256(3));
    }
    {   // PUSH1 1;2;3; SWAP2 -> top swaps with 3rd: [1, 2, 3]
        Evm vm(from_hex("0x600160026003" "91"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
        CHECK(vm.peek(1) == U256(2));
        CHECK(vm.peek(2) == U256(3));
    }
    {   // DUP1 on empty pile -> underflow
        Evm vm(from_hex("0x80"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::StackUnderflow);
    }
    {   // SWAP1 with one slip -> underflow
        Evm vm(from_hex("0x6001" "90"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::StackUnderflow);
    }
}

TEST(evm_shifts_byte_unary) {
    InMemoryHost host;

    {   // SHL: PUSH v=1, PUSH s=4 -> 1 << 4 = 16
        Evm vm(from_hex("0x60016004" "1b"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 16ull);
    }
    {   // SHR: 16 >> 4 = 1
        Evm vm(from_hex("0x60106004" "1c"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 1ull);
    }
    {   // SHL by 256 -> 0 (spec: shift >= 256 is all-gone)
        Evm vm(from_hex("0x6001610100" "1b"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // BYTE i=31 grabs the low byte of x=0x0102 -> 0x02
        Evm vm(from_hex("0x610102601f" "1a"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0x02ull);
    }
    {   // BYTE i=30 -> second-to-last byte -> 0x01
        Evm vm(from_hex("0x610102601e" "1a"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0x01ull);
    }
    {   // BYTE i=32 -> out of range -> 0
        Evm vm(from_hex("0x6101026020" "1a"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // ISZERO on 0 -> 1; on 5 -> 0
        Evm vm(from_hex("0x600015"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));

        Evm vm2(from_hex("0x600515"), host, CallContext{}, 1000);
        vm2.run();
        CHECK(vm2.peek(0).is_zero());
    }
    {   // NOT 0 -> all ones
        Evm vm(from_hex("0x600019"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256::max());
    }
}

TEST(evm_exp_sar_signextend) {
    InMemoryHost host;

    {   // EXP: PUSH e=3, PUSH base=2 -> 2^3 = 8
        Evm vm(from_hex("0x60036002" "0a"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 8ull);
    }
    {   // EXP 0^0 = 1
        Evm vm(from_hex("0x60006000" "0a"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256(1));
    }
    {   // SAR: -8 >> 1 = -4.  Build -8 = 0-8, push s=1
        Evm vm(from_hex("0x6008600003" "6001" "1d"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256::max() - U256(3));   // -4 wrapped
    }
    {   // SAR: -8 >> 256 = -1 (all sign bits); +8 >> 256 = 0
        Evm vm(from_hex("0x6008600003" "610100" "1d"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256::max());

        Evm vm2(from_hex("0x6008610100" "1d"), host, CallContext{}, 1000);
        vm2.run();
        CHECK(vm2.peek(0).is_zero());
    }
    {   // SIGNEXTEND: k=0, x=0x80 -> 0xFF..80 = -128
        Evm vm(from_hex("0x60806000" "0b"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) == U256() - U256(128));
    }
    {   // SIGNEXTEND: k=0, x=0x7f -> positive, unchanged
        Evm vm(from_hex("0x607f6000" "0b"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0x7full);
    }
    {   // SIGNEXTEND: k>=32 -> unchanged
        Evm vm(from_hex("0x60806020" "0b"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 0x80ull);
    }
}

TEST(evm_addmod_mulmod) {
    InMemoryHost host;

    {   // (5 + 3) % 4 = 0.  Stack order: push n, b, a (a on top)
        Evm vm(from_hex("0x60046003" "6005" "08"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // (7 * 5) % 6 = 5
        Evm vm(from_hex("0x60066005" "6007" "09"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 5ull);
    }
    {   // no-wrap trap: MAX*MAX mod 3 = 0 (naive (a*b)%n overflows)
        Evm vm(from_hex(std::string("0x7f") + std::string(64, 'f')
                        + "7f" + std::string(64, 'f') + "6003" "09"),
               host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
    {   // n = 0 -> 0
        Evm vm(from_hex("0x60006003" "6005" "08"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
}

TEST(evm_push0) {
    InMemoryHost host;
    Evm vm(from_hex("0x5f"), host, CallContext{}, 1000);          // PUSH0
    vm.run();
    CHECK_EQ(vm.sp(), 1u);
    CHECK(vm.peek(0).is_zero());
}

TEST(evm_stack_overflow) {
    InMemoryHost host;
    Bytes code;
    code.reserve(1025 * 2);
    for (int i = 0; i < 1025; ++i) {                              // 1025 PUSH1s
        code.push_back(0x60); code.push_back(0x01);
    }
    Evm vm(std::move(code), host, CallContext{}, 1000000);
    vm.run();
    CHECK(vm.result().reason == StopReason::Halt);
    CHECK(vm.result().error == Error::StackOverflow);
    CHECK_EQ(vm.sp(), 1024u);
}

// ------------------------------------------------------------------
// The desk: MSTORE/MLOAD/MSTORE8/MSIZE.
// ------------------------------------------------------------------

TEST(evm_memory) {
    InMemoryHost host;

    {   // PUSH1 42; PUSH1 0; MSTORE; PUSH1 0; MLOAD -> 42 back on the pile
        Evm vm(from_hex("0x602a600052" "600051" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK(vm.peek(0) == U256(42));
        CHECK_EQ(vm.memory().size(), 32u);
        CHECK(vm.memory()[31] == 0x2a);   // big-endian: low byte in the last square
    }
    {   // untouched desk reads as zero; the read grows it to one sheet
        Evm vm(from_hex("0x600051" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
        CHECK_EQ(vm.memory().size(), 32u);
    }
    {   // MSTORE8 writes a single square; desk still grows a whole sheet
        Evm vm(from_hex("0x60ab600053" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.memory()[0] == 0xab);
        CHECK(vm.memory()[1] == 0x00);
        CHECK_EQ(vm.memory().size(), 32u);
    }
    {   // MSIZE: empty desk -> 0; after one store -> 32
        Evm vm(from_hex("0x59" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0).is_zero());

        Evm vm2(from_hex("0x602a600052" "59" "00"), host, CallContext{}, 1000);
        vm2.run();
        CHECK_EQ(vm2.peek(0).low64(), 32ull);
    }
    {   // offset past the 64-bit universe -> OutOfBounds
        Evm vm(from_hex(std::string("0x7f") + std::string(64, 'f') + "51"),
               host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::OutOfBounds);
    }
}

// ------------------------------------------------------------------
// RETURN/REVERT: a desk slice becomes the filed report's output.
// ------------------------------------------------------------------

TEST(evm_return_revert) {
    InMemoryHost host;

    {   // desk holds 42 at square 31; RETURN(0, 32) ships the whole sheet
        Evm vm(from_hex("0x602a600052" "60206000f3"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Return);
        CHECK_EQ(vm.result().output.size(), 32u);
        CHECK(vm.result().output[31] == 0x2a);
        CHECK(vm.result().output[0] == 0x00);
    }
    {   // RETURN(0,0) on an empty desk -> Return with empty output
        Evm vm(from_hex("0x60006000f3"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Return);
        CHECK(vm.result().output.empty());
    }
    {   // REVERT: same mechanism, different stamp
        Evm vm(from_hex("0x60006000fd"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Revert);
    }
}

// ------------------------------------------------------------------
// The filing cabinet: SLOAD/SSTORE via the Host.
// ------------------------------------------------------------------

TEST(evm_storage) {
    {   // SSTORE slot0=42, SLOAD slot0 in the same call -> 42
        InMemoryHost host;
        Evm vm(from_hex("0x602a600055" "600054" "00"), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK(vm.peek(0) == U256(42));
    }
    {   // persistence: same host, a brand-new clerk -> the slot survives
        InMemoryHost host;
        Evm day1(from_hex("0x602a600055" "00"), host, CallContext{}, 100000);
        day1.run();
        Evm day2(from_hex("0x600054" "00"), host, CallContext{}, 100000);
        day2.run();
        CHECK(day2.peek(0) == U256(42));
    }
    {   // a different office's cabinet: same slot reads 0
        InMemoryHost host;
        Evm vm1(from_hex("0x602a600055" "00"), host, CallContext{}, 100000);
        vm1.run();
        CallContext other;
        other.address[0] = 1;
        Evm vm2(from_hex("0x600054" "00"), host, other, 100000);
        vm2.run();
        CHECK(vm2.peek(0).is_zero());
    }
    {   // untouched slot reads 0
        InMemoryHost host;
        Evm vm(from_hex("0x600754" "00"), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.peek(0).is_zero());
    }
}

// ------------------------------------------------------------------
// Stamps: per-op charging, GAS, OutOfGas.
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// The seal press: SHA3 presses a desk slice into a 256-bit seal.
// (Expected digests verified against a reference keccak-256.)
// ------------------------------------------------------------------

TEST(evm_sha3) {
    InMemoryHost host;

    {   // SHA3(0, 32) over untouched desk = keccak256 of 32 zeros
        Evm vm(from_hex("0x60206000" "20"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK(vm.peek(0) ==
              U256::from_hex("290decd9548b62a8d60345a988386fc84ba6bc9"
                             "5484008f6362f93160ef3e563"));
    }
    {   // MSTORE 42 @ 0, then SHA3(0,32): word ending in 0x2a
        Evm vm(from_hex("0x602a600052" "60206000" "20"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) ==
              U256::from_hex("beced09521047d05b8960b7e7bcc1d1292cf3e4b"
                             "2a6b63f48335cbde5f7545d2"));
    }
    {   // MSTORE8 0xab @ 0, then SHA3(0,1): single byte
        Evm vm(from_hex("0x60ab600053" "60016000" "20"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) ==
              U256::from_hex("468fc9c005382579139846222b7b0aebc9182ba0"
                             "73b2455938a86d9753bfb078"));
    }
    {   // SHA3(0,0): sealing nothing yields the empty-input digest
        Evm vm(from_hex("0x60006000" "20"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.peek(0) ==
              U256::from_hex("c5d2460186f7233c927e7db2dcc703c0e500b653"
                             "ca82273b7bfad8045d85a470"));
    }
    {   // len with high limbs set -> OutOfBounds (can't fit size_t)
        Evm vm(from_hex(std::string("0x7f") + std::string(64, 'f')
                        + "6000" "20"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::OutOfBounds);
    }
}

// ------------------------------------------------------------------
// The bulletin board: LOG0..LOG4 post (address, topics, body).
// ------------------------------------------------------------------

TEST(evm_log) {
    {   // MSTORE 42 @ 0; LOG2 topics 1,2 with the 32-byte body.
        // Push order: topic2, topic1, len, off (off ends on top).
        InMemoryHost host;
        Evm vm(from_hex("0x602a600052" "60026001" "60206000" "a2" "00"),
               host, CallContext{}, 5000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK_EQ(host.logs().size(), 1u);
        const auto& lg = host.logs()[0];
        CHECK_EQ(lg.topics.size(), 2u);
        CHECK(lg.topics[0] == U256(1));
        CHECK(lg.topics[1] == U256(2));
        CHECK_EQ(lg.data.size(), 32u);
        CHECK(lg.data[31] == 0x2a);
        CHECK(lg.address == Address{});          // this office's stamp
    }
    {   // LOG0: body only, no labels
        InMemoryHost host;
        Evm vm(from_hex("0x60006000" "a0" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(host.logs().size(), 1u);
        CHECK(host.logs()[0].topics.empty());
        CHECK(host.logs()[0].data.empty());
    }
    {   // a different office posts — the stamp follows call_.address
        InMemoryHost host;
        CallContext c;
        c.address[0] = 0x42;
        Evm vm(from_hex("0x60006000" "a0" "00"), host, c, 1000);
        vm.run();
        CHECK(host.logs()[0].address[0] == 0x42);
    }
}

// ------------------------------------------------------------------
// CALL: nested execution through the host. Push order for operands:
// out_len, out_off, in_len, in_off, value, to, gas (gas ends on top).
// ------------------------------------------------------------------

TEST(evm_call) {
    // callee B's wall: MSTORE 42 @ 0, RETURN 32 bytes
    Address b{}; b.fill(0x42);

    const std::string parent_code =
        "0x6020" "6000" "6000" "6000" "6000"  // out_len=32 out_off=0 in_len=0 in_off=0 value=0
        "73" "4242424242424242424242424242424242424242"  // PUSH20 B
        "61ffff"                                   // PUSH2 0xffff gas
        "f1"                                       // CALL
        "6000" "51"                                // MLOAD 0 -> the report
        "6020" "6000" "f3";                        // RETURN(0, 32)

    {   // success: flag 1, child's report readable on the desk
        InMemoryHost host;
        host.deploy(b, from_hex("0x602a600052" "60206000f3"));
        Evm vm(from_hex(parent_code), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Return);
        CHECK(vm.peek(1) == U256(1));                  // success flag under the report
        CHECK(vm.peek(0) == U256(0x2a));               // MLOAD pulled it off the desk
        CHECK_EQ(vm.result().output.size(), 32u);
        CHECK(vm.result().output[31] == 0x2a);         // the 42 came home
    }
    {   // child SSTOREs then REVERTs: flag 0, the write unwinds
        InMemoryHost host;
        host.deploy(b, from_hex("0x6001600055" "60006000fd"));
        Evm vm(from_hex(parent_code), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.peek(1) == U256());                   // failure flag
        CHECK(host.sload(b, U256()) == U256());        // slot 0 never committed
    }
    {   // dead end: callee has no code — empty wall, instant STOP, flag 1
        InMemoryHost host;
        Evm vm(from_hex(parent_code), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.peek(1) == U256(1));
        CHECK(vm.result().output[31] == 0);            // no report
    }
    {   // DELEGATECALL: B's wall runs in A's office — writes land in A's
        // cabinet, and CALLER passes through (Alice, not A)
        InMemoryHost host;
        Address a{}; a.fill(0x11);                     // the office
        Address alice{}; alice.fill(0xaa);
        CallContext c; c.address = a; c.caller = alice;
        // B's wall: SSTORE(0, 42); SSTORE(1, CALLER); RETURN(0,0)
        host.deploy(b, from_hex("0x602a600055" "33600155" "60006000f3"));
        const std::string dcall =
            "0x6020" "6000" "6000" "6000"           // out_len out_off in_len in_off
            "73" "4242424242424242424242424242424242424242"  // PUSH20 B
            "61ffff" "f4"                            // gas, DELEGATECALL
            "6000" "51" "6020" "6000" "f3";
        Evm vm(from_hex(dcall), host, c, 200000);
        vm.run();
        CHECK(vm.peek(1) == U256(1));
        CHECK(host.sload(a, U256()) == U256(42));      // A's cabinet got the write
        CHECK(host.sload(b, U256()) == U256());        // B's cabinet untouched
        U256 alice_v = from_bytes(alice.data(), alice.size());
        CHECK(host.sload(a, U256(1)) == alice_v);      // caller = Alice, not A
    }
    {   // STATICCALL: child's SSTORE is a hard fault — flag 0, nothing written
        InMemoryHost host;
        host.deploy(b, from_hex("0x602a600055" "60006000f3"));
        const std::string scall =
            "0x6020" "6000" "6000" "6000"
            "73" "4242424242424242424242424242424242424242"
            "61ffff" "fa"                            // gas, STATICCALL
            "6000" "51" "6020" "6000" "f3";
        Evm vm(from_hex(scall), host, CallContext{}, 200000);
        vm.run();
        CHECK(vm.peek(1) == U256());                   // failure flag
        CHECK(host.sload(b, U256()) == U256());        // write never happened
    }
}

// ------------------------------------------------------------------
// Surface sweep: EXTCODE*, BLOCKHASH, RETURNDATA*, CREATE, SELFDESTRUCT
// ------------------------------------------------------------------

TEST(evm_surface) {
    Address b{}; b.fill(0x42);
    {   // EXTCODESIZE reads another wall; BLOCKHASH reads the archive
        InMemoryHost host;
        host.deploy(b, from_hex("0x00"));               // 1-byte wall
        host.set_blockhash(7, U256(0xdead));
        Evm vm(from_hex("0x73" "4242424242424242424242424242424242424242"
                        "3b"                             // EXTCODESIZE B -> 1
                        "6007" "40"                      // BLOCKHASH 7
                        "00"), host, CallContext{}, 100000);
        vm.run();
        CHECK(vm.peek(0) == U256(0xdead));
        CHECK(vm.peek(1) == U256(1));
    }
    {   // child returns 42 -> RETURNDATASIZE/RETURNDATACOPY shuttle it out
        InMemoryHost host;
        host.deploy(b, from_hex("0x602a600052" "60206000f3"));
        Evm vm(from_hex("0x6020" "6000" "6000" "6000" "6000"
                        "73" "4242424242424242424242424242424242424242"
                        "61ffff" "f1"                    // CALL B
                        "50"                             // drop the flag
                        "3d"                             // RETURNDATASIZE -> 32
                        "6020" "6000" "6000" "3e"        // RETURNDATACOPY(0,0,32)
                        "6000" "51"                      // MLOAD 0 -> 42
                        "00"), host, CallContext{}, 200000);
        vm.run();
        CHECK(vm.peek(0) == U256(0x2a));
        CHECK(vm.peek(1) == U256(32));
    }
    {   // CREATE: init code in memory returns the runtime code -> deployed
        InMemoryHost host;
        // init: CODECOPY payload@12..22 to desk, RETURN it (its return IS the wall)
        const std::string init = "600a600c600039" "600a6000f3"
                                 + std::string("6001600055") + "0000000000";
        const std::string word = std::string(20, '0') + init;   // init sits at mem[10..32)
        Evm vm(from_hex("0x7f" + word +
                        "6000" "52"                            // MSTORE init @ 0
                        "6016" "600a" "6000" "f0"              // CREATE(0, 10, 22)
                        "00"), host, CallContext{}, 200000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        Byte buf[32] = {}; to_bytes32(vm.peek(0), buf);
        Address newborn; std::copy(buf + 12, buf + 32, newborn.begin());
        CHECK(newborn != Address{});                            // got a real address
        CHECK(host.code(newborn) == from_hex("0x6001600055" "0000000000"));
    }
    {   // SELFDESTRUCT: cash sweeps to the beneficiary, frame stops
        InMemoryHost host;
        Address a{}; a.fill(0x11);
        host.set_balance(a, U256(5));
        CallContext c; c.address = a;
        Evm vm(from_hex("0x73" "4242424242424242424242424242424242424242" "ff"),
               host, c, 100000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK(host.balance(a) == U256());
        CHECK(host.balance(b) == U256(5));
    }
}

TEST(evm_gas) {
    InMemoryHost host;

    {   // PUSH1 costs 3: a budget of 2 goes bankrupt before the push
        Evm vm(from_hex("0x6001"), host, CallContext{}, 2);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::OutOfGas);
    }
    {   // PUSH1 (3) + STOP (0) on 100 -> 97 stamps left
        Evm vm(from_hex("0x600100"), host, CallContext{}, 100);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK_EQ(vm.result().gas_left, 97);
    }
    {   // GAS pushes the remaining budget after paying its own 2
        Evm vm(from_hex("0x5a" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 998ull);
    }
    {   // SLOAD (50, Frontier schedule) bankrupts a budget that survived the PUSH1
        Evm vm(from_hex("0x600054"), host, CallContext{}, 50);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::OutOfGas);
    }
}

// ------------------------------------------------------------------
// The finger on the wall: JUMP/JUMPI/JUMPDEST/PC.
// ------------------------------------------------------------------

TEST(evm_jumps) {
    InMemoryHost host;

    {   // PUSH1 42 stays on the pile; PUSH1 5; JUMP -> JUMPDEST @5 -> STOP
        Evm vm(from_hex("0x602a600556" "5b00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
        CHECK(vm.peek(0) == U256(42));
    }
    {   // JUMP to position 0: that's a PUSH1 byte, not a bookmark
        Evm vm(from_hex("0x600056"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Halt);
        CHECK(vm.result().error == Error::InvalidJump);
    }
    {   // JUMP past the end of the wall
        Evm vm(from_hex("0x60ff56"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().error == Error::InvalidJump);
    }
    {   // JUMPI with cond=1 takes the jump to JUMPDEST @5
        Evm vm(from_hex("0x60016005" "57" "5b00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
    }
    {   // JUMPI with cond=0 falls through; the bad dest is never checked
        Evm vm(from_hex("0x600060ff" "57" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
    }
    {   // walking over a JUMPDEST does nothing
        Evm vm(from_hex("0x5b00"), host, CallContext{}, 1000);
        vm.run();
        CHECK(vm.result().reason == StopReason::Stop);
    }
    {   // PC @index2 pushes its own position: 2
        Evm vm(from_hex("0x6001" "58" "00"), host, CallContext{}, 1000);
        vm.run();
        CHECK_EQ(vm.peek(0).low64(), 2ull);
        CHECK_EQ(vm.peek(1).low64(), 1ull);
    }
}

