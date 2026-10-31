#include "dynamics.hpp"
#include "constants.hpp"
#include "moon_ephemeris.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

using namespace moon;

namespace {

const Vehicle kVehicle{100000.0, 2.0e6, 350.0};

State state_at(Vec3 position, double mass) { return State{position, Vec3{}, mass}; }

} // namespace

TEST(Gravity, AtEarthSurfaceIsAbout9Point8) {
    EXPECT_NEAR(norm(gravity(Vec3{kEarthRadius, 0, 0}, 0.0)), 9.798, 0.01);
}

TEST(Gravity, AtMoonSurfaceIsAbout1Point62) {
    const Vec3 moon = moon_position(0.0);
    const Vec3 surface = moon + (kMoonRadius / norm(moon)) * moon;
    EXPECT_NEAR(norm(gravity(surface, 0.0)), 1.62, 0.01);
}

TEST(Derivative, MassRateMatchesIsp) {
    const Rates r = derivative(state_at(Vec3{kEarthRadius * 2, 0, 0}, 200000.0), 0.0, kVehicle, {1.0, Vec3{1, 0, 0}});
    EXPECT_DOUBLE_EQ(r.mass_rate, -2.0e6 / (350.0 * kG0));
}

TEST(Derivative, ThrottleAboveOneSaturates) {
    const State s = state_at(Vec3{kEarthRadius * 2, 0, 0}, 200000.0);
    const Rates full = derivative(s, 0.0, kVehicle, {1.0, Vec3{1, 0, 0}});
    const Rates huge = derivative(s, 0.0, kVehicle, {7.0e6, Vec3{1.0e15, 0, 0}});
    EXPECT_DOUBLE_EQ(huge.acceleration.x, full.acceleration.x);
    EXPECT_DOUBLE_EQ(huge.mass_rate, full.mass_rate);
}

TEST(Derivative, NanCommandLeavesEngineOff) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const State s = state_at(Vec3{kEarthRadius * 2, 0, 0}, 200000.0);
    const Vec3 g = gravity(s.position, 0.0);
    for (const EngineCommand& c : {EngineCommand{nan, Vec3{1, 0, 0}}, EngineCommand{1.0, Vec3{nan, 0, 0}}}) {
        const Rates r = derivative(s, 0.0, kVehicle, c);
        EXPECT_DOUBLE_EQ(r.acceleration.x, g.x);
        EXPECT_EQ(r.mass_rate, 0.0);
    }
}

TEST(Derivative, EmptyTanksGiveNoThrust) {
    const Rates r = derivative(state_at(Vec3{kEarthRadius * 2, 0, 0}, kVehicle.dry_mass), 0.0, kVehicle, {1.0, Vec3{1, 0, 0}});
    EXPECT_EQ(r.mass_rate, 0.0);
}
