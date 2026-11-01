#include "integrator.hpp"
#include "constants.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

namespace {

const Vehicle kCoasting{1000.0, 0.0, 300.0};
const EngineCommand kOff{0.0, Vec3{1, 0, 0}};

double specific_energy(const State& s) { return 0.5 * dot(s.velocity, s.velocity) - kGmMoon / norm(s.position); }

} // namespace

TEST(Rk4, LowLunarOrbitConservesEnergyAndAngularMomentum) {
    const double r = kMoonRadius + 100000.0;
    State s{Vec3{r, 0, 0}, Vec3{0, std::sqrt(kGmMoon / r), 0}, 1000.0};
    const double period = 2 * M_PI * std::sqrt(r * r * r / kGmMoon);
    const int steps = 5000;
    const double dt = period / steps;

    const double e0 = specific_energy(s);
    const Vec3 h0 = cross(s.position, s.velocity);
    for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, dt, kCoasting, kOff);
    }
    EXPECT_NEAR(specific_energy(s) / e0, 1.0, 1e-5);
    EXPECT_NEAR(norm(cross(s.position, s.velocity)) / norm(h0), 1.0, 1e-5);
}

TEST(Rk4, BurnStopsAtDryMass) {
    const Vehicle v{1000.0, 50000.0, 300.0};
    State s{Vec3{kMoonRadius * 2, 0, 0}, Vec3{}, 1010.0};
    for (int i = 0; i < 100; ++i) {
        s = rk4_step(s, 0.1, v, {1.0, Vec3{1, 0, 0}});
        EXPECT_GE(s.mass, v.dry_mass);
    }
    EXPECT_EQ(s.mass, v.dry_mass);
}
