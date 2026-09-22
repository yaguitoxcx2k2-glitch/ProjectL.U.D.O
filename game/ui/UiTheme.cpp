#include "UiTheme.h"

#include "core/Editor.h"

#include <QtGlobal>

namespace game::ui {

UiTheme UiTheme::ludoDefault()
{
    UiTheme t;
    t.window.fill = QColor(12, 16, 32, 225);
    t.window.border = QColor("#cfd8ff");
    t.window.innerBorder = QColor(90, 110, 180, 180);
    t.window.borderWidth = 2.0;
    t.window.innerBorderWidth = 1.0;
    t.window.radius = 8.0;
    t.window.innerInset = 4.0;

    t.choiceWindow = t.window;
    t.choiceWindow.fill = QColor(12, 16, 32, 235);

    t.selection = t.window;
    t.selection.fill = QColor(70, 105, 190, 180);
    t.selection.border = Qt::transparent;
    t.selection.innerBorder = Qt::transparent;
    t.selection.borderWidth = 0.0;
    t.selection.innerBorderWidth = 0.0;
    t.selection.radius = 5.0;
    t.selection.innerInset = 0.0;
    return t;
}

UiTheme UiTheme::fromSettings(const core::GameUiSettings& s)
{
    UiTheme t = ludoDefault();
    t.window.fill = s.windowFill;
    t.window.border = s.windowBorder;
    t.window.innerBorder = s.innerBorder;
    t.choiceWindow = t.window;
    t.choiceWindow.fill = s.windowFill;
    t.selection.fill = s.selectionColor;
    t.text = s.textColor;
    t.selectedText = s.selectedTextColor;
    t.accent = s.accentColor;
    t.windowSkin = s.windowSkin;
    t.windowSkinSlices = s.windowSkinSlices;
    t.cursorImage = s.cursorImage;
    t.paddingX = qBound(0, s.paddingX, 128);
    t.paddingY = qBound(0, s.paddingY, 128);
    t.fontFamily = s.fontFamily.trimmed();
    t.fontSize = qBound(6, s.fontSize, 96);
    t.windowOpacity = qBound<qreal>(0.0, s.windowOpacity / 100.0, 1.0);
    t.openAnimation = s.openAnimation;
    t.closeAnimation = s.closeAnimation;
    t.animationEasing = s.animationEasing;
    t.animationMs = qBound(0, s.animationMs, 2000);
    t.cursorSePath = s.cursorSePath;
    t.confirmSePath = s.confirmSePath;
    t.cancelSePath = s.cancelSePath;
    t.soundVolume = qBound(0, s.soundVolume, 100);
    t.menuListRect=s.menuListRect;t.menuDetailRect=s.menuDetailRect;
    t.battleArenaRect=s.battleArenaRect;t.battlePartyRect=s.battlePartyRect;t.battleCommandRect=s.battleCommandRect;
    t.shopListRect=s.shopListRect;t.shopDetailRect=s.shopDetailRect;
    t.themeName = s.themeName;
    t.widgetsInheritWindowSkin = s.widgetsInheritWindowSkin;
    t.styleClasses = s.styleClasses;
    t.widgetTypeStyles = s.widgetTypeStyles;
    t.nativeComponentStyles = s.nativeComponentStyles;
    t.widgets = s.widgets;
    t.layoutElements = s.layoutElements;
    t.screenTransparent = s.screenTransparent;
    t.screenStates = s.screenStates;
    return t;
}

} // namespace game::ui
