#pragma once

#include <string>
#include <vector>
#include <utility>

namespace ducker {

// Minimal JSON value/parser used by the native-messaging host. This is not a
// general-purpose JSON library; it supports exactly the structures Audio
// Ducker needs (objects, strings, numbers, booleans, arrays).
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* Find(const std::string& key) const;
    std::string AsString(const std::string& key, const std::string& def = "") const;
    int AsInt(const std::string& key, int def = 0) const;
    bool AsBool(const std::string& key, bool def = false) const;
};

bool ParseJson(const std::string& text, JsonValue& out, std::string* err = nullptr);
bool WriteJson(const JsonValue& v, std::string& out, std::string* err = nullptr);

std::string EscapeJsonString(const std::string& s);

} // namespace ducker
