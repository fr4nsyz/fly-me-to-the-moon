#pragma once

#include "vec3.hpp"

namespace moon {

struct EngineCommand {
    double throttle = 0.0;
    Vec3 direction;
};

struct Sensors {
    double t = 0.0;
    Vec3 position;
    Vec3 velocity;
    double mass = 0.0;
};

} // namespace moon

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command);
