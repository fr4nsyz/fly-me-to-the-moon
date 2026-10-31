#include "runtime.hpp"
#include "trace_writer.hpp"
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <string>
#include <sstream>
#include <bit>

namespace sdc {

static TraceWriter g_writer;
static std::unordered_map<OpId, Occurrence> g_count;
static bool g_inited = false;
static std::string inj_op;
static std::string inj_occ;
static std::string inj_bit;
static OpId g_inj_op = 0;
static Occurrence g_inj_occ = 0;
static BitPos g_inj_bit = 0;
static bool g_inj_enabled = false;
static std::uint64_t g_final_result_bits = 0;
static bool g_result_reported = false;
static bool g_detected = false;
static bool g_corrected = false;

static std::string get_env(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

void runtime_init() {
    if (g_inited) return;
    g_inited = true;

    // The trace path. SDC_TRACE is the documented interface; SDC_TRACE_PATH is
    // accepted as an alias because it reads more clearly in scripts.
    std::string path = get_env("SDC_TRACE");
    if (path.empty()) path = get_env("SDC_TRACE_PATH");
    if (path.empty()) path = "trace.sdct";

    // The injection configuration. SDC_INJECT is the documented interface and
    // carries all three fields in one value: "op=17,occ=3,bit=42".
    // The individual variables are accepted as an alternative.
    std::string inj = get_env("SDC_INJECT");
    if (!inj.empty()) {
        std::string op, occ, bit;
        std::stringstream ss(inj);
        std::string part;
        while (std::getline(ss, part, ',')) {
            const std::size_t eq = part.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = part.substr(0, eq);
            const std::string value = part.substr(eq + 1);
            if (key == "op") op = value;
            else if (key == "occ") occ = value;
            else if (key == "bit") bit = value;
        }
        inj_op = op;
        inj_occ = occ;
        inj_bit = bit;
    } else {
        inj_op = get_env("SDC_INJECT_OP_ID");
        inj_occ = get_env("SDC_INJECT_OCC");
        inj_bit = get_env("SDC_INJECT_BIT");
    }

    if (!inj_op.empty() && !inj_occ.empty() && !inj_bit.empty()) {
        g_inj_op = static_cast<OpId>(std::stoul(inj_op));
        g_inj_occ = static_cast<Occurrence>(std::stoull(inj_occ));
        g_inj_bit = static_cast<BitPos>(std::stoul(inj_bit));
        g_inj_enabled = true;
    }

    std::string workload = get_env("SDC_WORKLOAD");
    if (workload.empty()) workload = "workload";
    std::string build_id = get_env("SDC_BUILD_ID");
    if (build_id.empty()) build_id = "unknown";
    std::string flags = get_env("SDC_COMPILER_FLAGS");
    if (flags.empty()) flags = "unknown";

    std::string meta = "{\"workload\":\"" + workload + "\",\"build_id\":\"" + build_id +
                       "\",\"compiler_flags\":\"" + flags + "\",\"schema\":1,\"inject\":{";
    if (g_inj_enabled) {
        meta += "\"enabled\":true,\"op_id\":" + inj_op + ",\"occurrence\":" + inj_occ + ",\"bit\":" + inj_bit;
    } else {
        meta += "\"enabled\":false";
    }
    meta += "}}";

    g_writer.open(path, meta);
    std::atexit(runtime_fini);
}

void runtime_fini() {
    if (!g_inited) return;
    ExitRecord er;
    er.exit_code = 0;
    er.detected_by_workload = g_detected;
    er.corrected_by_mechanism = g_corrected;
    er.final_result_bits = g_final_result_bits;
    g_writer.write_exit(er);
    g_writer.close();
    g_inited = false;
}

static double apply_binary(OpId op_id, double lhs, double rhs, double computed) {
    if (!g_inited) runtime_init();

    Occurrence occ = ++g_count[op_id];

    std::uint64_t lb = std::bit_cast<std::uint64_t>(lhs);
    std::uint64_t rb = std::bit_cast<std::uint64_t>(rhs);
    std::uint64_t rbits = std::bit_cast<std::uint64_t>(computed);

    if (g_inj_enabled && op_id == g_inj_op && occ == g_inj_occ) {
        if (g_inj_bit < 64) {
            rbits ^= (std::uint64_t)1ULL << g_inj_bit;
            computed = std::bit_cast<double>(rbits);
        }
    }

    const std::uint64_t delivered = std::bit_cast<std::uint64_t>(computed);

    Event ev;
    ev.op_id = op_id;
    ev.occurrence = occ;
    ev.operand_bits[0] = lb;
    ev.operand_bits[1] = rb;
    ev.result_bits = delivered;
    g_writer.write_event(ev);

    // The last delivered result is the default final result. A workload that
    // knows better can override it with sdc_report_result, but a plain workload
    // with no annotations still produces a meaningful exit record.
    if (!g_result_reported) {
        g_final_result_bits = delivered;
    }

    return computed;
}

double runtime_f64_add(OpId op_id, double lhs, double rhs) {
    return apply_binary(op_id, lhs, rhs, lhs + rhs);
}

double runtime_f64_sub(OpId op_id, double lhs, double rhs) {
    return apply_binary(op_id, lhs, rhs, lhs - rhs);
}

double runtime_f64_mul(OpId op_id, double lhs, double rhs) {
    return apply_binary(op_id, lhs, rhs, lhs * rhs);
}

double runtime_f64_div(OpId op_id, double lhs, double rhs) {
    return apply_binary(op_id, lhs, rhs, lhs / rhs);
}

} // namespace sdc

extern "C" double sdc_runtime_f64_add_hook(std::uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_add(static_cast<sdc::OpId>(op_id), lhs, rhs);
}

extern "C" double sdc_runtime_f64_sub_hook(std::uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_sub(static_cast<sdc::OpId>(op_id), lhs, rhs);
}

extern "C" double sdc_runtime_f64_mul_hook(std::uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_mul(static_cast<sdc::OpId>(op_id), lhs, rhs);
}

extern "C" double sdc_runtime_f64_div_hook(std::uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_div(static_cast<sdc::OpId>(op_id), lhs, rhs);
}

extern "C" void sdc_report_result(double value) {
    if (!sdc::g_inited) sdc::runtime_init();
    sdc::g_final_result_bits = std::bit_cast<std::uint64_t>(value);
    sdc::g_result_reported = true;
}

extern "C" void sdc_report_detected() {
    if (!sdc::g_inited) sdc::runtime_init();
    sdc::g_detected = true;
}

extern "C" void sdc_report_corrected() {
    if (!sdc::g_inited) sdc::runtime_init();
    sdc::g_corrected = true;
}
