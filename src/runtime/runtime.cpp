#include "runtime.hpp"
#include "trace_writer.hpp"
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <vector>
#include <bit>
#include <cmath>

namespace sdc {

static TraceWriter g_writer;
static std::unordered_map<OpId, Occurrence> g_count;
static bool g_inited = false;
static OpId g_inj_op = 0;
static Occurrence g_inj_occ = 0;
static BitPos g_inj_bit = 0;
static bool g_inj_enabled = false;
static bool g_inj_persistent = false;

// Random injection mode. The driver supplies a plan of candidate operations
// with the number of times each one actually executes, and the runtime picks
// one of them with std::rand seeded from SDC_SEED.
enum class InjectMode { Manual, Random };

struct InjectPlanEntry {
    OpId       op_id;
    Occurrence max_occurrence;
};

static InjectMode g_inj_mode = InjectMode::Manual;
static std::vector<InjectPlanEntry> g_inj_plan;
static unsigned g_inj_seed = 0;
static int g_inj_bit_lo = 0;
static int g_inj_bit_hi = 63;
static std::uint64_t g_final_result_bits = 0;
static bool g_result_reported = false;
static bool g_detected = false;
static bool g_corrected = false;

static std::string get_env(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}


// The injection plan is a text file, one candidate per line:
//   <op_id> <max_occurrence>
// Lines starting with '#' are comments. It is produced by the campaign driver
// from a clean run, so every listed operation is known to execute.
static void load_inject_plan(const std::string& path) {
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "sdc: cannot open inject plan '%s'\n", path.c_str());
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        OpId op_id = 0;
        Occurrence max_occ = 0;
        if (!(ss >> op_id >> max_occ)) continue;
        if (max_occ == 0) continue;
        g_inj_plan.push_back({op_id, max_occ});
    }
}

// Pick one injection site from the plan with std::rand.
//
// The seed alone determines the choice, so the same seed always produces the
// same injection. The resolved triple is written into the trace metadata, so
// the campaign is auditable even if the C library's rand sequence differs
// between platforms.
static void resolve_random_injection() {
    if (g_inj_plan.empty()) {
        std::fprintf(stderr, "sdc: random injection requested but the plan is empty\n");
        return;
    }

    std::srand(g_inj_seed);

    const std::size_t plan_index = static_cast<std::size_t>(std::rand()) % g_inj_plan.size();
    const InjectPlanEntry& entry = g_inj_plan[plan_index];

    g_inj_op = entry.op_id;
    g_inj_occ = 1 + static_cast<Occurrence>(std::rand()) % entry.max_occurrence;
    g_inj_bit = static_cast<BitPos>(g_inj_bit_lo + std::rand() % (g_inj_bit_hi - g_inj_bit_lo + 1));
    g_inj_enabled = true;
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
    // SDC_INJECT=random switches to random selection.
    // The individual variables are accepted as an alternative.
    const std::string inj = get_env("SDC_INJECT");

    if (inj == "random" || inj == "rand") {
        g_inj_mode = InjectMode::Random;
    } else {
        std::string op, occ, bit;
        if (!inj.empty()) {
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
        } else {
            op = get_env("SDC_INJECT_OP_ID");
            occ = get_env("SDC_INJECT_OCC");
            bit = get_env("SDC_INJECT_BIT");
        }

        if (!op.empty() && !occ.empty() && !bit.empty()) {
            g_inj_op = static_cast<OpId>(std::stoul(op));
            g_inj_occ = static_cast<Occurrence>(std::stoull(occ));
            g_inj_bit = static_cast<BitPos>(std::stoul(bit));
            g_inj_enabled = true;
        }
    }

    g_inj_persistent = get_env("SDC_INJECT_PERSIST") == "1";

    if (g_inj_mode == InjectMode::Random) {
        const std::string seed_text = get_env("SDC_SEED");
        g_inj_seed = seed_text.empty() ? 0u : static_cast<unsigned>(std::stoul(seed_text));

        const std::string range = get_env("SDC_INJECT_BIT_RANGE");
        if (!range.empty()) {
            const std::size_t dash = range.find('-');
            if (dash != std::string::npos) {
                g_inj_bit_lo = std::stoi(range.substr(0, dash));
                g_inj_bit_hi = std::stoi(range.substr(dash + 1));
            }
        }
        if (g_inj_bit_lo < 0) g_inj_bit_lo = 0;
        if (g_inj_bit_hi > 63) g_inj_bit_hi = 63;
        if (g_inj_bit_hi < g_inj_bit_lo) g_inj_bit_hi = g_inj_bit_lo;

        load_inject_plan(get_env("SDC_INJECT_PLAN"));
        resolve_random_injection();
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
        meta += "\"enabled\":true,\"op_id\":" + std::to_string(g_inj_op) +
                ",\"occurrence\":" + std::to_string(g_inj_occ) +
                ",\"bit\":" + std::to_string(static_cast<unsigned>(g_inj_bit));
        if (g_inj_mode == InjectMode::Random) {
            meta += ",\"mode\":\"random\",\"seed\":" + std::to_string(g_inj_seed);
        }
        if (g_inj_persistent) {
            meta += ",\"persistent\":true";
        }
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

    if (g_inj_enabled && op_id == g_inj_op && (occ == g_inj_occ || (g_inj_persistent && occ > g_inj_occ))) {
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

double runtime_f64_sqrt(OpId op_id, double x) {
    return apply_binary(op_id, x, 0.0, std::sqrt(x));
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

extern "C" double sdc_runtime_f64_sqrt_hook(std::uint32_t op_id, double x) {
    return sdc::runtime_f64_sqrt(static_cast<sdc::OpId>(op_id), x);
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
