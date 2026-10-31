#pragma once

#include "types.hpp"

namespace sdc {

// Arithmetic kinds recorded in the trace and used by the analyzer's audit.
enum class OpKind : std::uint8_t {
    FAdd = 0,
    FSub = 1,
    FMul = 2,
    FDiv = 3,
    Unknown = 255,
};

const char* op_kind_name(OpKind kind);
bool op_kind_from_name(const char* name, OpKind& out);

struct Event {
    OpId       op_id;
    Occurrence occurrence;
    std::uint64_t operand_bits[2]; // left, right (or unary)
    std::uint64_t result_bits;
};

} // namespace sdc
