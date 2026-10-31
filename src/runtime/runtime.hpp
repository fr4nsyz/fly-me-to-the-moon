#pragma once

#include <cstdint>
#include "sdc/types.hpp"

namespace sdc {

void runtime_init();
void runtime_fini();

double runtime_f64_add(OpId op_id, double lhs, double rhs);

} // namespace sdc

extern "C" double sdc_runtime_f64_add_hook(uint32_t op_id, double lhs, double rhs);

double runtime_f64_mul(sdc::OpId op_id, double lhs, double rhs);
extern "C" double sdc_runtime_f64_mul_hook(uint32_t op_id, double lhs, double rhs);
namespace sdc {
double runtime_f64_mul(sdc::OpId op_id, double lhs, double rhs);
}
