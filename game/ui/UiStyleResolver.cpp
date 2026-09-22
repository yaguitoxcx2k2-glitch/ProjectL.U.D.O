#include "UiStyleResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QtGlobal>

namespace game::ui {
namespace {

QImage loadProjectImage(const core::Editor& editor, const QString& path, const QImage& embedded)
{
    if (!embedded.isNull()) return embedded;
    if (path.trimmed().isEmpty()) return {};
    QFileInfo info(path);
    const QString absolute = info.isAbsolute() ? path : QDir(editor.projectRoot()).filePath(path);
    return QImage(absolute).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void applyState(UiResolvedStyle& out, const core::UiStyleStateSettings& state)
{
    if (state.fillColor.isValid()) out.stateOverlay = state.fillColor;
    if (state.textColor.isValid()) out.text = state.textColor;
    if (state.accentColor.isValid()) out.accent = state.accentColor;
    if (state.borderColor.isValid()) out.panel.border = state.borderColor;
    if (state.innerBorderColor.isValid()) out.panel.innerBorder = state.innerBorderColor;
    out.opacity *= qBound<qreal>(0.0, state.opacityMultiplier, 1.0);
}

bool windowLike(const QString& id)
{
    static const QSet<QString> ids = {
        QStringLiteral("message"), QStringLiteral("name-box"), QStringLiteral("choices"),
        QStringLiteral("menu"), QStringLiteral("modal"), QStringLiteral("battle"),
        QStringLiteral("shop"), QStringLiteral("hud"), QStringLiteral("window")
    };
    return ids.contains(id);
}

} // namespace

UiResolvedStyle resolveStyleClass(const UiTheme& theme, const core::Editor& editor,
                                  const core::UiStyleClassSettings& cls,
                                  const QString& stateId, const QFont& fallbackFont)
{
    UiResolvedStyle out;
    out.panel.fill = cls.fillColor;
    out.panel.border = cls.borderColor;
    out.panel.innerBorder = cls.innerBorderColor;
    out.panel.borderWidth = qBound<qreal>(0, cls.borderWidth, 32);
    out.panel.innerBorderWidth = qBound<qreal>(0, cls.innerBorderWidth, 32);
    out.panel.radius = qBound<qreal>(0, cls.radius, 256);
    out.panel.innerInset = qBound<qreal>(0, cls.innerInset, 128);
    out.text = cls.textColor;
    out.accent = cls.accentColor;
    out.backgroundMode = cls.backgroundMode;
    out.slices = cls.slices;
    out.opacity = qBound<qreal>(0, cls.opacity, 1);
    out.paddingX = qBound(0, cls.paddingX, 128);
    out.paddingY = qBound(0, cls.paddingY, 128);
    out.font = fallbackFont;
    if (!cls.fontFamily.trimmed().isEmpty()) out.font.setFamily(cls.fontFamily.trimmed());
    if (cls.fontSize > 0) out.font.setPixelSize(qBound(6, cls.fontSize, 96));
    if (out.backgroundMode == QLatin1String("window-skin")) out.backgroundImage = theme.windowSkin;
    else if (out.backgroundMode == QLatin1String("nine-slice")) out.backgroundImage = loadProjectImage(editor, cls.imagePath, cls.image);
    const auto state = cls.states.constFind(stateId);
    if (state != cls.states.cend()) applyState(out, state.value());
    return out;
}

UiResolvedStyle resolveNativeStyle(const UiTheme& theme, const core::Editor& editor,
                                   const QString& componentId, const QString& stateId,
                                   const QFont& fallbackFont)
{
    const QString id = componentId.trimmed().toLower();
    const QString classId = theme.nativeComponentStyles.value(id);
    const auto cls = theme.styleClasses.constFind(classId);
    if (!classId.isEmpty() && cls != theme.styleClasses.cend())
        return resolveStyleClass(theme, editor, cls.value(), stateId, fallbackFont);

    UiResolvedStyle out;
    out.font = fallbackFont;
    if (!theme.fontFamily.trimmed().isEmpty()) out.font.setFamily(theme.fontFamily.trimmed());
    out.font.setPixelSize(qMax(6, theme.fontSize));
    out.text = theme.text;
    out.accent = theme.accent;
    out.paddingX = theme.paddingX;
    out.paddingY = theme.paddingY;
    out.opacity = theme.windowOpacity;

    if (id == QLatin1String("selection")) {
        out.panel = theme.selection;
        out.backgroundMode = QStringLiteral("color");
        out.text = theme.selectedText;
    } else if (id == QLatin1String("choice-item")) {
        out.panel = theme.selection;
        out.backgroundMode = QStringLiteral("none");
        if (stateId == QLatin1String("selected") || stateId == QLatin1String("hover") || stateId == QLatin1String("focused")) { out.backgroundMode=QStringLiteral("color"); out.text=theme.selectedText; out.stateOverlay=QColor(); }
        else if (stateId == QLatin1String("disabled")) { out.text=QColor("#5f6670"); out.opacity*=0.85; }
    } else if (id == QLatin1String("name-box") || id == QLatin1String("choices")) {
        out.panel = theme.choiceWindow;
        out.backgroundMode = theme.hasWindowSkin() ? QStringLiteral("window-skin") : QStringLiteral("color");
    } else {
        out.panel = theme.window;
        out.backgroundMode = windowLike(id) && theme.hasWindowSkin() ? QStringLiteral("window-skin") : QStringLiteral("color");
    }
    if (out.backgroundMode == QLatin1String("window-skin")) {
        out.backgroundImage = theme.windowSkin;
        out.slices = theme.windowSkinSlices;
    }
    if (id != QLatin1String("choice-item") && (stateId == QLatin1String("hover") || stateId == QLatin1String("focused") || stateId == QLatin1String("selected")))
        out.stateOverlay = theme.selection.fill;
    else if (stateId == QLatin1String("pressed"))
        out.stateOverlay = theme.selection.fill.darker(125);
    else if (stateId == QLatin1String("disabled") && id != QLatin1String("choice-item"))
        out.opacity *= .58;
    return out;
}

UiResolvedStyle resolveNativeStyle(const UiTheme& theme, const QString& componentId,
                                   const QString& stateId, const QFont& fallbackFont)
{
    const QString id = componentId.trimmed().toLower();
    const QString classId = theme.nativeComponentStyles.value(id);
    const auto cls = theme.styleClasses.constFind(classId);
    if (!classId.isEmpty() && cls != theme.styleClasses.cend()) {
        UiResolvedStyle out;
        const auto& c = cls.value();
        out.panel.fill=c.fillColor; out.panel.border=c.borderColor; out.panel.innerBorder=c.innerBorderColor;
        out.panel.borderWidth=qBound<qreal>(0,c.borderWidth,32); out.panel.innerBorderWidth=qBound<qreal>(0,c.innerBorderWidth,32);
        out.panel.radius=qBound<qreal>(0,c.radius,256); out.panel.innerInset=qBound<qreal>(0,c.innerInset,128);
        out.text=c.textColor; out.accent=c.accentColor; out.backgroundMode=c.backgroundMode; out.backgroundImage=c.image;
        out.slices=c.slices; out.opacity=qBound<qreal>(0,c.opacity,1); out.paddingX=qBound(0,c.paddingX,128); out.paddingY=qBound(0,c.paddingY,128);
        out.font=fallbackFont; if(!c.fontFamily.trimmed().isEmpty())out.font.setFamily(c.fontFamily.trimmed()); if(c.fontSize>0)out.font.setPixelSize(qBound(6,c.fontSize,96));
        if(out.backgroundMode==QLatin1String("window-skin"))out.backgroundImage=theme.windowSkin;
        const auto st=c.states.constFind(stateId); if(st!=c.states.cend())applyState(out,st.value());
        return out;
    }
    // Usa um Editor vazio apenas para reaproveitar a semântica de fallback;
    // imagens de Style já viajam carregadas no UiTheme/Runtime Snapshot.
    UiResolvedStyle out; out.font=fallbackFont;
    if(!theme.fontFamily.trimmed().isEmpty())out.font.setFamily(theme.fontFamily.trimmed());out.font.setPixelSize(qMax(6,theme.fontSize));
    out.text=theme.text;out.accent=theme.accent;out.paddingX=theme.paddingX;out.paddingY=theme.paddingY;out.opacity=theme.windowOpacity;
    if(id==QLatin1String("selection")){out.panel=theme.selection;out.backgroundMode=QStringLiteral("color");out.text=theme.selectedText;}
    else if(id==QLatin1String("choice-item")){out.panel=theme.selection;out.backgroundMode=QStringLiteral("none");if(stateId==QLatin1String("selected")||stateId==QLatin1String("hover")||stateId==QLatin1String("focused")){out.backgroundMode=QStringLiteral("color");out.text=theme.selectedText;}else if(stateId==QLatin1String("disabled")){out.text=QColor("#5f6670");out.opacity*=0.85;}}
    else if(id==QLatin1String("name-box")||id==QLatin1String("choices")){out.panel=theme.choiceWindow;out.backgroundMode=theme.hasWindowSkin()?QStringLiteral("window-skin"):QStringLiteral("color");}
    else{out.panel=theme.window;out.backgroundMode=windowLike(id)&&theme.hasWindowSkin()?QStringLiteral("window-skin"):QStringLiteral("color");}
    if(out.backgroundMode==QLatin1String("window-skin")){out.backgroundImage=theme.windowSkin;out.slices=theme.windowSkinSlices;}
    if(id!=QLatin1String("choice-item")&&(stateId==QLatin1String("hover")||stateId==QLatin1String("focused")||stateId==QLatin1String("selected")))out.stateOverlay=theme.selection.fill;
    else if(stateId==QLatin1String("pressed"))out.stateOverlay=theme.selection.fill.darker(125);
    else if(stateId==QLatin1String("disabled")&&id!=QLatin1String("choice-item"))out.opacity*=.58;
    return out;
}

void appendResolvedBackground(UiDrawList& list, const QRectF& rect, const UiTheme& theme,
                              const UiResolvedStyle& style, qreal opacity)
{
    const qreal effective = qBound<qreal>(0, opacity * style.opacity, 1);
    if (style.backgroundMode != QLatin1String("none")) {
        if (style.backgroundMode == QLatin1String("window-skin") && !theme.windowSkin.isNull())
            list.addNineSlice(rect, theme.windowSkin, style.slices, effective, false);
        else if (style.backgroundMode == QLatin1String("nine-slice") && !style.backgroundImage.isNull())
            list.addNineSlice(rect, style.backgroundImage, style.slices, effective, false);
        else
            list.addPanel(rect, style.panel, effective);
    }
    if (style.stateOverlay.isValid() && style.stateOverlay.alpha() > 0) {
        UiPanelStyle overlay = style.panel;
        overlay.fill = style.stateOverlay;
        overlay.border = Qt::transparent;
        overlay.innerBorder = Qt::transparent;
        overlay.borderWidth = 0;
        overlay.innerBorderWidth = 0;
        list.addPanel(rect, overlay, effective);
    }
}

QStringList nativeUiComponentIds()
{
    return {QStringLiteral("theme"), QStringLiteral("message"), QStringLiteral("name-box"),
            QStringLiteral("choices"), QStringLiteral("choice-item"), QStringLiteral("menu"),
            QStringLiteral("modal"), QStringLiteral("battle"), QStringLiteral("shop"),
            QStringLiteral("hud"), QStringLiteral("selection")};
}

QString nativeUiComponentLabel(const QString& componentId)
{
    const QString id = componentId.trimmed().toLower();
    if (id == QLatin1String("theme")) return QObject::tr("Tema Geral");
    if (id == QLatin1String("message")) return QObject::tr("Mensagem");
    if (id == QLatin1String("name-box")) return QObject::tr("Name Box");
    if (id == QLatin1String("choices")) return QObject::tr("Escolhas");
    if (id == QLatin1String("choice-item")) return QObject::tr("Item de Escolha");
    if (id == QLatin1String("menu")) return QObject::tr("Menus");
    if (id == QLatin1String("modal")) return QObject::tr("Janelas / Modais");
    if (id == QLatin1String("battle")) return QObject::tr("Batalha");
    if (id == QLatin1String("shop")) return QObject::tr("Loja");
    if (id == QLatin1String("hud")) return QObject::tr("HUD");
    if (id == QLatin1String("selection")) return QObject::tr("Seleção / Cursor");
    return componentId;
}

} // namespace game::ui
