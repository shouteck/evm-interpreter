# evm — an Ethereum Virtual Machine interpreter in C++20

The execution core of Ethereum, built from scratch: a 256-bit stack machine,
gas-metered and deterministic, with nested calls, journaled state rollback,
contract creation, and hand-rolled Keccak-256.

**It passes 585/585 official Ethereum VMTests** (the `legacytests` corpus —
Frontier-era fork rules), plus 73 unit tests.

**[Live demo →](https://shouteck.github.io/evm-interpreter/)** — the interpreter
compiled to WebAssembly, stepping through real compiled Solidity contracts:
an ERC-20 `transfer`, a proxy `DELEGATECALL`, and the DAO reentrancy bug.

## The mental model: a country full of offices

Ethereum is a country; every contract account is an office.

- **Code on the wall** — the bytecode, read one instruction at a time.
- **The clerk** — `Evm`. `step()` fetches, charges gas, executes, advances
  `pc`. `run()` is `while (step()) {}` plus frame-boundary bookkeeping.
- **Stack of paper (max 1024)** — the operand stack.
- **The desk** — memory: byte-addressed scratchpad, wiped between visits.
- **The filing cabinet** — storage: permanent, 256-bit slots, priciest
  thing to touch.
- **A visitor with a letter** — `CallContext` + `calldata`.
- **A stamp budget** — gas: run out and everything done is shredded.
- **A phone line to the government** — `Host`: balances, other cabinets,
  block metadata. `InMemoryHost` is a flat in-memory world.
- **The receipt book** — the journal: every world-state write files an
  undo receipt. `checkpoint()` notes the stack height; `revert(cp)` replays
  receipts backwards. A nested call that fails unwinds completely — cash
  transfer, cabinet writes, posted notices.

## Features

- **Hand-rolled `U256`** — 4×`u64` limbs, full arithmetic incl. signed
  division/modulo, `addmod`/`mulmod`, `exp`, two's-complement semantics,
  mod-2^256 wraparound. No bigint library.
- **Full opcode surface** — arithmetic, bitwise, memory, storage, flow,
  all context/block ops, `SHA3`, `LOG0–4`, `CALL`/`DELEGATECALL`/
  `STATICCALL`, `CREATE`/`CREATE2` (init code → runtime code, correct
  address derivation), `SELFDESTRUCT`, `EXTCODE*`, `BLOCKHASH`,
  `RETURNDATA*`, `INVALID`.
- **Nested execution** — `CALL` checkpoints the host, transfers value,
  forwards gas under the 63/64 rule, runs a child `Evm` on the callee's
  code, refunds unused gas, commits or reverts the child's world-state
  footprint, and pastes the return data into parent memory.
- **Journal rollback** — every storage write, balance move, nonce bump,
  log post, and account creation is journaled; `REVERT` and exceptional
  halts unwind the frame's receipts. Static frames refuse writes.
- **Gas** — Frontier-era schedule (what the legacy VMTests encode):
  tiered opcode fares, memory expansion `3w + w²/512`, copy/hash/log
  per-word and per-byte metering, `SSTORE` fresh/dirty pricing with
  clear-slot refunds, `CALL` value/new-account surcharges.
- **Deterministic JUMPDEST analysis** — one pre-scan marks real jump
  targets, skipping `PUSH` immediates.
- **Tracer hook** — `Evm::on_step` fires per instruction (propagates
  into child frames); the WASM demo and any future debugger ride on it.

## Conformance

`build\evm_vectors.exe` sweeps `tests/fixtures/vmtests` (609 files from
`ethereum/legacytests`):

```
vmArithmeticTest              196 pass     0 fail
vmBitwiseLogicOperation        61 pass     0 fail
vmBlockInfoTest                 5 pass     0 fail
vmEnvironmentalInfo            33 pass     0 fail
vmIOandFlowOperations         144 pass     0 fail
vmLogTest                      46 pass     0 fail
vmPushDupSwapTest              74 pass     0 fail
vmSha3Test                     18 pass     0 fail
vmSystemOperations              7 pass     0 fail
vmTests                         1 pass     0 fail
--------------------------------------------------------------
TOTAL                         585 pass     0 fail
```

Each fixture maps `pre` → `InMemoryHost`, runs `exec` through the real
machine, and diffs `post` balances/nonces/storage, remaining gas, return
data, and the `keccak(rlp(logs))` receipt hash. `vmPerformance` and
`vmRandomTest` are excluded from the sweep (benchmark loops / noise), not
from the repo.

**Fork note:** the legacy VMTests encode Frontier-era semantics — `SLOAD`
is 50, `SELFDESTRUCT` is free, `CALL` is 40. A post-Berlin port (warm/cold
access pricing, `PUSH0`) is a deliberate non-goal; `GeneralStateTests`
would need a Merkle trie, also out of scope.

## The demo

`docs/` is a GitHub Pages site. `build_wasm.bat` compiles the same
`evm` library + `tools/demo/scenario.cpp` to WASM via emsdk; the page
calls `evm_run_scenario(json)` → a full step trace (pc, opcode, stack,
gas, journal events, per-frame depth) and animates it: the wall with the
clerk's finger highlighted, the pile of slips, the stamp budget, and the
receipt book as writes land and unwind.

Scenarios are real Solidity compiled with `solc 0.8.19 --evm-version
paris` (`tools/demo/contracts.sol`): an ERC-20 `transfer`, a proxy
`DELEGATECALL` showing storage lands in the proxy's cabinet, and a
re-entrant bank drain — call frames nested three deep before the receipts
come home.

## Build & run (Windows, MSVC)

```bat
build.bat                 :: configure + build (VS-bundled CMake/Ninja)
build.bat test            :: unit suite — 73 tests
build\evm_run 0x6001600101
build\evm_vectors.exe tests\fixtures\vmtests
build\evm_scenario.exe tools\demo\scenarios\dao.json
build_wasm.bat            :: docs/evm.js + docs/evm.wasm (needs emsdk)
```

## Layout

```
include/evm/  types.hpp uint256.hpp opcode.hpp host.hpp evm.hpp
              hex.hpp keccak.hpp
src/          uint256.cpp evm.cpp host.cpp opcode.cpp hex.cpp
              keccak.cpp main.cpp
tests/        test_util/json harness + unit suites + test_vectors.cpp
              fixtures/vmtests — official corpus (609 files)
tools/demo/   contracts.sol + compiled runtimes + scenario.cpp shim
docs/         index.html + evm.js/evm.wasm + scenarios.js (GitHub Pages)
```

## Who built what

Honest split — this project is deliberately teaching-shaped:

- **Hand-written by the author** — `src/uint256.cpp` (M1: all 256-bit
  arithmetic) and `src/evm.cpp` internals (M2+: the interpreter loop, every
  opcode arm, gas, the CALL/CREATE machinery). AI supplied explanations,
  test cases, and review.
- **AI-assisted plumbing (Devin)** — scaffold and harness:
  `host.hpp`/`host.cpp` (world state + journal), `opcode.hpp`/`opcode.cpp`
  tables, `hex`, `keccak.cpp` (hand-rolled SHA-3 permutation), `evm_run`
  CLI, test harness + suites, the VMTests corpus runner, `scenario.cpp`,
  WASM build, and this page's demo.

## Roadmap

- [x] M0 — scaffold, harness, smoke tests
- [x] M1 — `U256`: 4×u64 arithmetic, signed variants, addmod/mulmod
- [x] M2 — interpreter core: stack/arith, memory, flow, storage, gas
- [x] M3 — context ops, calldata, block fields, SHA3, LOG, metered gas
- [x] M4 — official VMTests corpus: **585/585**
- [x] Stretch — CALL/CREATE/SELFDESTRUCT family, journal rollback,
      WASM demo with narrated scenarios
- [ ] Stretch — perf pass (`vmPerformance` benchmarks), post-Berlin fork
