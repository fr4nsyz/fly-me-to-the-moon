#include "dynamics.hpp"
#include "constants.hpp"
#include <cmath>

namespace moon {

Vec3 gravity(Vec3 position) {
    const double r = norm(position);
    return (-kGmMoon / (r * r * r)) * position;
}

Rates derivative(const State& state, const Vehicle& vehicle, const EngineCommand& command) {
    // Actuator limits: throttle saturates at [0, 1]; NaN, zero, or infinite commands leave the engine off.
    double throttle = command.throttle > 1.0 ? 1.0 : (command.throttle > 0.0 ? command.throttle : 0.0);
    const double length = norm(command.direction);
    if (!std::isfinite(length) || length == 0.0 || state.mass <= vehicle.dry_mass) {
        throttle = 0.0;
    }
    const double thrust = throttle * vehicle.max_thrust;

    Rates rates;
    rates.velocity = state.velocity;
    rates.acceleration = gravity(state.position);
    if (thrust > 0.0) {
        rates.acceleration = rates.acceleration + (thrust / (state.mass * length)) * command.direction;
        rates.mass_rate = -thrust / (vehicle.isp * kG0);
    }
    return rates;
}

} // namespace moon
