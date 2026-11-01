#include "guidance.hpp"
#include "constants.hpp"
#include "lander.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

namespace {

NavState nav_at(double altitude, double vertical_speed, Vec3 horizontal) {
    NavState nav;
    nav.up = Vec3{0, 0, 1};
    nav.altitude = altitude;
    nav.vertical_speed = vertical_speed;
    nav.velocity = horizontal + vertical_speed * nav.up;
    nav.position = (kMoonRadius + altitude) * nav.up;
    nav.horizontal_speed = norm(horizontal);
    return nav;
}

double hover_throttle(double altitude, double mass) {
    const double r = kMoonRadius + altitude;
    return kGmMoon / (r * r) * mass / kLanderMaxThrust;
}

} // namespace

TEST(Guidance, BrakingIsFullThrottleAgainstVelocity) {
    GuidanceState state;
    const EngineCommand c = guide(nav_at(15000.0, -5.0, Vec3{1700.0, 0, 0}), 15200.0, state);
    EXPECT_EQ(state.phase, DescentPhase::Braking);
    EXPECT_EQ(c.throttle, 1.0);
    EXPECT_LT(c.direction.x, 0.0);
}

TEST(Guidance, SwitchesToFinalBelowSwitchSpeedAndStaysThere) {
    GuidanceState state;
    guide(nav_at(5000.0, -20.0, Vec3{30.0, 0, 0}), 9000.0, state);
    EXPECT_EQ(state.phase, DescentPhase::Final);
    guide(nav_at(5000.0, -20.0, Vec3{300.0, 0, 0}), 9000.0, state);
    EXPECT_EQ(state.phase, DescentPhase::Final);
}

TEST(Guidance, SwitchesToFinalBelowSwitchAltitudeEvenWhenFast) {
    GuidanceState state;
    guide(nav_at(400.0, -40.0, Vec3{200.0, 0, 0}), 9000.0, state);
    EXPECT_EQ(state.phase, DescentPhase::Final);
}

TEST(Guidance, BrakingHoldsAltitudeWhenSinkingTooFast) {
    GuidanceState state;
    const EngineCommand c = guide(nav_at(10000.0, -200.0, Vec3{1000.0, 0, 0}), 12000.0, state);
    EXPECT_EQ(state.phase, DescentPhase::Braking);
    EXPECT_EQ(c.throttle, 1.0);
    EXPECT_GT(c.direction.z, 0.0);
}

TEST(Guidance, OnTargetDescentRateThrottleCancelsGravity) {
    GuidanceState state{DescentPhase::Final};
    const EngineCommand c = guide(nav_at(100.0, -10.0, Vec3{}), 8000.0, state);
    EXPECT_NEAR(c.throttle, hover_throttle(100.0, 8000.0), 1e-12);
    EXPECT_NEAR(c.direction.x, 0.0, 1e-12);
}

TEST(Guidance, FallingTooFastAddsThrottle) {
    GuidanceState state{DescentPhase::Final};
    const EngineCommand c = guide(nav_at(100.0, -20.0, Vec3{}), 8000.0, state);
    EXPECT_GT(c.throttle, hover_throttle(100.0, 8000.0));
}

TEST(Guidance, SidewaysDriftTiltsThrustAgainstIt) {
    GuidanceState state{DescentPhase::Final};
    const EngineCommand c = guide(nav_at(100.0, -10.0, Vec3{2.0, 0, 0}), 8000.0, state);
    EXPECT_LT(c.direction.x, 0.0);
}

TEST(Guidance, NeverThrustsTowardTheGround) {
    GuidanceState state{DescentPhase::Final};
    const EngineCommand c = guide(nav_at(500.0, 20.0, Vec3{}), 8000.0, state);
    EXPECT_EQ(c.throttle, 0.0);
}
