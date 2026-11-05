#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nsync::core {

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    [[nodiscard]] const JsonValue* find(std::string_view key) const;
};

bool parse_json(
    std::string_view text,
    JsonValue& value,
    std::string* error = nullptr);

} // namespace nsync::core
