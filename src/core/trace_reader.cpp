#include "trace_reader.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace sdc {

#pragma pack(push, 1)
struct Header {
    char magic[8];
    uint32_t version;
    uint32_t flags;
    uint32_t meta_len;
    uint32_t record_size;
    uint64_t reserved;
};
struct Rec {
    uint32_t type;
    uint32_t op_id;
    uint64_t occurrence;
    uint64_t operand0;
    uint64_t operand1;
    uint64_t result;
};
#pragma pack(pop)

Trace TraceReader::read(const std::string& path) {
    Trace trace;
    error_.clear();
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        error_ = "open failed";
        return trace;
    }

    Header hdr;
    ssize_t n = ::read(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr)) {
        error_ = "short header";
        ::close(fd);
        return trace;
    }
    if (memcmp(hdr.magic, "SDCTRACE", 8) != 0) {
        error_ = "bad magic";
        ::close(fd);
        return trace;
    }
    if (hdr.version != 1) {
        error_ = "bad version";
        ::close(fd);
        return trace;
    }
    if (hdr.record_size != 40) {
        error_ = "bad record_size";
        ::close(fd);
        return trace;
    }
    if ((hdr.flags & ~1u) != 0) {
        error_ = "bad flags";
        ::close(fd);
        return trace;
    }

    if (hdr.meta_len > 0) {
        trace.metadata_json.resize(hdr.meta_len);
        n = ::read(fd, trace.metadata_json.data(), hdr.meta_len);
        if (n != static_cast<ssize_t>(hdr.meta_len)) {
            error_ = "short metadata";
            ::close(fd);
            return trace;
        }
    }

    std::unordered_map<uint32_t, uint64_t> last_occ;
    while (true) {
        Rec r;
        n = ::read(fd, &r, sizeof(r));
        if (n == 0) break;
        if (n < static_cast<ssize_t>(sizeof(r))) {
            // torn final record - discard
            break;
        }
        if (r.type == 0) { // EVENT
            auto it = last_occ.find(r.op_id);
            if (it != last_occ.end() && r.occurrence <= it->second) {
                error_ = "non-monotonic occurrence";
                ::close(fd);
                return trace;
            }
            last_occ[r.op_id] = r.occurrence;
            Event ev;
            ev.op_id = r.op_id;
            ev.occurrence = r.occurrence;
            ev.operand_bits[0] = r.operand0;
            ev.operand_bits[1] = r.operand1;
            ev.result_bits = r.result;
            trace.events.push_back(ev);
        } else if (r.type == 1) { // EXIT
            trace.has_exit = true;
            trace.exit_record.exit_code = static_cast<int>(static_cast<uint32_t>(r.operand0));
            trace.exit_record.detected_by_workload = (r.operand1 & 1ULL) != 0;
            trace.exit_record.corrected_by_mechanism = (r.operand1 & 2ULL) != 0;
            trace.exit_record.final_result_bits = r.result;
            break;
        } else {
            error_ = "bad type";
            ::close(fd);
            return trace;
        }
    }

    ::close(fd);
    return trace;
}

} // namespace sdc
