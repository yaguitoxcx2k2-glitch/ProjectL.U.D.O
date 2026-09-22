#pragma once

#include "UiDrawList.h"
#include "UiTheme.h"
#include "core/Editor.h"

#include <QFont>
#include <QSize>
#include <QString>
#include <functional>

namespace game { class GameState; }

namespace game::ui {

/// Renderer declarativo do Widget Framework 2D. Não depende de QWidget e
/// converte widgets persistidos em UiDrawList, compartilhado por CPU/QRhi.
class UiWidgetRenderer
{
public:
    using StateFn = std::function<QString(const QString&)>;
    using ClipFn = std::function<QString(const QString&)>;
    using ClipTimeFn = std::function<int(const QString&)>;
    using VisibilityFn = std::function<bool(const QString&, bool)>;
    using WidgetFn = std::function<core::UiWidgetSettings(const QString&, const core::UiWidgetSettings&)>;

    static void appendScreen(UiDrawList& list, const UiTheme& theme,
                             const core::Editor& editor, const GameState& state,
                             const QString& screen, const QSize& size,
                             const QFont& font,
                             const QString& transitionTrigger = QStringLiteral("none"),
                             qreal transitionProgress = 1.0,
                             const StateFn& stateFn = {},
                             const ClipFn& clipFn = {},
                             const ClipTimeFn& clipTimeFn = {},
                             const VisibilityFn& visibilityFn = {},
                             qint64 elapsedMs = 0,
                             const WidgetFn& widgetFn = {});
};

} // namespace game::ui
