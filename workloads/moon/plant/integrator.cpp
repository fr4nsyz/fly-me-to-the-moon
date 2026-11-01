#include "integrator.hpp"

namespace moon {

namespace {

State advance(const State& s, const Rates& k, double h) {
    return {s.position + h * k.velocity, s.velocity + h * k.acceleration, s.mass + h * k.mass_rate};
}

} // namespace

State rk4_step(const State& state, double dt, const Vehicle& vehicle, const EngineCommand& command) {
    const Rates k1 = derivative(state, vehicle, command);
    const Rates k2 = derivative(advance(state, k1, dt / 2), vehicle, command);
    const Rates k3 = derivative(advance(state, k2, dt / 2), vehicle, command);
    const Rates k4 = derivative(advance(state, k3, dt), vehicle, command);

    State next;
    next.position = state.position + (dt / 6) * (k1.velocity + 2 * k2.velocity + 2 * k3.velocity + k4.velocity);
    next.velocity = state.velocity + (dt / 6) * (k1.acceleration + 2 * k2.acceleration + 2 * k3.acceleration + k4.acceleration);
    next.mass = state.mass + (dt / 6) * (k1.mass_rate + 2 * k2.mass_rate + 2 * k3.mass_rate + k4.mass_rate);
    // A burn can empty the tanks partway through a step.
    if (next.mass < vehicle.dry_mass) {
        next.mass = vehicle.dry_mass;
    }
    return next;
}

} // namespace moon
