#include "constants.hpp"
#include "integrator.hpp"
#include "interface.hpp"
#include "lander.hpp"
#include <cmath>
#include <cstdio>

int main() {
    const double r = moon::kMoonRadius + 15000.0;
    moon::State state{moon::Vec3{r, 0, 0}, moon::Vec3{0, std::sqrt(moon::kGmMoon / r), 0}, moon::kLanderStartMass};
    const moon::Vehicle vehicle{moon::kLanderDryMass, moon::kLanderMaxThrust, moon::kLanderIsp};
    const double dt = 0.1;
    for (int i = 0; i < 100; ++i) {
        const moon::Sensors sensors{state.position, state.velocity, state.mass};
        moon::EngineCommand command;
        fsw_tick(&sensors, &command);
        state = moon::rk4_step(state, dt, vehicle, command);
    }
    std::printf("%.6f %.6f %.6f\n", state.position.x, state.position.y, state.position.z);
}
