#pragma once

#include "types.hpp"

namespace sdc {

struct Event {
    OpId       op_id;
    Occurrence occurrence;
    std::uint64_t operand_bits[2]; // left, right (or unary)
    std::uint64_t result_bits;
};

} // namespace sdc
