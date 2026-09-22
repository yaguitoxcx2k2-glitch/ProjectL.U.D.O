#pragma once

#include <QDialog>

namespace core { class Editor; }
class QMenuBar;

namespace ui {

/// Preferências locais da ferramenta. Nenhum campo deste diálogo é serializado
/// no .ludo; configurações de mapa pertencem ao projeto e a integração de jogo fica no RPG Maker escolhido pelo projeto.
class PreferencesDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(core::Editor& ed, QMenuBar* menuBar, QWidget* parent = nullptr);
    bool restartRequired() const { return m_restartRequired; }

private:
    core::Editor& ed;
    bool m_restartRequired = false;
};

} // namespace ui
