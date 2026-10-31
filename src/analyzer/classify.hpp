#pragma once

#include <string>
#include <vector>

namespace sdc {

// ADR-0005: the taxonomy includes CORRECTED, which is distinct from MASKED.
// Precedence: CRASH > CORRECTED > DETECTED > SDC > MASKED > INCONCLUSIVE.
enum class Classification {
    CRASH,
    CORRECTED,
    DETECTED,
    SDC,
    MASKED,
    INCONCLUSIVE,
};

const char* classification_name(Classification c);

struct VerdictInput {
    bool clean_has_exit = false;
    bool injected_has_exit = false;
    int  injected_exit_code = 0;
    bool injected_detected = false;
    bool corrected_by_mechanism = false;
    bool final_result_outside_tolerance = false;
    bool divergence_found = false;
    bool arithmetic_inconsistent = false;
    bool evidence_sufficient = true;
};

struct Verdict {
    Classification classification = Classification::INCONCLUSIVE;
    std::vector<std::string> reasons;
};

Verdict classify(const VerdictInput& input);

} // namespace sdc
