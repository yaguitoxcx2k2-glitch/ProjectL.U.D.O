#pragma once

#include <QDialog>
#include <functional>

namespace core { class Editor; }

namespace ui {

/// Configurações persistentes do jogo/projeto. Preferências da máquina do autor
/// ficam deliberadamente fora daqui (PreferencesDialog / EditorUiPreferences).
class GameSettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit GameSettingsDialog(core::Editor& editor,
                                std::function<void()> configurePlayer,
                                QWidget* parent = nullptr);

private:
    core::Editor& m_editor;
    std::function<void()> m_configurePlayer;
};

} // namespace ui
