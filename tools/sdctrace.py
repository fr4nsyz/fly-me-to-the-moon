#!/usr/bin/env python3
"""Reader for the SDCTRACE binary trace format.

The normative layout is in docs/trace-format.md. This module is the Python
side of that contract: the campaign drivers need to read traces to build an
injection plan and to aggregate results.
"""

import json
import struct

MAGIC = b"SDCTRACE"
VERSION = 1
RECORD_SIZE = 40
HEADER_PREFIX = 32

RECORD_EVENT = 0
RECORD_EXIT = 1

FLAG_DETECTED = 1 << 0
FLAG_CORRECTED = 1 << 1


class TraceError(Exception):
    pass


class Event:
    __slots__ = ("op_id", "occurrence", "operand0", "operand1", "result")

    def __init__(self, op_id, occurrence, operand0, operand1, result):
        self.op_id = op_id
        self.occurrence = occurrence
        self.operand0 = operand0
        self.operand1 = operand1
        self.result = result

    def __repr__(self):
        return (
            f"Event(op_id={self.op_id}, occurrence={self.occurrence}, "
            f"result=0x{self.result:016x})"
        )


class ExitRecord:
    __slots__ = ("exit_code", "detected", "corrected", "final_result")

    def __init__(self, exit_code, detected, corrected, final_result):
        self.exit_code = exit_code
        self.detected = detected
        self.corrected = corrected
        self.final_result = final_result


class Trace:
    def __init__(self):
        self.metadata = {}
        self.events = []
        self.exit = None
        self.torn = False

    def occurrence_counts(self):
        """Map op_id to the number of times it executed."""
        counts = {}
        for event in self.events:
            counts[event.op_id] = max(counts.get(event.op_id, 0), event.occurrence)
        return counts

    def total_events(self):
        return len(self.events)


def read(path):
    """Read a trace file. Raises TraceError on a malformed file."""
    with open(path, "rb") as handle:
        data = handle.read()

    if len(data) < HEADER_PREFIX:
        raise TraceError(f"{path}: short header")

    magic = data[0:8]
    if magic != MAGIC:
        raise TraceError(f"{path}: bad magic {magic!r}")

    version, flags, meta_len, record_size = struct.unpack("<IIII", data[8:24])
    (reserved,) = struct.unpack("<Q", data[24:32])

    if version != VERSION:
        raise TraceError(f"{path}: unsupported version {version}")
    if record_size != RECORD_SIZE:
        raise TraceError(f"{path}: unexpected record size {record_size}")
    if flags & ~1:
        raise TraceError(f"{path}: unknown flag bits set")
    if reserved != 0:
        raise TraceError(f"{path}: reserved field is not zero")

    trace = Trace()

    meta_end = HEADER_PREFIX + meta_len
    if len(data) < meta_end:
        raise TraceError(f"{path}: short metadata")
    meta_text = data[HEADER_PREFIX:meta_end].decode("utf-8")
    try:
        trace.metadata = json.loads(meta_text) if meta_text.strip() else {}
    except json.JSONDecodeError as error:
        raise TraceError(f"{path}: metadata is not valid JSON: {error}") from error

    offset = meta_end
    last_occurrence = {}
    while offset < len(data):
        chunk = data[offset:offset + RECORD_SIZE]
        if len(chunk) < RECORD_SIZE:
            # The process died mid-write. The partial tail is discarded.
            trace.torn = True
            break

        rtype, op_id, occurrence, operand0, operand1, result = struct.unpack(
            "<IIQQQQ", chunk
        )

        if rtype == RECORD_EVENT:
            previous = last_occurrence.get(op_id)
            if previous is not None and occurrence <= previous:
                raise TraceError(
                    f"{path}: occurrence for op {op_id} went backwards "
                    f"({previous} then {occurrence})"
                )
            last_occurrence[op_id] = occurrence
            trace.events.append(
                Event(op_id, occurrence, operand0, operand1, result)
            )
        elif rtype == RECORD_EXIT:
            trace.exit = ExitRecord(
                exit_code=operand0 & 0xFFFFFFFF,
                detected=bool(operand1 & FLAG_DETECTED),
                corrected=bool(operand1 & FLAG_CORRECTED),
                final_result=result,
            )
            offset += RECORD_SIZE
            if offset != len(data):
                raise TraceError(f"{path}: records follow the exit record")
            break
        else:
            raise TraceError(f"{path}: unknown record type {rtype}")

        offset += RECORD_SIZE

    return trace


def hex64(value):
    return f"0x{value:016x}"


def bits_to_double(value):
    import struct as _struct

    return _struct.unpack("<d", _struct.pack("<Q", value))[0]
