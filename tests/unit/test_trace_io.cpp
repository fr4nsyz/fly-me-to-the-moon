#include "trace_writer.hpp"
#include "trace_reader.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

using namespace sdc;

int main() {
    const std::string meta = "{\"workload\":\"test\",\"build_id\":\"abc\",\"compiler_flags\":\"-O2\",\"schema\":1,\"inject\":{}}";
    TraceWriter w;
    assert(w.open("/tmp/test.sdct", meta));
    
    Event e1; e1.op_id = 1; e1.occurrence = 1; e1.operand_bits[0] = 0x3ff0000000000000ULL; e1.operand_bits[1] = 0x4000000000000000ULL; e1.result_bits = 0x4008000000000000ULL;
    Event e2; e2.op_id = 2; e2.occurrence = 1; e2.operand_bits[0] = e1.result_bits; e2.operand_bits[1] = 0x3ff0000000000000ULL; e2.result_bits = 0x4010000000000000ULL;
    w.write_event(e1);
    w.write_event(e2);
    
    ExitRecord er; er.exit_code = 0; er.detected_by_workload = false; er.final_result_bits = e2.result_bits;
    w.write_exit(er);
    w.close();
    
    TraceReader r;
    Trace t = r.read("/tmp/test.sdct");
    assert(r.error().empty());
    assert(t.metadata_json == meta);
    assert(t.events.size() == 2);
    assert(t.has_exit);
    assert(t.events[0].op_id == 1);
    assert(t.events[1].result_bits == 0x4010000000000000ULL);
    
    std::cout << "ok" << std::endl;
    return 0;
}
