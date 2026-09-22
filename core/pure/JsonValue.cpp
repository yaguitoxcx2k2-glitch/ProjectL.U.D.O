#include "JsonValue.h"
namespace ludo::core {
const JsonValue* JsonValue::find(TextView key) const noexcept {
    if (kind_ != Kind::Object) return nullptr;
    const auto it = object_.find(Text(key));
    return it == object_.end() ? nullptr : &it->second;
}
} // namespace ludo::core
