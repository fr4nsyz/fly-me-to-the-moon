#include "align.hpp"

namespace sdc {

namespace {

bool same_key(const Event& a, const Event& b) {
    return a.op_id == b.op_id && a.occurrence == b.occurrence;
}

bool same_payload(const Event& a, const Event& b) {
    return a.operand_bits[0] == b.operand_bits[0] &&
           a.operand_bits[1] == b.operand_bits[1] &&
           a.result_bits == b.result_bits;
}

} // namespace

AlignmentResult align_traces(const Trace& clean, const Trace& injected) {
    AlignmentResult result;
    std::size_t ci = 0;
    std::size_t ii = 0;

    while (ci < clean.events.size() && ii < injected.events.size()) {
        const Event& c = clean.events[ci];
        const Event& j = injected.events[ii];
        if (!same_key(c, j) || !same_payload(c, j)) {
            result.diverged = true;
            result.clean_index = ci;
            result.injected_index = ii;
            result.matched = ci;
            return result;
        }
        ++ci;
        ++ii;
        result.matched = ci;
    }

    // One stream ended before the other. The unmatched tail is a divergence.
    if (ci < clean.events.size() || ii < injected.events.size()) {
        result.diverged = true;
        result.clean_index = ci;
        result.injected_index = ii;
        result.matched = ci;
    }

    return result;
}

} // namespace sdc
