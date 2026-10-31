#include "trace_writer.hpp"
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
#pragma pack(pop)

TraceWriter::~TraceWriter() {
    close();
}

bool TraceWriter::open(const std::string& path, const std::string& metadata_json) {
    close();
    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ < 0) return false;

    Header hdr;
    memcpy(hdr.magic, "SDCTRACE", 8);
    hdr.version = 1;
    hdr.flags = 1; // LITTLE_ENDIAN
    hdr.meta_len = static_cast<uint32_t>(metadata_json.size());
    hdr.record_size = 40;
    hdr.reserved = 0;

    if (write(fd_, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        close();
        return false;
    }
    if (hdr.meta_len > 0) {
        if (write(fd_, metadata_json.data(), hdr.meta_len) != static_cast<ssize_t>(hdr.meta_len)) {
            close();
            return false;
        }
    }
    return true;
}

void TraceWriter::write_event(const Event& event) {
    if (fd_ < 0 || wrote_exit_) return;
    struct {
        uint32_t type;
        uint32_t op_id;
        uint64_t occurrence;
        uint64_t operand0;
        uint64_t operand1;
        uint64_t result;
    } rec{};
    rec.type = 0; // EVENT
    rec.op_id = event.op_id;
    rec.occurrence = event.occurrence;
    rec.operand0 = event.operand_bits[0];
    rec.operand1 = event.operand_bits[1];
    rec.result = event.result_bits;
    write(fd_, &rec, sizeof(rec));
}

void TraceWriter::write_exit(const ExitRecord& exit_rec) {
    if (fd_ < 0 || wrote_exit_) return;
    struct {
        uint32_t type;
        uint32_t op_id;
        uint64_t occurrence;
        uint64_t operand0;
        uint64_t operand1;
        uint64_t result;
    } rec{};
    rec.type = 1; // EXIT
    rec.op_id = 0;
    rec.occurrence = 0;
    rec.operand0 = static_cast<uint64_t>(static_cast<uint32_t>(exit_rec.exit_code));
    rec.operand1 = exit_rec.detected_by_workload ? 1ULL : 0ULL;
    rec.result = exit_rec.final_result_bits;
    write(fd_, &rec, sizeof(rec));
    wrote_exit_ = true;
}

void TraceWriter::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    wrote_exit_ = false;
}

} // namespace sdc
