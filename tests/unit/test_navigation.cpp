#include "navigation.hpp"
#include "constants.hpp"
#include "moon_ephemeris.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

namespace {

Sensors hovering_above_moon(double t, double altitude, double vertical_speed) {
    const Vec3 moon_r = moon_position(t);
    const Vec3 moon_v = moon_velocity(t);
    const Vec3 up = moon_r / norm(moon_r);
    const Vec3 offset = (kMoonRadius + altitude) * up;
    const Vec3 ground = cross(moon_spin(moon_r, moon_v), offset);
    return {t, moon_r + offset, moon_v + ground + vertical_speed * up, 15200.0};
}

} // namespace

TEST(Navigation, AltitudeAboveMoonSurface) {
    EXPECT_NEAR(navigate(hovering_above_moon(0.0, 15000.0, 0.0)).altitude, 15000.0, 1e-3);
}

TEST(Navigation, HoveringOverRotatingGroundHasNoHorizontalSpeed) {
    const NavState nav = navigate(hovering_above_moon(3600.0, 100.0, 0.0));
    EXPECT_NEAR(nav.vertical_speed, 0.0, 1e-3);
    EXPECT_NEAR(nav.horizontal_speed, 0.0, 1e-3);
}

TEST(Navigation, DescendingGivesNegativeVerticalSpeed) {
    const NavState nav = navigate(hovering_above_moon(0.0, 500.0, -10.0));
    EXPECT_NEAR(nav.vertical_speed, -10.0, 1e-3);
    EXPECT_NEAR(nav.horizontal_speed, 0.0, 1e-3);
}

TEST(MoonEphemeris, SpinRateIncludesNodeRegression) {
    const double per_century = kDegToRad / (36525.0 * 86400.0);
    const double longitude_rate = 481267.88123421 * per_century;
    const double node_rate = 1934.1362891 * per_century;
    const double expected = longitude_rate + node_rate * (1.0 - std::cos(kMoonInclinationToEcliptic));
    EXPECT_NEAR(norm(moon_spin(moon_position(0.0), moon_velocity(0.0))), expected, expected * 1e-6);
}
