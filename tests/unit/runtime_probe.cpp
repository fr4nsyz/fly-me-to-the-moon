#include "runtime.hpp"

int main() {
    for (int round = 0; round < 3; ++round) {
        for (std::uint32_t op = 1; op <= 3; ++op) {
            sdc_runtime_f64_add_hook(op, 1.0, 1.0);
        }
    }
}
