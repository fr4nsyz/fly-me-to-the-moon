#include "guidance.hpp"
#include "interface.hpp"
#include "navigation.hpp"

namespace {

moon::GuidanceState g_guidance;

} // namespace

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command) {
    const moon::NavState nav = moon::navigate(*sensors);
    *command = moon::guide(nav, sensors->mass, g_guidance);
}
