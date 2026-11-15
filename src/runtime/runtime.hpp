#pragma once

#include "sdc/types.hpp"
#include <cstdint>

namespace sdc {

void runtime_init();
void runtime_fini();

double runtime_f64_add(OpId op_id, double lhs, double rhs);
double runtime_f64_sub(OpId op_id, double lhs, double rhs);
double runtime_f64_mul(OpId op_id, double lhs, double rhs);
double runtime_f64_div(OpId op_id, double lhs, double rhs);
double runtime_f64_sqrt(OpId op_id, double x);

} // namespace sdc

#define SDC_F64_BINARY_OP(suffix, kind, enumerator)                            \
    extern "C" double sdc_runtime_f64_##suffix##_hook(std::uint32_t op_id,     \
                                                      double lhs, double rhs);
#define SDC_F64_UNARY_OP(suffix, kind, enumerator)                             \
    extern "C" double sdc_runtime_f64_##suffix##_hook(std::uint32_t op_id,     \
                                                      double        x);
#include "sdc/op_table.def"
#undef SDC_F64_BINARY_OP
#undef SDC_F64_UNARY_OP

// Reporting API. A workload calls these to tell the runtime what its own
// verdict was, so the analyzer can classify the outcome.
extern "C" void sdc_report_result(double value);
extern "C" void sdc_report_detected();
extern "C" void sdc_report_corrected();
