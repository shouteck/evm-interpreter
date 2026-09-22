# EVM Project Guide — what you're building and why

An Ethereum Virtual Machine interpreter in C++20: the clerk inside every
smart contract's office, reading instructions off the wall one at a time.
C++ EVM interpreters are real production code (evmone, silkworm) — this is
the crypto-infra counterpart to the order book's quant-infra lane.

## The mental model: a country full of offices

Ethereum is a country. Every contract account is an office:

- **Code on the wall** — the bytecode. Immutable instructions, read one
  at a time.
- **The clerk** — your `Evm` class. `step()` reads one instruction,
  charges the gas, does what it says, moves the program counter. Never
  improvises, never skips.
- **A stack of paper, max 1024 sheets** — the operand stack. The clerk
  can only touch the top few sheets. Sheet 1025 = stack overflow halt.
- **The desk** — memory: cheap, byte-addressed scratchpad, zero-initialized
  as it grows, wiped clean when the visitor leaves.
- **The filing cabinet** — storage: permanent across visits, keyed by
  256-bit slots, the most expensive thing the clerk can touch.
- **A visitor with a letter** — a transaction: `calldata` is the letter,
  `caller` is who sent them.
- **A stamp budget** — gas: every instruction costs stamps. Run out
  mid-sentence and the clerk shreds everything it did (revert semantics).
- **A phone line to the government** — `Host`: balances, other offices'
  cabinets, block metadata. The office can't see the country directly;
  it asks. `InMemoryHost` is a toy government for testing.

## Division of labor

What the AI built (the harness — plumbing, not the portfolio piece):

- Hex/bytecode utilities (`hex.hpp/.cpp`), opcode table (`opcode.hpp/.cpp`).
- `InMemoryHost` — a flat world state to run against.
- Test harness + smoke tests (`tests/`).
- `evm_run` CLI — load hex, run, dump the outcome.
- Later, per milestone: disassembler, `ethereum/tests` JSON vector runner,
  benchmark harness.

What YOU own (the thing worth asking about):

- `src/uint256.cpp` — 4×u64 limb arithmetic. The `Book` internals of this
  project: carry chains, full-width multiply, long division, signed
  variants, mod-2^256 wraparound. Every op currently throws.
- `src/evm.cpp` — the interpreter core: fetch/charge/dispatch, stack and
  memory discipline, jump validity, halt semantics.

The rule, same as LOB: the harness tells you IF you're right. The
interpreter is where the learning and the resume lines live.

## How the pieces fit

```
bytecode (hex / file)
      |
      v
+-----------+     asks: sload / sstore / balance / block
|   Evm     | --------------------> +-------------------+
|  (yours)  | <-------------------- | Host (InMemoryHost)|
+-----------+    answers            +-------------------+
      |  ^
      |  | inspect: pc, sp, stack top, memory, gas, result
      v  |
 tests + evm_run
```

## The contract (what your Evm must do)

1. Words are 256-bit, all arithmetic wraps mod 2^256. No overflow, ever.
2. Stack holds at most 1024 words; ops pop inputs then push results.
3. Gas is charged per instruction (fixed costs + memory expansion);
   running out is `Halt/OutOfGas`, not a crash.
4. JUMP/JUMPI may only land on a JUMPDEST that is *real* — not one
   hiding inside PUSH data.
5. Memory is byte-addressed, zero-initialized, and expands in 32-byte
   words (expansion costs gas, quadratically at the margin).
6. STOP / RETURN / REVERT end cleanly; bad opcodes, stack violations,
   OOG, and bad jumps halt with an `Error`. Halting is a result, not
   an exception.
7. Determinism: same code + same host state = same trace, always.

## Milestones

### M0 — Scaffold + orientation (this step)
- `build.bat test` is green; `evm_run` loads bytecode and reports the
  not-implemented stub.
- Read `types.hpp`, `evm.hpp`, `host.hpp` until you can explain every
  field. Trace `PUSH1 3; PUSH1 4; ADD` through the machine on paper —
  what does the stack look like after each step?
- Learn: what lives in the machine vs behind the Host, why gas is
  signed, why halts are results not exceptions.

### M1 — uint256: the clerk's arithmetic
- Implement `src/uint256.cpp`: comparisons, add/sub (carry chains),
  mul (schoolbook across limbs, keep the low 256 bits), div/mod
  (binary long division is fine — measure later), bitwise, shifts,
  sdiv/smod/slt/sgt (two's-complement), byte/bit access, hex io.
- Then addmod/mulmod: the trap is that a+b and a*b overflow 256 bits —
  you need the intermediate wide value. mulmod is the boss fight.
- Learn: carry propagation, why limb order is little-endian while the
  wire is big-endian, two's complement, why div is the hard one.

### M2 — The core interpreter
- `step()`: fetch → charge gas → dispatch → advance pc.
- Stack ops: PUSH0–32, POP, DUP, SWAP. Arithmetic/comparison/bitwise:
  the whole 0x0_ and 0x1_ blocks. Memory: MLOAD/MSTORE/MSTORE8/MSIZE.
  Storage: SLOAD/SSTORE via Host. Flow: STOP, PC, JUMP, JUMPI,
  JUMPDEST, RETURN, REVERT. GAS opcode.
- The subtle ones: SIGNEXTEND, BYTE's indexing convention, JUMPDEST
  validity (dests inside PUSH data don't count), memory-expansion gas.
- Learn: stack-machine dispatch, why PUSH has inline data, gas as a
  DoS budget, why every client agrees byte-for-byte on results.

### M3 — Context opcodes
- ADDRESS/CALLER/ORIGIN/CALLVALUE, CALLDATALOAD/SIZE/COPY, CODESIZE/
  CODECOPY, GASPRICE, block fields via `Host::block()`, BALANCE/
  SELFBALANCE, SHA3 (keccak-256 — decide then whether you hand-roll it
  or it's harness), LOG0–4 (record topics+data — no real output needed).
- Learn: what "environment" means to a contract, why calldata is
  read-only, why keccak specifically.

### M4 — The oracle + the writeup
- JSON runner for the official `ethereum/tests` VMTests; get a file
  green, then a directory.
- Perf pass: JUMPDEST bitmap (precompute once, O(1) validity checks),
  dispatch table vs switch, U256 small-value fast paths. Measure with
  a bench harness, report honestly like the LOB numbers.
- README writeup: architecture, the gas/invariant subtleties, results.

### Stretch
- CALL/CREATE family (breaks "execution only" — full sub-execution with
  its own gas, calldata, return data).
- Wasm build → `docs/` + GitHub Pages demo (trace viewer).
- Step tracer/debugger UI; gas-per-opcode heatmap.

## What to learn (the actual syllabus)

- Data structures: multi-limb bignum, bitmaps, stack machines, dispatch
  strategies (switch vs computed goto vs table).
- Systems: branchless carry arithmetic, memory-expansion cost models,
  why interpreters are the way they are (EVM is deliberately simple —
  a stack machine so the spec fits on one page).
- Domain: gas economics, the Yellow Paper, fork differences (PUSH0 is
  Shanghai; PREVRANDAO renamed DIFFICULTY post-merge), consensus-level
  determinism.
- Method: spec-driven testing against official vectors — stronger than
  the LOB's differential test because the oracle is the protocol itself.

## Interview questions this prepares you for

- "How does a smart contract actually execute?" (Fetch-decode-execute
  on a 256-bit stack machine, gas-metered, deterministic.)
- "Implement 256-bit add on a 64-bit machine." (Carry chain: `add`,
  detect wraparound by result < operand, or `__builtin_addc`/adc.)
- "Why can't a JUMP land inside PUSH data?" (JUMPDEST validity — the
  analyzer must know which bytes are instructions vs data.)
- "Why is SSTORE so expensive?" (Writes consensus state every node must
  store forever — gas prices the externality.)
- "How do you know your EVM is correct?" (Official execution-spec test
  vectors — the same ones geth and evmone run.)

## Commands cheat sheet

```
build.bat                       build everything
build.bat test                  build + run all tests
build\evm_run 0x6001600101      run bytecode, dump outcome
build\evm_run code.hex          run bytecode from a file
```

## Attribution

Same deal as LOB: project direction and the owned internals (uint256,
interpreter core) are yours, implemented with AI mentoring; the harness
(opcode table, host, tests, CLI, docs) is AI-assisted. The point is
owning the theory — every opcode you implement, you should be able to
defend against the Yellow Paper.
