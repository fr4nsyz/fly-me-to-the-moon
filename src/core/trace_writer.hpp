#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"

namespace sdc {

class TraceWriter {
public:
    TraceWriter() = default;
    ~TraceWriter();

    bool open(const std::string& path, const std::string& metadata_json);
    void write_event(const Event& event);
    void write_exit(const ExitRecord& exit_rec);
    void close();

private:
    int fd_ = -1;
    bool wrote_exit_ = false;
};

} // namespace sdc
