#pragma once

#include "vec3.hpp"

namespace moon {

enum class Defense { None = 0, TmrLockstep = 1 };

struct EngineCommand {
    double throttle = 0.0;
    Vec3 direction;
};

struct Sensors {
    Vec3 position;
    Vec3 velocity;
    double mass = 0.0;
};

struct FswStatus {
    bool detected = false;
    bool corrected = false;
};

} // namespace moon

extern "C" void fsw_configure(int defense);
extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command, moon::FswStatus* status);
