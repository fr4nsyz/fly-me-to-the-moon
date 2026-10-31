#pragma once

#include <cstdint>

namespace sdc {

struct ExitRecord {
    int         exit_code = 0;
    bool        detected_by_workload = false;
    // ADR-0005: the correction signal, kept separate from the detection signal
    // so the analyzer can tell "a mechanism repaired this" from "the workload
    // noticed this". Both ride in the exit record's detection word.
    bool        corrected_by_mechanism = false;
    std::uint64_t final_result_bits = 0;
};

} // namespace sdc
