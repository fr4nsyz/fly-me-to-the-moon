#include "constants.hpp"
#include "integrator.hpp"
#include "interface.hpp"
#include <cstdio>

int main() {
    moon::State state{moon::Vec3{moon::kEarthRadius + 400000.0, 0, 0}, moon::Vec3{0, 7668.6, 0}, 15200.0};
    const moon::Vehicle vehicle{7000.0, 45040.0, 311.0};
    const double dt = 0.1;
    for (int i = 0; i < 100; ++i) {
        const moon::Sensors sensors{i * dt, state.position, state.velocity, state.mass};
        moon::EngineCommand command;
        fsw_tick(&sensors, &command);
        state = moon::rk4_step(state, i * dt, dt, vehicle, command);
    }
    std::printf("%.6f %.6f %.6f\n", state.position.x, state.position.y, state.position.z);
}
