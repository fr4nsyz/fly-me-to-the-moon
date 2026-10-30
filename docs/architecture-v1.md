# SDC Investigator — Implementation Blueprint

> **Revision 1, frozen. Do not edit.** This is the original blueprint, preserved unchanged except for this banner. The living spec is [`architecture.md`](architecture.md). Every change since revision 1 is recorded in [`audit-log.md`](audit-log.md).
>
> Source: `../sdc-investigator/docs/architecture.md` at commit `5a2f2c4`, sha256 `29124267b4e006a51df31e481d5ff56349ca343a3cbef305b36a5aa1e2592bec`.

**A learning-oriented platform for studying Silent Data Corruption in numerical C++ programs**

This document is the single source of truth for building the system. It is written so you can implement and learn at the same time. Every major design decision is stated explicitly, together with the technology, the reason for the choice, and the concrete steps required.

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
| Trace format             | JSONL (one JSON object per line)                       | Human-readable, streamable, crash-safe with a simple raw-append path                                              |
| Evidence package         | Single JSON file                                       | Self-contained, easy to archive and diff                                                                          |
| Experiment orchestration | Python 3.11+ (or Rust if you prefer systems languages) | Rapid iteration for sweeps, reporting, and plotting. Start with Python; rewrite hot parts in Rust later if needed |
| Workloads                | Plain C++                                              | No special libraries required                                                                                     |

**Explicit non-choices**

- No database
- No micro-services
- No network dependency
- No source-level annotations

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
│              →  trace.jsonl                 │
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

---

## 5. Recommended Repository Layout

```
sdc-investigator/
├── CMakeLists.txt
├── README.md
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
│   │   ├── jsonl_writer.cpp
│   │   ├── jsonl_reader.cpp
│   │   └── CMakeLists.txt
│   └── analyzer/                 # Stand-alone CLI
│       ├── main.cpp
│       ├── align.cpp
│       ├── classify.cpp
│       ├── audit.cpp
│       └── CMakeLists.txt
├── tools/
│   ├── instrument.sh             # Convenience wrapper around clang + opt
│   └── study/                    # Experiment drivers (Python)
│       ├── sweep.py
│       ├── defenses.py
│       ├── overhead.py
│       └── report.py
├── workloads/
│   ├── reductions/
│   ├── solvers/
│   └── flight_computer/          # Hardened example with lockstep / vote / etc.
├── docs/
│   └── blueprint.md              # This document
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

- Walk LLVM IR looking for floating-point arithmetic (`fadd`, `fsub`, `fmul`, `fdiv`, `frem`, and optionally `fma`).
- Replace each with a call to a runtime hook that receives the original operands and returns the (possibly corrupted) result.
- Assign a stable `OpId` based on source location (file + line + column + function + opcode). The mapping is written to `ops.json`.

**Key implementation notes**

- Use `llvm::DebugLoc` to recover source location.
- Generate a deterministic hash or sequential ID; the same source + same compiler flags must always produce the same IDs.
- The pass should be runnable with:
  ```bash
  opt -load-pass-plugin=libSDCInstrument.so -passes=sdc-instrument -o instrumented.bc input.bc
  ```

**Learning focus**: LLVM PassManager, IRBuilder, DebugInfo, ModulePass / FunctionPass.

### 7.2 Runtime Witness (`src/runtime`)

**Responsibility**

- Maintain a global counter per `OpId`.
- On the configured `(op_id, occurrence)` pair, flip the requested bit in the result before returning it.
- Record every arithmetic event (or a configurable subset) into the JSONL trace.
- At program exit (or via `atexit` / signal handlers) write an `ExitRecord`.

**Critical requirements**

- The injection must be **exactly once** and deterministic.
- Recording must be crash-safe: use a simple append-only raw write path in addition to the normal JSONL writer.
- Overhead should be measurable (the experiment driver will quantify it).

**Learning focus**: low-overhead instrumentation, signal-safe I/O, IEEE-754 bit manipulation (`std::bit_cast` or unions).

### 7.3 Trace Core (`src/core`)

**Responsibility**

- JSONL writer that can fall back to a raw binary dump if the process is dying.
- Strict reader that validates schema and produces a sequence of `Event` + final `ExitRecord`.

**Format sketch for `trace.jsonl`**

```json
{"type":"header","version":1,"workload":"...","build_id":"..."}
{"type":"event","op_id":42,"occ":7,"ops":[0x3ff0000000000000,0x4000000000000000],"res":0x4008000000000000}
...
{"type":"exit","code":0,"detected":false,"result":0x4010000000000000}
```

### 7.4 Analyzer (`src/analyzer`)

**Responsibility** (pure, deterministic function)

1. Read clean trace + injected trace + `ops.json`.
2. Align the two event streams (they should be identical up to the injection point).
3. Locate the first divergence.
4. Audit arithmetic consistency (optional but powerful: re-execute the op from recorded operands and check the recorded result).
5. Classify the outcome according to the taxonomy below.
6. Emit a self-contained `evidence.json`.

**Outcome taxonomy (fixed precedence)**

| Class        | Meaning                                                       |
| ------------ | ------------------------------------------------------------- |
| CRASH        | Process died; a readable trace was still produced             |
| DETECTED     | Workload’s own check raised a flag                            |
| SDC          | Final result outside acceptance criterion and nothing noticed |
| MASKED       | Final result stayed inside the acceptance criterion           |
| INCONCLUSIVE | Evidence is insufficient for a verdict                        |

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

---

## 8. Data Artifacts (contracts)

| Artifact        | Producer        | Key contents                                                                                                                |
| --------------- | --------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `ops.json`      | Instrumentation | `op_id → {file, line, column, function, kind}`                                                                              |
| `trace.jsonl`   | Runtime         | header + events + exit record                                                                                               |
| `evidence.json` | Analyzer        | classification, reasons, injected event, first divergence, window of surrounding events, both exit records, source metadata |

The evidence package is the **only** hand-off artifact between the analysis stage and any later consumer (reporting, visualization, arbiter research, etc.). Keep it bounded and self-contained.

---

## 9. Phased Implementation Plan

### Phase 0 — Skeleton (1–2 days)

- Create the directory layout and top-level CMake.
- Write the shared headers in `include/sdc`.
- Build an empty analyzer CLI that just prints “hello”.

### Phase 1 — Trace I/O (2–3 days)

- Implement JSONL writer + reader in `src/core`.
- Unit-test round-trip of synthetic events.
- Add a tiny hand-written “runtime” that emits a known trace; verify the reader.

### Phase 2 — Minimal Runtime + Manual Injection (3–4 days)

- Implement the witness with a hard-coded injection point.
- Link it into a simple workload (array sum).
- Produce clean vs injected traces and inspect them by hand.

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

### Phase 6 — Hardened Workload & Defense Study (1–2 weeks)

- Implement the six-core flight-computer simulation.
- Add the different defense modes (lockstep, vote, invariant, checksum, all).
- Reproduce (or improve upon) the classic table:
  ```
  none      → many SDCs
  lockstep  → all detected
  vote      → all corrected
  …
  ```

### Phase 7 — Polish & Measurement

- Overhead numbers (runtime, binary size, trace size).
- Crash-safety of the recorder.
- Documentation and example notebooks / reports.

---

## 10. Key Learning Resources (in recommended order)

1. LLVM “Writing an LLVM Pass” + “How to use the new PassManager”
2. IEEE-754 bit patterns and `std::bit_cast` (C++20)
3. JSONL design (line-delimited JSON) and why it is preferable to a single giant JSON array for long traces
4. Deterministic builds and content-addressable operation IDs
5. Basic numerical analysis: condition numbers, error magnification, masking

---

## 11. Success Criteria for the First Complete Run

You should be able to execute something equivalent to:

```bash
# Build instrumented binary
./tools/instrument.sh workloads/reductions/sum.cpp -o sum_instr

# Run clean
SDC_TRACE=clean.jsonl ./sum_instr input.bin

# Run with injection
SDC_INJECT=op=17,occ=3,bit=42 SDC_TRACE=injected.jsonl ./sum_instr input.bin

# Analyze
./build/analyzer --clean clean.jsonl --injected injected.jsonl --ops ops.json -o evidence.json

# Inspect
jq .classification evidence.json
```

and obtain a reproducible, cited verdict (MASKED / SDC / DETECTED / …).

---

## 12. Future Extensions (do not implement yet)

- Selective recording (only record a window around the injection)
- Multi-bit or multi-site injection campaigns
- Integration with an “arbiter” that uses the evidence packages to decide which regions need hardening
- Visualization of value propagation (data-flow graph extracted from the two traces)
- Support for integer arithmetic or mixed-precision codes

---

This blueprint is intentionally concrete so you can start coding immediately while still understanding the “why” behind every choice. Treat the evidence package format and the outcome taxonomy as the stable public interface; everything else can evolve as you learn.

When you finish Phase 1 or Phase 2, the next natural question is usually “how do I make the LLVM pass robust?” — that is the right moment to dig deeper into the LLVM documentation and the existing open-source instrumentation passes (e.g., parts of AddressSanitizer or custom floating-point research tools).

Good luck — this is a high-leverage systems + numerical project. Build it carefully and the insights will be real.
