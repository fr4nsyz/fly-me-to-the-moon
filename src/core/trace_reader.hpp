#pragma once

#include "sdc/event.hpp"
#include "sdc/exit_record.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sdc {

struct Trace {
    std::string        metadata_json;
    std::vector<Event> events;
    bool               has_exit = false;
    ExitRecord         exit_record;
};

class TraceReader {
  public:
    Trace       read(const std::string &path);
    std::string error() const { return error_; }

  private:
    std::string error_;
};

} // namespace sdc
