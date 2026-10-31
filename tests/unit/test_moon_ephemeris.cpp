#include "moon_ephemeris.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

TEST(MoonEphemeris, DistanceStaysAtOrbitRadius) {
    for (double days : {0.0, 7.0, 13.5, 100.0}) {
        EXPECT_NEAR(norm(moon_position(days * 86400.0)), kMoonOrbitRadius, 1e-3);
    }
}

TEST(MoonEphemeris, OrbitTiltToEquatorMatchesClosedForm) {
    const Vec3 a = moon_position(0.0);
    const Vec3 b = moon_position(3600.0);
    const Vec3 normal = cross(a, b) / norm(cross(a, b));
    const double tilt = std::acos(normal.z);

    const double centuries = (kMissionEpochJd - 2451545.0) / 36525.0;
    const double node = (125.0445479 - 1934.1362891 * centuries) * kDegToRad;
    const double expected = std::acos(std::cos(kObliquity) * std::cos(kMoonInclinationToEcliptic) -
                                      std::sin(kObliquity) * std::sin(kMoonInclinationToEcliptic) * std::cos(node));

    EXPECT_NEAR(tilt, expected, 1e-4);
    EXPECT_GT(tilt, 18.3 * kDegToRad);
    EXPECT_LT(tilt, 28.6 * kDegToRad);
}
