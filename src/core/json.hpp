#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sdc {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : type_(Type::Null) {}
    explicit Json(bool b) : type_(Type::Bool), bool_(b) {}
    explicit Json(double d) : type_(Type::Number), number_(d) {}
    explicit Json(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_string() const { return type_ == Type::String; }
    bool is_number() const { return type_ == Type::Number; }

    bool as_bool() const { return bool_; }
    double as_number() const { return number_; }
    const std::string& as_string() const { return string_; }

    std::vector<Json>& items() { return array_; }
    const std::vector<Json>& items() const { return array_; }
    std::vector<std::pair<std::string, Json>>& fields() { return object_; }
    const std::vector<std::pair<std::string, Json>>& fields() const { return object_; }

    void push_back(Json v) { array_.push_back(std::move(v)); }

    void set(const std::string& key, Json v) {
        for (auto& kv : object_) {
            if (kv.first == key) { kv.second = std::move(v); return; }
        }
        object_.emplace_back(key, std::move(v));
    }

    const Json* find(const std::string& key) const {
        for (const auto& kv : object_) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    std::string dump(int indent = 2) const {
        std::string out;
        write(out, indent, 0);
        out.push_back('\n');
        return out;
    }

    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void write(std::string& out, int indent, int depth) const;

    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

inline std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xf]);
                    out.push_back(hex[c & 0xf]);
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

inline void Json::write(std::string& out, int indent, int depth) const {
    const std::string pad(static_cast<size_t>(indent * depth), ' ');
    const std::string pad_inner(static_cast<size_t>(indent * (depth + 1)), ' ');
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.17g", number_);
            out += buf;
            break;
        }
        case Type::String:
            out.push_back('"');
            out += json_escape(string_);
            out.push_back('"');
            break;
        case Type::Array: {
            if (array_.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < array_.size(); ++i) {
                out += pad_inner;
                array_[i].write(out, indent, depth + 1);
                if (i + 1 < array_.size()) out.push_back(',');
                out.push_back('\n');
            }
            out += pad;
            out.push_back(']');
            break;
        }
        case Type::Object: {
            if (object_.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < object_.size(); ++i) {
                out += pad_inner;
                out.push_back('"');
                out += json_escape(object_[i].first);
                out += "\": ";
                object_[i].second.write(out, indent, depth + 1);
                if (i + 1 < object_.size()) out.push_back(',');
                out.push_back('\n');
            }
            out += pad;
            out.push_back('}');
            break;
        }
    }
}

namespace json_detail {

struct Parser {
    const std::string& text;
    size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& t) : text(t) {}

    void skip_ws() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }

    bool fail(const std::string& msg) {
        if (error.empty()) error = msg + " at offset " + std::to_string(pos);
        return false;
    }

    bool parse_value(Json& out) {
        skip_ws();
        if (pos >= text.size()) return fail("unexpected end of input");
        char c = text[pos];
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') {
            std::string s;
            if (!parse_string(s)) return false;
            out = Json(std::move(s));
            return true;
        }
        if (c == 't') {
            if (text.compare(pos, 4, "true") != 0) return fail("invalid literal");
            pos += 4;
            out = Json(true);
            return true;
        }
        if (c == 'f') {
            if (text.compare(pos, 5, "false") != 0) return fail("invalid literal");
            pos += 5;
            out = Json(false);
            return true;
        }
        if (c == 'n') {
            if (text.compare(pos, 4, "null") != 0) return fail("invalid literal");
            pos += 4;
            out = Json();
            return true;
        }
        return parse_number(out);
    }

    bool parse_number(Json& out) {
        size_t start = pos;
        if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
        while (pos < text.size() && (isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.' ||
                                     text[pos] == 'e' || text[pos] == 'E' || text[pos] == '-' || text[pos] == '+')) {
            ++pos;
        }
        if (start == pos) return fail("invalid number");
        try {
            out = Json(std::stod(text.substr(start, pos - start)));
        } catch (...) {
            return fail("invalid number");
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (pos >= text.size() || text[pos] != '"') return fail("expected string");
        ++pos;
        out.clear();
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos >= text.size()) return fail("unterminated escape");
            char e = text[pos++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > text.size()) return fail("bad \\u escape");
                    unsigned code = std::stoul(text.substr(pos, 4), nullptr, 16);
                    pos += 4;
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    } else {
                        out.push_back(static_cast<char>(0xe0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_array(Json& out) {
        out = Json::array();
        ++pos;
        skip_ws();
        if (pos < text.size() && text[pos] == ']') { ++pos; return true; }
        while (true) {
            Json item;
            if (!parse_value(item)) return false;
            out.push_back(std::move(item));
            skip_ws();
            if (pos >= text.size()) return fail("unterminated array");
            if (text[pos] == ',') { ++pos; continue; }
            if (text[pos] == ']') { ++pos; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_object(Json& out) {
        out = Json::object();
        ++pos;
        skip_ws();
        if (pos < text.size() && text[pos] == '}') { ++pos; return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos >= text.size() || text[pos] != ':') return fail("expected ':'");
            ++pos;
            Json value;
            if (!parse_value(value)) return false;
            out.set(key, std::move(value));
            skip_ws();
            if (pos >= text.size()) return fail("unterminated object");
            if (text[pos] == ',') { ++pos; continue; }
            if (text[pos] == '}') { ++pos; return true; }
            return fail("expected ',' or '}'");
        }
    }
};

} // namespace json_detail

inline bool Json::parse(const std::string& text, Json& out, std::string& error) {
    json_detail::Parser p(text);
    if (!p.parse_value(out)) {
        error = p.error;
        return false;
    }
    p.skip_ws();
    if (p.pos != text.size()) {
        error = "trailing content at offset " + std::to_string(p.pos);
        return false;
    }
    return true;
}

} // namespace sdc
