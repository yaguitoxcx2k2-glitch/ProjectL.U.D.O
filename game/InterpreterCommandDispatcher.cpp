#include "InterpreterCommandDispatcher.h"
#include <utility>
namespace game {
void InterpreterCommandDispatcher::registerHandler(QString type, Handler handler){if(!type.isEmpty()&&handler)m_handlers.insert(std::move(type),std::move(handler));}
bool InterpreterCommandDispatcher::hasHandler(const QString& type) const{return m_handlers.contains(type);}
CommandDispatchResult InterpreterCommandDispatcher::dispatch(const core::EventCommand& command, Interpreter& interpreter) const {const auto it=m_handlers.constFind(command.type);return it==m_handlers.cend()?CommandDispatchResult::Unhandled:it.value()(command,interpreter);}
}
