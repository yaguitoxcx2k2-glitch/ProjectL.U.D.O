#include "EditorCommand.h"

namespace core {

void CompositeEditorCommand::add(std::unique_ptr<EditorCommand> command)
{
    if (command) m_commands.push_back(std::move(command));
}

bool CompositeEditorCommand::execute(QString* error)
{
    m_executedCount = 0;
    for (auto& command : m_commands) {
        if (!command->execute(error)) {
            undo();
            return false;
        }
        ++m_executedCount;
    }
    return true;
}

void CompositeEditorCommand::undo()
{
    while (m_executedCount > 0) {
        --m_executedCount;
        m_commands[size_t(m_executedCount)]->undo();
    }
}

bool EditorCommandStack::push(std::unique_ptr<EditorCommand> command, QString* error)
{
    if (!command || !command->execute(error)) return false;
    if (m_cursor < int(m_commands.size()))
        m_commands.erase(m_commands.begin() + m_cursor, m_commands.end());
    m_commands.push_back(std::move(command));
    m_cursor = int(m_commands.size());
    return true;
}

QString EditorCommandStack::undoLabel() const
{
    return canUndo() ? m_commands[size_t(m_cursor - 1)]->label() : QString();
}

QString EditorCommandStack::redoLabel() const
{
    return canRedo() ? m_commands[size_t(m_cursor)]->label() : QString();
}

bool EditorCommandStack::undo()
{
    if (!canUndo()) return false;
    --m_cursor;
    m_commands[size_t(m_cursor)]->undo();
    return true;
}

bool EditorCommandStack::redo(QString* error)
{
    if (!canRedo()) return false;
    if (!m_commands[size_t(m_cursor)]->execute(error)) return false;
    ++m_cursor;
    return true;
}

void EditorCommandStack::clear()
{
    m_commands.clear();
    m_cursor = 0;
}

} // namespace core
