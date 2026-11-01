#include "navigation.hpp"
#include "constants.hpp"
#include <gtest/gtest.h>

using namespace moon;

namespace {

Sensors hovering_above_moon(Vec3 up, double altitude, double vertical_speed) {
    const Vec3 position = (kMoonRadius + altitude) * up;
    return {position, cross(kMoonSpin, position) + vertical_speed * up, 15200.0};
}

const Vec3 kTilted = Vec3{0.6, 0.0, 0.8};

} // namespace

TEST(Navigation, AltitudeAboveMoonSurface) {
    EXPECT_NEAR(navigate(hovering_above_moon(Vec3{1, 0, 0}, 15000.0, 0.0)).altitude, 15000.0, 1e-3);
}

TEST(Navigation, HoveringOverRotatingGroundHasNoHorizontalSpeed) {
    const NavState nav = navigate(hovering_above_moon(kTilted, 100.0, 0.0));
    EXPECT_NEAR(nav.vertical_speed, 0.0, 1e-3);
    EXPECT_NEAR(nav.horizontal_speed, 0.0, 1e-3);
}

TEST(Navigation, DescendingGivesNegativeVerticalSpeed) {
    const NavState nav = navigate(hovering_above_moon(Vec3{1, 0, 0}, 500.0, -10.0));
    EXPECT_NEAR(nav.vertical_speed, -10.0, 1e-3);
    EXPECT_NEAR(nav.horizontal_speed, 0.0, 1e-3);
}

TEST(Navigation, GroundSpeedAtEquatorIsAbout4Point6) {
    EXPECT_NEAR(norm(cross(kMoonSpin, Vec3{kMoonRadius, 0, 0})), 4.624, 0.001);
}
