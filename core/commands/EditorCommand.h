#pragma once

#include <QString>
#include <memory>
#include <vector>

namespace core {

class EditorCommand
{
public:
    virtual ~EditorCommand() = default;
    virtual QString label() const = 0;
    virtual bool execute(QString* error = nullptr) = 0;
    virtual void undo() = 0;
};

class CompositeEditorCommand final : public EditorCommand
{
public:
    explicit CompositeEditorCommand(QString label) : m_label(std::move(label)) {}
    void add(std::unique_ptr<EditorCommand> command);
    QString label() const override { return m_label; }
    bool execute(QString* error = nullptr) override;
    void undo() override;

private:
    QString m_label;
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_executedCount = 0;
};

class EditorCommandStack final
{
public:
    bool push(std::unique_ptr<EditorCommand> command, QString* error = nullptr);
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < int(m_commands.size()); }
    QString undoLabel() const;
    QString redoLabel() const;
    bool undo();
    bool redo(QString* error = nullptr);
    void clear();

private:
    std::vector<std::unique_ptr<EditorCommand>> m_commands;
    int m_cursor = 0;
};

} // namespace core
