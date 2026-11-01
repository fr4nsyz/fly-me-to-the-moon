#include "guidance.hpp"
#include "constants.hpp"
#include "lander.hpp"
#include <algorithm>
#include <cmath>

namespace moon {

namespace {

constexpr double kSwitchSpeed = 50.0;          // m/s, horizontal speed that ends braking
constexpr double kSwitchAltitude = 500.0;      // m, braking ends here regardless of speed
constexpr double kBrakingEndAltitude = 1500.0; // m, braking aims to arrive here as horizontal speed reaches zero
constexpr double kDescentTimeConstant = 10.0;  // s, target descent rate is altitude over this
constexpr double kMinDescentRate = 1.0;        // m/s, touchdown rate
constexpr double kMaxDescentRate = 30.0;       // m/s
constexpr double kVerticalGain = 1.0;          // 1/s
constexpr double kHorizontalGain = 0.5;        // 1/s

} // namespace

EngineCommand guide(const NavState& nav, double mass, GuidanceState& state) {
    if (state.phase == DescentPhase::Braking &&
        (nav.horizontal_speed < kSwitchSpeed || nav.altitude < kSwitchAltitude)) {
        state.phase = DescentPhase::Final;
    }

    const Vec3 horizontal = nav.velocity - nav.vertical_speed * nav.up;
    const double r = nav.altitude + kMoonRadius;
    const double gravity = kGmMoon / (r * r);

    if (state.phase == DescentPhase::Braking) {
        const double max_accel = kLanderMaxThrust / mass;
        const double time_to_stop = nav.horizontal_speed / max_accel;
        const double target = -(nav.altitude - kBrakingEndAltitude) / std::fmax(time_to_stop, 1.0);
        const double vertical = std::clamp(gravity - nav.horizontal_speed * nav.horizontal_speed / r +
                                               kVerticalGain * (target - nav.vertical_speed),
                                           0.0, max_accel);
        const double sideways = std::sqrt((max_accel - vertical) * (max_accel + vertical));
        return {1.0, vertical * nav.up - (sideways / nav.horizontal_speed) * horizontal};
    }

    const double target = -std::clamp(nav.altitude / kDescentTimeConstant, kMinDescentRate, kMaxDescentRate);
    const Vec3 wanted = (gravity + kVerticalGain * (target - nav.vertical_speed)) * nav.up -
                        kHorizontalGain * horizontal;
    if (dot(wanted, nav.up) <= 0.0) {
        return {0.0, nav.up};
    }
    return {norm(wanted) * mass / kLanderMaxThrust, wanted};
}

} // namespace moon
