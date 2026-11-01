#pragma once

#include "vec3.hpp"

namespace moon {

// GM value: JPL DE430.
inline constexpr double kGmMoon = 4.902800066e12;         // m^3/s^2
inline constexpr double kMoonRadius = 1737400.0;          // m, mean
inline constexpr double kMoonSpinRate = 2.6616995e-6;     // rad/s, one turn per 27.321661 days
inline constexpr Vec3 kMoonSpin{0.0, 0.0, kMoonSpinRate};
inline constexpr double kG0 = 9.80665;                    // m/s^2, standard gravity for Isp

} // namespace moon
