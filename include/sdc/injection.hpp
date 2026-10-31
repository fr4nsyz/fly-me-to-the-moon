#pragma once

#include "types.hpp"

namespace sdc {

struct InjectionConfig {
    OpId       target_op = 0;
    Occurrence target_occurrence = 0;
    BitPos     bit = 0;
    bool       enabled = false;
};

} // namespace sdc
