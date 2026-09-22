#pragma once
#include "Types.h"
#include <cstdint>
#include <optional>
#include <variant>

namespace ludo::core {

class Value {
public:
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, Text>;

    Value() = default;
    Value(bool v) : data_(v) {}
    Value(int v) : data_(std::int64_t(v)) {}
    Value(std::int64_t v) : data_(v) {}
    Value(double v) : data_(v) {}
    Value(Text v) : data_(std::move(v)) {}
    Value(const char* v) : data_(Text(v ? v : "")) {}

    const Storage& storage() const noexcept { return data_; }
    bool isNull() const noexcept { return std::holds_alternative<std::monostate>(data_); }

    template <typename T> const T* getIf() const noexcept { return std::get_if<T>(&data_); }

    std::optional<double> number() const noexcept;
    Text toText() const;

private:
    Storage data_;
};

} // namespace ludo::core
