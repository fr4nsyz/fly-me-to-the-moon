# SDC Investigator: project state

Written 2026-10-29 so a fresh opencode session can pick this up without re-deriving anything.
Every measurement below was run on this machine and can be re-run with the commands given.

## Capsule

- `GOLD/` is now a git repository, `fr4nsyz/fly-me-to-the-moon`, private. It holds the living spec, the frozen original, the normative trace format, and the audit log.
- The blueprint the user pasted is verbatim `../sdc-investigator/docs/architecture.md`, now preserved at `docs/architecture-v1.md`.
- The JSONL to binary trace change is written up and accepted as ADR-0001. The canonical compiler flag set is ADR-0002. Four decisions are still open.
- `../sdc-investigator/` is a complete working implementation of the same blueprint with 9 commits plus 21 files of uncommitted work. It is prior art to read, not to build on.
- `../SDC/` is the research repo that produced the blueprint.

## Where things are

| Path | What it is |
|---|---|
| `/Users/fransys/Documents/fly-me-to-the-moon/` | Current working directory. Git repo, remote `fr4nsyz/fly-me-to-the-moon` (private). Originally created as `/Users/fr4nsyz/vault/GOLD/` on 2026-10-29 21:49. |
| `/Users/fr4nsyz/vault/sdc-investigator/` | Built implementation. Git repo, branch `main`, tracks `origin/main`. 9 commits. Not present on the current machine. |
| `/Users/fr4nsyz/vault/SDC/` | Research repo. `rq.md`, `plan.md`, `sdc.md`, `RESULTS.md`, `ARBITER_2511.04052.pdf`. Background, not implementation. Not present on the current machine. |

`GOLD/` was created one minute after `sdc-investigator/docs/architecture.md` was touched (21:48, content unchanged). Whether `GOLD/` is a clean-room rebuild or a scratch directory is not established. **Ask before assuming.**

## Threads

- `[verified, uncommitted]` Prior art in `../sdc-investigator` is complete through milestone M6 and has a TMR defense mode in flight. Details below.
- `[planned, not started]` Trace format change from JSONL to binary. Agreed in principle, not written up, not approved.
- `[planned, not started]` Document set: frozen original, living spec, normative format spec, append-only audit log. Requested, not created.
- `[planned, not started]` Canonical compiler flag set and op-identity scheme for optimized builds. Measurements done, decisions not taken.

## Prior art: what `../sdc-investigator` already does

Commits, newest first:

```
5a2f2c4 docs: future work section - scope widening and the RQ's open ends
06aacf5 docs: drop commit hashes from the README milestone table
04ff15d llm: verification loop - machine-checked claims and executed falsifiable predictions
9776184 study: two-workload fault sweep, overheads, and the M6 report
e7485c2 llm: evidence-grounded hypothesis adapter and evaluation suite
b4b6380 analyzer: divergence analysis, arithmetic audit, classification, evidence
6477bbf instrumentation: LLVM 23 pass, two-TU auto workload, M2 gates
bea8736 runtime: fault-injection runtime, workloads, sdc-diff, M1 gates
4fc97df core: trace contracts, JSONL writer/reader, and test scaffold
```

What already exists and works:

- **Instrumentation.** `src/instrumentation/sdc_pass.cpp`. Module pass `sdc-instrument` rewrites every scalar `double` add/sub/mul/div into a call to `sdc_f64_bin(i32 op_id, i8 kind, i64 lhs, i64 rhs, i64 result)`, passing the original result bits alongside the operands. The original result is carried in so the runtime can record delivered bits versus original bits.
- **Build.** `tools/instrument.sh` compiles each translation unit to bitcode with `-O0 -g -fno-fast-math -ffp-contract=off`, merges with `llvm-link`, runs the pass, emits `ops.json` (op id to file/line/col/function/kind from debug info), links the runtime.
- **Op ids.** Assigned `1..N` in deterministic module order. Not a source-location hash. This matters: the source-location key collision described under "Inlining" below does not affect this scheme.
- **Runtime.** `src/runtime/sdc_runtime.cpp`. Env-var config (`SDC_TRACE_PATH`, `SDC_INJECT_OP_ID`, `SDC_INJECT_OCC`, `SDC_INJECT_BIT`). Missing vars cause a sleep, malformed vars abort loudly. `atexit` writes the summary record. A segfault is caught by a signal handler that writes the summary with async-signal-safe primitives and re-raises. The workload reports in-band via `sdc_report_detected(msg)` and `sdc_report_result(x)`.
- **Trace core.** `src/core/trace_writer.cpp` and `src/core/jsonl_reader.cpp`. JSONL. Line-buffered, which the README notes costs about 6x wall time.
- **Analyzer.** `src/analyzer/{alignment,audit,classify,evidence}.cpp`. CLI `sdc-analyze` with `--reference-bits`, `--abs-tol`, `--rel-tol`, `--evidence`.
- **LLM layer.** `tools/llm_analyzer/` with `investigate.py`, `evaluate.py`, `schema.py`, prompts v1 and v2.
- **Study.** `tools/study/run_study.py` and `run_defenses.py`. Results in `experiments/study/report.md`.
- **Measured results.** Fault outcome is workload-dominated: 98.5% masking on a convergence-dominated algorithm versus 36% on a plain reduction. The deterministic pipeline localizes the injection completely in about 40 ms. The LLM verifies 88% of its quantitative claims against the evidence but its stated predictions fail 60% of the time.

### In-flight uncommitted work: TMR defense mode

21 files, about 1006 insertions, all uncommitted on `main`. This is the "vote, all corrected" scenario the blueprint asks for in Phase 6.

- `DefenseMode { Off, Tmr }` and `kReplicaCount = 3` in `include/sdc/event.hpp`.
- New hook `sdc_f64_tmr(op_id, kind, lhs, rhs, r0, r1, r2)` plus `sdc_register_tmr()` in `include/sdc/runtime_api.hpp`.
- Per-replica injection: `InjectConfig` gains `replica` and `replica_count`.
- `OpEvent` gains `corrected`, `injected_bit`, `has_replicas`, `replicas[3]`.
- `ExitRecord` gains `corrected_count` and `uncorrected_count`.
- `kTraceSchemaVersion` bumped 1 to 2.
- Touched: `include/sdc/event.hpp`, `include/sdc/runtime_api.hpp`, `src/analyzer/{alignment,audit,classify,evidence,sdc_analyze}.cpp`, `src/core/{jsonl_reader,trace_writer}.cpp`, `src/instrumentation/sdc_pass.cpp`, `src/runtime/sdc_runtime.cpp`, `tests/CMakeLists.txt`, `tests/integration/test_m4_gates.cpp`, `tests/unit/{test_analyzer,test_contracts,test_trace_io}.cpp`, `tools/instrument.sh`, `README.md`.

`docs/architecture.md` has no content diff despite a recent mtime.

### What the README already lists as future work

Relevant because it overlaps with this session's findings.

- **Optimized builds.** `-O0` was a deliberate v1 choice. `-O1`/`-O2` need answers for mem2reg, vectorization, and instruction scheduling before op id to source mapping stays meaningful.
- **Binary trace format and selective capture.** The SDD anticipated both once the event schema stabilized, which it has.
- **Integer and f32 operations (schema v2).** Integer faults would let corruption change control flow.
- **Branch-affecting corruption stories.** No current fixture produces one.
- **Multithreaded trace reconstruction.** Excluded by design. Needs per-thread sequence domains.
- **Batched trace buffering.** The line-buffered crash-safety trade-off costs about 6x wall time.
- **Linux x86-64 validation.** The SDD's primary platform, where `long double` is genuinely 80-bit.

## Toolchain on this machine

| Thing | Path or version |
|---|---|
| LLVM | 23.1.4 at `/opt/homebrew/opt/llvm` (`opt`, `llc`, `llvm-config`, `lib/cmake/llvm`) |
| CMake / Ninja | 4.4.3 / 1.13.2 |
| GoogleTest | 1.18.0 via Homebrew, `/opt/homebrew/lib/cmake/GTest` |
| Python | System `python3` is 3.10.5. Use `/opt/homebrew/bin/python3.14`; `tools/study/sweep.py` reproduces the stored seed-1337 sweep with it. |
| Target | arm64-apple-darwin24.6.0 (current machine). Measurements above were taken on darwin25.6.0. |

Notes that will save time later:

- In LLVM 23 the pass plugin header is `llvm/Plugins/PassPlugin.h`. Older tutorials say `llvm/Passes/PassPlugin.h` and will not compile.
- `add_llvm_pass_plugin` exists in LLVM 23's `AddLLVM.cmake`.
- `llvm-config` reports `--has-rtti YES`, `--assertion-mode OFF`, default `-std=c++17`. Set `-std=c++20` explicitly for project code.
- Build the pass plugin and the workloads with the same Homebrew clang 23. A plugin built against a different LLVM will not load into `opt`.

## Measured findings from this session

All commands used `$L=/opt/homebrew/opt/llvm` and its `bin/clang++`.

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

## Trace format change: accepted

ADR-0001, accepted. The trace moves from JSONL to a binary format, and the evidence package stays JSON. Full rationale and evidence in `docs/audit-log.md`, normative byte layout in `docs/trace-format.md`.

Reasons, in order of weight:

1. Fixed-size records make the trace randomly indexable. With 40-byte records, event *i* is at `header_len + i*40`. The aligner compares two streams element-wise and then pulls a window around the divergence, so index arithmetic beats scanning for line offsets.
2. "Bit-exact, never decimal text" is native in binary and awkward in JSON. JSON has no hex literals, so every bit pattern becomes a 20-character string carrying 8 bytes of payload.
3. Size and parse cost multiply across a sweep. A JSONL event is about 113 bytes against 40 for binary. A 1M-event trace is 113 MB against 40 MB, and a 200-site sweep is 22 GB against 8 GB.
4. Framing is more crash-safe, not less. A torn final record shows up as a short read, and a full-length garbage record is caught by validating the type field plus the invariant that `occurrence` never decreases for a given `op_id`.

Sketch:

```
File := Header Record*

Header:  "SDCTRACE" | u32 version | u32 flags | u32 meta_len | u32 record_size | u64 reserved
         then meta_len bytes of UTF-8 JSON metadata (workload, build id, flags)
         records begin at offset 32 + meta_len

Record (40 bytes, fixed):
  u32 type        0 = event, 1 = exit
  u32 op_id
  u64 occurrence
  u64 operand0
  u64 operand1
  u64 result
```

For `type = 1` the fields are reused: `operand0` holds the exit code, `operand1` holds the detected flag, `result` holds the final result bits, and the two top slots are free for workload-specific summary.

What is given up: `head` and `grep` on traces. The evidence package already embeds a window of surrounding events, so humans see the relevant slice as JSON. A small dump tool covers the rest.

JSON evidence rules: encode u64 bit patterns as hex strings, because JSON numbers are decimal and a u64 exceeds the 2^53 exact range of a JavaScript parser. Keep a fixed key order and 2-space indent so diffs stay stable.

Note: `../sdc-investigator/README.md` already lists the binary trace format as anticipated future work, so this change is consistent with prior intent rather than a departure from it.

## Open decisions

Nothing below has been decided. Each one blocks writing code.

| ID | Decision | Current lean |
|---|---|---|
| D1 | Bit numbering convention | Bit 0 is the LSB of the uint64 IEEE-754 pattern. Bit 63 is the sign bit. |
| D2 | u64 encoding inside JSON | Hex strings, not decimal numbers. |
| D3 | Op id scheme | Sequential ids in canonical sorted order, not a hash. A 32-bit hash collides with 50 percent probability around 77k ops. The prior art uses module order, which also works. |
| D4 | JSON library | Hand-rolled strict subset, because the schema is fixed and the project is dependency-free. `nlohmann/json` is the safer alternative. |
| D5 | Crash model for the trace | One torn trailing record is tolerated and means the process died during recording. The blueprint's "fall back to a raw binary dump in the same file" is self-contradictory, since the reader cannot parse it. |
| D6 | Where the document set lives | Resolved. The document set lives in this repository. The original stays at `../sdc-investigator/docs/architecture.md`, untouched. |
| D7 | Does the taxonomy need a CORRECTED class | ADR-0005, open. The blueprint fixes five classes with no CORRECTED, yet its own defense table says "vote, all corrected". The prior art's in-flight TMR work adds exactly this concept via `corrected` and `corrected_count`, which validates the concern. |
| D8 | Op identity under inlining | ADR-0003, open. Walk the `inlinedAt` chain and use the whole chain as the key, or derive the id from module order. |

## Canonical compiler flags

For any build that is not `-O0`:

```
-O2 -gline-tables-only
-ffp-contract=off
-fno-vectorize -fno-slp-vectorize
```

Never fast-math. The pass should reject any FP instruction carrying a fast-math flag rather than instrument it, so a bad build fails loudly instead of silently changing the arithmetic. Pin the flag string in one place and store it in the trace header so the analyzer can reject a clean and injected pair that came from different builds.

## Document set: created

The user asked for:

> make a new document making this change, but don't delete the original. as we make changes, i want to make edits upon it. this is part of the architectural development stage, and i want to have an audit log of all of it

Delivered:

| File | Purpose |
|---|---|
| `docs/architecture-v1.md` | Frozen original, preserved unchanged apart from a banner recording its provenance. |
| `docs/architecture.md` | The living spec. Carries the trace format change, and is edited as decisions accumulate. |
| `docs/trace-format.md` | Normative SDCTRACE binary specification. |
| `docs/audit-log.md` | Append-only log. ADR-0001 through ADR-0006. |

Convention for edits: change the living spec, then add or rewrite the audit-log entry naming the sections touched, then record the decision ID in the spec's revision history and open decisions tables. Entries are rewritten when a decision changes; git history keeps earlier versions.

The original at `../sdc-investigator/docs/architecture.md` stays untouched.

## Decisions (locked)
- Plant: hand-rolled C++ 3D dynamics (MIT). FSW instrumented only.
- Mission: powered lunar descent in a Moon-centered frame (ADR-0007, ADR-0016).
- Defenses: none/lockstep/vote/TMR/invariant/checksum/all (measured equally).
- Viewer: Three.js HUD, vendored offline, reads trajectory.json + evidence.json.
- ADR-0003 accepted (module-order op ids), ADR-0005 accepted (CORRECTED), ADR-0006 accepted (bit 0 = LSB).
- ADR-0007-0010 accepted via ADR-0013. ADR-0004 closed by ADR-0014 (prefix-scan alignment).

## Phase status
- Phase 0 skeleton: done
- Phase 1 trace I/O: done
- Phase 2 runtime + manual injection: done
- Phase 3 LLVM pass (fadd/fsub/fmul/fdiv, real ops.json): done
- Phase 4 analyzer (align, audit, classify, evidence): done
- Phase 5 drivers + first sweep: done. 200-trial campaign, seed 1337, in `results/sweep_sum_plain/`. 83.0% SDC, 17.0% MASKED.
- Phase 6 lunar descent + defenses: plant, sqrt instrumentation, o0 build profile, navigation, and guidance done; Moon-centered rework next
- Phase 7 viewer: after

## Measured results
- Instrumentation overhead on `sum_plain`: 3.9x median wall time, 40.0 bytes per event.
- Campaign bit-region split: low mantissa masked 30/128, exponent masked 1/35.
- Trial traces are deleted after analysis unless `--keep-traces` is passed.
- Lunar descent, no defenses, single-tick transient faults: 200/200 MASKED (seed 1337, `results/sweep_moon_none/`); 194 landed with the clean score, worst 0.356. The closed loop re-measures true state every tick, so one bad tick is corrected on the next. Defenses need persistent faults or FSW state to show anything.
- Clean lunar landing: 49 FSW ops, 381,838 events, 15.3 MB trace, 1.2 s per instrumented run; a 200-trial sweep takes about 6 min.

## Next move
Phase 6 build order (ADR-0016). The user wants to understand this phase deeply: explain each problem, decide together, keep them in the loop during code generation.
1. Moon-centered rework: delete the ephemeris, Earth gravity, and unused constants; Moon-only gravity; navigation with constant spin.
2. Piece G: simulation loop, start orbit, touchdown interpolation, landing judge and score, flight log.
3. First clean landing; tune guidance values.
4. Per-trial timeout and HANG class: done (ADR-0022).
5. Defenses in FSW source at -O0 (ADR-0017): lockstep, TMR, invariant (checksum to be decided).
6. Sweep with landing score, defense table.
Open: clean-vs-clean determinism check, how defenses report DETECTED/CORRECTED given the plain-workload principle, and checksum vs the ALU-result fault model (checksums protect memory, faults hit op results). Math library calls (`sin`, `cos`, `atan2`) are not faulted; `sqrt` is instrumented (ADR-0019).
Then Phase 7: Three.js HUD reading `trajectory.json` and `evidence.json` (ADR-0010).
