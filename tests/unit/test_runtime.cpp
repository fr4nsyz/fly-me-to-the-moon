#include "trace_reader.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

namespace {

constexpr std::uint64_t kTwo = 0x4000000000000000ULL;

int launch_probe(const std::string& path, const char* inject, const char* persist) {
    setenv("SDC_TRACE", path.c_str(), 1);
    setenv("SDC_INJECT", inject, 1);
    if (persist) setenv("SDC_INJECT_PERSIST", persist, 1);
    else unsetenv("SDC_INJECT_PERSIST");
    return std::system(RUNTIME_PROBE_PATH);
}

sdc::Trace run_probe(const std::string& name, const char* inject, const char* persist = nullptr) {
    const std::string path = testing::TempDir() + name + ".sdct";
    EXPECT_EQ(launch_probe(path, inject, persist), 0);
    sdc::TraceReader reader;
    sdc::Trace trace = reader.read(path);
    EXPECT_TRUE(reader.error().empty()) << reader.error();
    return trace;
}

std::uint64_t result(const sdc::Trace& trace, sdc::OpId op, sdc::Occurrence occ) {
    for (const sdc::Event& e : trace.events) {
        if (e.op_id == op && e.occurrence == occ) return e.result_bits;
    }
    ADD_FAILURE() << "no event for op " << op << " occurrence " << occ;
    return 0;
}

int flipped_count(const sdc::Trace& trace) {
    int n = 0;
    for (const sdc::Event& e : trace.events) n += e.result_bits != kTwo;
    return n;
}

} // namespace

TEST(RuntimeInjection, SingleSiteFlipsOnlyThatExecutionAndKeepsItsMetadata) {
    const sdc::Trace trace = run_probe("single", "op=2,occ=3,bit=5");
    EXPECT_EQ(result(trace, 2, 3), kTwo ^ (1ULL << 5));
    EXPECT_EQ(flipped_count(trace), 1);
    EXPECT_NE(trace.metadata_json.find("\"op_id\":2,\"occurrence\":3,\"bit\":5"), std::string::npos);
    EXPECT_EQ(trace.metadata_json.find("\"sites\""), std::string::npos);
}

TEST(RuntimeInjection, TwoSitesFlipOnlyTheirOwnExecutions) {
    const sdc::Trace trace = run_probe("two", "op=1,occ=2,bit=0;op=3,occ=1,bit=63");
    EXPECT_EQ(result(trace, 1, 2), kTwo ^ 1ULL);
    EXPECT_EQ(result(trace, 3, 1), kTwo ^ (1ULL << 63));
    EXPECT_EQ(flipped_count(trace), 2);
}

TEST(RuntimeInjection, PersistenceIsPerSite) {
    const sdc::Trace trace = run_probe("persist", "op=1,occ=2,bit=0,persist=1;op=3,occ=1,bit=63");
    EXPECT_EQ(result(trace, 1, 2), kTwo ^ 1ULL);
    EXPECT_EQ(result(trace, 1, 3), kTwo ^ 1ULL);
    EXPECT_EQ(result(trace, 3, 1), kTwo ^ (1ULL << 63));
    EXPECT_EQ(flipped_count(trace), 3);
}

TEST(RuntimeInjection, MultiSiteMetadataListsEverySite) {
    const sdc::Trace trace = run_probe("meta", "op=1,occ=2,bit=0,persist=1;op=3,occ=1,bit=63");
    EXPECT_NE(trace.metadata_json.find(
                  "\"sites\":[{\"op_id\":1,\"occurrence\":2,\"bit\":0,\"persistent\":true},"
                  "{\"op_id\":3,\"occurrence\":1,\"bit\":63,\"persistent\":false}]"),
              std::string::npos)
        << trace.metadata_json;
}

TEST(RuntimeInjection, DuplicateSitesFlipTheBitOnce) {
    const sdc::Trace trace = run_probe("dup", "op=1,occ=2,bit=0;op=1,occ=2,bit=0");
    EXPECT_EQ(result(trace, 1, 2), kTwo ^ 1ULL);
    EXPECT_EQ(flipped_count(trace), 1);
}

TEST(RuntimeInjection, OverlappingPersistentAndTransientSitesFlipTheBitOnce) {
    const sdc::Trace trace = run_probe("overlap", "op=1,occ=2,bit=0,persist=1;op=1,occ=3,bit=0");
    EXPECT_EQ(result(trace, 1, 2), kTwo ^ 1ULL);
    EXPECT_EQ(result(trace, 1, 3), kTwo ^ 1ULL);
    EXPECT_EQ(flipped_count(trace), 2);
}

TEST(RuntimeInjection, InvalidSiteFailsTheRun) {
    for (const char* inject : {"op=1,occ=2,bit=0;op=3,occ=1", "op=1,occ=2,bit=64", "op=1,occ=2,bit=300",
                               "op=1,occ=0,bit=1", "op=1,occ=2,bit=1,persist=true",
                               "op=1,occ=2,bit=1,flavor=x", "op=1x,occ=2,bit=1"}) {
        EXPECT_NE(launch_probe(testing::TempDir() + "invalid.sdct", inject, nullptr), 0) << inject;
    }
}
