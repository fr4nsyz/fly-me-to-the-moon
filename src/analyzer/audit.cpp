#include "audit.hpp"
#include <bit>
#include <cmath>

namespace sdc {

AuditResult audit_event(const Event& event, OpKind kind) {
    AuditResult result;

    const double lhs = std::bit_cast<double>(event.operand_bits[0]);
    const double rhs = std::bit_cast<double>(event.operand_bits[1]);
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return result;
    }

    double recomputed = 0.0;
    switch (kind) {
        case OpKind::FAdd: recomputed = lhs + rhs; break;
        case OpKind::FSub: recomputed = lhs - rhs; break;
        case OpKind::FMul: recomputed = lhs * rhs; break;
        case OpKind::FDiv:
            if (rhs == 0.0) return result;
            recomputed = lhs / rhs;
            break;
        case OpKind::FSqrt: recomputed = std::sqrt(lhs); break;
        default:
            return result;
    }

    result.computable = true;
    result.recomputed_bits = std::bit_cast<std::uint64_t>(recomputed);
    result.consistent = (result.recomputed_bits == event.result_bits);
    return result;
}

} // namespace sdc
