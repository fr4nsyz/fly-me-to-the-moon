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

TEST(MoonEphemeris, VelocityMatchesMeanOrbitalSpeed) {
    const double rate = 481267.88123421 * kDegToRad / (36525.0 * 86400.0); // rad/s
    EXPECT_NEAR(norm(moon_velocity(0.0)), kMoonOrbitRadius * rate, 1.0);
}

TEST(MoonEphemeris, VelocityIsPerpendicularToPositionOnACircle) {
    for (double days : {0.0, 9.0, 21.0}) {
        const Vec3 r = moon_position(days * 86400.0);
        const Vec3 v = moon_velocity(days * 86400.0);
        EXPECT_NEAR(dot(r, v) / (norm(r) * norm(v)), 0.0, 1e-9);
    }
}

TEST(MoonEphemeris, VelocityDoesNotDependOnDifferenceStep) {
    for (double days = 0.0; days < 30.0; days += 0.37) {
        const double t = days * 86400.0;
        const Vec3 half_step = (moon_position(t + 0.5) - moon_position(t - 0.5)) / 1.0;
        EXPECT_LT(norm(moon_velocity(t) - half_step), 2e-3);
    }
}
