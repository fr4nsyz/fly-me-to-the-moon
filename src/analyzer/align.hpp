#pragma once

#include "trace_reader.hpp"
#include <cstddef>
#include <vector>

namespace sdc {

// ADR-0004: alignment must survive corruption that changes control flow.
// The aligner walks both streams with a (op_id, occurrence) key and resynchronizes
// when the streams diverge structurally, rather than stopping at the first mismatch.
struct AlignmentResult {
    bool diverged = false;
    std::size_t clean_index = 0;   // index into clean.events
    std::size_t injected_index = 0; // index into injected.events
    // Number of events that matched before the divergence.
    std::size_t matched = 0;
};

AlignmentResult align_traces(const Trace& clean, const Trace& injected);

} // namespace sdc
