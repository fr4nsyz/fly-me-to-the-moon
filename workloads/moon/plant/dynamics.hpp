#pragma once

#include "interface.hpp"
#include "vec3.hpp"

namespace moon {

struct State {
    Vec3 position;
    Vec3 velocity;
    double mass = 0.0;
};

struct Vehicle {
    double dry_mass = 0.0;   // kg
    double max_thrust = 0.0; // N
    double isp = 0.0;        // s
};

struct Rates {
    Vec3 velocity;
    Vec3 acceleration;
    double mass_rate = 0.0;
};

Vec3 gravity(Vec3 position, double t);
Rates derivative(const State& state, double t, const Vehicle& vehicle, const EngineCommand& command);

} // namespace moon
