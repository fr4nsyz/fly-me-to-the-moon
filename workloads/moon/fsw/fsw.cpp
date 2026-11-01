#include "guidance.hpp"
#include "interface.hpp"
#include "navigation.hpp"

namespace {

moon::Defense g_defense = moon::Defense::None;
moon::GuidanceState g_guidance;

} // namespace

extern "C" void fsw_configure(int defense) {
    g_defense = static_cast<moon::Defense>(defense);
}

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command, moon::FswStatus* status) {
    *status = {};
    const moon::NavState nav = moon::navigate(*sensors);
    *command = moon::guide(nav, sensors->mass, g_guidance);
}
