#include "constants.hpp"
#include "integrator.hpp"
#include "interface.hpp"
#include "judge.hpp"
#include "lander.hpp"
#include "runtime.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

constexpr double kDt = 0.1;                // s, flight software tick and plant step
constexpr double kTimeLimit = 3600.0;      // s
constexpr double kStartAltitude = 15000.0; // m

} // namespace

int main(int argc, char** argv) {
    moon::Vec3 reference;
    bool have_reference = false;
    std::FILE* log = nullptr;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--reference") == 0) {
            have_reference = std::sscanf(argv[i + 1], "%lf,%lf,%lf", &reference.x, &reference.y, &reference.z) == 3;
        } else if (std::strcmp(argv[i], "--log") == 0) {
            log = std::fopen(argv[i + 1], "w");
        }
    }
    if (log) std::fprintf(log, "t,altitude,vertical_speed,horizontal_speed,mass,throttle\n");

    const double r0 = moon::kMoonRadius + kStartAltitude;
    moon::State state{moon::Vec3{r0, 0, 0}, moon::Vec3{0, std::sqrt(moon::kGmMoon / r0), 0}, moon::kLanderStartMass};
    const moon::Vehicle vehicle{moon::kLanderDryMass, moon::kLanderMaxThrust, moon::kLanderIsp};
    moon::SurfaceState before = moon::surface_state(state);
    double score = std::numeric_limits<double>::infinity();

    for (int i = 0; i * kDt < kTimeLimit; ++i) {
        const double t = i * kDt;
        const moon::Sensors sensors{state.position, state.velocity, state.mass};
        moon::EngineCommand command;
        fsw_tick(&sensors, &command);
        if (log && i % 10 == 0) {
            std::fprintf(log, "%.1f,%.3f,%.3f,%.3f,%.3f,%.4f\n", t, before.altitude, before.vertical_speed,
                         before.horizontal_speed, state.mass, command.throttle);
        }

        state = moon::rk4_step(state, kDt, vehicle, command);
        const moon::SurfaceState after = moon::surface_state(state);
        if (after.altitude <= 0.0) {
            // Interpolate to the moment altitude crossed zero within the step.
            const double f = before.altitude / (before.altitude - after.altitude);
            const double descent = -(before.vertical_speed + f * (after.vertical_speed - before.vertical_speed));
            const double sideways = before.horizontal_speed + f * (after.horizontal_speed - before.horizontal_speed);
            const double t_touch = t + f * kDt;
            const moon::Vec3 spot = moon::moon_fixed(before.position + f * (after.position - before.position), t_touch);
            const double distance = have_reference ? moon::norm(spot - reference) : 0.0;
            score = moon::landing_score(descent, sideways, distance);
            std::printf("touchdown t=%.2f descent=%.3f horizontal=%.3f fuel=%.1f spot=%.3f,%.3f,%.3f score=%.4f\n",
                        t_touch, descent, sideways, state.mass - moon::kLanderDryMass, spot.x, spot.y, spot.z, score);
            break;
        }
        before = after;
    }
    if (std::isinf(score)) std::printf("no touchdown within %.0f s score=inf\n", kTimeLimit);
    if (log) std::fclose(log);
    sdc_report_result(score);
}
