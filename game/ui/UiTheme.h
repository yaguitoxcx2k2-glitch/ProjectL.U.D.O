#pragma once

#include "UiDrawList.h"
#include "core/Editor.h"

namespace game::ui {

/// Tema do runtime. A Fase 2 liga esta estrutura às configurações persistentes
/// do projeto: skin/cursor, cores, padding, animações e sons ficam fora do
/// layout das janelas e são compartilhados por CPU/GPU.
struct UiTheme {
    UiPanelStyle window;
    UiPanelStyle choiceWindow;
    UiPanelStyle selection;
    QColor text = QColor("#d7dcf0");
    QColor selectedText = Qt::white;
    QColor accent = QColor("#cfd8ff");
    QColor gaugeBackground = QColor(0, 0, 0, 150);
    QColor gaugeFill = QColor("#79d17c");
    QColor gaugeBorder = QColor(255, 255, 255, 150);

    QImage windowSkin;
    QMargins windowSkinSlices{12, 12, 12, 12};
    QImage cursorImage;
    int paddingX = 12;
    int paddingY = 12;
    QString fontFamily;
    int fontSize = 16;
    qreal windowOpacity = 1.0;
    QString openAnimation = QStringLiteral("scale-fade");
    QString closeAnimation = QStringLiteral("fade");
    QString animationEasing = QStringLiteral("ease-out");
    int animationMs = 140;
    QString cursorSePath;
    QString confirmSePath;
    QString cancelSePath;
    int soundVolume = 80;

    QRectF menuListRect{0.04,0.18,0.38,0.68};
    QRectF menuDetailRect{0.44,0.18,0.52,0.68};
    QRectF battleArenaRect{0.02,0.02,0.96,0.48};
    QRectF battlePartyRect{0.02,0.53,0.56,0.45};
    QRectF battleCommandRect{0.59,0.53,0.39,0.45};
    QRectF shopListRect{0.04,0.14,0.44,0.72};
    QRectF shopDetailRect{0.51,0.14,0.45,0.72};

    // Widget Framework 2D + Style System / Theme do UI Designer 2.6.
    QString themeName = QStringLiteral("LUDO Theme");
    bool widgetsInheritWindowSkin = true;
    QHash<QString, core::UiStyleClassSettings> styleClasses;
    QHash<QString, QString> widgetTypeStyles;
    QHash<QString, QString> nativeComponentStyles;
    QHash<QString, core::UiWidgetSettings> widgets;
    QHash<QString, core::UiLayoutElementSettings> layoutElements;
    QHash<QString, bool> screenTransparent;
    // UI Designer 2.7 / Block B: Screen States viajam com o snapshot do tema
    // para que Menu/Batalha/Loja renderizem o mesmo estado declarativo.
    QHash<QString, core::UiScreenStateSettings> screenStates;

    bool hasWindowSkin() const { return !windowSkin.isNull(); }
    bool hasCursorImage() const { return !cursorImage.isNull(); }

    static UiTheme ludoDefault();
    static UiTheme fromSettings(const core::GameUiSettings& settings);
};

} // namespace game::ui
