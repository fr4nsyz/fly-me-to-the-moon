#include "align.hpp"
#include "audit.hpp"
#include "classify.hpp"
#include "ops_map.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace sdc;

namespace {

Event make_event(OpId op_id, Occurrence occ, std::uint64_t a, std::uint64_t b, std::uint64_t r) {
    Event e;
    e.op_id = op_id;
    e.occurrence = occ;
    e.operand_bits[0] = a;
    e.operand_bits[1] = b;
    e.result_bits = r;
    return e;
}

void test_align_identical() {
    Trace clean;
    Trace injected;
    for (int i = 0; i < 10; ++i) {
        Event e = make_event(1, i + 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
        clean.events.push_back(e);
        injected.events.push_back(e);
    }
    AlignmentResult r = align_traces(clean, injected);
    assert(!r.diverged);
    assert(r.matched == 10);
}

void test_align_detects_divergence() {
    Trace clean;
    Trace injected;
    for (int i = 0; i < 10; ++i) {
        Event e = make_event(1, i + 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
        clean.events.push_back(e);
        injected.events.push_back(e);
    }
    // Corrupt the result of the 5th event.
    injected.events[4].result_bits ^= 1ULL;

    AlignmentResult r = align_traces(clean, injected);
    assert(r.diverged);
    assert(r.clean_index == 4);
    assert(r.injected_index == 4);
    assert(r.matched == 4);
}

void test_align_reports_structural_change_as_divergence() {
    Trace clean;
    Trace injected;
    for (int i = 0; i < 10; ++i) {
        Event e = make_event(1, i + 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
        clean.events.push_back(e);
        injected.events.push_back(e);
    }
    // The injected run executes an extra operation, shifting every later key.
    Event extra = make_event(2, 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
    injected.events.insert(injected.events.begin() + 5, extra);

    AlignmentResult r = align_traces(clean, injected);
    assert(r.diverged);
    assert(r.clean_index == 5);
    assert(r.injected_index == 5);
    assert(r.matched == 5);
}

void test_align_reports_injection_before_control_flow_change() {
    Trace clean;
    Trace injected;
    for (int i = 0; i < 10; ++i) {
        Event e = make_event(1, i + 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
        clean.events.push_back(e);
        if (i >= 5 && i <= 7) continue;
        if (i == 4) e.result_bits ^= 1ULL << 40;
        injected.events.push_back(e);
    }

    AlignmentResult r = align_traces(clean, injected);
    assert(r.diverged);
    assert(r.clean_index == 4);
    assert(r.injected_index == 4);
    assert(r.matched == 4);
}

void test_audit_detects_corruption() {
    // 2.0 + 3.0 == 5.0
    Event good = make_event(1, 1, 0x4000000000000000ULL, 0x4008000000000000ULL, 0x4014000000000000ULL);
    AuditResult ok = audit_event(good, OpKind::FAdd);
    assert(ok.computable);
    assert(ok.consistent);

    Event bad = good;
    bad.result_bits ^= 1ULL;
    AuditResult corrupted = audit_event(bad, OpKind::FAdd);
    assert(corrupted.computable);
    assert(!corrupted.consistent);
}

void test_classify_precedence() {
    VerdictInput vi;
    vi.evidence_sufficient = true;
    vi.clean_has_exit = true;
    vi.injected_has_exit = true;

    // Crash wins over everything else.
    vi.injected_has_exit = false;
    assert(classify(vi).classification == Classification::CRASH);
    vi.injected_has_exit = true;

    // Corrected wins over detected, because a repair is the stronger result.
    vi.corrected_by_mechanism = true;
    vi.injected_detected = true;
    vi.final_result_outside_tolerance = false;
    assert(classify(vi).classification == Classification::CORRECTED);

    // A repair that did not land inside tolerance is SDC, not CORRECTED.
    vi.final_result_outside_tolerance = true;
    assert(classify(vi).classification == Classification::SDC);
    vi.corrected_by_mechanism = false;

    // Detection without a repair.
    vi.injected_detected = true;
    vi.final_result_outside_tolerance = false;
    assert(classify(vi).classification == Classification::DETECTED);

    // Nothing noticed, result outside tolerance.
    vi.injected_detected = false;
    vi.final_result_outside_tolerance = true;
    assert(classify(vi).classification == Classification::SDC);

    // Nothing noticed, result inside tolerance.
    vi.final_result_outside_tolerance = false;
    assert(classify(vi).classification == Classification::MASKED);

    // Insufficient evidence.
    vi.evidence_sufficient = false;
    assert(classify(vi).classification == Classification::INCONCLUSIVE);
}

void test_classification_names() {
    assert(std::string(classification_name(Classification::CRASH)) == "CRASH");
    assert(std::string(classification_name(Classification::CORRECTED)) == "CORRECTED");
    assert(std::string(classification_name(Classification::DETECTED)) == "DETECTED");
    assert(std::string(classification_name(Classification::SDC)) == "SDC");
    assert(std::string(classification_name(Classification::MASKED)) == "MASKED");
    assert(std::string(classification_name(Classification::INCONCLUSIVE)) == "INCONCLUSIVE");
}

} // namespace

int main() {
    test_align_identical();
    test_align_detects_divergence();
    test_align_reports_structural_change_as_divergence();
    test_align_reports_injection_before_control_flow_change();
    test_audit_detects_corruption();
    test_classify_precedence();
    test_classification_names();
    std::cout << "ok" << std::endl;
    return 0;
}
