#pragma once

#include "constants.hpp"
#include "vec3.hpp"
#include <cmath>

namespace moon {

inline constexpr double kDegToRad = 0.017453292519943295;

// Earth-centered, Earth-equatorial frame; t is seconds since kMissionEpochJd.
inline Vec3 moon_position(double t) {
    const double centuries = (kMissionEpochJd + t / 86400.0 - 2451545.0) / 36525.0;
    // Mean elements: Meeus, Astronomical Algorithms, ch. 47, linear terms.
    const double mean_longitude = (218.3164477 + 481267.88123421 * centuries) * kDegToRad;
    const double node = (125.0445479 - 1934.1362891 * centuries) * kDegToRad;
    const double u = mean_longitude - node;
    const double i = kMoonInclinationToEcliptic;

    const double x = kMoonOrbitRadius * (std::cos(node) * std::cos(u) - std::sin(node) * std::sin(u) * std::cos(i));
    const double y = kMoonOrbitRadius * (std::sin(node) * std::cos(u) + std::cos(node) * std::sin(u) * std::cos(i));
    const double z = kMoonOrbitRadius * std::sin(u) * std::sin(i);

    return {x,
            y * std::cos(kObliquity) - z * std::sin(kObliquity),
            y * std::sin(kObliquity) + z * std::cos(kObliquity)};
}

} // namespace moon
