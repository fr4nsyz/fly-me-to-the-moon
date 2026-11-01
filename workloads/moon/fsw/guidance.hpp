#pragma once

#include "interface.hpp"
#include "navigation.hpp"

namespace moon {

enum class DescentPhase { Braking, Final };

struct GuidanceState {
    DescentPhase phase = DescentPhase::Braking;
};

EngineCommand guide(const NavState& nav, double mass, GuidanceState& state);

} // namespace moon
