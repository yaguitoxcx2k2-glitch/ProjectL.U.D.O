#pragma once

#include "core/Editor.h"

#include <QObject>
#include <QHash>
#include <QSet>

QT_BEGIN_NAMESPACE
class QDialog;
QT_END_NAMESPACE

namespace game {

/// Leitor leve de controle. No Windows usa XInput por carregamento dinâmico,
/// sem DLL adicional; nas outras plataformas compila como fallback vazio.
class GamepadInput
{
public:
    GamepadInput();
    ~GamepadInput();
    GamepadInput(const GamepadInput&) = delete;
    GamepadInput& operator=(const GamepadInput&) = delete;

    QSet<core::GameAction> poll(const core::InputSystemSettings& settings);
    bool connected() const { return m_connected; }
    double axisX() const { return m_axisX; }
    double axisY() const { return m_axisY; }
    double leftTrigger() const { return m_leftTrigger; }
    double rightTrigger() const { return m_rightTrigger; }

private:
    void* m_library = nullptr;
    void* m_getState = nullptr;
    bool m_connected = false;
    double m_axisX = 0.0, m_axisY = 0.0;
    double m_leftTrigger = 0.0, m_rightTrigger = 0.0;
};

/// Acrescenta navegação por controle a qualquer janela modal do runtime.
/// O objeto pertence ao diálogo e se destrói automaticamente com ele.
class GamepadDialogNavigator final : public QObject
{
public:
    GamepadDialogNavigator(QDialog* dialog,
                           const core::InputSystemSettings& settings);

private:
    void poll();

    QDialog* m_dialog = nullptr;
    const core::InputSystemSettings* m_settings = nullptr;
    GamepadInput m_input;
    QSet<core::GameAction> m_held;
    QHash<core::GameAction,int> m_holdTicks;
};

} // namespace game
