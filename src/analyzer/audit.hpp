#pragma once

#include "sdc/event.hpp"
#include <cstdint>

namespace sdc {

struct AuditResult {
    bool      consistent = false;
    bool      computable = false; // false when the op kind is unknown or operands are not finite
    std::uint64_t recomputed_bits = 0;
};

// Recomputes the operation from the recorded operands and compares against the
// recorded result bits. This is how the analyzer proves a corruption happened
// rather than inferring it from a mismatch.
AuditResult audit_event(const Event& event, OpKind kind);

} // namespace sdc
