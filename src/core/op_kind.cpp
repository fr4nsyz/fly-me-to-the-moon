#include "sdc/event.hpp"
#include <cstring>

namespace sdc {

const char *op_kind_name(OpKind kind) {
    switch (kind) {
#define SDC_F64_BINARY_OP(suffix, kind_str, enumerator)                        \
    case OpKind::enumerator:                                                   \
        return #kind_str;
#define SDC_F64_UNARY_OP(suffix, kind_str, enumerator)                         \
    case OpKind::enumerator:                                                   \
        return #kind_str;
#include "sdc/op_table.def"
#undef SDC_F64_BINARY_OP
#undef SDC_F64_UNARY_OP
    default:
        return "unknown";
    }
}

bool op_kind_from_name(const char *name, OpKind &out) {
    if (!name)
        return false;
#define SDC_F64_BINARY_OP(suffix, kind_str, enumerator)                        \
    if (std::strcmp(name, #kind_str) == 0) {                                   \
        out = OpKind::enumerator;                                              \
        return true;                                                           \
    }
#define SDC_F64_UNARY_OP(suffix, kind_str, enumerator)                         \
    if (std::strcmp(name, #kind_str) == 0) {                                   \
        out = OpKind::enumerator;                                              \
        return true;                                                           \
    }
#include "sdc/op_table.def"
#undef SDC_F64_BINARY_OP
#undef SDC_F64_UNARY_OP
    return false;
}

} // namespace sdc
