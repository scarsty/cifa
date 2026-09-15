# Focused Cifa / Lua big-number benchmark

This isolates the PI script's base-10000 big-number operations without the arctangent series or PI formatting. `workload.c` and `workload.lua` implement multiplication by a small integer, division by a small integer, and a multiplication/division round trip.

Each sample creates the same 128-block input, with block i = (i * 73 + 19) % 10000 for zero-based i, then performs 1,000 independent rounds against that input. The factor/divisor is 239. Multiplication splits digit traversal and carry draining. Division retains the two-array temporary/result algorithm. Quotients use floating division followed by floor in both languages. Remainders and blocks are integers. Roundtrip computes divide(multiply(input, 239), 239).

The driver embeds both engines and uses the same C++ steady_clock. Scripts compile once; one complete sample warms up before 15 measured samples. Each sample includes input construction, repeated arithmetic and a small checksum, and final result serialization. Compilation, console output, and native validation are outside timing. Lua uses its default GC and Cifa uses default allocation or an explicitly enabled standard pool. Fresh result allocation is included.

Every sample must match an independent native oracle's checksum and all final blocks. The Cifa AST interpreter is also checked outside timing. The native oracle uses exact integer division; these small positive inputs make its results equivalent to the scripts' floating division/floor. No timings for the native oracle are used as a performance baseline.

## Build and run

Requires CMake, a C++23 compiler, and Lua 5.4.5 source (not LuaJIT). From the repository root, with CMake on PATH:

```powershell
cmake -S benchmarks/big_mult_div -B build/big-mult-div-repro -A x64 -DLUA_SOURCE_DIR="D:/projects/cifa/build/lua-audit/lua-5.4.5/src"
cmake --build build/big-mult-div-repro --config Release --target big_mult_div --parallel
./build/big-mult-div-repro/Release/big_mult_div.exe cifa 0 1000 15
./build/big-mult-div-repro/Release/big_mult_div.exe lua 0 1000 15
./build/big-mult-div-repro/Release/big_mult_div.exe pool 0 1000 15
```

Modes: 0 = multiply, 1 = divide, 2 = multiply then divide, 3 = scalar increment loop, 4 = explicit element copy, 5 = large-integer append, 6 = small-integer append, 7 = scalar increment without an inner Cifa block scope. The last arguments are rounds per sample and measured sample count. Run from the repository root. The Lua source path above is local to this audit; substitute your source directory when reproducing elsewhere.

The focused runner now defaults to `pool` when the engine is omitted. Use `pool` for Cifa comparisons going forward; `cifa` remains an explicit direct-allocation control. The main `cifa_benchmark` also now defaults to a pool, accepts `--no-pool` for direct allocation, and prints the selected allocator. This changes benchmark defaults, not CifaBytecode's public constructor behavior.

## Measured results, 2026-09-15

MSVC x64 Release, Lua 5.4.5, same executable and timer. Three batches per engine/mode, 15 samples per batch. Engine order rotated Cifa/Lua/pool, Lua/pool/Cifa, pool/Cifa/Lua. Values are medians of three batch medians, milliseconds per 1,000 rounds (numerically also microseconds per round).

| Workload | Cifa default | Cifa pool | Lua | Default / Lua |
|---|---:|---:|---:|---:|
| Multiply | 28.1872 | 27.7501 | 7.0009 | 4.03x |
| Divide | 46.4839 | 45.8625 | 10.0725 | 4.61x |
| Multiply then divide | 74.3022 | 73.0054 | 17.5825 | 4.23x |

Raw outputs are local ignored artifacts under `build/big-mult-div/batch-*.txt`. The measured executable is `build/big-mult-div/out/Release/big_mult_div.exe`, built using a temporary CMake wrapper against these same sources and the same Lua source.

The gap survives removal of the PI series and its formatting. Division has both the larger absolute cost and the larger ratio. Pooling changes timings only about 1-2%; this does not establish that all allocation/container costs are negligible. A CPU profile or additional controlled scalar/array/call ablations would be needed to attribute the remaining gap.

This compares matching algorithms and numeric behavior, not identical runtime semantics. Cifa keeps reserve calls and container value semantics; Lua uses reference tables, numeric for loops, small size/modulo/append wrappers, and a local math.floor binding. This focused Lua implementation is derived from the supplied PI script but is not its untouched performance harness. Both languages still allocate new outputs per operation.

## Scalar and array isolation (pool is the Cifa baseline)

New cases are in `storage.c` and `workload.lua`. Each sample performs 5,000 rounds of 128 iterations (640,000 inner iterations). Large integers start at 9007199254740993 (2^53 + 1); small integers start at 19. Scalar mode increments a local without an array, then checksums once per round. Copy mode reads 128 source elements and appends them to a fresh result; this is an explicit element copy, not assignment/COW/reference aliasing. Append modes generate consecutive integers and append them to a fresh result. Cifa reserves 128 elements. Lua uses direct table append syntax, without the old append helper in these cases.

Same timer, one warmup, 15 samples, three alternating engine batches. All results match the native integer oracle, including every final array element; Cifa AST checks also pass. Final serialization is once per sample, outside the repeated inner loop but included in timing.

| Case | Cifa pool ms | Lua ms | Ratio |
|---|---:|---:|---:|
| Scalar increment, braced Cifa body | 22.7939 | 1.5751 | 14.47x |
| Explicit element copy | 51.7202 | 10.7816 | 4.80x |
| Append large integers | 38.3429 | 10.3799 | 3.69x |
| Append small integers | 38.4001 | 10.3785 | 3.70x |

Large and small integer appends are effectively equal here. The scalar result establishes a substantial gap even without arrays, multiplication, division, or math calls in the inner loop. Copying is slower than producing consecutive values in Cifa, but these are different operation sequences, so their difference is not a precise isolated index-read cost.

### Scope control

A subsequent pool-only A/B/B/A/A/B changes only the Cifa inner loop body from `{ value++; }` to `value++;`. Three batch medians were 25.6193/23.7148/25.9221 ms with braces versus 12.7342/12.6861/12.6118 ms without. Median-of-medians: 25.6193 versus 12.6861 ms, about 50.5% lower time. The output is unchanged. A Lua control run returned 1.6940 ms. These later absolute times should not be mixed into the earlier table.

The compiler emits ScopeEnter/ScopeLeave for a braced statement block (CifaBytecode.cpp around 2257); the runtime handlers manipulate scope and local-lifetime state (around 3834). NumericForNext increments and jumps back to condition evaluation; unlike Lua's numeric loop instruction it does not itself combine the limit test with iteration. These are concrete implementation leads, not a full CPU-time attribution. The engine was not changed in this investigation.

An initial combined Cifa case function was rejected with `bytecode control flow register mismatch`. Separate standalone Cifa functions avoid that compilation path; the compiler issue remains unresolved and is outside this benchmark change.

Raw logs: `build/big-mult-div/storage-results`. Reproduce a case with, for example:

```powershell
./build/big-mult-div/out/Release/big_mult_div.exe pool 4 5000 15
./build/big-mult-div/out/Release/big_mult_div.exe lua 4 5000 15
```

### Binding analysis follow-up (2026-09-15)

The compiler now uses a separate lexical binding analysis with branch intersections and loop backedge convergence. The scope regression tests check multiple-name updates, numeric assignment loops, nested scope ownership, and conservative barriers. A local benchmark variant replaces the two scalar bodies' `value++` with `value = value + 1`; Lua already uses that assignment expression.

Using the standard pool, 5,000 × 128 iterations, one warmup and 15 samples in each of three alternating batches, the braced assignment loop fell from 30.5070 ms in the saved earlier scope-elision executable to 20.2291 ms in the new build (33.7% lower). The unbraced assignment control was 19.8838 / 20.0426 ms. The original increment loop was 12.8759 / 12.6234 ms, and the fresh Lua scalar control was 1.7194 ms. Copy and append changed by only about 1%; those small cross-build changes are not attributed to scope removal.

The saved baseline binary may predate the last narrow literal-binding edits; this is not an exact source checkout A/B. Structural instruction-count tests and the same-build braced/unbraced assignment control independently support the scope-elision result. Full per-sample outputs, executable hashes, the assignment variant, and reproduction instructions are in the local `build/binding-analysis/` artifacts, especially `results.md` and `timings.txt`. All samples passed the native result oracle; Release and Debug passed all four CTest targets, and the rebuilt PI verifier matched the complete 502-character Lua output in AST and both VM modes.
