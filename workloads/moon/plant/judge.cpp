#include "judge.hpp"
#include "constants.hpp"
#include <cmath>

namespace moon {

// Ground truth: computed independently of the flight software's navigation.
SurfaceState surface_state(const State& state) {
    SurfaceState s;
    s.position = state.position;
    s.velocity = state.velocity - cross(kMoonSpin, state.position);
    const double r = norm(s.position);
    const Vec3 up = s.position / r;
    s.altitude = r - kMoonRadius;
    s.vertical_speed = dot(s.velocity, up);
    s.horizontal_speed = norm(s.velocity - s.vertical_speed * up);
    return s;
}

Vec3 moon_fixed(Vec3 p, double t) {
    const double c = std::cos(-kMoonSpinRate * t);
    const double s = std::sin(-kMoonSpinRate * t);
    return {c * p.x - s * p.y, s * p.x + c * p.y, p.z};
}

double landing_score(double descent_speed, double horizontal_speed, double distance) {
    // ADR-0020: Lunar Module touchdown envelope.
    const double vertical_limit = 3.05 - (3.05 - 2.13) * horizontal_speed / 1.22;
    return std::fmax(std::fmax(descent_speed / vertical_limit, horizontal_speed / 1.22), distance / 1000.0);
}

} // namespace moon
