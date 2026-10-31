#pragma once

#include "trace_reader.hpp"
#include <cstddef>
#include <vector>

namespace sdc {

// ADR-0014: the first divergence is the first index where key or payload differs.
// A single fault always mismatches on payload before control flow can change.
struct AlignmentResult {
    bool diverged = false;
    std::size_t clean_index = 0;   // index into clean.events
    std::size_t injected_index = 0; // index into injected.events
    // Number of events that matched before the divergence.
    std::size_t matched = 0;
};

AlignmentResult align_traces(const Trace& clean, const Trace& injected);

} // namespace sdc
