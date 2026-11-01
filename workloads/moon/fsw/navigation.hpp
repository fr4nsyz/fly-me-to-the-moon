#pragma once

#include "interface.hpp"

namespace moon {

struct NavState {
    Vec3 position;                 // m, from the Moon's center
    Vec3 velocity;                 // m/s, relative to the rotating lunar surface
    Vec3 up;                       // unit vector away from the Moon's center
    double altitude = 0.0;         // m, above the mean lunar radius
    double vertical_speed = 0.0;   // m/s, positive up
    double horizontal_speed = 0.0; // m/s
};

NavState navigate(const Sensors& sensors);

} // namespace moon
