#pragma once

#include <cstdint>

namespace sdc {

struct ExitRecord {
    int         exit_code = 0;
    bool        detected_by_workload = false;
    std::uint64_t final_result_bits = 0;
};

} // namespace sdc
