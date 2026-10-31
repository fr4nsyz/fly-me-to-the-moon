#include "ops_map.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>

namespace sdc {

bool load_ops_map(const std::string& path, std::map<OpId, OpInfo>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    Json root;
    if (!Json::parse(text, root, error)) {
        error = path + ": " + error;
        return false;
    }
    if (!root.is_object()) {
        error = path + ": expected a JSON object at the top level";
        return false;
    }

    for (const auto& kv : root.fields()) {
        const std::string& key = kv.first;
        const Json& value = kv.second;
        if (!value.is_object()) {
            error = path + ": entry \"" + key + "\" is not an object";
            return false;
        }

        OpInfo info;
        try {
            info.op_id = static_cast<OpId>(std::stoul(key));
        } catch (...) {
            error = path + ": entry \"" + key + "\" has a non-numeric op id";
            return false;
        }

        if (const Json* f = value.find("file")) { if (f->is_string()) info.file = f->as_string(); }
        if (const Json* l = value.find("line")) { if (l->is_number()) info.line = static_cast<unsigned>(l->as_number()); }
        if (const Json* c = value.find("column")) { if (c->is_number()) info.column = static_cast<unsigned>(c->as_number()); }
        if (const Json* fn = value.find("function")) { if (fn->is_string()) info.function = fn->as_string(); }
        if (const Json* k = value.find("kind")) {
            if (k->is_string()) {
                info.kind = k->as_string();
                op_kind_from_name(info.kind.c_str(), info.op_kind);
            }
        }

        out[info.op_id] = info;
    }

    return true;
}

} // namespace sdc
