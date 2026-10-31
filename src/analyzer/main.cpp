#include "align.hpp"
#include "audit.hpp"
#include "classify.hpp"
#include "evidence.hpp"
#include "ops_map.hpp"
#include "trace_reader.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr <<
        "usage: sdc-analyze --clean <trace.sdct> --injected <trace.sdct>\n"
        "                    [--ops <ops.json>] [--out|-o <evidence.json>]\n"
        "                    [--abs-tol <double>] [--rel-tol <double>]\n"
        "                    [--reference-bits <hex>]\n";
}

bool parse_double(const char* text, double& out) {
    try {
        out = std::stod(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_u64_hex(const char* text, std::uint64_t& out) {
    if (!text) return false;
    std::string s(text);
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    if (s.empty()) return false;
    try {
        out = std::stoull(s, nullptr, 16);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string clean_path;
    std::string injected_path;
    std::string ops_path;
    std::string out_path;
    double abs_tol = 0.0;
    double rel_tol = 0.0;
    bool have_reference = false;
    std::uint64_t reference_bits = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "sdc-analyze: " << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--clean") clean_path = next("--clean");
        else if (arg == "--injected") injected_path = next("--injected");
        else if (arg == "--ops") ops_path = next("--ops");
        else if (arg == "--out" || arg == "-o") out_path = next(arg.c_str());
        else if (arg == "--abs-tol") {
            if (!parse_double(next("--abs-tol"), abs_tol)) { usage(); return 2; }
        } else if (arg == "--rel-tol") {
            if (!parse_double(next("--rel-tol"), rel_tol)) { usage(); return 2; }
        } else if (arg == "--reference-bits") {
            if (!parse_u64_hex(next("--reference-bits"), reference_bits)) { usage(); return 2; }
            have_reference = true;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::cerr << "sdc-analyze: unknown argument '" << arg << "'\n";
            usage();
            return 2;
        }
    }

    if (clean_path.empty() || injected_path.empty()) {
        usage();
        return 2;
    }

    sdc::TraceReader reader;
    sdc::Trace clean = reader.read(clean_path);
    if (!reader.error().empty()) {
        std::cerr << "sdc-analyze: clean trace: " << reader.error() << "\n";
        return 1;
    }
    sdc::Trace injected = reader.read(injected_path);
    if (!reader.error().empty()) {
        std::cerr << "sdc-analyze: injected trace: " << reader.error() << "\n";
        return 1;
    }

    std::map<sdc::OpId, sdc::OpInfo> ops;
    if (!ops_path.empty()) {
        std::string error;
        if (!sdc::load_ops_map(ops_path, ops, error)) {
            std::cerr << "sdc-analyze: " << error << "\n";
            return 1;
        }
    }

    sdc::AlignmentResult alignment = sdc::align_traces(clean, injected);

    // Audit the first diverging event: recompute it from its operands and see
    // whether the recorded result is self-consistent.
    bool arithmetic_inconsistent = false;
    if (alignment.diverged && alignment.injected_index < injected.events.size()) {
        auto it = ops.find(injected.events[alignment.injected_index].op_id);
        if (it != ops.end()) {
            sdc::AuditResult audit = sdc::audit_event(injected.events[alignment.injected_index], it->second.op_kind);
            arithmetic_inconsistent = audit.computable && !audit.consistent;
        }
    }

    // Determine whether the final result is inside the acceptance criterion.
    //
    // The reference is the explicit --reference-bits value when given, and
    // otherwise the clean run's own final result. The clean run is the
    // baseline the whole platform is built around, so comparing the injected
    // run against it is the natural default acceptance criterion.
    if (!have_reference && clean.has_exit) {
        reference_bits = clean.exit_record.final_result_bits;
        have_reference = true;
    }

    bool outside_tolerance = false;
    if (have_reference) {
        const std::uint64_t actual = injected.exit_record.final_result_bits;
        if (actual != reference_bits) {
            const double a = std::bit_cast<double>(actual);
            const double b = std::bit_cast<double>(reference_bits);
            const double diff = std::fabs(a - b);
            const double denom = std::fabs(b);
            const bool within_abs = diff <= abs_tol;
            const bool within_rel = denom > 0.0 && (diff / denom) <= rel_tol;
            outside_tolerance = !(within_abs || within_rel);
        }
    }

    sdc::VerdictInput vi;
    vi.clean_has_exit = clean.has_exit;
    vi.injected_has_exit = injected.has_exit;
    vi.injected_exit_code = injected.exit_record.exit_code;
    vi.injected_detected = injected.exit_record.detected_by_workload;
    vi.corrected_by_mechanism = injected.exit_record.corrected_by_mechanism;
    vi.final_result_outside_tolerance = outside_tolerance;
    vi.divergence_found = alignment.diverged;
    vi.arithmetic_inconsistent = arithmetic_inconsistent;
    vi.evidence_sufficient = true;

    sdc::Verdict verdict = sdc::classify(vi);

    sdc::EvidenceInput ei{
        clean,
        injected,
        ops,
        alignment,
        verdict,
        outside_tolerance,
        vi.corrected_by_mechanism,
        5,
    };

    const std::string json = sdc::build_evidence(ei);

    if (out_path.empty()) {
        std::cout << json;
    } else {
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            std::cerr << "sdc-analyze: cannot write " << out_path << "\n";
            return 1;
        }
        out << json;
    }

    std::cerr << "classification: " << sdc::classification_name(verdict.classification) << "\n";
    return 0;
}
