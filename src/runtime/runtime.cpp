#include "runtime.hpp"
#include "trace_writer.hpp"
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <string>
#include <bit>

namespace sdc {

static TraceWriter g_writer;
static std::unordered_map<OpId, Occurrence> g_count;
static bool g_inited = false;
static OpId g_inj_op = 0;
static Occurrence g_inj_occ = 0;
static BitPos g_inj_bit = 0;
static bool g_inj_enabled = false;

static std::string get_env(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

void runtime_init() {
    if (g_inited) return;
    g_inited = true;
    
    std::string path = get_env("SDC_TRACE_PATH");
    if (path.empty()) path = "trace.sdct";
    
    std::string inj_op = get_env("SDC_INJECT_OP_ID");
    std::string inj_occ = get_env("SDC_INJECT_OCC");
    std::string inj_bit = get_env("SDC_INJECT_BIT");
    if (!inj_op.empty() && !inj_occ.empty() && !inj_bit.empty()) {
        g_inj_op = static_cast<OpId>(std::stoul(inj_op));
        g_inj_occ = static_cast<Occurrence>(std::stoull(inj_occ));
        g_inj_bit = static_cast<BitPos>(std::stoul(inj_bit));
        g_inj_enabled = true;
    }
    
    std::string meta = "{\"workload\":\"manual\",\"build_id\":\"phase2\",\"compiler_flags\":\"manual\",\"schema\":1,\"inject\":{";
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
    er.detected_by_workload = false;
    er.final_result_bits = 0;
    g_writer.write_exit(er);
    g_writer.close();
    g_inited = false;
}

double runtime_f64_add(OpId op_id, double lhs, double rhs) {
    if (!g_inited) runtime_init();
    
    double res = lhs + rhs;
    Occurrence occ = ++g_count[op_id];
    
    uint64_t lb = std::bit_cast<uint64_t>(lhs);
    uint64_t rb = std::bit_cast<uint64_t>(rhs);
    uint64_t rbits = std::bit_cast<uint64_t>(res);
    
    if (g_inj_enabled && op_id == g_inj_op && occ == g_inj_occ) {
        if (g_inj_bit < 64) {
            rbits ^= (uint64_t)1ULL << g_inj_bit;
            res = std::bit_cast<double>(rbits);
        }
    }
    
    Event ev;
    ev.op_id = op_id;
    ev.occurrence = occ;
    ev.operand_bits[0] = lb;
    ev.operand_bits[1] = rb;
    ev.result_bits = std::bit_cast<uint64_t>(res);
    g_writer.write_event(ev);
    
    return res;
}

double runtime_f64_mul(OpId op_id, double lhs, double rhs) {
    if (!g_inited) runtime_init();
    
    double res = lhs * rhs;
    Occurrence occ = ++g_count[op_id];
    
    uint64_t lb = std::bit_cast<uint64_t>(lhs);
    uint64_t rb = std::bit_cast<uint64_t>(rhs);
    uint64_t rbits = std::bit_cast<uint64_t>(res);
    
    if (g_inj_enabled && op_id == g_inj_op && occ == g_inj_occ) {
        if (g_inj_bit < 64) {
            rbits ^= (uint64_t)1ULL << g_inj_bit;
            res = std::bit_cast<double>(rbits);
        }
    }
    
    Event ev;
    ev.op_id = op_id;
    ev.occurrence = occ;
    ev.operand_bits[0] = lb;
    ev.operand_bits[1] = rb;
    ev.result_bits = std::bit_cast<uint64_t>(res);
    g_writer.write_event(ev);
    
    return res;
}

} // namespace sdc

extern "C" double sdc_runtime_f64_add_hook(uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_add(static_cast<sdc::OpId>(op_id), lhs, rhs);
}

extern "C" double sdc_runtime_f64_mul_hook(uint32_t op_id, double lhs, double rhs) {
    return sdc::runtime_f64_mul(static_cast<sdc::OpId>(op_id), lhs, rhs);
}
