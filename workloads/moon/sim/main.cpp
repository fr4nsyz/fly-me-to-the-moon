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

// JSON has no NaN or infinity; a faulty run can produce both.
void put(std::FILE* f, const char* format, double v) {
    if (std::isfinite(v)) {
        std::fprintf(f, format, v);
    } else {
        std::fputs("null", f);
    }
}

void put_vec(std::FILE* f, moon::Vec3 v) {
    std::fputc('[', f);
    put(f, "%.17g", v.x);
    std::fputc(',', f);
    put(f, "%.17g", v.y);
    std::fputc(',', f);
    put(f, "%.17g", v.z);
    std::fputc(']', f);
}

void put_sample(std::FILE* f, bool first, double t, moon::Vec3 position, double altitude, double vertical_speed,
                double horizontal_speed, double mass, double throttle, unsigned computers) {
    std::fputs(first ? "\n{\"t\":" : ",\n{\"t\":", f);
    put(f, "%.4f", t);
    std::fputs(",\"position\":", f);
    put_vec(f, position);
    std::fputs(",\"altitude\":", f);
    put(f, "%.4f", altitude);
    std::fputs(",\"vertical_speed\":", f);
    put(f, "%.4f", vertical_speed);
    std::fputs(",\"horizontal_speed\":", f);
    put(f, "%.4f", horizontal_speed);
    std::fputs(",\"mass\":", f);
    put(f, "%.3f", mass);
    std::fputs(",\"throttle\":", f);
    put(f, "%.5f", throttle);
    std::fprintf(f, ",\"computers\":%u}", computers);
}

} // namespace

int main(int argc, char** argv) {
    moon::Vec3 reference;
    bool have_reference = false;
    std::FILE* log = nullptr;
    std::FILE* trajectory = nullptr;
    moon::Defense defense = moon::Defense::None;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--reference") == 0) {
            have_reference = std::sscanf(argv[i + 1], "%lf,%lf,%lf", &reference.x, &reference.y, &reference.z) == 3;
        } else if (std::strcmp(argv[i], "--log") == 0) {
            log = std::fopen(argv[i + 1], "w");
        } else if (std::strcmp(argv[i], "--trajectory") == 0) {
            trajectory = std::fopen(argv[i + 1], "w");
        } else if (std::strcmp(argv[i], "--defense") == 0) {
            if (std::strcmp(argv[i + 1], "none") == 0) {
                defense = moon::Defense::None;
            } else if (std::strcmp(argv[i + 1], "tmr-lockstep") == 0) {
                defense = moon::Defense::TmrLockstep;
            } else {
                std::fprintf(stderr, "unknown defense '%s'; expected none or tmr-lockstep\n", argv[i + 1]);
                return 2;
            }
        }
    }
    fsw_configure(static_cast<int>(defense));
    if (log) std::fprintf(log, "t,altitude,vertical_speed,horizontal_speed,mass,throttle\n");
    if (trajectory) {
        std::fprintf(trajectory, "{\"defense\":\"%s\",\"samples\":[",
                     defense == moon::Defense::TmrLockstep ? "tmr-lockstep" : "none");
    }

    const double r0 = moon::kMoonRadius + kStartAltitude;
    moon::State state{moon::Vec3{r0, 0, 0}, moon::Vec3{0, std::sqrt(moon::kGmMoon / r0), 0}, moon::kLanderStartMass};
    const moon::Vehicle vehicle{moon::kLanderDryMass, moon::kLanderMaxThrust, moon::kLanderIsp};
    moon::SurfaceState before = moon::surface_state(state);
    double score = std::numeric_limits<double>::infinity();
    bool detected = false;
    bool corrected = false;

    for (int i = 0; i * kDt < kTimeLimit; ++i) {
        const double t = i * kDt;
        const moon::Sensors sensors{state.position, state.velocity, state.mass};
        moon::EngineCommand command;
        moon::FswStatus status;
        fsw_tick(&sensors, &command, &status);
        detected = detected || status.detected;
        corrected = corrected || status.corrected;
        if (log && i % 10 == 0) {
            std::fprintf(log, "%.1f,%.3f,%.3f,%.3f,%.3f,%.4f\n", t, before.altitude, before.vertical_speed,
                         before.horizontal_speed, state.mass, command.throttle);
        }
        if (trajectory && i % 10 == 0) {
            put_sample(trajectory, i == 0, t, moon::moon_fixed(before.position, t), before.altitude,
                       before.vertical_speed, before.horizontal_speed, state.mass, command.throttle, status.computers);
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
            if (trajectory) {
                put_sample(trajectory, false, t_touch, spot, 0.0, -descent, sideways, state.mass, command.throttle,
                           status.computers);
                std::fputs("],\n\"touchdown\":{\"t\":", trajectory);
                put(trajectory, "%.4f", t_touch);
                std::fputs(",\"descent\":", trajectory);
                put(trajectory, "%.4f", descent);
                std::fputs(",\"horizontal\":", trajectory);
                put(trajectory, "%.4f", sideways);
                std::fputs(",\"fuel\":", trajectory);
                put(trajectory, "%.3f", state.mass - moon::kLanderDryMass);
                std::fputs(",\"score\":", trajectory);
                put(trajectory, "%.6g", score);
                std::fputs(",\"spot\":", trajectory);
                put_vec(trajectory, spot);
                std::fputs("}}\n", trajectory);
                std::fclose(trajectory);
                trajectory = nullptr;
            }
            break;
        }
        before = after;
    }
    if (std::isinf(score)) std::printf("no touchdown within %.0f s score=inf\n", kTimeLimit);
    if (log) std::fclose(log);
    if (trajectory) {
        std::fputs("],\n\"touchdown\":null}\n", trajectory);
        std::fclose(trajectory);
    }
    if (detected) sdc_report_detected();
    if (corrected) sdc_report_corrected();
    sdc_report_result(score);
}
