#include "interface.hpp"

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command) {
    *command = {0.0, sensors->velocity + moon::Vec3{0.0, 0.0, 1.0}};
}
