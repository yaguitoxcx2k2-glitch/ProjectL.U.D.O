#pragma once
#include "Types.h"
#include <map>

namespace ludo::core {

class JsonValue {
public:
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::map<Text, JsonValue>;

    JsonValue() = default;
    JsonValue(std::nullptr_t) {}
    JsonValue(bool v) : kind_(Kind::Boolean), boolean_(v) {}
    JsonValue(double v) : kind_(Kind::Number), number_(v) {}
    JsonValue(int v) : JsonValue(double(v)) {}
    JsonValue(Text v) : kind_(Kind::String), string_(std::move(v)) {}
    JsonValue(const char* v) : JsonValue(Text(v ? v : "")) {}
    JsonValue(Array v) : kind_(Kind::Array), array_(std::move(v)) {}
    JsonValue(Object v) : kind_(Kind::Object), object_(std::move(v)) {}

    Kind kind() const noexcept { return kind_; }
    bool asBool(bool fallback=false) const noexcept { return kind_ == Kind::Boolean ? boolean_ : fallback; }
    double asNumber(double fallback=0.0) const noexcept { return kind_ == Kind::Number ? number_ : fallback; }
    const Text& asString() const noexcept { return string_; }
    const Array& asArray() const noexcept { return array_; }
    const Object& asObject() const noexcept { return object_; }
    Array& array() noexcept { kind_ = Kind::Array; return array_; }
    Object& object() noexcept { kind_ = Kind::Object; return object_; }

    const JsonValue* find(TextView key) const noexcept;

private:
    Kind kind_ = Kind::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    Text string_;
    Array array_;
    Object object_;
};

} // namespace ludo::core
