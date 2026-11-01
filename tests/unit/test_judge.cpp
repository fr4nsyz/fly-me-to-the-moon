#include "judge.hpp"
#include "constants.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

TEST(LandingScore, GentleVerticalLandingPasses) {
    EXPECT_NEAR(landing_score(1.0, 0.0, 0.0), 1.0 / 3.05, 1e-12);
}

TEST(LandingScore, EnvelopeCornerScoresExactlyOne) {
    EXPECT_NEAR(landing_score(2.13, 1.22, 0.0), 1.0, 1e-12);
}

TEST(LandingScore, BoxCornerFailsTheTrapezoid) {
    EXPECT_GT(landing_score(3.0, 1.2, 0.0), 1.0);
}

TEST(LandingScore, LandingOneAndAHalfKilometersAwayFails) {
    EXPECT_NEAR(landing_score(1.0, 0.0, 1500.0), 1.5, 1e-12);
}

TEST(Judge, SameGroundSpotIsSameMoonFixedPointAtDifferentTimes) {
    const Vec3 p0{kMoonRadius * 0.8, kMoonRadius * 0.6, 0.0};
    const double dt = 600.0;
    const double a = kMoonSpinRate * dt;
    const Vec3 p1{std::cos(a) * p0.x - std::sin(a) * p0.y, std::sin(a) * p0.x + std::cos(a) * p0.y, 0.0};
    EXPECT_LT(norm(moon_fixed(p0, 0.0) - moon_fixed(p1, dt)), 1e-6);
}

TEST(Judge, HoveringOverGroundHasAltitudeAndNoSpeed) {
    const Vec3 position{0.0, kMoonRadius + 250.0, 0.0};
    const SurfaceState s = surface_state(State{position, cross(kMoonSpin, position), 9000.0});
    EXPECT_NEAR(s.altitude, 250.0, 1e-6);
    EXPECT_NEAR(s.horizontal_speed, 0.0, 1e-9);
}
