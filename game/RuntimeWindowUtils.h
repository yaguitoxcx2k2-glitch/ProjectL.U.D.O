// ============================================================================
// RuntimeWindowUtils.h — comportamento de janela compartilhado por CPU/GPU.
//
// Funções pequenas e independentes de QWidget/QRhiWidget para que entrada e
// conversão de coordenadas não tenham duas implementações quase idênticas.
// ============================================================================
#pragma once

#include "core/InputMap.h"

#include <QPointF>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QtCore/Qt>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace game {

class GameSession;
class GamepadInput;

enum class GamepadPollResult { Continue, OpenMenu };

GamepadPollResult pollRuntimeGamepad(GamepadInput& gamepad,
                                     QSet<core::GameAction>& held,
                                     GameSession& session);

QPointF logicalPointFromWindow(const QPointF& windowPoint,
                               const QRect& destination,
                               const QSize& logicalSize);

/// Alt+Enter é reservado pelo host de apresentação e nunca vira input do jogo.
bool isRuntimeBorderlessToggleShortcut(int key, Qt::KeyboardModifiers modifiers,
                                       bool autoRepeat = false);

/// Restaura a geometria Windowed sem ressuscitar coordenadas de monitor que
/// não existe mais. A posição original é preservada sempre que couber.
QRect safeRuntimeWindowedGeometry(const QRect& saved, const QRect& available);

/// Fullscreen Windowed/Borderless compartilhado por F5/F6 e LudoPlayer.
/// Usa o WindowFullScreen do Qt (compositor desktop, sem trocar resolução de
/// vídeo), preserva geometria/estado Windowed e opcionalmente QSettings.
bool runtimeBorderlessWindow(const QWidget* window);
void setRuntimeBorderlessWindow(QWidget* window, bool enabled, bool persist = true);
void toggleRuntimeBorderlessWindow(QWidget* window, bool persist = true);

} // namespace game
