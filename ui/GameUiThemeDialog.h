#pragma once

#include "core/Editor.h"

#include <QDialog>

namespace ui {

/// Configura o tema visual/sonoro do novo sistema In-Game UI. O dialog trabalha
/// numa cópia e só altera o projeto ao confirmar.
class GameUiThemeDialog : public QDialog
{
public:
    explicit GameUiThemeDialog(core::Editor& ed, QWidget* parent = nullptr);

private:
    core::Editor& m_editor;
    core::GameUiSettings m_working;
};

} // namespace ui
