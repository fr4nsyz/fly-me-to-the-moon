#pragma once

#include "dynamics.hpp"

namespace moon {

struct SurfaceState {
    Vec3 position;                 // m, from the Moon's center
    Vec3 velocity;                 // m/s, relative to the rotating lunar surface
    double altitude = 0.0;         // m
    double vertical_speed = 0.0;   // m/s, positive up
    double horizontal_speed = 0.0; // m/s
};

SurfaceState surface_state(const State& state);
Vec3 moon_fixed(Vec3 position, double t);
double landing_score(double descent_speed, double horizontal_speed, double distance);

} // namespace moon
