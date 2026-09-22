#pragma once

#include "EditorCommand.h"
#include "core/project/StableId.h"

#include <QSize>

namespace core {
class Editor;

class RenameMapCommand final : public EditorCommand
{
public:
    RenameMapCommand(Editor& editor, MapId mapId, QString newName);
    QString label() const override { return QStringLiteral("Renomear mapa"); }
    bool execute(QString* error = nullptr) override;
    void undo() override;

private:
    Editor& m_editor;
    MapId m_mapId;
    QString m_newName;
    QString m_oldName;
    bool m_captured = false;
};

/// Comando transacional para resize. Ele reutiliza o histórico canônico do
/// Editor através de MapService; o undo chama o histórico legado exatamente
/// uma vez, mantendo compatibilidade enquanto o CommandStack é adotado.
class ResizeMapCommand final : public EditorCommand
{
public:
    ResizeMapCommand(Editor& editor, MapId mapId, QSize newSize);
    QString label() const override { return QStringLiteral("Redimensionar mapa"); }
    bool execute(QString* error = nullptr) override;
    void undo() override;

private:
    Editor& m_editor;
    MapId m_mapId;
    QSize m_newSize;
    QSize m_oldSize;
    bool m_captured = false;
};

} // namespace core
