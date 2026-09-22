#include "Value.h"
#include <iomanip>
#include <sstream>

namespace ludo::core {

std::optional<double> Value::number() const noexcept {
    if (const auto* i = getIf<std::int64_t>()) return double(*i);
    if (const auto* d = getIf<double>()) return *d;
    if (const auto* b = getIf<bool>()) return *b ? 1.0 : 0.0;
    return std::nullopt;
}

Text Value::toText() const {
    if (isNull()) return {};
    if (const auto* b = getIf<bool>()) return *b ? "true" : "false";
    if (const auto* i = getIf<std::int64_t>()) return std::to_string(*i);
    if (const auto* d = getIf<double>()) {
        std::ostringstream out; out << std::setprecision(15) << *d; return out.str();
    }
    if (const auto* s = getIf<Text>()) return *s;
    return {};
}

} // namespace ludo::core
