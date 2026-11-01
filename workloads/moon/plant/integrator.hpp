#pragma once

#include "dynamics.hpp"

namespace moon {

State rk4_step(const State& state, double dt, const Vehicle& vehicle, const EngineCommand& command);

} // namespace moon
