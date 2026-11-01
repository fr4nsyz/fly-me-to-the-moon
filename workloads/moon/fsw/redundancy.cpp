#include "redundancy.hpp"
#include <bit>
#include <cstdint>

namespace moon {

namespace {

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

int longest_record(const bool (&eligible)[kComputers], const int (&record)[kComputers]) {
    int best = -1;
    for (int i = 0; i < kComputers; ++i) {
        if (eligible[i] && (best < 0 || record[i] > record[best])) {
            best = i;
        }
    }
    return best;
}

} // namespace

bool same_command(const EngineCommand& a, const EngineCommand& b) {
    return same_bits(a.throttle, b.throttle) && same_bits(a.direction.x, b.direction.x) &&
           same_bits(a.direction.y, b.direction.y) && same_bits(a.direction.z, b.direction.z);
}

Selection select_command(const EngineCommand (&cores)[kCores], const int (&record)[kComputers]) {
    bool healthy[kComputers];
    int healthy_count = 0;
    for (int i = 0; i < kComputers; ++i) {
        healthy[i] = same_command(cores[2 * i], cores[2 * i + 1]);
        healthy_count += healthy[i] ? 1 : 0;
    }

    Selection s;
    if (healthy_count == 0) {
        const bool all[kComputers] = {true, true, true};
        s.computer = longest_record(all, record);
        s.detected = true;
        return s;
    }

    s.computer = -1;
    for (int i = 0; i < kComputers && s.computer < 0; ++i) {
        if (!healthy[i]) continue;
        int agreeing = 0;
        for (int j = 0; j < kComputers; ++j) {
            agreeing += (healthy[j] && same_command(cores[2 * i], cores[2 * j])) ? 1 : 0;
        }
        if (2 * agreeing > healthy_count) s.computer = i;
    }
    if (s.computer < 0) s.computer = longest_record(healthy, record);

    for (int i = 0; i < kComputers; ++i) {
        s.trusted[i] = healthy[i] && same_command(cores[2 * i], cores[2 * s.computer]);
        s.corrected = s.corrected || !s.trusted[i];
    }
    return s;
}

} // namespace moon
