#include "guidance.hpp"
#include "constants.hpp"
#include "lander.hpp"
#include <algorithm>

namespace moon {

namespace {

constexpr double kSwitchSpeed = 50.0;          // m/s, ground speed that ends braking
constexpr double kSwitchAltitude = 2000.0;     // m, braking ends here regardless of speed
constexpr double kDescentTimeConstant = 10.0;  // s, target descent rate is altitude over this
constexpr double kMinDescentRate = 1.0;        // m/s, touchdown rate
constexpr double kMaxDescentRate = 30.0;       // m/s
constexpr double kVerticalGain = 1.0;          // 1/s
constexpr double kHorizontalGain = 0.5;        // 1/s

} // namespace

EngineCommand guide(const NavState& nav, double mass, GuidanceState& state) {
    const double ground_speed = norm(nav.velocity);
    if (state.phase == DescentPhase::Braking &&
        (ground_speed < kSwitchSpeed || nav.altitude < kSwitchAltitude)) {
        state.phase = DescentPhase::Final;
    }

    if (state.phase == DescentPhase::Braking) {
        return {1.0, (-1.0 / ground_speed) * nav.velocity};
    }

    const double target = -std::clamp(nav.altitude / kDescentTimeConstant, kMinDescentRate, kMaxDescentRate);
    const double r = nav.altitude + kMoonRadius;
    const double gravity = kGmMoon / (r * r);
    const Vec3 horizontal = nav.velocity - nav.vertical_speed * nav.up;
    const Vec3 wanted = (gravity + kVerticalGain * (target - nav.vertical_speed)) * nav.up -
                        kHorizontalGain * horizontal;
    if (dot(wanted, nav.up) <= 0.0) {
        return {0.0, nav.up};
    }
    return {norm(wanted) * mass / kLanderMaxThrust, wanted};
}

} // namespace moon
