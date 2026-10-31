#include "dynamics.hpp"
#include "constants.hpp"
#include "moon_ephemeris.hpp"
#include <cmath>

namespace moon {

Vec3 gravity(Vec3 position, double t) {
    const double r = norm(position);
    const Vec3 moon = moon_position(t);
    const Vec3 to_moon = moon - position;
    const double d = norm(to_moon);
    const double m = norm(moon);
    // The last term is the Moon's pull on Earth, required because the frame is Earth-centered.
    return (-kGmEarth / (r * r * r)) * position +
           kGmMoon * (to_moon / (d * d * d) - moon / (m * m * m));
}

Rates derivative(const State& state, double t, const Vehicle& vehicle, const EngineCommand& command) {
    // Actuator limits: throttle saturates at [0, 1]; NaN, zero, or infinite commands leave the engine off.
    double throttle = command.throttle > 1.0 ? 1.0 : (command.throttle > 0.0 ? command.throttle : 0.0);
    const double length = norm(command.direction);
    if (!std::isfinite(length) || length == 0.0 || state.mass <= vehicle.dry_mass) {
        throttle = 0.0;
    }
    const double thrust = throttle * vehicle.max_thrust;

    Rates rates;
    rates.velocity = state.velocity;
    rates.acceleration = gravity(state.position, t);
    if (thrust > 0.0) {
        rates.acceleration = rates.acceleration + (thrust / (state.mass * length)) * command.direction;
        rates.mass_rate = -thrust / (vehicle.isp * kG0);
    }
    return rates;
}

} // namespace moon
