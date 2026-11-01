#include "interface.hpp"
#include "navigation.hpp"

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command) {
    const moon::NavState nav = moon::navigate(*sensors);
    *command = {0.0, nav.up};
}
