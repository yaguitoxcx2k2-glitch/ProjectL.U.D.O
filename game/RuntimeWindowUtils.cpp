#include "RuntimeWindowUtils.h"

#include "GameSession.h"
#include "GamepadInput.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWidget>

namespace game {

GamepadPollResult pollRuntimeGamepad(GamepadInput& gamepad,
                                     QSet<core::GameAction>& held,
                                     GameSession& session)
{
    const QSet<core::GameAction> current = gamepad.poll(session.inputSettings());
    session.setGamepadAnalog(gamepad.connected(), gamepad.axisX(), gamepad.axisY(),
                             gamepad.leftTrigger(), gamepad.rightTrigger());
    const double promptDeadzone=qMax(0.08,qBound(0.0,session.inputSettings().analogDeadzone,0.95));
    if (!current.isEmpty() || qAbs(gamepad.axisX()) > promptDeadzone || qAbs(gamepad.axisY()) > promptDeadzone ||
        gamepad.leftTrigger() > 0.10 || gamepad.rightTrigger() > 0.10) session.notifyControllerInput();

    for (core::GameAction action : core::allGameActions()) {
        if (current.contains(action) && !held.contains(action)) {
            if (action == core::GameAction::Cancel && session.canOpenMenu()) {
                session.actionPress(action); // registra borda H antes de o host abrir o menu
                held = current;
                return GamepadPollResult::OpenMenu;
            }
            session.actionPress(action);
        } else if (!current.contains(action) && held.contains(action)) {
            session.actionRelease(action);
        }
    }
    held = current;
    return GamepadPollResult::Continue;
}

QPointF logicalPointFromWindow(const QPointF& windowPoint,
                               const QRect& destination,
                               const QSize& logicalSize)
{
    if (!destination.isValid() || destination.width() <= 0 || destination.height() <= 0 ||
        !logicalSize.isValid()) return QPointF(-1.0, -1.0);
    return QPointF((windowPoint.x() - destination.x()) * logicalSize.width() / destination.width(),
                   (windowPoint.y() - destination.y()) * logicalSize.height() / destination.height());
}



bool isRuntimeBorderlessToggleShortcut(int key, Qt::KeyboardModifiers modifiers,
                                       bool autoRepeat)
{
    if (autoRepeat) return false;
    if (key != Qt::Key_Return && key != Qt::Key_Enter) return false;
    return modifiers.testFlag(Qt::AltModifier);
}

QRect safeRuntimeWindowedGeometry(const QRect& saved, const QRect& available)
{
    if (!available.isValid() || available.isEmpty()) return saved;
    QSize size = saved.isValid() ? saved.size() : QSize(960, 540);
    size.setWidth(qBound(qMin(320, available.width()), size.width(), available.width()));
    size.setHeight(qBound(qMin(240, available.height()), size.height(), available.height()));
    QPoint topLeft = saved.isValid() ? saved.topLeft() : available.center() - QPoint(size.width()/2, size.height()/2);
    topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - size.width() + 1));
    topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - size.height() + 1));
    return QRect(topLeft, size);
}

bool runtimeBorderlessWindow(const QWidget* window)
{
    if (!window) return false;
    const QWidget* host = window->window();
    return host->property("ludoRuntimeBorderless").toBool() || host->isFullScreen();
}

void setRuntimeBorderlessWindow(QWidget* window, bool enabled, bool persist)
{
    if (!window) return;
    QWidget* host = window->window();
    if (!host) return;
    const bool current = runtimeBorderlessWindow(host);
    if (current == enabled) {
        if (persist) QSettings().setValue(QStringLiteral("game/fullscreen"), enabled);
        return;
    }

    if (enabled) {
        // A resolução lógica pertence ao GameSession; só o host muda de área de
        // apresentação. `showFullScreen` no desktop Qt é borderless/compositor,
        // sem trocar o modo de vídeo nem reconstruir GameData/QRhi state.
        host->setProperty("ludoRuntimeWindowedGeometry", host->geometry());
        host->setProperty("ludoRuntimeWindowedState", host->windowState().toInt());
        host->setProperty("ludoRuntimeBorderless", true);
        host->showFullScreen();
    } else {
        const QRect saved = host->property("ludoRuntimeWindowedGeometry").toRect();
        const Qt::WindowStates savedState = Qt::WindowStates::fromInt(
            host->property("ludoRuntimeWindowedState").toInt());
        host->setProperty("ludoRuntimeBorderless", false);
        host->showNormal();
        QScreen* screen = QGuiApplication::screenAt(saved.center());
        if (!screen) screen = host->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen && saved.isValid())
            host->setGeometry(safeRuntimeWindowedGeometry(saved, screen->availableGeometry()));
        if (savedState.testFlag(Qt::WindowMaximized)) host->showMaximized();
    }
    if (persist) QSettings().setValue(QStringLiteral("game/fullscreen"), enabled);
    host->raise();
    host->activateWindow();
}

void toggleRuntimeBorderlessWindow(QWidget* window, bool persist)
{
    setRuntimeBorderlessWindow(window, !runtimeBorderlessWindow(window), persist);
}


} // namespace game
