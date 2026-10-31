#include "integrator.hpp"
#include "constants.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace moon;

namespace {

const Vehicle kCoasting{1000.0, 0.0, 300.0};
const EngineCommand kOff{0.0, Vec3{1, 0, 0}};

double specific_energy(const State& s) { return 0.5 * dot(s.velocity, s.velocity) - kGmEarth / norm(s.position); }

} // namespace

TEST(Rk4, LowEarthOrbitConservesEnergyAndAngularMomentum) {
    const double r = kEarthRadius + 400000.0;
    State s{Vec3{r, 0, 0}, Vec3{0, std::sqrt(kGmEarth / r), 0}, 1000.0};
    const double period = 2 * M_PI * std::sqrt(r * r * r / kGmEarth);
    const int steps = 5000;
    const double dt = period / steps;

    const double e0 = specific_energy(s);
    const Vec3 h0 = cross(s.position, s.velocity);
    for (int i = 0; i < steps; ++i) {
        s = rk4_step(s, i * dt, dt, kCoasting, kOff);
    }
    EXPECT_NEAR(specific_energy(s) / e0, 1.0, 1e-5);
    EXPECT_NEAR(norm(cross(s.position, s.velocity)) / norm(h0), 1.0, 1e-5);
}

TEST(Rk4, BurnStopsAtDryMass) {
    const Vehicle v{1000.0, 50000.0, 300.0};
    State s{Vec3{kEarthRadius * 2, 0, 0}, Vec3{}, 1010.0};
    for (int i = 0; i < 100; ++i) {
        s = rk4_step(s, i * 0.1, 0.1, v, {1.0, Vec3{1, 0, 0}});
        EXPECT_GE(s.mass, v.dry_mass);
    }
    EXPECT_EQ(s.mass, v.dry_mass);
}

TEST(Gravity, IndirectTermKeepsMoonEffectTidalNearEarth) {
    const Vec3 r{kEarthRadius + 400000.0, 0, 0};
    const Vec3 earth_only = (-kGmEarth / std::pow(norm(r), 3)) * r;
    EXPECT_LT(norm(gravity(r, 0.0) - earth_only), 2e-6);
}
