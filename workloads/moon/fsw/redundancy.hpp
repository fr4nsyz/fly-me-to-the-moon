#pragma once

#include "interface.hpp"

namespace moon {

inline constexpr int kComputers = 3;
inline constexpr int kCores = 2 * kComputers; // computer i runs cores 2i and 2i+1

struct Selection {
    int computer = 0;               // whose command flies
    bool trusted[kComputers] = {};  // healthy and agreed with the chosen command
    bool corrected = false;         // a computer was silenced or outvoted
    bool detected = false;          // no computer passed its lockstep check
};

bool same_command(const EngineCommand& a, const EngineCommand& b);
Selection select_command(const EngineCommand (&cores)[kCores], const int (&record)[kComputers]);

} // namespace moon
