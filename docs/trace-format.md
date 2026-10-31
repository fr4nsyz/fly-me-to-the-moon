# SDCTRACE binary trace format

Normative specification for the trace artifact produced by the runtime and read by the analyzer.

Status: accepted under [ADR-0001](audit-log.md#adr-0001--trace-format-moves-from-jsonl-to-binary-sdctrace). This document is the authority on byte layout. If the code and this document disagree, the code is wrong.

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
| 24 | 8 | `reserved` | `u64` little-endian, must be zero in version 1 |
| 32 | `meta_len` | `metadata` | UTF-8 JSON object |

The header prefix is 32 bytes. Records begin at offset `32 + meta_len`.

`flags` bit assignments:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `LITTLE_ENDIAN` | Set when payload integers are little-endian. Must be set in version 1. |
| 1..31 | | Reserved, must be zero. |

### 2.2 Metadata

`metadata` is a JSON object. Version 1 requires these members:

| Member | Type | Required | Meaning |
| --- | --- | --- | --- |
| `workload` | string | yes | Workload name, for example `dot_product_auto` |
| `build_id` | string | yes | Identifier that changes when the binary changes |
| `compiler_flags` | string | yes | The exact flag string the workload was compiled with |
| `schema` | integer | yes | Trace schema version, independent of the file format version |
| `inject` | object | yes | The injection configuration, or an empty object when disabled |

The `inject` object carries `op_id`, `occurrence`, `bit`, and `enabled`. Its presence lets the analyzer confirm that the clean and injected traces really are a matched pair.

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

Every integer is little-endian. The field order is chosen so that no field straddles an 8-byte boundary, which means a record can be read with two aligned 16-byte loads on architectures that have them.

`type` selects the interpretation:

| Value | Name | Interpretation |
| --- | --- | --- |
| 0 | `EVENT` | `op_id` is the operation, `occurrence` is its visit count, `operand0` and `operand1` are operand bit patterns, `result` is the delivered result bits |
| 1 | `EXIT` | The final record. `operand0` holds the exit code, `operand1` holds the outcome flags, `result` holds the final result bits, and `op_id` and `occurrence` are zero |

The `operand1` word in an `EXIT` record is a flag set, not a boolean:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | `DETECTED` | The workload's own check raised a flag |
| 1 | `CORRECTED` | A redundancy mechanism repaired the fault |
| 2..63 | | Reserved, must be zero |

Version 1 originally defined `operand1` as a single detected boolean. It was widened to a flag set under ADR-0011 so that `CORRECTED` can be recorded distinctly from `DETECTED`, which the outcome taxonomy requires (ADR-0005). Bit 0 keeps its original meaning, so a reader written against the earlier definition still reads the detection signal correctly.

`op_id` values that the reader does not recognize are an error, not something to skip silently.

### 2.4 Value encoding

- `operand0`, `operand1`, and `result` hold the raw bit pattern of an IEEE-754 value in a `uint64`.
- For `double` operations the full 64 bits are used.
- For `float` operations the 32-bit pattern occupies the low half of the `uint64` and the upper 32 bits are zero.
- For an `f64` operation the low 32 bits are zero.
- The distinction between `float` and `double` comes from `ops.json`, which records the type per `op_id`. The trace does not repeat it per record.

## 3. Write path

The writer keeps the file open with `O_WRONLY | O_CREAT | O_TRUNC | O_APPEND`.

Each record is serialized into a stack buffer and written with a single `write(2)` call. One syscall per record keeps the append atomic with respect to other writers, and keeps a partial write to one record rather than a partial record.

Records are written in execution order. The runtime does not buffer the whole trace in memory, because a workload that dies must leave a readable prefix behind.

A signal handler that writes to the same file must not use this path. It writes a minimal fixed-size exit record with an async-signal-safe call, and the reader must tolerate whatever the interrupted write left behind.

## 4. Read path

The reader validates in this order, and stops at the first failure:

1. `magic` matches `SDCTRACE`.
2. `version` is a version the reader understands.
3. `record_size` equals the reader's expected record size.
4. `flags` has only defined bits set.
5. `meta_len` is sane and `metadata` parses as a JSON object.

Then, for each record:

6. `type` is `EVENT` or `EXIT`.
7. For an `EVENT`, `op_id` is present in `ops.json`.
8. For an `EVENT`, `occurrence` is greater than the previous `occurrence` seen for the same `op_id`. This is the integrity check that catches a garbage record of the right length.

A record stream ends when the reader reaches end of file, or when it reads an `EXIT` record. Any records after the `EXIT` record are an error.

### 4.1 Torn final record

If the process died mid-write, the file ends with a partial record. The reader sees fewer bytes than `record_size` and stops.

There are two cases to distinguish, and the analyzer needs both:

| Case | What the reader sees | Classification |
| --- | --- | --- |
| Process died after writing a complete record stream but before the `EXIT` record | All records intact, no `EXIT` record | `CRASH` |
| Process died mid-write | Complete records followed by a short tail | `CRASH`, with the tail discarded |

In both cases the absence of an `EXIT` record is the signal. The reader never guesses.

## 5. Determinism requirements

For two clean runs to produce byte-identical files:

- The metadata must not contain timestamps, process ids, absolute paths, or hostnames.
- `build_id` must be a function of the binary contents, not of the build time.
- `compiler_flags` must be the canonical flag string, identical across both runs.
- Record order must be a function of execution order alone.
- The occurrence counter must be per `op_id` and must not depend on thread scheduling.

## 6. Related documents

- [`architecture.md`](architecture.md), the living spec, section 7.3.
- [`audit-log.md`](audit-log.md), ADR-0001, the decision and its rationale.
