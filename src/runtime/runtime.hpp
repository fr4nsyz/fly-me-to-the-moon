#pragma once

#include <cstdint>
#include "sdc/types.hpp"

namespace sdc {

void runtime_init();
void runtime_fini();

double runtime_f64_add(OpId op_id, double lhs, double rhs);
double runtime_f64_sub(OpId op_id, double lhs, double rhs);
double runtime_f64_mul(OpId op_id, double lhs, double rhs);
double runtime_f64_div(OpId op_id, double lhs, double rhs);

} // namespace sdc

extern "C" double sdc_runtime_f64_add_hook(std::uint32_t op_id, double lhs, double rhs);
extern "C" double sdc_runtime_f64_sub_hook(std::uint32_t op_id, double lhs, double rhs);
extern "C" double sdc_runtime_f64_mul_hook(std::uint32_t op_id, double lhs, double rhs);
extern "C" double sdc_runtime_f64_div_hook(std::uint32_t op_id, double lhs, double rhs);

// Reporting API. A workload calls these to tell the runtime what its own
// verdict was, so the analyzer can classify the outcome.
extern "C" void sdc_report_result(double value);
extern "C" void sdc_report_detected();
extern "C" void sdc_report_corrected();
