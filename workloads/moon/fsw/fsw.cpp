#include "guidance.hpp"
#include "interface.hpp"
#include "redundancy.hpp"

// Defined by tools/instrument.sh --replicate 6 --replicate-entry run_core from core.cpp.
extern "C" {
void run_core_0(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
void run_core_1(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
void run_core_2(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
void run_core_3(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
void run_core_4(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
void run_core_5(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
}

namespace {

using CoreFn = void (*)(const moon::Sensors*, moon::GuidanceState*, moon::EngineCommand*);
constexpr CoreFn kCoreFns[moon::kCores] = {run_core_0, run_core_1, run_core_2, run_core_3, run_core_4, run_core_5};

moon::Defense g_defense = moon::Defense::None;
moon::GuidanceState g_states[moon::kCores];
int g_record[moon::kComputers] = {};

} // namespace

extern "C" void fsw_configure(int defense) {
    g_defense = static_cast<moon::Defense>(defense);
}

extern "C" void fsw_tick(const moon::Sensors* sensors, moon::EngineCommand* command, moon::FswStatus* status) {
    *status = {};
    if (g_defense == moon::Defense::None) {
        kCoreFns[0](sensors, &g_states[0], command);
        status->computers = 1;
        return;
    }

    moon::EngineCommand cores[moon::kCores];
    for (int i = 0; i < moon::kCores; ++i) {
        kCoreFns[i](sensors, &g_states[i], &cores[i]);
    }
    const moon::Selection sel = moon::select_command(cores, g_record);
    for (int i = 0; i < moon::kComputers; ++i) {
        g_record[i] = sel.trusted[i] ? g_record[i] + 1 : 0;
        if (sel.trusted[i]) status->computers |= 1u << i;
    }
    const moon::GuidanceState chosen = g_states[2 * sel.computer];
    for (moon::GuidanceState& state : g_states) {
        state = chosen;
    }
    *command = cores[2 * sel.computer];
    status->corrected = sel.corrected;
    status->detected = sel.detected;
}
