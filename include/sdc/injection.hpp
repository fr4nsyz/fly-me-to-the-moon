#pragma once

#include "types.hpp"

namespace sdc {

// Planned multi-site configuration (schema 2, STATE.md). The runtime does not
// use this struct yet; it keeps an internal std::vector<InjectSite>.
struct InjectionConfig {
    OpId       target_op = 0;
    Occurrence target_occurrence = 0;
    BitPos     bit = 0;
    bool       enabled = false;
};

} // namespace sdc
