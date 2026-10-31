#include "sdc/event.hpp"
#include <cstring>

namespace sdc {

const char* op_kind_name(OpKind kind) {
    switch (kind) {
        case OpKind::FAdd: return "fadd";
        case OpKind::FSub: return "fsub";
        case OpKind::FMul: return "fmul";
        case OpKind::FDiv: return "fdiv";
        default: return "unknown";
    }
}

bool op_kind_from_name(const char* name, OpKind& out) {
    if (!name) return false;
    if (std::strcmp(name, "fadd") == 0) { out = OpKind::FAdd; return true; }
    if (std::strcmp(name, "fsub") == 0) { out = OpKind::FSub; return true; }
    if (std::strcmp(name, "fmul") == 0) { out = OpKind::FMul; return true; }
    if (std::strcmp(name, "fdiv") == 0) { out = OpKind::FDiv; return true; }
    return false;
}

} // namespace sdc
