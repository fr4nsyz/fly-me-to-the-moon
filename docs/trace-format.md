# SDCTRACE binary trace format

Normative specification for the trace artifact produced by the runtime and read by the analyzer.

Status: accepted under [ADR-0001](audit-log.md#adr-0001--trace-format-moves-from-jsonl-to-binary-sdctrace). Byte layout is normative for version 1. An earlier revision promised reader and writer behavior the implementation does not have; [ADR-0026](audit-log.md#adr-0026--trace-format-v1-the-spec-describes-the-implemented-reader) narrowed this document to the implementation. The code and this document now describe the same artifact.

## 1. Purpose and constraints

The trace is the record of one execution of an instrumented workload. It carries every arithmetic event the workload performed, the bits of each operand and result, and a closing exit record.

Four properties drive the design.

- **Bit-exact.** Values are stored as raw IEEE-754 bit patterns. Nothing is ever converted to decimal text, and no value round-trips through a floating-point parser.
- **Indexable.** Event *i* must be findable without scanning. The analyzer aligns two event streams against each other and then extracts a window around the divergence, so random access is the common case, not the exception.
- **Crash-tolerant.** The workload can die from the corruption it was given. Whatever was recorded before the death must still be readable.
- **Deterministic.** Two clean runs of the same binary on the same input produce byte-identical files. Nothing in the file may depend on wall-clock time, addresses, thread scheduling, or hash iteration order.

## 2. File layout

```
File   := Header Record*
Header := Magic Version Flags MetaLen RecordSize Reserved Metadata
Record := fixed 40 bytes, repeated until end of file
```

### 2.1 Header

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 8 | `magic` | ASCII `SDCTRACE`, no terminating NUL |
| 8 | 4 | `version` | `u32` little-endian, currently `1` |
| 12 | 4 | `flags` | `u32` little-endian, see below |
| 16 | 4 | `meta_len` | `u32` little-endian, byte length of `metadata` |
| 20 | 4 | `record_size` | `u32` little-endian, always `40` in version 1 |
| 24 | 8 | `reserved` | `u64` little-endian. The writer sets it to zero in version 1; the reader does not check it. |
| 32 | `meta_len` | `metadata` | UTF-8 JSON object |

The header prefix is 32 bytes. Records begin at offset `32 + meta_len`.

`flags` bit assignments:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `LITTLE_ENDIAN` | Set when payload integers are little-endian. The writer sets it in version 1; the reader does not require it. |
| 1..31 | | Reserved, must be zero. |

### 2.2 Metadata

`metadata` is a JSON object. Version 1 requires these members:

| Member | Type | Required | Meaning |
| --- | --- | --- | --- |
| `workload` | string | yes | Workload name, for example `dot_product_auto` |
| `build_id` | string | yes | SHA-256 prefix of the instrumented bitcode, 16 hex characters; it does not cover the runtime or the plain TUs |
| `compiler_flags` | string | yes | The exact flag string the workload was compiled with |
| `schema` | integer | yes | Trace schema version, independent of the file format version |
| `inject` | object | yes | The injection configuration, or `{"enabled":false}` when disabled |

A single site carries `op_id`, `occurrence`, `bit`, and `enabled`. Random mode adds `mode:"random"` and `seed`. Persistent mode adds `persistent:true` (ADR-0023). When more than one manual site is configured, `inject` also carries a `sites` array of `{op_id, occurrence, bit, persistent}`; `schema` stays `1`.

The core reader treats the metadata as opaque bytes. The analyzer parses the injected trace's metadata to copy the injection configuration into the evidence package. `build_id` and `compiler_flags` are recorded for the sweep report; the analyzer does not currently compare them between the clean and injected traces.

The metadata is JSON on purpose. It is variable in shape, read exactly once per file, and never on a hot path. The records carry the data that needs to be fast and compact.

### 2.3 Record

All records are exactly 40 bytes, including the exit record. A uniform size keeps the index arithmetic trivial.

| Offset | Size | Field | Type |
| --- | --- | --- | --- |
| 0 | 4 | `type` | `u32` |
| 4 | 4 | `op_id` | `u32` |
| 8 | 8 | `occurrence` | `u64` |
| 16 | 8 | `operand0` | `u64` |
| 24 | 8 | `operand1` | `u64` |
| 32 | 8 | `result` | `u64` |

Every integer is little-endian. The writer writes the host's byte order and sets `LITTLE_ENDIAN`; version 1 supports little-endian hosts only, and the reader does not byte-swap. The field order is chosen so that no field straddles an 8-byte boundary, which means a record can be read with two aligned 16-byte loads on architectures that have them.

`type` selects the interpretation:

| Value | Name | Interpretation |
| --- | --- | --- |
| 0 | `EVENT` | `op_id` is the operation, `occurrence` is its visit count, `operand0` and `operand1` are operand bit patterns, `result` is the delivered result bits. For a one-operand operation such as `sqrt` (ADR-0019), `operand1` is zero |
| 1 | `EXIT` | The final record. `operand0` holds the exit code, `operand1` holds the outcome flags, `result` holds the final result bits, and `op_id` and `occurrence` are zero |

The writer sets `op_id` and `occurrence` to zero in an `EXIT` record. `operand0` holds the exit code, but the runtime writes `0`; a nonzero value is reserved for a future signal handler or a workload that writes its own exit record. In version 1, CRASH is the absence of an `EXIT` record, not a nonzero code.

The `operand1` word in an `EXIT` record is a flag set, not a boolean:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `DETECTED` | The workload's own check raised a flag |
| 1 | `CORRECTED` | A redundancy mechanism repaired the fault |
| 2..63 | | Reserved, must be zero |

Version 1 originally defined `operand1` as a single detected boolean. It was widened to a flag set under ADR-0011 so that `CORRECTED` can be recorded distinctly from `DETECTED`, which the outcome taxonomy requires (ADR-0005). Bit 0 keeps its original meaning, so a reader written against the earlier definition still reads the detection signal correctly.

The writer leaves bits 2..63 of `operand1` zero; the reader ignores them.

The core reader does not consult `ops.json`; an `op_id` it has never seen is recorded as an event. The analyzer looks the id up and omits `source` and `audit` when the id is absent. An unknown id is not an error in either component.

### 2.4 Value encoding

- `operand0`, `operand1`, and `result` hold the raw bit pattern of an IEEE-754 `double` in a `uint64`.
- Version 1 instruments and records `double` only. The full 64 bits are used.
- The `float` encoding (low 32 bits, upper 32 zero) is reserved for a future version; no current component emits it.
- `ops.json` records the operation `kind` (`add`, `sub`, `mul`, `div`, `sqrt`) and the source location. It does not carry a value type, because the trace is double-only.

## 3. Write path

The writer opens the file with `O_WRONLY | O_CREAT | O_TRUNC` (mode 0644). There is one writer per file, so `O_APPEND` is not used.

Each record is serialized into a stack buffer and written with a single `write(2)` call. The return value is not checked. A short write leaves a partial record, which the reader discards as a torn tail.

Records are written in execution order. The runtime does not buffer the whole trace in memory, because a workload that dies must leave a readable prefix behind.

Version 1 does not install a signal handler. If one is added, it must not use this path: it writes a minimal fixed-size exit record with an async-signal-safe call, and the reader must tolerate whatever the interrupted write left behind.

## 4. Read path

The reader validates in this order, and stops at the first failure:

1. `magic` matches `SDCTRACE`.
2. `version` is `1`.
3. `record_size` is `40`.
4. `flags` has no bits above bit 0 set. The reader does not require bit 0 to be set.
5. `meta_len` bytes are read and stored as opaque bytes. The reader does not parse them as JSON and does not impose a separate cap on `meta_len`.

Then, for each record:

6. `type` is `EVENT` or `EXIT`. Any other value is an error.
7. For an `EVENT`, `occurrence` is greater than the previous `occurrence` seen for the same `op_id`. This is the integrity check that catches a garbage record of the right length.
8. For an `EXIT`, the reader takes the exit code and flags and stops. It does not check `op_id`, `occurrence`, or bits 2..63 of `operand1`.

The reader does not consult `ops.json`; that lookup belongs to the analyzer (see 2.3). A record stream ends at end of file or at an `EXIT` record. Records after the `EXIT` record are ignored.

### 4.1 Torn final record

If the process died mid-write, the file ends with a partial record. The reader sees fewer bytes than `record_size` and stops.

There are two cases to distinguish, and the analyzer needs both:

| Case | What the reader sees | Classification |
| --- | --- | --- |
| Process died after writing a complete record stream but before the `EXIT` record | All records intact, no `EXIT` record | `CRASH` |
| Process died mid-write | Complete records followed by a short tail | `CRASH`, with the tail discarded |

In both cases the absence of an `EXIT` record is the signal. The reader never guesses. The runtime writes exit code `0`; a nonzero exit code is not produced in version 1.

## 5. Determinism requirements

For two clean runs to produce byte-identical files:

- The metadata must not contain timestamps, process ids, absolute paths, or hostnames.
- `build_id` is the first 16 hex characters of the SHA-256 of the instrumented bitcode (`tools/instrument.sh`). It does not cover the runtime or the plain translation units.
- `compiler_flags` must be the canonical flag string, identical across both runs.
- Record order must be a function of execution order alone.
- The occurrence counter must be per `op_id` and must not depend on thread scheduling.

## 6. Related documents

- [`architecture.md`](architecture.md), the living spec, section 7.3.
- [`audit-log.md`](audit-log.md), ADR-0001 and ADR-0026, the decisions and their rationale.
