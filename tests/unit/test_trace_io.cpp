#include "trace_writer.hpp"
#include "trace_reader.hpp"
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <cassert>
#include <cstdio>
#include <unistd.h>
#include <iostream>

using namespace sdc;

namespace {

const char* kPath = "/tmp/sdc_test_trace.sdct";

void test_round_trip() {
    const std::string meta =
        "{\"workload\":\"test\",\"build_id\":\"abc\",\"compiler_flags\":\"-O2\","
        "\"schema\":1,\"inject\":{}}";

    TraceWriter w;
    assert(w.open(kPath, meta));

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
    Trace t = r.read(kPath);
    assert(r.error().empty());
    assert(t.metadata_json == meta);
    assert(t.events.size() == 2);
    assert(t.has_exit);
    assert(t.events[0].op_id == 1);
    assert(t.events[0].operand_bits[0] == 0x3ff0000000000000ULL);
    assert(t.events[1].result_bits == 0x4010000000000000ULL);
    assert(t.exit_record.final_result_bits == e2.result_bits);
    assert(!t.exit_record.detected_by_workload);
    assert(!t.exit_record.corrected_by_mechanism);
}

void test_exit_flags() {
    TraceWriter w;
    assert(w.open(kPath, "{}"));

    ExitRecord er;
    er.exit_code = 3;
    er.detected_by_workload = true;
    er.corrected_by_mechanism = true;
    er.final_result_bits = 0xdeadbeefcafef00dULL;
    w.write_exit(er);
    w.close();

    TraceReader r;
    Trace t = r.read(kPath);
    assert(r.error().empty());
    assert(t.has_exit);
    assert(t.exit_record.exit_code == 3);
    assert(t.exit_record.detected_by_workload);
    assert(t.exit_record.corrected_by_mechanism);
    assert(t.exit_record.final_result_bits == 0xdeadbeefcafef00dULL);
}

void test_torn_final_record() {
    TraceWriter w;
    assert(w.open(kPath, "{}"));

    Event e;
    e.op_id = 1; e.occurrence = 1;
    e.result_bits = 0x3ff0000000000000ULL;
    w.write_event(e);
    w.close();

    // Truncate the file mid-record. The reader must discard the partial tail
    // rather than fail, and must report no exit record.
    const long full_size = 32 + 2 + 40; // header + "{}" metadata + one record
    assert(::truncate(kPath, full_size - 20) == 0);

    TraceReader r;
    Trace t = r.read(kPath);
    assert(r.error().empty());
    assert(t.events.empty());
    assert(!t.has_exit);
}

void test_rejects_bad_magic() {
    TraceWriter w;
    assert(w.open(kPath, "{}"));
    w.close();

    std::FILE* f = std::fopen(kPath, "r+b");
    assert(f != nullptr);
    std::fputs("NOTATRAC", f);
    std::fclose(f);

    TraceReader r;
    r.read(kPath);
    assert(r.error() == "bad magic");
}

void test_rejects_non_monotonic_occurrence() {
    TraceWriter w;
    assert(w.open(kPath, "{}"));

    Event e;
    e.op_id = 1;
    e.occurrence = 5;
    w.write_event(e);
    e.occurrence = 3; // goes backwards for the same op_id
    w.write_event(e);
    w.close();

    TraceReader r;
    r.read(kPath);
    assert(r.error() == "non-monotonic occurrence");
}

} // namespace

int main() {
    test_round_trip();
    test_exit_flags();
    test_torn_final_record();
    test_rejects_bad_magic();
    test_rejects_non_monotonic_occurrence();
    std::remove(kPath);
    std::cout << "ok" << std::endl;
    return 0;
}
