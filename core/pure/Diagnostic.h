#pragma once
#include "Types.h"
#include "Value.h"
#include <map>

namespace ludo::core {

enum class Severity { Info, Warning, Error };

struct Message {
    Text code;                        // stable, non-localized identifier
    std::map<Text, Value> arguments;  // UI adapter decides how to render it
};

struct Diagnostic {
    Severity severity = Severity::Info;
    Message message;
    Text location;
    Text suggestionCode;
    std::vector<Text> relatedLocations;
    bool safelyFixable = false;
};

} // namespace ludo::core
