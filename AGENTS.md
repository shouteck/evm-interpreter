# Agent notes

## Toolchain (not on PATH — bundled inside Visual Studio)

- vcvars: `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`
- cmake 4.1.1: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- ninja: same tree, `...\CMake\Ninja\ninja.exe`

## Commands

- Build: `build.bat` (calls vcvars, configures Ninja once, then builds)
- Test: `build.bat test` (ctest)
- Run bytecode: `build\evm_run.exe 0x6001600101` or `evm_run file.hex`

## Conventions

- Mental model is the country-and-offices analogy (see GUIDE.md):
  `Evm` is the clerk, `Host` is the phone line to the country, memory is
  the desk, storage is the filing cabinet, gas is the stamp budget.
- Division of labor (same as the LOB project): the user owns
  `src/uint256.cpp` (M1) and `src/evm.cpp` internals (M2+). Everything
  else — hex, opcode tables, `InMemoryHost`, test harness, CLI — is
  harness. AI assists but doesn't implement the owned pieces.
- `U256` limbs are little-endian (`l[0]` = least significant); EVM byte
  order on the wire is big-endian — `from_bytes`/`byte_at` are the
  boundary where that matters.
- `Gas` is `int64_t` (signed) so out-of-gas is `gas < 0`.
- Halting is a *result*, not an exception: `StopReason` + `Error`.
  Exceptions mean bugs.
- Tests use the `TEST(name)`/`CHECK`/`CHECK_EQ` harness in
  `tests/test_util.hpp`; add files to `evm_tests` in CMakeLists.txt.
