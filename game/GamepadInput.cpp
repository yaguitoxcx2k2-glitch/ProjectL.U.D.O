#include "GamepadInput.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QTimer>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <xinput.h>
#endif

using namespace core;

namespace game {

namespace {

int defaultButton(GameAction action)
{
    switch (action) {
    case GameAction::Confirm:      return 0;  // A
    case GameAction::Cancel:       return 1;  // B
    case GameAction::SkipCutscene: return 2;  // X
    case GameAction::ToggleHud:    return 3;  // Y
    case GameAction::ZoomOut:      return 4;  // LB
    case GameAction::ZoomIn:       return 5;  // RB
    case GameAction::QuickSave:    return 6;  // Back
    case GameAction::QuickLoad:    return 7;  // Start
    case GameAction::Action1:
    case GameAction::Action2:
    case GameAction::Action3:
    case GameAction::Action4:      return -1; // projeto decide o binding
    case GameAction::Quit:         return -1; // sair pelo menu evita conflito com carregar
    case GameAction::Up:           return 10;
    case GameAction::Down:         return 11;
    case GameAction::Left:         return 12;
    case GameAction::Right:        return 13;
    }
    return -1;
}

#ifdef Q_OS_WIN
WORD buttonMask(int index)
{
    static const WORD buttons[] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT
    };
    return index >= 0 && index < int(sizeof(buttons) / sizeof(buttons[0])) ? buttons[index] : 0;
}
#endif

} // namespace

GamepadInput::GamepadInput()
{
#ifdef Q_OS_WIN
    const wchar_t* libraries[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    for (const wchar_t* name : libraries) {
        HMODULE module = LoadLibraryW(name);
        if (!module) continue;
        auto function = GetProcAddress(module, "XInputGetState");
        if (function) { m_library = module; m_getState = reinterpret_cast<void*>(function); break; }
        FreeLibrary(module);
    }
#endif
}

GamepadInput::~GamepadInput()
{
#ifdef Q_OS_WIN
    if (m_library) FreeLibrary(static_cast<HMODULE>(m_library));
#endif
}

QSet<GameAction> GamepadInput::poll(const InputSystemSettings& settings)
{
    QSet<GameAction> result;
#ifdef Q_OS_WIN
    if (!m_getState) { m_connected = false; m_axisX=m_axisY=m_leftTrigger=m_rightTrigger=0.0; return result; }
    XINPUT_STATE state{};
    using GetState = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    const DWORD status = reinterpret_cast<GetState>(m_getState)(0, &state);
    m_connected = status == ERROR_SUCCESS;
    if (!m_connected) { m_axisX=m_axisY=m_leftTrigger=m_rightTrigger=0.0; return result; }
    m_axisX = qBound(-1.0, state.Gamepad.sThumbLX / 32767.0, 1.0);
    m_axisY = qBound(-1.0, state.Gamepad.sThumbLY / 32767.0, 1.0);
    m_leftTrigger = state.Gamepad.bLeftTrigger / 255.0;
    m_rightTrigger = state.Gamepad.bRightTrigger / 255.0;
    const WORD pressed = state.Gamepad.wButtons;
    for (GameAction action : allGameActions()) {
        const QString id = gameActionId(action);
        int index = settings.controllerButtons.value(id, -1);
        if (index < 0) index = defaultButton(action);
        const WORD mask = buttonMask(index);
        if (mask != 0 && (pressed & mask) != 0) result.insert(action);
    }
    if (settings.analogMovement) {
        const double x = m_axisX;
        const double y = m_axisY;
        const double deadzone = qBound(0.0, settings.analogDeadzone, 0.95);
        if (x < -deadzone) result.insert(GameAction::Left);
        if (x >  deadzone) result.insert(GameAction::Right);
        if (y < -deadzone) result.insert(GameAction::Down);
        if (y >  deadzone) result.insert(GameAction::Up);
    }
#else
    Q_UNUSED(settings);
    m_connected = false; m_axisX=m_axisY=m_leftTrigger=m_rightTrigger=0.0;
#endif
    return result;
}

GamepadDialogNavigator::GamepadDialogNavigator(
    QDialog* dialog, const InputSystemSettings& settings)
    : QObject(dialog), m_dialog(dialog), m_settings(&settings)
{
    dialog->setProperty("ludoGamepadNavigator", true);
    // Ignora os botões que já estavam pressionados ao abrir o diálogo.
    // Isso impede que o mesmo B que abriu/fechou uma tela ative duas ações.
    m_held = m_input.poll(settings);
    auto* timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start();
}

void GamepadDialogNavigator::poll()
{
    if (!m_dialog || !m_settings || !m_dialog->isVisible()) return;
    const QSet<GameAction> now = m_input.poll(*m_settings);
    QSet<GameAction> pressed = now - m_held;
    // Em listas grandes, segurar o direcional repete depois de ~350 ms.
    // Confirmar/cancelar continuam somente por borda para nunca disparar duas vezes.
    for(GameAction action:{GameAction::Up,GameAction::Down,GameAction::Left,GameAction::Right}){
        if(!now.contains(action)){m_holdTicks.remove(action);continue;}
        const int ticks=m_holdTicks.value(action)+1;m_holdTicks[action]=ticks;
        if(m_held.contains(action)&&ticks>=22&&(ticks-22)%7==0)pressed.insert(action);
    }
    m_held = now;

    int key = 0;
    if (pressed.contains(GameAction::Up)) key = Qt::Key_Up;
    else if (pressed.contains(GameAction::Down)) key = Qt::Key_Down;
    else if (pressed.contains(GameAction::Left)) key = Qt::Key_Left;
    else if (pressed.contains(GameAction::Right)) key = Qt::Key_Right;
    else if (pressed.contains(GameAction::Confirm)) key = Qt::Key_Return;
    else if (pressed.contains(GameAction::Cancel)) key = Qt::Key_Escape;
    if (!key) return;

    QWidget* modal=QApplication::activeModalWidget();
    // Um diálogo filho com navegador próprio processa o controle sozinho.
    // Diálogos nativos auxiliares (QMessageBox/QInputDialog) herdam o navegador
    // do pai, evitando telas que só possam ser fechadas pelo teclado.
    if(modal&&modal!=m_dialog&&modal->property("ludoGamepadNavigator").toBool())return;
    QWidget* target = QApplication::focusWidget();
    QWidget* targetWindow=modal?modal:m_dialog;
    if (!target || target->window() != targetWindow) target = targetWindow;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QApplication::sendEvent(target, &release);
}

} // namespace game
