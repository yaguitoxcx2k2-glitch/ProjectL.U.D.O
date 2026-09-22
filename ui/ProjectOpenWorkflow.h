#pragma once

#include <QString>

class QWidget;
namespace core { class Editor; }

namespace ui {

struct ProjectOpenResult
{
    QString projectPath;
    bool recoveredFromBackup = false;
    bool restoredAutosave = false;
};

/// Fronteira única de abertura interativa do Editor: arquivo principal,
/// backup, autosave de recuperação e estrutura Assets usam a mesma semântica
/// no startup e em Arquivo -> Abrir.
bool openProjectWorkflow(core::Editor& editor, const QString& projectPath, QWidget* parent,
                         ProjectOpenResult* result = nullptr, QString* error = nullptr);

} // namespace ui
