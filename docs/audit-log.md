# Architectural audit log

Append-only record of every architectural decision in this project.

## How this log works

One entry per decision. Each entry has the same six parts: what was proposed, what was decided, why, what evidence backs it, what was rejected instead, and what it touches.

**Never rewrite history.** A decision that turns out to be wrong gets a new entry that supersedes it. The old entry stays, with its status changed to "Superseded by ADR-XXXX", and the new entry explains what was learned. A reviewer reading this log top to bottom should be able to reconstruct what was known at each point, including the parts that turned out to be wrong.

**Evidence is a pointer, not prose.** A command, a file path, a measurement, or a commit. If a claim in this log cannot be checked against something, it belongs in a discussion, not here.

**Open decisions are logged too.** An entry with status "Open" records the question, the current lean, and what it blocks, so that nobody re-derives the analysis later.

## Adding an entry

Copy the template below, give it the next ADR number, and add it below the existing entries in date order. Then update the two places that reference it:

1. The revision history table in [`architecture.md`](architecture.md#14-revision-history).
2. The open decisions table in [`architecture.md`](architecture.md#13-open-decisions), if the entry is still open.

## Template

```markdown
### ADR-XXXX — Title

**Date:** YYYY-MM-DD
**Status:** Proposed | Accepted | Open | Superseded by ADR-YYYY

**Proposed.** What was on the table.

**Decision.** What was chosen, in one or two sentences.

**Why.** The reasoning in plain words.

**Evidence.** Commands run, measurements taken, files read.

**Rejected.** The alternatives considered and why they lost.

**Impact.** Which parts of the system this touches.

**Follow-up.** What happens next, or what remains open.
```

---

## Entries

### ADR-0001 — Trace format moves from JSONL to binary SDCTRACE

**Date:** 2026-10-29
**Status:** Accepted

**Proposed.** Revision 1 of the blueprint specified JSONL for the trace, one JSON object per line, with the rationale "human-readable, streamable, crash-safe with a simple raw-append path". The evidence package stays JSON in every option.

**Decision.** The trace becomes a binary format with fixed 40-byte records, specified in [`trace-format.md`](trace-format.md). The evidence package stays a single JSON file. File extension `.sdct`.

**Why.** The trace is a machine-to-machine artifact between the runtime and the analyzer, and it is high volume. The evidence package is the machine-to-human artifact and it is bounded. Those two artifacts have opposite requirements, and the original blueprint used one format for both.

Four reasons, in order of weight.

1. Fixed-size records make the trace randomly indexable. Event *i* sits at `32 + meta_len + i*40`. The analyzer aligns two event streams against each other and then extracts a window around the divergence, so random access is the common case. With JSONL the analyzer has to build a line-offset index first.
2. Bit-exactness is native in binary and awkward in JSON. JSON has no hex literals, so every bit pattern becomes a 20-character string carrying 8 bytes of payload. The blueprint's own principle, "traces store raw IEEE-754 bit patterns, never decimal text", is satisfied by the format rather than fought by it.
3. Size and parse cost multiply across a sweep. A sweep runs one clean and one injected trace per injection site.
4. Framing is more crash-tolerant than line-delimited text, not less. A torn final record is a short read, and a full-length garbage record is caught by the type field plus the invariant that `occurrence` never decreases for a given `op_id`.

**Evidence.** Measurements taken on this machine with Homebrew LLVM 23.1.4, arm64-apple-darwin25.6.0.

- Bytes per event, JSONL against binary: about 113 against 40, roughly 2.8x.
- A 1M-event trace: 113 MB against 40 MB. A 200-site sweep: 22 GB against 8 GB.
- The blueprint's own trace example, `{"type":"event","op_id":42,"occ":7,"ops":[...],"res":...}`, is not valid JSON as written, since `0x3ff0000000000000` is not a JSON number. Every bit pattern would have to become a quoted string.
- A `u64` exceeds the 2^53 exact range of a JavaScript parser, so a numeric encoding silently loses precision for any reader that is not Python or Rust.
- Crash behavior: a short read at the tail is unambiguous, and the occurrence monotonicity check catches a garbage record of the correct length.

**Rejected.** Keeping JSONL and accepting the parse cost, rejected because the sweep multiplies the cost and the aligner, which is the hardest part of the analyzer, is exactly where indexability pays. Writing a raw binary dump as a fallback inside the JSONL file, rejected because the reader cannot parse a file with two interleaved formats. Making both artifacts binary, rejected because the evidence package must stay human-auditable and diffable, and it is bounded enough that size does not matter.

**Impact.** `docs/architecture.md` sections 3, 4, 5, 7.2, 7.3, 8, 9, 10, 11. New file `docs/trace-format.md`. New tool `tools/trace_dump.py`. Future source files `src/core/trace_writer.cpp` and `src/core/trace_reader.cpp`.

**Follow-up.** Decide whether `tools/trace_dump.py` or a C++ dumper is the better first cut. The human-inspection loss is real but bounded, since the evidence package embeds a window of surrounding events as JSON.

### ADR-0002 — One canonical compiler flag set, and the pass rejects fast-math IR

**Date:** 2026-10-29
**Status:** Accepted

**Proposed.** The blueprint said "same sources + flags gives identical operation numbering" but never fixed the flags. Nothing stopped a workload being built with `-O2 -ffast-math` on one day and `-O0` on another.

**Decision.** Every instrumented workload is compiled with one flag set. The pass refuses to instrument an instruction carrying any fast-math flag, and fails loudly rather than silently changing the arithmetic.

```
-O2 -gline-tables-only
-ffp-contract=off
-fno-vectorize -fno-slp-vectorize
```

The flag string is recorded in the trace header, so the analyzer can reject a clean and injected pair that came from different builds.

**Why.** Each flag earns its place, and the reasons are mechanical rather than stylistic.

- `-ffp-contract=off`. Without it, `a*b+c` becomes a single `llvm.fmuladd` and the two operations fuse into one rounding. The pass matches `fmul` and `fadd`, so it never sees the fused operation at all, and the most common numerical pattern in the codebase becomes invisible and un-injectable.
- `-fno-vectorize -fno-slp-vectorize`. Without them, a reduction loop becomes calls to `llvm.vector.reduce.fadd` and the additions happen inside the intrinsic, below the IR, where the pass cannot rewrite them. There is not a single vector `fadd` instruction in the IR to hook.
- `-gline-tables-only`. Column numbers are needed for the op-id key, and line tables alone carry them on this target, so full debug info buys nothing.
- No fast-math. `reassoc` permits reordering `a+b+c`, changing both the structure and the results. `nnan` and its relatives let the compiler delete NaN and Inf checks, including the workload's own detection logic.

**Evidence.** Commands and outputs, Homebrew LLVM 23.1.4.

- Default flags, IR: `%4 = tail call double @llvm.fmuladd.f64(double %0, double %1, double %2)`. With `-ffp-contract=off`: separate `%4 = fmul double %0, double %1` and `%5 = fadd double %4, double %2`, with no `contract` flag on either.
- Default flags, aarch64 assembly: `fmadd d0, d0, d1, d2`. With `-ffp-contract=off`: `fmul d0, d0, d1` then `fadd d0, d0, d2`. The backend does not re-form the FMA once the flag is off, so one flag covers the IR level and the machine level.
- Default flags, reduction loop: 4 calls to `llvm.vector.reduce.fadd.v2f64` plus 1 scalar `fadd`, and zero `fadd <2 x double>` instructions. With the vectorization flags: 1 scalar `fadd`.
- Fast-math flag check: with `-ffp-contract=off`, neither the `fmul` nor the `fadd` carries a fast-math flag.
- `-gline-tables-only`: 8 `DILocation` entries, all with a `column:` field.

**Rejected.** Staying at `-O0` permanently, rejected because the blueprint's workloads include an iterative solver and a flight-computer simulation, and `-O0` is a poor model of the code that will eventually run in production. Accepting fast-math with a warning, rejected because it destroys the numerical fidelity the whole platform rests on.

**Impact.** `docs/architecture.md` section 7.1. The build driver in `tools/instrument.sh`. The `compiler_flags` member of the trace metadata in [`trace-format.md`](trace-format.md).

**Follow-up.** Whether the pass should hard-fail on fast-math or offer an opt-in override for research use.

### ADR-0003 — Op id derivation under inlining

**Date:** 2026-10-29
**Status:** Accepted (ADR-0013)

**Proposed.** Revision 1 of the blueprint derives the `OpId` from "source location (file + line + column + function + opcode)".

**Decision.** Not yet made. Two candidates are on the table: derive the id from module order, or walk the `inlinedAt` chain and use the whole chain as the key.

**Why.** The proposed key is not unique. Inlining turns one source multiply into several IR instructions, and the inlined copies carry identical innermost `DebugLoc` values. They differ only in their `inlinedAt` chain, which the innermost location does not expose.

**Evidence.** With `-O2 -g`, a helper containing one multiply and called from two places produces 3 `fmul` instructions: two inlined copies in the caller plus one standalone copy in the helper. The two inlined copies:

```
!35 = !DILocation(line: 1, column: 46, scope: helper, inlinedAt: !DILocation(line: 3, column: 16, scope: caller))
!38 = !DILocation(line: 1, column: 46, scope: helper, inlinedAt: !DILocation(line: 4, column: 16, scope: caller))
```

Both are line 1, column 46, function `helper`, opcode `fmul`. `atomGroup` and `atomRank` are identical on both (1 and 2), so LLVM's newer same-line tracking does not separate them, and there are no discriminators in this build.

**Rejected.** Using the innermost location alone, rejected because it collides. Using a per-instruction ordinal within the function as the tiebreaker, rejected as a primary key because the ordinal shifts when unrelated code in that function is edited, so ids are not stable across edits.

**Impact.** `docs/architecture.md` sections 6 and 7.1. The `ops.json` schema.

**Follow-up.** Blocks Phase 3. Needs a decision on whether op ids are source-anchored or module-anchored. Module order is simpler and is collision-free, and it costs the ability to say "the multiply on line 12" in an evidence package without also carrying the location.

### ADR-0004 — Trace alignment when corruption changes control flow

**Date:** 2026-10-29
**Status:** Accepted (ADR-0014)

**Proposed.** Revision 1 says the two streams "should be identical up to the injection point" and the analyzer locates the first divergence.

**Decision.** Not yet made.

**Why.** The assumption holds only when the corrupted value does not change control flow. If the corrupted value feeds a comparison, the injected run executes a different sequence of operations, and a prefix comparison stops telling you anything after the first difference.

**Evidence.** No direct measurement yet. The consequence follows from the instrumentation design: the runtime records every arithmetic event, and a branch that takes a different path skips or repeats arithmetic events, so the two event streams have different lengths and different contents from that point on.

**Rejected.** Nothing rejected yet, this is an open question.

**Impact.** `docs/architecture.md` section 7.4. The `align.cpp` component.

**Follow-up.** Blocks Phase 4. Worth deciding alongside whether any workload deliberately produces a branch-affecting corruption, since without such a workload the harder alignment path is never exercised.

### ADR-0005 — The outcome taxonomy needs a CORRECTED class

**Date:** 2026-10-29
**Status:** Accepted (ADR-0013)

**Proposed.** Revision 1 fixes five classes: CRASH, DETECTED, SDC, MASKED, INCONCLUSIVE. The same document's Phase 6 expects a defense table with a "vote → all corrected" row.

**Decision.** Not yet made. The leaning is to add a CORRECTED class.

**Why.** A corrected run fits none of the five classes. It is not CRASH, not DETECTED, and not SDC. It is arguably MASKED, since the final result ends up back inside the acceptance criterion, but MASKED means the corruption was absorbed numerically rather than repaired by a redundancy mechanism. The two have different implications and the defense study is the entire point of Phase 6, so conflating them loses the result that phase exists to produce.

**Evidence.** The blueprint's own Phase 6 table. The classifier must also decide precedence: a run that is corrected but whose correction is itself wrong belongs to SDC, not CORRECTED.

**Rejected.** Nothing rejected yet.

**Impact.** `docs/architecture.md` section 7.4. The `classify.cpp` component. The evidence package schema, which carries the classification.

**Follow-up.** Blocks Phase 6's headline table and the classifier in Phase 4. The taxonomy is described as a stable public interface, so the change should land before anything downstream depends on it.

### ADR-0006 — Bit position convention

**Date:** 2026-10-29
**Status:** Accepted

**Proposed.** `BitPos` is a `uint8_t` covering 0 to 63 for double and 0 to 31 for float, but the blueprint never says which end of the pattern bit 0 is.

**Decision.** Bit 0 is the LSB of the uint64 IEEE-754 bit pattern. bit 63 is the sign bit, bits 62..52 are the exponent.

**Why.** Fixes the meaning of every `bit=` argument before the first injection campaign; prevents silent invalidation of results.

**Evidence.** No measurement needed. Flipping LSB is a small perturbation in mantissa context; sign/exponent conventions follow standard uint64 view of IEEE-754 bits.

**Rejected.** Bit 0 as MSB (would make bit 0 flips sign-dominant by default).

**Impact.** `docs/architecture.md` section 6. Runtime bit-flip routine. All drivers and evidence packages.

**Follow-up.** None.

### ADR-0007 — Moon workload: full surface→lunar landing, hand-rolled plant, FSW-only instrumentation

**Date:** 2026-10-31
**Status:** Accepted (ADR-0013)

**Proposed.** Use JEOD/Trick or hand-rolled plant for a full mission. User chose hand-rolled C++ plant for a clean MIT-licensed portfolio repo.

**Decision.** Mission is full surface → lunar landing. Plant is hand-rolled C++ 3D dynamics (not instrumented). FSW is our C++ GNC+defenses (instrumented). Phases: Ascent → TLI → Coast → LOI → Powered Descent → Landing. Success = landing within position/velocity tolerance. Trajectory logged separately for viewer.

**Why.** Portfolio clarity (single MIT license), removes LLVM-23/JEOD compatibility risk, keeps instrumentation scope tight.

**Evidence.** License review: NOSA-1.3 is reciprocal; mixed license is possible but noisier for a public portfolio. Basilisk ISC is clean but heavier build. Hand-rolled is fastest path to clean repo.

**Rejected.** JEOD/Trick (NOSA mixed license/obligations), Basilisk (heavier spike) — both viable but less clean for this goal.

**Impact.** `docs/architecture.md` sections 3,5,6,7.4,9.6,15. `workloads/moon/{plant,fsw}/`. Trajectory log format.

**Follow-up.** Define trajectory log schema in next iteration; keep `SDCTRACE` unchanged.

### ADR-0008 — Defense semantics

**Date:** 2026-10-31
**Status:** Accepted (ADR-0013)

**Proposed.** Define how TMR/lockstep/invariants/checksums behave with injection.

**Decision.** Replicate before critical section, vote after, protect the voter. Injection before replication corrupts all replicas identically (common-mode). Modes: `none`, `lockstep`, `vote/TMR`, `invariant`, `checksum`, `all` — measured equally.

**Why.** Prevents common TMR pitfall (injecting after replication) and makes the comparison fair across modes.

**Evidence.** Standard redundancy design; matches "vote → all corrected" goal.

**Rejected.** Injecting after replication (would mask common-mode class of faults).

**Impact.** `docs/architecture.md` section 16. Runtime/injection logic and analyzer classification.

**Follow-up.** Ensure injection targets follow this rule in drivers.

### ADR-0009 — Instrumentation scope: FSW TUs only

**Date:** 2026-10-31
**Status:** Accepted (ADR-0013)

**Proposed.** Instrument everything or just FSW.

**Decision.** Only FSW translation units instrumented. Plant TUs compiled without pass.

**Why.** Focuses study on flight computer SDC; plant is oracle/dynamics; reduces trace volume; cleaner separation of concerns.

**Evidence.** Goal is to compare defenses on FSW; plant correctness separate from FSW corruption effects.

**Rejected.** Instrumenting plant (larger traces, blurs responsibility).

**Impact.** `docs/architecture.md` sections 7.1,9.6,17. Build system.

**Follow-up.** Enforce in CMake/build scripts.

### ADR-0010 — Trajectory log + Three.js HUD

**Date:** 2026-10-31
**Status:** Accepted (ADR-0013)

**Proposed.** How to visualize mission.

**Decision.** Separate `trajectory.json` from `SDCTRACE`. Three.js HUD vendored offline, reads only trajectory + evidence. No CDN/network.

**Why.** Keeps normative trace format unchanged; viewer is read-only consumer; offline/portable.

**Evidence.** Design principle: fully offline.

**Rejected.** Embedding trajectory in SDCTRACE or fetching assets from CDN.

**Impact.** `docs/architecture.md` sections 3,4,9.7,18. `viewer/` with vendored Three.js.

**Follow-up.** Define trajectory log schema.

### ADR-0011 — The exit record's detection word becomes a flag set

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** Version 1 of the trace format defined the exit record's `operand1` as "the detected flag", a single boolean. ADR-0005 added a `CORRECTED` class to the outcome taxonomy, which requires the analyzer to distinguish a run that a redundancy mechanism repaired from a run where the workload merely noticed the fault. A single boolean cannot carry both signals.

**Decision.** `operand1` in an exit record becomes a flag set. Bit 0 is `DETECTED` and keeps the meaning the original definition gave it. Bit 1 is `CORRECTED`. Bits 2 and up are reserved and must be zero.

**Why.** The two signals have different implications and the defense study is the point of Phase 6. A lockstep pair detects a mismatch but cannot repair it, so it belongs in `DETECTED`. A TMR vote repairs the fault, so it belongs in `CORRECTED`. Collapsing them into one boolean would erase the result that phase exists to produce.

Bit 0 keeps its original position and meaning, so a reader written against the earlier definition still reads the detection signal correctly. Widening the existing word in place is smaller and less disruptive than adding a field, and it keeps the record at 40 bytes, which is what makes the index arithmetic in the aligner work.

**Evidence.** The runtime now carries three separate signals out of the workload: `sdc_report_result` (the final result bits), `sdc_report_detected` (the workload noticed), and `sdc_report_corrected` (a mechanism repaired it). The exit record has room for all three: the exit code in `operand0`, the two flags in `operand1`, and the final result bits in `result`.

The runtime also needs a default final result, because the workload source is required to stay plain with no annotations. The runtime tracks the last delivered result bit pattern and uses it unless the workload called `sdc_report_result` to override it. A plain workload therefore still produces a meaningful exit record.

**Rejected.** Adding a second exit record type, rejected because the record size is fixed at 40 bytes for the index arithmetic and a second type doubles the reader's branching for one extra bit. Overloading `op_id` or `occurrence` in the exit record, rejected because the format spec requires both to be zero there, and a record that violates its own spec is not auditable.

**Impact.** `docs/trace-format.md` section 2.3. `docs/architecture.md` section 7.3. `include/sdc/exit_record.hpp`. `src/core/trace_writer.cpp` and `src/core/trace_reader.cpp`. `src/runtime/runtime.cpp`. `src/analyzer/classify.cpp` and `src/analyzer/evidence.cpp`.

**Follow-up.** The version number stays at 1 because the change is backward compatible on read. If a future change makes the record layout incompatible, the version goes to 2 and the reader must reject the older shape rather than guess.

### ADR-0012 — Random injection resolves its site in the runtime, from a plan built by the driver

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** The campaign needs to try many injection sites without hand-picking each one. The choice of site has to be random enough to be interesting and reproducible enough to be auditable, and it has to be recorded so a reviewer can re-run the exact same fault.

**Decision.** The runtime gains a random injection mode. The driver writes an injection plan, a text file listing every operation that actually executes together with the number of times it executes. The runtime reads the plan, seeds `std::rand` from `SDC_SEED`, and draws one operation, one occurrence within that operation, and one bit position. The resolved triple is written into the trace metadata, so every run records exactly which site it hit.

The interface is `SDC_INJECT=random`, `SDC_SEED=<n>`, `SDC_INJECT_PLAN=<path>`, and `SDC_INJECT_BIT_RANGE=<lo>-<hi>`.

**Why.** Resolving the site in the runtime rather than in the driver keeps the whole decision in one place, and it means the runtime knows the occurrence counts that make a site reachable. A driver that picked the triple would need the counts anyway, and it would need to guess whether the operation is on a path the workload actually takes.

The plan is built from a clean run, which solves the reachability problem exactly: an operation the compiler optimized away never appears in the clean trace, so it never enters the plan, and the campaign never wastes a trial on an unreachable site.

Recording the resolved triple is what makes the campaign auditable. `std::rand` is specified by the C standard only up to its algorithm, so the sequence for a given seed can differ between C library implementations. The platform's reproducibility requirement is that the same binary and inputs give the same result, which holds. But a recorded result must also be replayable on another machine, and that is what the metadata buys: any run can be reproduced exactly with the manual `SDC_INJECT=op=..,occ=..,bit=..` form, regardless of which libc produced the original choice.

**Evidence.** A 200-trial campaign with seed 1337 against the `sum_plain` workload produced identical injection sites and verdicts across two independent runs. The results are in `results/sweep_sum_plain/`.

The measured distribution shows the expected structure. 83.0 percent of the trials classified as SDC and 17.0 percent as MASKED, and the split by bit region is the interesting part: a fault in the low mantissa masked 30 times out of 128, while a fault in the exponent masked only once out of 35. A fault high in the mantissa or in the exponent changes the value by orders of magnitude, and a fault in the low mantissa is a rounding-sized change that the accumulation absorbs.

**Rejected.** Picking the site in the Python driver, rejected because the driver cannot know how many times each operation executes without reading a clean trace, and the logic would be split across two languages. Using a stronger PRNG than `std::rand`, rejected as unnecessary: the sequence only has to be reproducible for a given binary, and the metadata makes the resolved site portable. Recording a timestamp or a process id with the seed, rejected because the trace format forbids anything that varies between two clean runs.

**Impact.** `src/runtime/runtime.cpp`. `tools/sdctrace.py`, a Python reader for the trace format. `tools/study/sweep.py`, the campaign driver. `tools/study/report.py`, the cross-sweep comparison table. `tools/study/overhead.py`, the instrumentation cost measurement.

**Follow-up.** The plan format is a text file with one `op_id max_occurrence` pair per line. It is internal to the driver and the runtime, and is not part of the trace format contract.

### ADR-0013 — Ratify ADR-0003, ADR-0005, and ADR-0007 to ADR-0010

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** ADR-0003 and ADR-0005 were logged as Open and ADR-0007 to ADR-0010 as Proposed. The living spec and the code already treated ADR-0003 and ADR-0005 as settled, so the log disagreed with both.

**Decision.** All six are accepted as written. ADR-0003 resolves to op ids in deterministic module order, with debug locations carried in `ops.json` for readability only. ADR-0005 resolves to adding the `CORRECTED` class.

**Why.** Phases 3 to 5 were built on ADR-0003 and ADR-0005, and Phase 6 builds on ADR-0007 to ADR-0010. The log has to state what the code depends on.

**Evidence.**
- ADR-0003: `src/instrumentation/SDCInstrument.cpp` assigns `++opId` in module iteration order.
- ADR-0005: `src/analyzer/classify.cpp` emits `CORRECTED`. ADR-0011 defines the exit-record bit that carries it.
- ADR-0007 to ADR-0010: user decision, 2026-10-31.

**Rejected.** For ADR-0003, keying on the full `inlinedAt` chain: it gives source-anchored ids but adds a chain walk and still needs a tiebreaker for identical chains. Module order is collision-free, and `ops.json` restores the source location. For ADR-0005, folding corrected runs into `MASKED`: rejected for the reasons in ADR-0005.

**Impact.** Status lines of ADR-0003, ADR-0005, ADR-0007, ADR-0008, ADR-0009, ADR-0010. `docs/architecture.md` sections 13 and 14. No code change.

**Follow-up.** ADR-0004 stays Open. Phase 4 shipped a bounded-lookahead resynchronization in `src/analyzer/align.cpp` without a recorded decision, and that needs its own entry.

### ADR-0014 — Alignment is a prefix scan on key and payload

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** ADR-0004 left open how alignment handles corruption that changes control flow. Phase 4 shipped a bounded-lookahead resynchronization in `src/analyzer/align.cpp` without a recorded decision: on a key mismatch it searched up to 64 events ahead in both streams for a common key, skipped to it, and did not mark a divergence.

**Decision.** The first divergence is the first index where either the `(op_id, occurrence)` key or the payload differs. The aligner stops there. There is no resynchronization.

**Why.** The runtime records the delivered, flipped result bits, so in a single-fault run the injected event always mismatches the clean event on payload at the same key. Control flow can only change after that event, so a prefix scan always reports the injection point. The resynchronization path only ran when two streams differed structurally with no corrupted value first, which means a nondeterministic workload, and in that case it reported no divergence or a later one.

**Evidence.**
- `src/runtime/runtime.cpp` writes `result_bits` after the bit flip.
- Probe against the Phase 4 aligner: a flip at event 4 followed by skipped events 5 to 7 reported divergence at 4. Skipped events 5 to 7 with no flip reported `diverged=false` and `matched=10` against a 7-event injected stream. A skipped event 5 followed by a flip at 8 reported divergence at 8.
- `tests/unit/test_analyzer.cpp`: `test_align_reports_structural_change_as_divergence` fails against the resynchronizing aligner and passes after this change. `test_align_reports_injection_before_control_flow_change` passes on both.
- The 200-trial seed-1337 sweep on `sum_plain` reproduces `results/sweep_sum_plain/report.md` exactly after this change.

**Rejected.** Bounded-lookahead resynchronization, rejected because it only runs in cases where it gives a wrong answer. Full sequence alignment over keys, rejected because the first divergence is always the injection event, so analysis of what happens after it belongs in evidence windowing, not in the aligner.

**Impact.** `src/analyzer/align.cpp`, `src/analyzer/align.hpp`, `tests/unit/test_analyzer.cpp`. `docs/architecture.md` sections 7.4, 13, 14.

**Follow-up.** If a multi-fault mode or a nondeterministic workload is added, a structural divergence ahead of any payload mismatch is now reported at its true index, and the classifier sees it.

### ADR-0015 — Unit tests use GoogleTest

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** The unit tests used `assert` from `<cassert>` with a hand-written `main`. No document recorded a test framework decision.

**Decision.** Unit tests use GoogleTest 1.18, found through `find_package(GTest CONFIG)` from the Homebrew install, and registered per test case with `gtest_discover_tests`.

**Why.** `assert` compiles to nothing when `NDEBUG` is defined, so a Release build of the old tests passed without checking anything. The main driver is Phase 6: flight software tests need gMock to stand in for flight-specific dependencies (sensors, the plant, voters, mission-phase sequencing) with call expectations and scripted returns, which plain `assert` and doctest do not provide. GoogleTest also prints actual and expected values on failure, and `TEST_P` covers the matrix of defense modes and bit regions. The project already depends on a system-installed Homebrew LLVM, so a system-installed GoogleTest adds no new class of dependency and keeps the build offline.

**Evidence.**
- Homebrew googletest 1.18.0 links and runs with Homebrew clang 23.1.4, including `std::string` across the library boundary.
- `ctest -j8` runs 12 discovered tests and all pass, in both the default build and a `-DCMAKE_BUILD_TYPE=Release` build.
- A deliberately wrong expectation reports `Expected equality of these values: r.clean_index Which is: 5, 6u`.

**Rejected.** Plain `assert`, because `NDEBUG` silently disables it. doctest, because it has no mocking library and weaker parameterized tests than `TEST_P`. FetchContent, because it downloads at configure time and breaks offline builds.

**Impact.** `tests/unit/CMakeLists.txt`, `tests/unit/test_trace_io.cpp`, `tests/unit/test_analyzer.cpp`. Each trace I/O test now writes its own file under `/tmp`, so parallel `ctest` runs do not collide.

**Follow-up.** None.

### ADR-0016 — Moon mission model: full mission in 3D, simplified physics, precomputed plan, per-phase campaigns

**Date:** 2026-10-31
**Status:** Accepted. Refines ADR-0007.

**Proposed.** ADR-0007 fixes a full surface-to-landing mission but not the model fidelity, the guidance approach, or how faults are distributed across a multi-day mission.

**Decision.**
- Full mission: ascent, parking orbit, TLI, coast, LOI, powered descent, landing.
- 3D from the start. Rotating Earth with a real launch latitude. The Moon on a fixed circular orbit inclined to Earth's equator.
- Point-mass dynamics: position, velocity, propellant mass. Thrust direction is commanded directly, with no attitude dynamics. Earth and Moon gravity only. No atmosphere drag, no Sun.
- The plant integrates with fixed-step RK4 and is not instrumented (ADR-0009).
- Flight software ticks at 10 Hz during ascent, burns, and descent, and at 1 Hz during coast.
- Burn times, directions, and targets come from a tuning script run once against the clean simulation and are stored as mission-plan constants. The flight software executes the plan with simple closed-loop rules per phase.
- Every trial flies the whole mission. Faults are drawn per phase with equal trials per phase, not uniformly over operations.
- The outcome is judged on the plant's true state at touchdown.

**Why.** The aim is a recognizable full Earth-to-Moon trip that demonstrates SDC, not a flight-dynamics research code. Point mass with two-body gravity is the textbook level used for early mission planning. The 1 Hz coast rate keeps a trace at about 20M events instead of about 1.3B (estimated, unmeasured). Drawing faults per phase stops the coast phase, which executes about 99% of operations at a uniform rate, from dominating the results.

**Evidence.** Estimates only: about 500 flight software ops per busy tick, which gives about 800 MB per trace. The first measurement of a real trace replaces these numbers.

**Rejected.** Planar mission: rejected in favor of real 3D. Powered descent only: smaller, but not a full trip. One uniform campaign over the full mission: results dominated by coast. Onboard guidance that recomputes burns in flight: too deep for this phase.

**Limitations.** The precomputed plan fits only the nominal trajectory. The flight software cannot retarget after a fault pushes it off course, so some faults that replanning guidance would absorb will classify as SDC; results lean pessimistic. The plan is tied to one launch date and geometry.

**Impact.** `docs/architecture.md` sections 9 (Phase 6), 12, 15. New `workloads/moon/{plant,fsw}/`. New tuning script under `tools/`.

**Follow-up.** Open: touchdown tolerances that separate MASKED from SDC; how the runtime learns phase boundaries; a clean-vs-clean determinism check.

### ADR-0017 — Moon flight software builds at -O0; TMR and lockstep are written in the flight software source

**Date:** 2026-10-31
**Status:** Accepted. Supersedes ADR-0002 for the moon workload only.

**Proposed.** ADR-0008 requires replicating a computation and voting after it. At the canonical -O2, identical replicas merge and the voter is deleted before the pass runs.

**Decision.** The moon flight software uses its own pinned flag set, `-O0 -gline-tables-only -ffp-contract=off`, selected with `tools/instrument.sh --profile moon`. TMR (three replicas plus a vote) and lockstep (two replicas plus a compare) are written as plain code in the flight software. All other workloads keep the ADR-0002 flags.

**Why.** At -O0 each replica stays a separate call, so a fault inside one replica can spread through its operations before the vote. A fault before replication reaches all replicas identically, which is the common-mode case ADR-0008 asks to measure. The voter uses comparisons, which the pass does not instrument, so it is never faulted; that matches ADR-0008's protected voter. The flag string is in the trace header, so the analyzer still rejects a pair built with different flags. Run time is not a reason to keep -O2 here: instrumentation turns every FP op into a runtime hook call, which already dominates run time (3.9x median overhead on `sum_plain` at -O2), so fighting the optimizer would buy little speed. ADR-0002 rejected -O0 for fidelity to production code, not speed, and that cost is recorded under Limitations.

**Evidence.** Three `guidance()` replicas plus `vote()`, built with Homebrew clang 23.1.4. At -O2: one copy of the arithmetic (`fmul`, `fmul`, `fmul`, `fsub`, `ret`) and no voter. At -O0: three `call @guidance` and one `call @vote`.

**Rejected.** Keeping -O2 with compiler barriers (volatile, opaque calls, separate TUs, optnone): out of scope for now. Per-operation TMR inside the runtime: it survives -O2 but votes before a fault can spread and corrects everything by construction.

**Limitations.** Results describe unoptimized flight software. -O0 keeps operations an optimized build would remove, so the set of fault sites differs from an optimized binary.

**Impact.** `tools/instrument.sh`. `docs/architecture.md` sections 7.1, 9, 12, 16. `workloads/moon/fsw/`.

**Follow-up.** None.

### ADR-0018 — Powered descent: start state, guidance rule, vehicle, sensors, landing score, build

**Date:** 2026-10-31
**Status:** Accepted. Refines ADR-0016.

**Proposed.** Step 2 of the Phase 6 build order needs a descent the flight software can fly, a vehicle that can fly it, a rule for judging the landing, and a build that instruments only the flight software.

**Decision.**
- Start: circular lunar orbit at 15 km altitude at a chosen mission time, built from the Moon's position and velocity. The ephemeris gains a velocity function.
- Guidance, two phases. Braking: full throttle against the Moon-relative velocity. Final descent: thrust straight up, with throttle set by a proportional controller that tracks a target descent rate shrinking with altitude.
- Vehicle: generic engine with Apollo Lunar Module descent-class numbers (about 15,100 kg at start, about 6,900 kg dry, about 45 kN, Isp about 311 s), verified against a source before coding.
- Sensors: perfect. The flight software's navigation stage converts the Earth-centered state to Moon-relative terms using its own Moon ephemeris, then derives altitude and descent rate. This stage runs before guidance, so under TMR a fault in it reaches every replica (common-mode).
- Landing score, computed by the plant at touchdown: max(vertical speed / 3 m/s, horizontal speed / 1.2 m/s, distance from the clean run's touchdown point / 1000 m). Speeds are relative to the rotating lunar surface; the Moon is tidally locked, so it rotates at its orbital rate. No touchdown within the time limit scores infinity. The plant reports the score with `sdc_report_result`. The analyzer runs with reference 0.0 and `--abs-tol 1`: a score up to 1 passes; above 1, or NaN, is SDC. A hard landing is SDC, not CRASH; CRASH means the process died.
- Build: `workloads/moon/fsw/` is instrumented with the moon profile (ADR-0017). `workloads/moon/sim/` and the plant are compiled normally. `tools/instrument.sh --profile moon` merges the flight software TUs with `llvm-link`, runs the pass, then internalizes every flight software symbol except `fsw_tick`.

**Why.** Two-phase descent is about 30-40 lines and stable (estimated), and fits a demo that should stay out of the weeds. Lunar Module numbers are proven to cover this descent. Perfect sensors still give a navigation stage with real arithmetic ahead of guidance. Scoring in the plant keeps the judge on ground truth. One scalar score fits the analyzer's existing tolerance check, so the analyzer and the trace format stay unchanged. Internalizing is required because shared headers compile to `linkonce_odr` functions, and the linker would otherwise keep one copy for both sides: either the plant gets hooks, or the flight software loses them, with no build error.

**Evidence.**
- At -O0, `Vec3` `a + b` compiles to `call @_ZN4moonplENS_4Vec3ES0_`, defined `linkonce_odr`.
- `src/analyzer/main.cpp:138-150`: `within_abs` and `within_rel` are both false for NaN, so a NaN score is outside tolerance.

**Rejected.** Apollo polynomial guidance to a chosen site: about 3-4x the code (estimated). Noisy sensors with a filter: needs seeded noise to keep clean runs identical. Multiple metrics in the exit record: needs trace format and analyzer changes. Judging the landing in the flight software: the judge must be ground truth.

**Limitations.** The lander touches down where braking ends, not at a chosen site; position is judged against the clean run. Touchdown tolerances follow the Lunar Module's gear limits (to verify). Sensors are perfect.

**Impact.** New `workloads/moon/fsw/`, `workloads/moon/sim/`. `workloads/moon/common/moon_ephemeris.hpp` (velocity). `tools/instrument.sh`. `docs/architecture.md` sections 12, 15.

**Follow-up.** Verify the Lunar Module numbers and gear limits before coding. Choose the time limit. Still open: how defenses report DETECTED and CORRECTED; how the runtime learns phase boundaries; the clean-vs-clean determinism check.

### ADR-0019 — The pass instruments llvm.sqrt

**Date:** 2026-10-31
**Status:** Accepted

**Proposed.** The pass hooks only `fadd`, `fsub`, `fmul`, and `fdiv`. `std::sqrt` compiles to the intrinsic `llvm.sqrt.f64`, so square roots are never faulted, and the descent guidance computes distances with them.

**Decision.** Calls to `llvm.sqrt.f64` become calls to `sdc_runtime_f64_sqrt_hook(op_id, x)`. The event records `x` in `operand0` and zero in `operand1`. `ops.json` records the kind as `sqrt`. The audit recomputes with `std::sqrt`, which IEEE 754 requires to be correctly rounded, so the recomputation is exact. Calls into the math library (`sin`, `cos`, `atan2`) stay uninstrumented.

**Why.** A square root is one hardware instruction, as exposed to a fault as an add. Leaving it out would exclude every distance and altitude computation in the guidance from the campaign.

**Evidence.** At -O0, `std::sqrt(x)` is `call double @llvm.sqrt.f64` in IR and `fsqrt d0, d0` on arm64. `src/instrumentation/SDCInstrument.cpp` selects only `BinaryOperator`.

**Rejected.** Leaving the gap: biases results for guidance math. Instrumenting math library calls now: the descent rule does not use them.

**Impact.** `src/instrumentation/SDCInstrument.cpp`, `src/runtime/runtime.{hpp,cpp}`, `src/core/op_kind.cpp`, `src/analyzer/audit.cpp`, `docs/trace-format.md` section 2, `docs/architecture.md` section 7.1.

**Follow-up.** Math library calls, as one operation each with the flip on the result, when guidance first needs trigonometry.

### ADR-0020 — Verified lander numbers and the Lunar Module touchdown envelope

**Date:** 2026-10-31
**Status:** Accepted. Supersedes the landing-score formula in ADR-0018; the rest of ADR-0018 stands.

**Proposed.** ADR-0018 used remembered Lunar Module numbers and a box-shaped touchdown limit, `max(v / 3, h / 1.2, d / 1000)`, both to be verified before coding.

**Decision.**
- Vehicle: max thrust 45.04 kN, Isp 311 s, start mass 15,200 kg, dry mass 7,000 kg (15,200 kg total minus 8,200 kg descent propellant).
- Touchdown score, with `v` vertical and `h` horizontal speed relative to the surface and `d` distance from the clean run's touchdown point:
  `v_limit(h) = 3.05 - (3.05 - 2.13) * h / 1.22` (m/s), `score = max(v / v_limit(h), h / 1.22, d / 1000)`. A score up to 1 passes.
- The real engine's throttle limits (10% floor, 65% to 92.5% avoided) are not modeled; the engine stays generic.

**Why.** The Lunar Module gear's design envelope is not a box: it allows 3.05 m/s vertical when moving straight down but only 2.13 m/s at the 1.22 m/s horizontal limit, because sideways speed makes tipping more likely. The box would pass landings outside the real envelope. The descent guidance runs at full throttle while braking and in the 10% to 60% band near touchdown (estimated), so the unmodeled throttle limits rarely matter.

**Evidence.**
- Rogers, "Apollo Lunar Module Landing Gear", NASA JSC (NTRS 19730010151), Figures 6 and 8: design velocity envelope labeled 3.05 m/s (10 ft/s) and 2.13 m/s (7 ft/s) vertical, 1.22 m/s (4 ft/s) horizontal; all Apollo landings fell inside it. The trapezoid shape is read from the figure layout.
- Wikipedia, "Descent propulsion system": "throttle between 1,050 and 10,125 pounds-force (4.67–45.04 kN)"; Isp "311 s (at full thrust)"; 65% to 92.5% "was avoided to prevent excessive nozzle erosion".
- Wikipedia, "Apollo Lunar Module": launch mass "15,200 kg standard", DPS propellant "8,200 kg".
- Available speed change: 311 s x 9.80665 m/s^2 x ln(15,200 / 7,000) = 2,365 m/s, against roughly 2,000 to 2,100 m/s needed (estimated).

**Rejected.** The box limit: passes landings the real gear would not survive. Modeling the throttle limits: contradicts the generic engine and rarely binds.

**Limitations.** Isp is the full-thrust value; the engine is less efficient at low throttle (285 s at 10%), and a flight measurement gave 305.8 s. Slope limits (12 degrees) do not apply because the model has no terrain.

**Impact.** The landing score in `workloads/moon/sim/`. `docs/architecture.md` sections 13, 14, 15. `docs/math/moon-mission-math.tex` when the score is implemented.

**Follow-up.** None.

### ADR-0021 — Multi-file build profile and phase markers are workload-generic

**Date:** 2026-10-31
**Status:** Accepted. Supersedes the `--profile moon` naming in ADR-0018.

**Proposed.** ADR-0018 named the moon build `tools/instrument.sh --profile moon`, and per-phase campaigns (ADR-0016) need the runtime to know phase boundaries. Both risked tying the SDC platform to one workload.

**Decision.** The build profile is named for what it does, `--profile o0`, and takes the list of files to instrument as arguments; any multi-file workload can use it. Phase boundaries are reported through a generic region marker that any workload can call, not a moon-specific concept.

**Why.** The SDC platform (`src/`, `include/`, `tools/`) contains no workload-specific code, and `sum_plain` runs through it unchanged. Keeping it that way keeps the platform usable for other workloads.

**Evidence.** `grep -rliE 'moon|lander|landing|plant' src include tools` finds no files.

**Rejected.** Moon-named options in platform tools.

**Impact.** `tools/instrument.sh`. The runtime API when region markers are added.

**Follow-up.** Define the region marker API when per-phase campaigns are built.
