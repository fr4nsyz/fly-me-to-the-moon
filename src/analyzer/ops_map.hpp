#pragma once

#include "sdc/event.hpp"
#include "sdc/types.hpp"
#include <map>
#include <string>

namespace sdc {

struct OpInfo {
    OpId       op_id = 0;
    std::string file;
    unsigned    line = 0;
    unsigned    column = 0;
    std::string function;
    std::string kind;
    OpKind     op_kind = OpKind::Unknown;
};

// Loads ops.json: op_id -> {file, line, column, function, kind}
bool load_ops_map(const std::string& path, std::map<OpId, OpInfo>& out, std::string& error);

} // namespace sdc
