#include "align.hpp"
#include "audit.hpp"
#include "classify.hpp"
#include "ops_map.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

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

TEST(Align, Identical) {
    Trace clean;
    Trace injected;
    for (int i = 0; i < 10; ++i) {
        Event e = make_event(1, i + 1, 0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x4000000000000000ULL);
        clean.events.push_back(e);
        injected.events.push_back(e);
    }
    AlignmentResult r = align_traces(clean, injected);
    EXPECT_FALSE(r.diverged);
    EXPECT_EQ(r.matched, 10u);
}

TEST(Align, DetectsDivergence) {
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
    EXPECT_TRUE(r.diverged);
    EXPECT_EQ(r.clean_index, 4u);
    EXPECT_EQ(r.injected_index, 4u);
    EXPECT_EQ(r.matched, 4u);
}

TEST(Align, ReportsStructuralChangeAsDivergence) {
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
    EXPECT_TRUE(r.diverged);
    EXPECT_EQ(r.clean_index, 5u);
    EXPECT_EQ(r.injected_index, 5u);
    EXPECT_EQ(r.matched, 5u);
}

TEST(Align, ReportsInjectionBeforeControlFlowChange) {
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
    EXPECT_TRUE(r.diverged);
    EXPECT_EQ(r.clean_index, 4u);
    EXPECT_EQ(r.injected_index, 4u);
    EXPECT_EQ(r.matched, 4u);
}

TEST(Audit, DetectsCorruption) {
    // 2.0 + 3.0 == 5.0
    Event good = make_event(1, 1, 0x4000000000000000ULL, 0x4008000000000000ULL, 0x4014000000000000ULL);
    AuditResult ok = audit_event(good, OpKind::FAdd);
    EXPECT_TRUE(ok.computable);
    EXPECT_TRUE(ok.consistent);

    Event bad = good;
    bad.result_bits ^= 1ULL;
    AuditResult corrupted = audit_event(bad, OpKind::FAdd);
    EXPECT_TRUE(corrupted.computable);
    EXPECT_FALSE(corrupted.consistent);
}

TEST(Audit, SqrtRecomputesExactly) {
    Event good = make_event(1, 1, 0x4010000000000000ULL, 0, 0x4000000000000000ULL); // sqrt(4) = 2
    EXPECT_TRUE(audit_event(good, OpKind::FSqrt).consistent);

    Event bad = good;
    bad.result_bits ^= 1ULL;
    EXPECT_FALSE(audit_event(bad, OpKind::FSqrt).consistent);
}

TEST(Classify, Precedence) {
    VerdictInput vi;
    vi.evidence_sufficient = true;
    vi.clean_has_exit = true;
    vi.injected_has_exit = true;

    // Crash wins over everything else.
    vi.injected_has_exit = false;
    EXPECT_EQ(classify(vi).classification, Classification::CRASH);
    vi.injected_has_exit = true;

    // Corrected wins over detected, because a repair is the stronger result.
    vi.corrected_by_mechanism = true;
    vi.injected_detected = true;
    vi.final_result_outside_tolerance = false;
    EXPECT_EQ(classify(vi).classification, Classification::CORRECTED);

    // A repair that did not land inside tolerance is SDC, not CORRECTED.
    vi.final_result_outside_tolerance = true;
    EXPECT_EQ(classify(vi).classification, Classification::SDC);
    vi.corrected_by_mechanism = false;

    // Detection without a repair.
    vi.injected_detected = true;
    vi.final_result_outside_tolerance = false;
    EXPECT_EQ(classify(vi).classification, Classification::DETECTED);

    // Nothing noticed, result outside tolerance.
    vi.injected_detected = false;
    vi.final_result_outside_tolerance = true;
    EXPECT_EQ(classify(vi).classification, Classification::SDC);

    // Nothing noticed, result inside tolerance.
    vi.final_result_outside_tolerance = false;
    EXPECT_EQ(classify(vi).classification, Classification::MASKED);

    // Insufficient evidence.
    vi.evidence_sufficient = false;
    EXPECT_EQ(classify(vi).classification, Classification::INCONCLUSIVE);
}

TEST(Classify, Names) {
    EXPECT_EQ(std::string(classification_name(Classification::CRASH)), "CRASH");
    EXPECT_EQ(std::string(classification_name(Classification::CORRECTED)), "CORRECTED");
    EXPECT_EQ(std::string(classification_name(Classification::DETECTED)), "DETECTED");
    EXPECT_EQ(std::string(classification_name(Classification::SDC)), "SDC");
    EXPECT_EQ(std::string(classification_name(Classification::MASKED)), "MASKED");
    EXPECT_EQ(std::string(classification_name(Classification::INCONCLUSIVE)), "INCONCLUSIVE");
}

} // namespace
