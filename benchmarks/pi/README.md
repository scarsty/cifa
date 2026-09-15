# 500-digit PI comparison checkpoint

Measured on 2026-09-15, Windows x64, MSVC Release. `comparison.lua` is the
user-supplied Lua comparison script, preserved unchanged. Cifa executes
`cifa/calc-pi.c`, including integer intermediates, floating division followed
by floor, inlined division, and split digit/carry loops.

Both engines use `/O2 /Ob2`; neither tested build uses PGO or JIT. Cifa uses
the standard memory pool. Compilation and console output are excluded from
execution timing. Lua uses its default garbage collector.

Three sequential batches reverse engine order in the middle batch:

| Batch | Cifa pool median, ms | Lua 5.4.5 median, ms |
| --- | ---: | ---: |
| 1: Cifa, Lua | 39.5485 | 10.4687 |
| 2: Lua, Cifa | 39.4602 | 10.5000 |
| 3: Cifa, Lua | 39.5947 | 10.4062 |
| Median of batch medians | 39.5485 | 10.4687 |

Cifa takes approximately **3.78 times** Lua's execution time on this complete
workload. This is distinct from the approximately 2.54-times gap in the minimal
scalar increment benchmark. It is a final comparison, not a before/after
measurement isolating any single optimization.

Cifa warms up once and measures 15 individual executions using C++
`steady_clock`. The supplied Lua harness warms up once and measures six batches
of 32 calculations using `os.clock`, reporting milliseconds per calculation.
Its median is the upper middle sample. The timer and sampling protocols differ;
these results preserve the original comparison harness rather than claiming
identical measurement machinery.

All runs return 502 characters, with Lua FNV1a32 `1d4b4c2f`. A separate verifier
compares the entire current Cifa result with the freshly generated Lua output
in AST, unoptimized VM, and optimized VM modes; all match.

From the repository root, build and test Cifa, then run each command in the
batch order above. Substitute the path to your stock Lua 5.4.5 executable:

```powershell
./tools/build.ps1 -Configuration Release
./build/cmake/Release/cifa_benchmark.exe 15 pi --pool --vm-only
lua benchmarks/pi/comparison.lua
```

The Lua script writes its full result to `build/lua_pi_result.txt`. The local
audit used `build/lua-audit/out/Release/lua.exe`; raw timing and full-output
verification logs are retained under ignored `build/final-pi/`.

The optimization checkpoint includes standard PMR storage, scope binding
analysis/elision, guarded in-place integer increments, byte alias flags, and
precomputed integer loop descriptors. It does not include the subsequently
proposed elimination of remaining helper calls, SIMD, or a JIT.
