#pragma once

#include "align.hpp"
#include "classify.hpp"
#include "ops_map.hpp"
#include "trace_reader.hpp"
#include <map>
#include <string>

namespace sdc {

struct EvidenceInput {
    Trace& clean;
    Trace& injected;
    const std::map<OpId, OpInfo>& ops;
    AlignmentResult alignment;
    Verdict verdict;
    bool final_result_outside_tolerance = false;
    bool corrected_by_mechanism = false;
    std::size_t window = 5;
};

std::string build_evidence(const EvidenceInput& input);

} // namespace sdc
