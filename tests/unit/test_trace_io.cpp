#include "trace_writer.hpp"
#include "trace_reader.hpp"
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <unistd.h>

using namespace sdc;

namespace {

class TraceIo : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = std::string("/tmp/sdc_test_") +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".sdct";
    }
    void TearDown() override { std::remove(path_.c_str()); }
    std::string path_;
};

TEST_F(TraceIo, RoundTrip) {
    const std::string meta =
        "{\"workload\":\"test\",\"build_id\":\"abc\",\"compiler_flags\":\"-O2\","
        "\"schema\":1,\"inject\":{}}";

    TraceWriter w;
    ASSERT_TRUE(w.open(path_.c_str(), meta));

    Event e1;
    e1.op_id = 1; e1.occurrence = 1;
    e1.operand_bits[0] = 0x3ff0000000000000ULL;
    e1.operand_bits[1] = 0x4000000000000000ULL;
    e1.result_bits = 0x4008000000000000ULL;

    Event e2;
    e2.op_id = 2; e2.occurrence = 1;
    e2.operand_bits[0] = e1.result_bits;
    e2.operand_bits[1] = 0x3ff0000000000000ULL;
    e2.result_bits = 0x4010000000000000ULL;

    w.write_event(e1);
    w.write_event(e2);

    ExitRecord er;
    er.exit_code = 0;
    er.detected_by_workload = false;
    er.corrected_by_mechanism = false;
    er.final_result_bits = e2.result_bits;
    w.write_exit(er);
    w.close();

    TraceReader r;
    Trace t = r.read(path_.c_str());
    ASSERT_EQ(r.error(), "");
    EXPECT_EQ(t.metadata_json, meta);
    ASSERT_EQ(t.events.size(), 2u);
    EXPECT_TRUE(t.has_exit);
    EXPECT_EQ(t.events[0].op_id, 1u);
    EXPECT_EQ(t.events[0].operand_bits[0], 0x3ff0000000000000ULL);
    EXPECT_EQ(t.events[1].result_bits, 0x4010000000000000ULL);
    EXPECT_EQ(t.exit_record.final_result_bits, e2.result_bits);
    EXPECT_FALSE(t.exit_record.detected_by_workload);
    EXPECT_FALSE(t.exit_record.corrected_by_mechanism);
}

TEST_F(TraceIo, ExitFlags) {
    TraceWriter w;
    ASSERT_TRUE(w.open(path_.c_str(), "{}"));

    ExitRecord er;
    er.exit_code = 3;
    er.detected_by_workload = true;
    er.corrected_by_mechanism = true;
    er.final_result_bits = 0xdeadbeefcafef00dULL;
    w.write_exit(er);
    w.close();

    TraceReader r;
    Trace t = r.read(path_.c_str());
    ASSERT_EQ(r.error(), "");
    ASSERT_TRUE(t.has_exit);
    EXPECT_EQ(t.exit_record.exit_code, 3);
    EXPECT_TRUE(t.exit_record.detected_by_workload);
    EXPECT_TRUE(t.exit_record.corrected_by_mechanism);
    EXPECT_EQ(t.exit_record.final_result_bits, 0xdeadbeefcafef00dULL);
}

TEST_F(TraceIo, TornFinalRecordIsDiscarded) {
    TraceWriter w;
    ASSERT_TRUE(w.open(path_.c_str(), "{}"));

    Event e;
    e.op_id = 1; e.occurrence = 1;
    e.result_bits = 0x3ff0000000000000ULL;
    w.write_event(e);
    w.close();

    const long full_size = 32 + 2 + 40; // header + "{}" metadata + one record
    ASSERT_EQ(::truncate(path_.c_str(), full_size - 20), 0);

    TraceReader r;
    Trace t = r.read(path_.c_str());
    EXPECT_EQ(r.error(), "");
    EXPECT_TRUE(t.events.empty());
    EXPECT_FALSE(t.has_exit);
}

TEST_F(TraceIo, RejectsBadMagic) {
    TraceWriter w;
    ASSERT_TRUE(w.open(path_.c_str(), "{}"));
    w.close();

    std::FILE* f = std::fopen(path_.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fputs("NOTATRAC", f);
    std::fclose(f);

    TraceReader r;
    r.read(path_.c_str());
    EXPECT_EQ(r.error(), "bad magic");
}

TEST_F(TraceIo, RejectsNonMonotonicOccurrence) {
    TraceWriter w;
    ASSERT_TRUE(w.open(path_.c_str(), "{}"));

    Event e;
    e.op_id = 1;
    e.occurrence = 5;
    w.write_event(e);
    e.occurrence = 3;
    w.write_event(e);
    w.close();

    TraceReader r;
    r.read(path_.c_str());
    EXPECT_EQ(r.error(), "non-monotonic occurrence");
}

} // namespace
