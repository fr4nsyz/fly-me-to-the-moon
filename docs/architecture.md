# SDC Investigator — Implementation Blueprint

**A learning-oriented platform for studying Silent Data Corruption in numerical C++ programs**

This document is the single source of truth for building the system. It is written so you can implement and learn at the same time. Every major design decision is stated explicitly, together with the technology, the reason for the choice, and the concrete steps required.

> **Revision 2, living.** This file is edited as decisions are made. The original blueprint is frozen at [`architecture-v1.md`](architecture-v1.md) and is never edited. Every architectural change is recorded in [`audit-log.md`](audit-log.md). Points marked **OPEN** are not settled yet and carry a decision ID. See [Revision history](#14-revision-history) for the list.

---

## 1. Project Goal

SDC Investigator is an offline, deterministic experiment platform that answers:

1. Where did a single bit-flip first become observable?
2. How did the corrupted value propagate?
3. Did the final result change, get absorbed, or stay numerically insignificant?
4. Which explanations and follow-up experiments are consistent with the evidence?
5. Are there regions of the program where bit-flips are not catastrophic (candidates for de-prioritization in an arbiter / selective hardening)?

The platform never relies on real radiation. Instead it injects a **single deterministic bit flip** into one arithmetic operation at a chosen occurrence count, records both a clean run and the injected run as bit-exact traces, and produces a self-contained evidence package.

---

## 2. Design Principles (non-negotiable)

| Principle                              | Concrete consequence                                                                         |
| -------------------------------------- | -------------------------------------------------------------------------------------------- |
| Deterministic layer is source of truth | Analyzer decides the outcome; evidence package contains the reasons + event citations        |
| Every claim is auditable               | Quantitative statements can be re-checked against the raw trace                              |
| Fully offline                          | No network, no credentials, no external services                                             |
| Workload source stays plain            | Zero annotations or macros in user code; instrumentation is added by the build               |
| Bit-exact records                      | Traces store raw IEEE-754 bit patterns, never decimal text                                   |
| Reproducible everything                | Same sources + flags → identical operation numbering; two clean runs → byte-identical traces |

---

## 3. Technology Stack (locked decisions)

| Layer                    | Technology                                             | Why this choice                                                                                                   |
| ------------------------ | ------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------- |
| Instrumentation          | LLVM IR pass (`opt`)                                   | Most precise and stable place to rewrite floating-point ops; works on any compiler that emits LLVM IR             |
| Runtime + Analyzer       | C++20 (or later)                                       | Zero-overhead hooks, bit-exact control, easy embedding into the instrumented binary                               |
| Build system             | CMake + Ninja                                          | Standard, fast, excellent LLVM integration                                                                        |
| Trace format             | SDCTRACE binary, fixed 40-byte records                | Fixed-size records are randomly indexable, bit patterns are stored natively rather than as hex text, and a torn final record is detected rather than guessed at |
| Evidence package         | Single JSON file                                       | Self-contained, easy to archive and diff                                                                          |
| Experiment orchestration | Python 3.11+ (or Rust if you prefer systems languages) | Rapid iteration for sweeps, reporting, and plotting. Start with Python; rewrite hot parts in Rust later if needed |
| Workloads                | Plain C++                                              | No special libraries required                                                                                     |
| Visualization            | Three.js, vendored offline                             | The mission HUD is a browser artifact; vendoring preserves the offline guarantee                                  |

**Explicit non-choices**

- No database
- No micro-services
- No network dependency
- No source-level annotations

**Note on the trace format.** Revision 1 used JSONL. The binary format was adopted in ADR-0001 for three reasons that are easy to state and easy to measure. Fixed-size records put event *i* at a computable offset, which is what makes the aligner simple. Bit patterns are the payload, and JSON has no way to carry them except as 20-character hex strings. A sweep multiplies traces by the number of injection sites, and JSONL costs about 113 bytes per event against 40 for binary. The evidence package stays JSON, because that is the artifact humans and downstream tools read.

---

## 4. High-Level Architecture

```
┌─────────────────── Build ───────────────────┐
│  plain C++ source  →  LLVM IR pass  →  instrumented binary  │
└─────────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────── Run ────────────────────┐
│  runtime witness (count / inject / record)  │
│              →  trace.sdct                 │
└─────────────────────────────────────────────┘
                 │
                 ▼
┌───────────────── Analyze ──────────────────┐
│  deterministic analyzer                     │
│              →  evidence.json               │
└─────────────────────────────────────────────┘

Experiment driver (Python/Rust) orchestrates:
  - building variants
  - choosing (op_id, occurrence, bit_position)
  - running clean + injected pairs
  - collecting evidence packages
  - producing reports / plots
```

Two traces always feed the analyzer: the clean baseline and the single-fault run.

The 3D viewer is a read-only consumer: it renders `trajectory.json` and annotates it with `evidence.json`. It never feeds back into the analyzer.

---

## 5. Recommended Repository Layout

```
sdc-investigator/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── include/sdc/                  # Shared contracts (header-only)
│   ├── event.hpp
│   ├── exit_record.hpp
│   ├── injection.hpp
│   └── types.hpp
├── src/
│   ├── instrumentation/          # LLVM IR pass
│   │   ├── SDCInstrument.cpp
│   │   └── CMakeLists.txt
│   ├── runtime/                  # Linked into instrumented binary
│   │   ├── witness.cpp
│   │   ├── recorder.cpp
│   │   └── CMakeLists.txt
│   ├── core/                     # Trace I/O
│   │   ├── trace_writer.cpp
│   │   ├── trace_reader.cpp
│   │   └── CMakeLists.txt
│   └── analyzer/                 # Stand-alone CLI
│       ├── main.cpp
│       ├── align.cpp
│       ├── classify.cpp
│       ├── audit.cpp
│       └── CMakeLists.txt
├── tools/
│   ├── instrument.sh             # Convenience wrapper around clang + opt
│   ├── trace_dump.py             # Pretty-print a binary trace
│   └── study/                    # Experiment drivers (Python)
│       ├── sweep.py
│       ├── defenses.py
│       ├── overhead.py
│       └── report.py
├── workloads/
│   ├── reductions/
│   ├── solvers/
│   └── moon/                     # Plant + instrumented FSW, lockstep / vote / etc.
│       ├── plant/                # Hand-rolled dynamics, not instrumented
│       └── fsw/                  # Instrumented flight software
├── docs/
│   ├── architecture.md           # This document (living spec)
│   ├── architecture-v1.md        # Frozen original blueprint
│   ├── trace-format.md           # Normative SDCTRACE binary spec
│   └── audit-log.md              # Append-only architectural change log
└── tests/
    ├── unit/
    └── integration/
```

---

## 6. Shared Contracts (`include/sdc`)

Keep these header-only and minimal. They are the only types that cross component boundaries.

```cpp
// types.hpp
using OpId = uint32_t;
using Occurrence = uint64_t;
using BitPos = uint8_t;          // 0–63 for double, 0–31 for float

// ADR-0006 (accepted): bit 0 is the LSB of the uint64 IEEE-754 pattern.
// bit 63 is the sign bit, bits 62..52 are the exponent. This is fixed.

// event.hpp
struct Event {
    OpId        op_id;
    Occurrence  occurrence;
    uint64_t    operand_bits[2]; // left, right (or unary)
    uint64_t    result_bits;
    // Optional: timestamp or call-stack hash for richer analysis later
};

// injection.hpp
struct InjectionConfig {
    OpId        target_op;
    Occurrence  target_occurrence;
    BitPos      bit;
    bool        enabled = false;
};

// exit_record.hpp
struct ExitRecord {
    int         exit_code;
    bool        detected_by_workload;   // workload’s own check
    uint64_t    final_result_bits;      // or a hash of the output
    // Any additional workload-specific summary
};
```

---

## 7. Component Specifications

### 7.1 Instrumentation Pass (`src/instrumentation`)

**Responsibility**

- Walk LLVM IR looking for floating-point arithmetic (`fadd`, `fsub`, `fmul`, `fdiv`, `frem`) and the matching intrinsics (`llvm.fma`, `llvm.fmuladd`, `llvm.fabs`, `llvm.sqrt`), plus `fneg` for unary sign flips.
- Replace each with a call to a runtime hook that receives the original operands and returns the (possibly corrupted) result.
- Assign a stable `OpId` to each instrumented instruction. The mapping is written to `ops.json`.

**Key implementation notes**

- Use `llvm::DebugLoc` to recover source location.
- **ADR-0003 (accepted): op id derivation.** The op id is assigned in deterministic module order of instrumented instructions encountered by the pass (in traversal order). Collisions from inlined source locations are avoided by not relying on (file,line,column,fn,opcode) alone. The `ops.json` entry stores debug location for human readability, but the identity key is module order. The mapping is written at pass time and is stable for identical sources, flags, and LLVM version.
- Only translation units in the instrumented set are rewritten. For the moon mission, **only FSW TUs** are instrumented; plant TUs are compiled without the pass.
- The same source plus the same compiler flags must always produce the same ids.
- The pass should be runnable with:
  ```bash
  opt -load-pass-plugin=libSDCInstrument.so -passes=sdc-instrument -o instrumented.bc input.bc
  ```

**Compiler flags**

The workload is compiled with one canonical flag set, and the pass refuses to instrument an instruction carrying a fast-math flag.

```
-O2 -gline-tables-only
-ffp-contract=off
-fno-vectorize -fno-slp-vectorize
```

Each flag earns its place. Without `-ffp-contract=off`, `a*b+c` becomes a single `llvm.fmuladd` and the two operations are fused into one rounding. Without `-fno-vectorize`, a reduction loop becomes calls to `llvm.vector.reduce.fadd` and the additions happen below the IR, where the pass cannot see them. Without `-gline-tables-only`, column numbers may be absent.

Record the flag string in the trace header, so the analyzer can reject a clean and injected pair that came from different builds.

**Learning focus**: LLVM PassManager, IRBuilder, DebugInfo, ModulePass / FunctionPass.

### 7.2 Runtime Witness (`src/runtime`)

**Responsibility**

- Maintain a global counter per `OpId`.
- On the configured `(op_id, occurrence)` pair, flip the requested bit in the result before returning it.
- Record every arithmetic event (or a configurable subset) into the binary trace.
- At program exit (or via `atexit` / signal handlers) write an `ExitRecord`.

**Critical requirements**

- The injection must be **exactly once** and deterministic.
- Recording must be crash-safe. The writer appends whole records to an `O_APPEND` file descriptor with one `write` per record. A torn final record shows up as a short read, and the reader treats it as the process having died during recording.
- Overhead should be measurable (the experiment driver will quantify it).

**Learning focus**: low-overhead instrumentation, signal-safe I/O, IEEE-754 bit manipulation (`std::bit_cast` or unions).

### 7.3 Trace Core (`src/core`)

**Responsibility**

- Binary writer for the `SDCTRACE` format.
- Strict reader that validates the header, the record type field, and the per-op occurrence monotonicity invariant, and produces a sequence of `Event` plus the final `ExitRecord`.

**Format**

The normative specification lives in [`trace-format.md`](trace-format.md). The short version:

```
File := Header Record*

Header:  "SDCTRACE" | u32 version | u32 flags | u32 meta_len | u32 record_size | u64 reserved
         followed by meta_len bytes of UTF-8 JSON metadata
         (workload name, build id, compiler flags, injection config)

Record (40 bytes, fixed):
  u32 type        0 = event, 1 = exit
  u32 op_id
  u64 occurrence
  u64 operand0
  u64 operand1
  u64 result
```

For an event record, `operand0` and `operand1` are the operand bit patterns and `result` is the delivered result bits. For the exit record the fields are reused: `operand0` holds the exit code, `operand1` holds the outcome flag set (bit 0 `DETECTED`, bit 1 `CORRECTED`, ADR-0011), `result` holds the final result bits.

All integers are little-endian. The file extension is `.sdct`.

### 7.4 Analyzer (`src/analyzer`)

**Responsibility** (pure, deterministic function)

1. Read clean trace + injected trace + `ops.json`.
2. Align the two event streams.
3. Locate the first divergence.
4. Audit arithmetic consistency (optional but powerful: re-execute the op from recorded operands and check the recorded result).
5. Classify the outcome according to the taxonomy below.
6. Emit a self-contained `evidence.json`.

**Alignment**

Revision 1 assumed the two streams are identical up to the injection point. That holds only when the corrupted value does not change control flow. If the corrupted value feeds a comparison, the injected run executes a different sequence of operations, and a prefix comparison is not enough.

**Decided (ADR-0014): the first divergence is the first index where the `(op_id, occurrence)` key or the payload differs.** A single fault always mismatches on payload at the injected event before control flow can change, so a prefix scan reports the injection point. A structural mismatch is a divergence at its own index; there is no resynchronization.

**Outcome taxonomy (fixed precedence)**

| Class        | Meaning                                                       |
| ------------ | ------------------------------------------------------------- |
| CRASH        | Process died; a readable trace was still produced             |
| DETECTED     | Workload’s own check raised a flag                            |
| CORRECTED    | A redundancy mechanism (e.g. TMR vote) corrected the corruption before producing the final result; outcome is within acceptance criterion |
| SDC          | Final result outside acceptance criterion and nothing noticed |
| MASKED       | Final result stayed inside the acceptance criterion with no correction mechanism applied |
| INCONCLUSIVE | Evidence is insufficient for a verdict                        |

**ADR-0005 (accepted): add CORRECTED.** The taxonomy distinguishes correction by mechanism (CORRECTED) from absorption (MASKED). Precedence: if a correction mechanism was applied and the corrected output is valid, classify CORRECTED (not MASKED); if correction was attempted but failed, classify SDC or DETECTED as appropriate.

**Acceptance criterion** is supplied by the experiment (absolute or relative tolerance, or a custom predicate).

**Learning focus**: careful stream alignment, numerical comparison of bit patterns, producing machine-readable yet human-auditable reports.

### 7.5 Experiment Drivers (`tools/study`)

Start in Python for speed of iteration:

- `sweep.py` — iterate over a set of `(op_id, occurrence, bit)` triples, run clean + injected, collect evidence packages.
- `defenses.py` — drive the hardened flight-computer workload under different redundancy modes.
- `overhead.py` — measure runtime and binary-size cost of instrumentation.
- `report.py` — aggregate evidence packages into tables and plots (the classic 14 masked / 50 SDC table lives here).

Later you can re-implement the hot loops in Rust if desired; the evidence packages remain the stable interface.

### 7.6 Workloads

Keep them plain C++. Suggested starter set:

1. Simple reduction (sum / product of an array) — easy to reason about masking.
2. Iterative linear solver (Jacobi or CG) — interesting propagation.
3. Hardened flight-computer simulation — six cores, lockstep twins, voting, invariants, checksums. This is where the defense-mode table is generated.
4. Moon mission (full surface → lunar landing) — plant + FSW; FSW TUs instrumented, plant untouched. Produces `trajectory.json` for the Three.js HUD.

---

## 8. Data Artifacts (contracts)

| Artifact        | Producer        | Key contents                                                                                                                |
| --------------- | --------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `ops.json`      | Instrumentation | `op_id → {file, line, column, function, kind}`                                                                              |
| `trace.sdct`    | Runtime         | binary trace: header + events + exit record                                                                                 |
| `evidence.json` | Analyzer        | classification, reasons, injected event, first divergence, window of surrounding events, both exit records, source metadata |

The evidence package is the **only** hand-off artifact between the analysis stage and any later consumer (reporting, visualization, arbiter research, etc.). Keep it bounded and self-contained.

**Encoding rule.** Every 64-bit value in `evidence.json` is written as a hex string, for example `"0x3ff0000000000000"`. JSON numbers are decimal, and a 64-bit value exceeds the 2^53 exact range of a JavaScript parser, so a numeric encoding silently loses precision in some readers.

---

## 9. Phased Implementation Plan

### Phase 0 — Skeleton (1–2 days)

- Create the directory layout and top-level CMake.
- Write the shared headers in `include/sdc`.
- Build an empty analyzer CLI that just prints “hello”.

### Phase 1 — Trace I/O (2–3 days)

- Implement the binary trace writer + reader in `src/core`.
- Unit-test round-trip of synthetic events.
- Add a tiny hand-written “runtime” that emits a known trace; verify the reader.

### Phase 2 — Minimal Runtime + Manual Injection (3–4 days)

- Implement the witness with a hard-coded injection point.
- Link it into a simple workload (array sum).
- Produce clean vs injected traces and inspect them with `tools/trace_dump.py`.

### Phase 3 — LLVM Instrumentation Pass (1–2 weeks)

- Learn the LLVM pass infrastructure (the official “Writing an LLVM Pass” tutorial is still the best starting point).
- Rewrite `fadd`/`fmul`/… into hook calls.
- Emit `ops.json`.
- Verify that operation IDs are stable across rebuilds.

### Phase 4 — Full Analyzer (1 week)

- Align two traces.
- Detect first divergence.
- Implement the five-class classifier.
- Emit `evidence.json` with citations back to events.

### Phase 5 — Experiment Drivers & First Sweep (1 week)

- Python driver that can launch a clean + injected pair and collect the evidence package.
- Run a systematic sweep over a small workload.
- Produce the first real “masked vs SDC” numbers.

**Delivered.** `tools/study/sweep.py` runs a campaign of random single-bit faults and aggregates the verdicts. `tools/study/report.py` compares several campaigns side by side, which is how the Phase 6 defense table is built. `tools/study/overhead.py` measures the instrumentation cost. `tools/sdctrace.py` is a Python reader for the trace format.

The campaign interface:

```bash
./tools/study/sweep.py --binary build/sum_plain_instr --ops build/ops.json \
    --out results/sweep_sum_plain --trials 200 --seed 1337 \
    --workload sum_plain --build-id <id> --compiler-flags '<flags>' 1024
```

The same seed always produces the same campaign. See ADR-0012 for how the injection site is resolved and why the resolved site is recorded in the trace metadata.

### Phase 6 — Hardened Workload & Defense Study (1–2 weeks)

- Implement the six-core flight-computer simulation (FSW). Plant is separate and not instrumented.
- Add the different defense modes (lockstep, vote, invariant, checksum, all).
- Reproduce (or improve upon) the classic table using the settled taxonomy (CORRECTED for vote/TMR when corrected):
  ```
  none      → many SDCs
  lockstep  → all detected
  vote      → all corrected
  …
  ```

### Phase 7 — Viewer & Polish

- Implement Three.js mission-control HUD (vendored offline). Reads only `trajectory.json` + `evidence.json`.
- Show clean vs injected trajectories, injection point, first divergence, outcome badge (CORRECTED/SDC/MASKED/DETECTED/CRASH).
- Overhead numbers (runtime, binary size, trace size).
- Crash-safety of the recorder.
- Documentation and example notebooks / reports.

---

## 10. Key Learning Resources (in recommended order)

1. LLVM “Writing an LLVM Pass” + “How to use the new PassManager”
2. IEEE-754 bit patterns and `std::bit_cast` (C++20)
3. Binary trace design: framing, fixed-size records, torn-record recovery, and why the payload does not want to be JSON
4. Deterministic builds and content-addressable operation IDs
5. Basic numerical analysis: condition numbers, error magnification, masking

---

## 11. Success Criteria for the First Complete Run

You should be able to execute something equivalent to:

```bash
# Build instrumented binary (compiles to bitcode, runs the pass, links the runtime)
./tools/instrument.sh workloads/reductions/sum_plain.cpp -o build/sum_instr

# Run clean
SDC_WORKLOAD=sum_plain SDC_BUILD_ID=<id> SDC_COMPILER_FLAGS='<flags>' \
  SDC_TRACE=clean.sdct ./build/sum_instr 1024

# Run with injection
SDC_WORKLOAD=sum_plain SDC_BUILD_ID=<id> SDC_COMPILER_FLAGS='<flags>' \
  SDC_INJECT=op=17,occ=3,bit=42 SDC_TRACE=injected.sdct ./build/sum_instr 1024

# Analyze
./build/src/analyzer/sdc-analyze --clean clean.sdct --injected injected.sdct \
  --ops build/ops.json -o evidence.json

# Inspect
jq .classification evidence.json
```

and obtain a reproducible, cited verdict (MASKED / SDC / DETECTED / CORRECTED / CRASH / INCONCLUSIVE).

**Runtime interface.** The workload is plain C++ with no annotations, so everything the runtime needs comes from the environment.

| Variable | Meaning |
| --- | --- |
| `SDC_TRACE` | Path of the trace file to write. Defaults to `trace.sdct`. |
| `SDC_INJECT` | Injection configuration as `op=<id>,occ=<n>,bit=<b>`. The literal `random` selects a site at random. Absent means no injection. |
| `SDC_SEED` | Seed for random injection. The same seed always resolves the same site. |
| `SDC_INJECT_PLAN` | Path to the injection plan for random mode. One `op_id max_occurrence` pair per line. |
| `SDC_INJECT_BIT_RANGE` | Inclusive bit range for random mode, `lo-hi`. Defaults to `0-63`. |
| `SDC_WORKLOAD` | Workload name, recorded in the trace metadata. |
| `SDC_BUILD_ID` | Build identifier, recorded in the trace metadata. The analyzer rejects a clean/injected pair whose build ids differ. |
| `SDC_COMPILER_FLAGS` | The exact flag string the workload was compiled with, recorded in the trace metadata. |

`SDC_TRACE_PATH`, `SDC_INJECT_OP_ID`, `SDC_INJECT_OCC`, and `SDC_INJECT_BIT` are accepted as alternatives to `SDC_TRACE` and `SDC_INJECT`.

**Analyzer interface.** `sdc-analyze` takes `--clean`, `--injected`, `--ops`, `-o`/`--out`, `--abs-tol`, `--rel-tol`, and `--reference-bits`. When `--reference-bits` is absent the clean run's own final result is used as the reference, which makes the acceptance criterion the baseline the platform is built around.

**Reporting API.** A workload that wants to state its own verdict calls `sdc_report_result(value)`, `sdc_report_detected()`, or `sdc_report_corrected()`. None of these are required: the runtime uses the last delivered result as the final result, so a plain workload still produces a meaningful exit record.

---

## 12. Future Extensions (do not implement yet)

- Selective recording (only record a window around the injection)
- Multi-bit or multi-site injection campaigns
- Integration with an “arbiter” that uses the evidence packages to decide which regions need hardening
- Visualization of value propagation (data-flow graph extracted from the two traces)
- Support for integer arithmetic or mixed-precision codes

---

## 13. Open Decisions

These are not settled. Each one blocks a specific piece of work.

| ID | Question | Current lean | Blocks |
| --- | --- | --- | --- |
| (none) | | | |

Settled in this revision:

| ID | Decision |
| --- | --- |
| ADR-0001 | Trace format is binary `SDCTRACE`, not JSONL |
| ADR-0002 | One canonical compiler flag set, and the pass rejects fast-math IR |
| ADR-0003 | Op id assigned in deterministic module order; debug locations for readability only |
| ADR-0004 | Alignment is a prefix scan on key and payload (ADR-0014) |
| ADR-0005 | Taxonomy adds `CORRECTED` class with precedence rules |
| ADR-0006 | Bit 0 = LSB of uint64 IEEE-754 pattern (bit 63 sign, 62..52 exponent) |
| ADR-0007 | Moon workload: full surface to lunar landing, hand-rolled plant |
| ADR-0008 | Defense semantics: replicate before, vote after, protect the voter |
| ADR-0009 | Only FSW translation units are instrumented |
| ADR-0010 | Separate `trajectory.json`, offline Three.js HUD |
| ADR-0011 | Exit record `operand1` is a flag set: bit 0 DETECTED, bit 1 CORRECTED |
| ADR-0012 | Random injection: the runtime resolves the site from a plan, seeded by `SDC_SEED` |
| ADR-0013 | Ratifies ADR-0003, ADR-0005, ADR-0007 to ADR-0010 |
| ADR-0014 | Alignment is a prefix scan on key and payload; no resynchronization |
| ADR-0015 | Unit tests use GoogleTest 1.18 from Homebrew |

---

## 14. Revision history

This document is the living spec. The frozen original is [`architecture-v1.md`](architecture-v1.md). Every change is recorded in full, with reasons and evidence, in [`audit-log.md`](audit-log.md).

| Change ID | Date | Summary | Status |
| --- | --- | --- | --- |
| ADR-0001 | 2026-10-29 | Trace format moves from JSONL to the binary `SDCTRACE` format | Accepted |
| ADR-0002 | 2026-10-29 | Canonical compiler flag set for instrumented builds | Accepted |
| ADR-0003 | 2026-10-29 | Op id derivation under inlining (module order) | Accepted |
| ADR-0004 | 2026-10-29 | Trace alignment when corruption changes control flow | Accepted |
| ADR-0005 | 2026-10-29 | Outcome taxonomy adds CORRECTED class | Accepted |
| ADR-0006 | 2026-10-29 | Bit position convention (LSB 0) | Accepted |
| ADR-0007 | 2026-10-31 | Moon workload: full surface→lunar landing, hand-rolled plant, FSW-only instrumentation | Accepted |
| ADR-0008 | 2026-10-31 | Defense semantics: replicate before critical section, vote after, protect voter; injection before replication = common-mode | Accepted |
| ADR-0009 | 2026-10-31 | Instrumentation scope: FSW translation units only; plant untouched | Accepted |
| ADR-0010 | 2026-10-31 | Trajectory log + Three.js HUD (vendored offline), separate from SDCTRACE | Accepted |
| ADR-0011 | 2026-10-31 | Exit record detection word becomes a flag set (DETECTED, CORRECTED) | Accepted |
| ADR-0012 | 2026-10-31 | Random injection resolves its site in the runtime from a driver-built plan | Accepted |
| ADR-0013 | 2026-10-31 | Ratify ADR-0003, ADR-0005, ADR-0007 to ADR-0010 | Accepted |
| ADR-0014 | 2026-10-31 | Alignment is a prefix scan on key and payload | Accepted |
| ADR-0015 | 2026-10-31 | Unit tests use GoogleTest | Accepted |

---

## 15. Moon Mission Specification (ADR-0007)

**Goal:** Full surface → lunar landing. The workload is a research platform to study SDC effects on a mission.

**Plant (not instrumented):** Hand-rolled C++ 3D dynamics (MIT). Simplified but deterministic; Earth surface launch context, translunar injection, coast, lunar orbit insertion, powered descent, landing. Accepts FSW commands; produces state to log.

**FSW (instrumented):** Our C++ GNC + defense logic. Instrumented TUs only.

**Phases:** Ascent → TLI → Coast → LOI → Powered Descent → Landing.

**Success criterion:** Landing within position/velocity tolerance (defined in `workloads/moon/` specs).

**Trajectory log:** `trajectory.json` written by the plant/driver, consumed only by the Three.js HUD. Separate from `SDCTRACE`.

## 16. Defense Semantics (ADR-0008)

- **Replication:** Replicate the critical computation before the critical section (three replicas for TMR).
- **Voting:** Vote after the computation, before using the result.
- **Voter protection:** The voter must be hardened or protected; do not rely on an unprotected voter.
- **Injection timing:** Injecting before replication corrupts all replicas identically (common-mode). The campaign must respect this.
- **Modes:** `none`, `lockstep`, `vote/TMR`, `invariant`, `checksum`, `all` — all measured equally.

## 17. Instrumentation Scope (ADR-0009)

- **FSW TUs only:** Instrument translation units that implement flight software logic.
- **Plant untouched:** Dynamics/plant code is not instrumented; no hooks injected there.
- **Build integration:** Instrumentation applied via the LLVM pass in the FSW build path only.

## 18. Viewer (ADR-0010)

- **Three.js HUD:** Vendored offline (no CDN). Reads `trajectory.json` + `evidence.json`.
- **Display:** Clean vs injected trajectories, injection point, first divergence, outcome badge.
- **Separation:** Never reads `SDCTRACE` directly; consumes only published artifacts.

This blueprint is intentionally concrete so you can start coding immediately while still understanding the “why” behind every choice. Treat the evidence package format and the outcome taxonomy as the stable public interface; everything else can evolve as you learn.

When you finish Phase 1 or Phase 2, the next natural question is usually “how do I make the LLVM pass robust?” — that is the right moment to dig deeper into the LLVM documentation and the existing open-source instrumentation passes (e.g., parts of AddressSanitizer or custom floating-point research tools).

Good luck — this is a high-leverage systems + numerical project. Build it carefully and the insights will be real.
