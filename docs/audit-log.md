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
**Status:** Open

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
**Status:** Open

**Proposed.** Revision 1 says the two streams "should be identical up to the injection point" and the analyzer locates the first divergence.

**Decision.** Not yet made.

**Why.** The assumption holds only when the corrupted value does not change control flow. If the corrupted value feeds a comparison, the injected run executes a different sequence of operations, and a prefix comparison stops telling you anything after the first difference.

**Evidence.** No direct measurement yet. The consequence follows from the instrumentation design: the runtime records every arithmetic event, and a branch that takes a different path skips or repeats arithmetic events, so the two event streams have different lengths and different contents from that point on.

**Rejected.** Nothing rejected yet, this is an open question.

**Impact.** `docs/architecture.md` section 7.4. The `align.cpp` component.

**Follow-up.** Blocks Phase 4. Worth deciding alongside whether any workload deliberately produces a branch-affecting corruption, since without such a workload the harder alignment path is never exercised.

### ADR-0005 — The outcome taxonomy needs a CORRECTED class

**Date:** 2026-10-29
**Status:** Open

**Proposed.** Revision 1 fixes five classes: CRASH, DETECTED, SDC, MASKED, INCONCLUSIVE. The same document's Phase 6 expects a defense table with a "vote → all corrected" row.

**Decision.** Not yet made. The leaning is to add a CORRECTED class.

**Why.** A corrected run fits none of the five classes. It is not CRASH, not DETECTED, and not SDC. It is arguably MASKED, since the final result ends up back inside the acceptance criterion, but MASKED means the corruption was absorbed numerically rather than repaired by a redundancy mechanism. The two have different implications and the defense study is the entire point of Phase 6, so conflating them loses the result that phase exists to produce.

**Evidence.** The blueprint's own Phase 6 table. The classifier must also decide precedence: a run that is corrected but whose correction is itself wrong belongs to SDC, not CORRECTED.

**Rejected.** Nothing rejected yet.

**Impact.** `docs/architecture.md` section 7.4. The `classify.cpp` component. The evidence package schema, which carries the classification.

**Follow-up.** Blocks Phase 6's headline table and the classifier in Phase 4. The taxonomy is described as a stable public interface, so the change should land before anything downstream depends on it.

### ADR-0006 — Bit position convention

**Date:** 2026-10-29
**Status:** Open

**Proposed.** `BitPos` is a `uint8_t` covering 0 to 63 for double and 0 to 31 for float, but the blueprint never says which end of the pattern bit 0 is.

**Decision.** Not yet made. The leaning is bit 0 as the LSB of the uint64 IEEE-754 pattern, which puts the sign bit at 63 and the exponent at bits 62 down to 52.

**Why.** The convention fixes the meaning of every `bit=` argument in every campaign and every evidence package. Changing it later would silently invalidate every previously collected result, so it has to be fixed and documented before the first injection campaign.

**Evidence.** No measurement needed. The consequence of signing bit 0 as the MSB instead is that flipping bit 0 would flip the sign, which is a much more destructive and much less interesting default than flipping a mantissa bit.

**Rejected.** Nothing rejected yet.

**Impact.** `docs/architecture.md` section 6. The runtime's bit-flip routine. The `bit=` argument in every driver.

**Follow-up.** Blocks Phase 2.
