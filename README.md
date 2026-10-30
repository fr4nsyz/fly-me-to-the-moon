# SDC Investigator

An offline, deterministic experiment platform for studying Silent Data Corruption in numerical C++ programs.

It compiles a numerical workload with an LLVM IR pass, injects a single deterministic bit flip into one arithmetic operation, records both a clean run and the injected run as bit-exact traces, and produces a self-contained evidence package that cites the events behind its verdict.

## Status

Design phase. No source code yet. The specification is written and the open decisions are logged.

| Document | What it is |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | The living spec. Edited as decisions are made. |
| [`docs/architecture-v1.md`](docs/architecture-v1.md) | The frozen original blueprint. Never edited. |
| [`docs/trace-format.md`](docs/trace-format.md) | Normative specification of the binary `SDCTRACE` trace format. |
| [`docs/audit-log.md`](docs/audit-log.md) | Append-only record of every architectural decision, with the evidence behind it. |
| [`STATE.md`](STATE.md) | Current state, measurements, open decisions, and the next move. |

## How changes happen

1. Edit `docs/architecture.md`.
2. Append an entry to `docs/audit-log.md` naming what changed, why, the evidence, and the sections touched.
3. Record the decision ID in the revision history and open decisions tables in `docs/architecture.md`.

History in the audit log is never rewritten. A decision that turns out to be wrong gets a new entry that supersedes it.

## Design principles

- The deterministic layer is the source of truth. The analyzer decides the outcome.
- Every quantitative claim is auditable against the raw trace.
- Fully offline. No network, no credentials, no external services.
- Workload source stays plain. Zero annotations or macros in user code.
- Bit-exact records. Raw IEEE-754 bit patterns, never decimal text.
- Reproducible everything. Same sources and flags give identical operation numbering, and two clean runs give byte-identical traces.
