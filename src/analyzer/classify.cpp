#include "classify.hpp"

namespace sdc {

const char* classification_name(Classification c) {
    switch (c) {
        case Classification::CRASH: return "CRASH";
        case Classification::HANG: return "HANG";
        case Classification::CORRECTED: return "CORRECTED";
        case Classification::DETECTED: return "DETECTED";
        case Classification::SDC: return "SDC";
        case Classification::MASKED: return "MASKED";
        default: return "INCONCLUSIVE";
    }
}

Verdict classify(const VerdictInput& input) {
    Verdict verdict;

    if (!input.evidence_sufficient) {
        verdict.classification = Classification::INCONCLUSIVE;
        verdict.reasons.push_back("insufficient evidence to reach a verdict");
        return verdict;
    }

    if (input.timed_out) {
        verdict.classification = Classification::HANG;
        verdict.reasons.push_back("injected run exceeded the time limit and was killed");
        return verdict;
    }

    if (!input.clean_has_exit || !input.injected_has_exit) {
        verdict.classification = Classification::CRASH;
        if (!input.injected_has_exit) {
            verdict.reasons.push_back("injected run produced no exit record; the process died during recording");
        } else {
            verdict.reasons.push_back("clean run produced no exit record; the baseline is unusable");
        }
        if (input.divergence_found) {
            verdict.reasons.push_back("event streams diverged before the crash");
        }
        return verdict;
    }

    if (input.injected_exit_code != 0) {
        verdict.classification = Classification::CRASH;
        verdict.reasons.push_back("injected run exited with a nonzero code");
        return verdict;
    }

    if (input.corrected_by_mechanism && !input.final_result_outside_tolerance) {
        verdict.classification = Classification::CORRECTED;
        verdict.reasons.push_back("a redundancy mechanism repaired the fault and the final result is within tolerance");
        if (input.arithmetic_inconsistent) {
            verdict.reasons.push_back("the corrupted intermediate was observed in the trace before repair");
        }
        return verdict;
    }

    if (input.corrected_by_mechanism && input.final_result_outside_tolerance) {
        verdict.classification = Classification::SDC;
        verdict.reasons.push_back("a repair was attempted but the final result is outside tolerance");
        return verdict;
    }

    if (input.injected_detected) {
        verdict.classification = Classification::DETECTED;
        verdict.reasons.push_back("the workload's own check raised a flag");
        if (input.arithmetic_inconsistent) {
            verdict.reasons.push_back("the corrupted intermediate was observed in the trace");
        }
        return verdict;
    }

    if (input.final_result_outside_tolerance) {
        verdict.classification = Classification::SDC;
        verdict.reasons.push_back("the final result is outside the acceptance criterion and nothing noticed the fault");
        if (input.divergence_found) {
            verdict.reasons.push_back("the injected value propagated to the final result");
        }
        return verdict;
    }

    verdict.classification = Classification::MASKED;
    verdict.reasons.push_back("the final result stayed inside the acceptance criterion");
    if (input.divergence_found) {
        verdict.reasons.push_back("a divergence occurred but was absorbed before the final result");
    }
    return verdict;
}

} // namespace sdc
