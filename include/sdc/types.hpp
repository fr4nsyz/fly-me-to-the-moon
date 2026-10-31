#pragma once

#include <cstdint>

namespace sdc {

using OpId = std::uint32_t;
using Occurrence = std::uint64_t;
using BitPos = std::uint8_t; // 0-63 for double, 0-31 for float

// ADR-0006 (accepted): bit 0 is the LSB of the uint64 IEEE-754 pattern.
// bit 63 is the sign bit, bits 62..52 are the exponent. This is fixed.

} // namespace sdc
