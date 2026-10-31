#pragma once

#include <cstdint>
#include "sdc/types.hpp"

namespace sdc {

void runtime_init();
void runtime_fini();

double runtime_f64_add(OpId op_id, double lhs, double rhs);

} // namespace sdc
