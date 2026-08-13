#include "common/JsonMini.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ducker {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool Parse(JsonValue& out, std::string* err) {
        SkipWs();
        if (!ParseValue(out)) {
            if (err) *err = ErrorAt();
            return false;
        }
        SkipWs();
        if (pos_ != s_.size()) {
            if (err) *err = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    std::string ErrorAt() const {
        std::ostringstream oss;
        oss << "JSON parse error near offset " << pos_;
        return oss.str();
    }

    bool Peek(char c) { return pos_ < s_.size() && s_[pos_] == c; }
    char Cur() { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    void SkipWs() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) pos_++;
    }

    bool Consume(char c) {
        if (Peek(c)) { pos_++; return true; }
        return false;
    }

    bool Literal(const char* lit) {
        size_t n = strlen(lit);
        if (s_.compare(pos_, n, lit) == 0) { pos_ += n; return true; }
        return false;
    }

    bool ParseValue(JsonValue& out) {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        char c = Cur();
        switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': return ParseString(out);
            case 't': if (Literal("true")) { out.type = JsonValue::Type::Bool; out.b = true; return true; } return false;
            case 'f': if (Literal("false")) { out.type = JsonValue::Type::Bool; out.b = false; return true; } return false;
            case 'n': if (Literal("null")) { out.type = JsonValue::Type::Null; return true; } return false;
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        pos_++; // '{'
        SkipWs();
        if (Consume('}')) return true;
        for (;;) {
            SkipWs();
            JsonValue key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Consume(':')) return false;
            JsonValue val;
            if (!ParseValue(val)) return false;
            out.obj.emplace_back(key.str, std::move(val));
            SkipWs();
            if (Consume('}')) return true;
            if (!Consume(',')) return false;
        }
    }

    bool ParseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        pos_++; // '['
        SkipWs();
        if (Consume(']')) return true;
        for (;;) {
            JsonValue val;
            if (!ParseValue(val)) return false;
            out.arr.push_back(std::move(val));
            SkipWs();
            if (Consume(']')) return true;
            if (!Consume(',')) return false;
        }
    }

    bool ParseString(JsonValue& out) {
        if (!Consume('"')) return false;
        std::string result;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') { out.type = JsonValue::Type::String; out.str = result; return true; }
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned code = std::strtoul(s_.substr(pos_, 4).c_str(), nullptr, 16);
                        pos_ += 4;
                        if (code < 0x80) result += static_cast<char>(code);
                        else if (code < 0x800) {
                            result += static_cast<char>(0xC0 | (code >> 6));
                            result += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (code >> 12));
                            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default: return false;
                }
            } else {
                result += c;
            }
        }
        return false; // unterminated
    }

    bool ParseNumber(JsonValue& out) {
        size_t start = pos_;
        if (Peek('-')) pos_++;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(Cur()))) pos_++;
        if (Peek('.')) {
            pos_++;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(Cur()))) pos_++;
        }
        if (pos_ < s_.size() && (Cur() == 'e' || Cur() == 'E')) {
            pos_++;
            if (Peek('+') || Peek('-')) pos_++;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(Cur()))) pos_++;
        }
        if (pos_ == start) return false;
        if (pos_ > start && (s_[start] == '-' && pos_ == start + 1)) return false;
        out.type = JsonValue::Type::Number;
        out.num = std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr);
        return true;
    }
};

void AppendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void WriteValue(const JsonValue& v, std::string& out) {
    switch (v.type) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.b ? "true" : "false"; break;
        case JsonValue::Type::Number: {
            char buf[32];
            if (v.num == static_cast<int>(v.num))
                snprintf(buf, sizeof(buf), "%d", static_cast<int>(v.num));
            else
                snprintf(buf, sizeof(buf), "%.6g", v.num);
            out += buf;
            break;
        }
        case JsonValue::Type::String: AppendEscaped(out, v.str); break;
        case JsonValue::Type::Array: {
            out += '[';
            for (size_t i = 0; i < v.arr.size(); i++) {
                if (i) out += ',';
                WriteValue(v.arr[i], out);
            }
            out += ']';
            break;
        }
        case JsonValue::Type::Object: {
            out += '{';
            for (size_t i = 0; i < v.obj.size(); i++) {
                if (i) out += ',';
                AppendEscaped(out, v.obj[i].first);
                out += ':';
                WriteValue(v.obj[i].second, out);
            }
            out += '}';
            break;
        }
    }
}

} // namespace

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj)
        if (k == key) return &v;
    return nullptr;
}

std::string JsonValue::AsString(const std::string& key, const std::string& def) const {
    const JsonValue* v = Find(key);
    if (v && v->type == Type::String) return v->str;
    return def;
}

int JsonValue::AsInt(const std::string& key, int def) const {
    const JsonValue* v = Find(key);
    if (v && v->type == Type::Number) return static_cast<int>(v->num);
    return def;
}

bool JsonValue::AsBool(const std::string& key, bool def) const {
    const JsonValue* v = Find(key);
    if (v && v->type == Type::Bool) return v->b;
    return def;
}

bool ParseJson(const std::string& text, JsonValue& out, std::string* err) {
    Parser p(text);
    return p.Parse(out, err);
}

bool WriteJson(const JsonValue& v, std::string& out, std::string* err) {
    try {
        WriteValue(v, out);
        return true;
    } catch (...) {
        if (err) *err = "failed to serialize JSON";
        return false;
    }
}

std::string EscapeJsonString(const std::string& s) {
    std::string out;
    AppendEscaped(out, s);
    return out;
}

} // namespace ducker
