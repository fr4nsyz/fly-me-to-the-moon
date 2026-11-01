# SDC Investigator: project state

Current as of 2026-10-31. Read this before doing anything in the repository. Decisions and their evidence live in `docs/audit-log.md`; earlier versions of this file are in git history.

## Where things are

| Path | What it is |
|---|---|
| `/Users/fransys/Documents/fly-me-to-the-moon/` | This repository. Remote `fr4nsyz/fly-me-to-the-moon`. |
| `/Users/fransys/Documents/fly-me-to-the-moon-videos/` | Private notes repository for videos and interviews (`notes/`). |

## Toolchain on this machine

| Thing | Path or version |
|---|---|
| LLVM | 23.1.4 at `/opt/homebrew/opt/llvm` (`opt`, `llc`, `llvm-config`, `lib/cmake/llvm`) |
| CMake / Ninja | 4.4.3 / 1.13.2 |
| GoogleTest | 1.18.0 via Homebrew, `/opt/homebrew/lib/cmake/GTest` |
| Python | System `python3` is 3.10.5. Use `/opt/homebrew/bin/python3.14`; `tools/study/sweep.py` reproduces the stored seed-1337 sweep with it. |
| Target | arm64-apple-darwin24.6.0 (current machine). The compiler findings below were measured on darwin25.6.0. |

Notes that will save time later:

- In LLVM 23 the pass plugin header is `llvm/Plugins/PassPlugin.h`. Older tutorials say `llvm/Passes/PassPlugin.h` and will not compile.
- `add_llvm_pass_plugin` exists in LLVM 23's `AddLLVM.cmake`.
- `llvm-config` reports `--has-rtti YES`, `--assertion-mode OFF`, default `-std=c++17`. Set `-std=c++20` explicitly for project code.
- Build the pass plugin and the workloads with the same Homebrew clang 23. A plugin built against a different LLVM will not load into `opt`.

## Decisions (locked)
- Platform: LLVM IR pass hooks double `fadd`, `fsub`, `fmul`, `fdiv`, and `llvm.sqrt` (ADR-0019); binary `SDCTRACE` traces (ADR-0001); analyzer with prefix-scan alignment (ADR-0014) and seven outcome classes including CORRECTED (ADR-0005) and HANG (ADR-0022).
- Fault models: transient (one flip) and persistent (the same bit on every execution of the op from a chosen occurrence on, ADR-0023).
- Workload: powered lunar descent in a Moon-centered frame (ADR-0007, ADR-0016), Lunar Module class vehicle and touchdown envelope (ADR-0020), flight software instrumented at -O0 (ADR-0017), plant not instrumented (ADR-0009).
- Defenses: `none` and `tmr-lockstep`, a TMR and lockstep hybrid that mimics real rocket flight computers (ADR-0008): three computers, each a lockstep pair of cores, commands compared across computers. Each core is a separate copy of the same flight software made by the build (`instrument.sh --replicate`, per-copy internalize), so each has its own operations. The voter is assumed reliable (integer comparisons are outside the fault model).
- Viewer: Three.js HUD, vendored offline, reads `trajectory.json` and `evidence.json` (ADR-0010).
- Audit-log entries are rewritten when a decision changes; a new entry is added only for a new decision.

## Phase status
- Phases 0 to 5 (skeleton, trace I/O, runtime, LLVM pass, analyzer, drivers): done. `results/sweep_sum_plain/`: 83.0% SDC, 17.0% MASKED.
- Phase 6 lunar descent and defenses: done. Plant, navigation, guidance, landing judge, hang timeout, persistent faults, the TMR and lockstep hybrid, and the defense comparison.
- Phase 7 viewer: built (`viewer/`, `tools/view.py`); docs walkthrough with the user pending.

## Measured results
- Instrumentation overhead on `sum_plain`: 3.9x median wall time, 40.0 bytes per event.
- `sum_plain` bit regions: low mantissa masked 30/128, exponent masked 1/35.
- Clean lunar landing: touchdown at 527.8 s, 1.000 m/s descent, 0.000 m/s horizontal, 1,157.6 kg propellant left, score 0.328. 49 flight software ops, 381,838 events, 15.3 MB trace, 1.2 s per instrumented run. Two clean runs give byte-identical traces.
- Lunar descent, no defenses, transient faults: 200/200 MASKED (`results/sweep_moon_none/`). The closed loop re-measures the true state every tick, so one bad tick is corrected on the next.
- Lunar descent, persistent faults, replicated build, seed 1337: `none` 31 SDC, 169 MASKED, 169/200 safe landings (`results/sweep_moon_persistent_none/`); `tmr-lockstep` 0 SDC, 199 CORRECTED, 1 MASKED, 200/200 safe (`results/sweep_moon_persistent_tmr_lockstep/`). With `--jobs 8`: 4.5 min and 27 min.
- A 200-trial lunar sweep takes about 6 to 7 minutes. Trial traces are deleted after analysis unless `--keep-traces` is passed.

## Next move
The user wants to understand this phase deeply: explain each problem, decide together, keep them in the loop during code generation.
1. Walk the user through the viewer code (`viewer/template.html`, `tools/view.py`).
2. Navigation filter, to give the flight software state so a single transient flip can linger.

Open: math library calls (`sin`, `cos`, `atan2`) are not faulted.

## Compiler findings (measured, Homebrew LLVM 23.1.4)

All commands used `/opt/homebrew/opt/llvm/bin/clang++`. ADR-0002 and ADR-0017 rely on these.

### 1. FP contraction fuses two source ops into one

Default `-ffp-contract=on`:

```
%4 = tail call double @llvm.fmuladd.f64(double %0, double %1, double %2)
```

With `-ffp-contract=off`:

```
%4 = fmul double %0, double %1
%5 = fadd double %4, double %2
```

No `contract` fast-math flag is set on either instruction. The fused form rounds once, the separate form rounds twice, so the numerical results differ.

### 2. The backend does not re-form FMA once the flag is off

aarch64 assembly, default flags: `fmadd d0, d0, d1, d2`.
aarch64 assembly, `-ffp-contract=off`: `fmul d0, d0, d1` then `fadd d0, d0, d2`.

One flag handles both the IR level and the machine level.

### 3. Vectorization moves the arithmetic below the IR

At `-O2`, `s += a[i]` in a loop becomes 4 calls to `llvm.vector.reduce.fadd.v2f64` plus 1 scalar `fadd`. There is not a single `fadd <2 x double>` instruction. The vectorizer emitted the reduction intrinsic form and the additions happen inside it, expanded later by the backend.

With `-fno-vectorize -fno-slp-vectorize` the same loop is 1 scalar `fadd`.

### 4. Intrinsics and library calls are not instructions

| Source | IR form |
|---|---|
| `-a` | `fneg double %a`, an instruction |
| `std::abs(double)` | `llvm.fabs.f64`, an intrinsic call |
| `std::fabs(float)` | `llvm.fabs.f32`, an intrinsic call |
| `std::sqrt(x)` | `llvm.sqrt.f64`, an intrinsic call |
| `std::fma(a,b,c)` | `llvm.fma.f64`, an intrinsic call |
| `std::cbrt(x)` | a call to libm `cbrt` |

### 5. Constant folding removes ops entirely

`double constfold() { return 1.0 + 2.0; }` produces no `fadd` at all. IEEE-754 requires this, so it cannot be turned off. The injectable population is the set of FP ops that survive to runtime.

### 6. Inlining multiplies one source site into several

Given a helper with one multiply called from two places, `-O2` produces 3 `fmul` instructions: two inlined copies in the caller plus one standalone copy in the helper. The two inlined copies have identical innermost debug locations:

```
!35 = !DILocation(line: 1, column: 46, scope: helper, inlinedAt: !DILocation(line: 3, column: 16, scope: caller))
!38 = !DILocation(line: 1, column: 46, scope: helper, inlinedAt: !DILocation(line: 4, column: 16, scope: caller))
```

Only the `inlinedAt` chain distinguishes them. `atomGroup` and `atomRank` are identical on both (1 and 2), so LLVM's newer same-line tracking does not help, and there are no discriminators in this build.

Consequence: the blueprint's proposed op key of "file plus line plus column plus function plus opcode" collides for these two instructions. The prior art avoids this by using module order for op ids.

### 7. Things that are fine

- `clang foo.bc -O2` does **not** re-optimize the IR. Verified by compiling at `-O0` to bitcode and confirming the `caller()` function is byte-identical afterwards. The pass runs exactly once.
- `-gline-tables-only` still carries column numbers on this target (8 `DILocation` entries with columns). Full `-g` is not required.
- The DWARF `DIFile` filename is the path exactly as given on the command line. Compiling with a relative path yields a relative filename, which is what makes op ids portable across machines.
- arm64 and x86-64 have no x87 excess precision, so the analyzer's recompute-from-operands audit matches what the hardware did.
