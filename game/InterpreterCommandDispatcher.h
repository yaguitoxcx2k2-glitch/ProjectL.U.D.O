#pragma once
#include "core/EventModel.h"
#include <QHash>
#include <QString>
#include <functional>

namespace game {
class Interpreter;
enum class CommandDispatchResult { Unhandled, Advance, Await };

/// Migration seam for breaking Interpreter.cpp into command-family handlers.
/// Registered handlers run before the legacy implementation; unregistered
/// commands follow the exact old path.
class InterpreterCommandDispatcher {
public:
    using Handler = std::function<CommandDispatchResult(const core::EventCommand&, Interpreter&)>;
    void registerHandler(QString type, Handler handler);
    bool hasHandler(const QString& type) const;
    CommandDispatchResult dispatch(const core::EventCommand&, Interpreter&) const;
private:
    QHash<QString, Handler> m_handlers;
};
}
