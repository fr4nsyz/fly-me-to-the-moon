#include "evidence.hpp"
#include "audit.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sdc {

namespace {

std::string hex64(std::uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

Json event_to_json(const Event& e, const std::map<OpId, OpInfo>& ops) {
    Json j = Json::object();
    j.set("op_id", Json(static_cast<double>(e.op_id)));
    j.set("occurrence", Json(static_cast<double>(e.occurrence)));
    j.set("operand0", Json(hex64(e.operand_bits[0])));
    j.set("operand1", Json(hex64(e.operand_bits[1])));
    j.set("result", Json(hex64(e.result_bits)));

    auto it = ops.find(e.op_id);
    if (it != ops.end()) {
        Json src = Json::object();
        src.set("file", Json(it->second.file));
        src.set("line", Json(static_cast<double>(it->second.line)));
        src.set("column", Json(static_cast<double>(it->second.column)));
        src.set("function", Json(it->second.function));
        src.set("kind", Json(it->second.kind));
        j.set("source", std::move(src));

        AuditResult audit = audit_event(e, it->second.op_kind);
        if (audit.computable) {
            Json a = Json::object();
            a.set("recomputed", Json(hex64(audit.recomputed_bits)));
            a.set("consistent", Json(audit.consistent));
            j.set("audit", std::move(a));
        }
    }
    return j;
}

Json exit_to_json(const ExitRecord& e) {
    Json j = Json::object();
    j.set("exit_code", Json(static_cast<double>(e.exit_code)));
    j.set("detected_by_workload", Json(e.detected_by_workload));
    j.set("corrected_by_mechanism", Json(e.corrected_by_mechanism));
    j.set("final_result", Json(hex64(e.final_result_bits)));
    return j;
}

} // namespace

std::string build_evidence(const EvidenceInput& input) {
    Json root = Json::object();
    root.set("schema", Json(1.0));
    root.set("classification", Json(std::string(classification_name(input.verdict.classification))));

    Json reasons = Json::array();
    for (const auto& r : input.verdict.reasons) reasons.push_back(Json(r));
    root.set("reasons", std::move(reasons));

    root.set("final_result_outside_tolerance", Json(input.final_result_outside_tolerance));
    root.set("corrected_by_mechanism", Json(input.corrected_by_mechanism));

    // Alignment summary.
    Json align = Json::object();
    align.set("diverged", Json(input.alignment.diverged));
    align.set("matched_events", Json(static_cast<double>(input.alignment.matched)));
    align.set("clean_event_count", Json(static_cast<double>(input.clean.events.size())));
    align.set("injected_event_count", Json(static_cast<double>(input.injected.events.size())));
    root.set("alignment", std::move(align));

    // The injected event: the first place the two runs disagree.
    Json injected_event = Json::object();
    if (input.alignment.diverged && input.alignment.injected_index < input.injected.events.size()) {
        const Event& e = input.injected.events[input.alignment.injected_index];
        injected_event = event_to_json(e, input.ops);
        injected_event.set("event_index", Json(static_cast<double>(input.alignment.injected_index)));
    }
    root.set("injected_event", std::move(injected_event));

    // The clean counterpart at the same index, for comparison.
    Json clean_event = Json::object();
    if (input.alignment.clean_index < input.clean.events.size()) {
        clean_event = event_to_json(input.clean.events[input.alignment.clean_index], input.ops);
        clean_event.set("event_index", Json(static_cast<double>(input.alignment.clean_index)));
    }
    root.set("clean_event", std::move(clean_event));

    // A bounded window around the divergence from both streams.
    Json window = Json::array();
    if (input.alignment.diverged) {
        const std::size_t w = input.window;
        const std::size_t inj_start = input.alignment.injected_index >= w ? input.alignment.injected_index - w : 0;
        const std::size_t inj_end = std::min(input.injected.events.size(), input.alignment.injected_index + w + 1);
        for (std::size_t i = inj_start; i < inj_end; ++i) {
            Json entry = Json::object();
            entry.set("source", Json(std::string("injected")));
            entry.set("event_index", Json(static_cast<double>(i)));
            entry.set("event", event_to_json(input.injected.events[i], input.ops));
            window.push_back(std::move(entry));
        }
    }
    root.set("window", std::move(window));

    root.set("clean_exit", exit_to_json(input.clean.exit_record));
    root.set("injected_exit", exit_to_json(input.injected.exit_record));

    // The injection configuration, as recorded by the runtime.
    Json inject = Json::object();
    {
        Json meta;
        std::string parse_error;
        if (Json::parse(input.injected.metadata_json, meta, parse_error) && meta.is_object()) {
            if (const Json* j = meta.find("inject")) {
                if (j->is_object()) inject = *j;
            }
        }
    }
    root.set("inject", std::move(inject));

    return root.dump();
}

} // namespace sdc
