#include "guidance.hpp"
#include "interface.hpp"
#include "navigation.hpp"

extern "C" void run_core(const moon::Sensors* sensors, moon::GuidanceState* state, moon::EngineCommand* command) {
    *command = moon::guide(moon::navigate(*sensors), sensors->mass, *state);
}
