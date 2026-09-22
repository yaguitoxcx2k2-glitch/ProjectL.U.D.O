#include "GameUiLayer.h"

#include "core/IconSet.h"
#include "game/RpgSystem.h"
#include "game/ui/UiDataBinding.h"
#include "game/ui/UiWidgetRenderer.h"
#include "game/ui/UiStyleResolver.h"

#include <QFontMetricsF>
#include <QtGlobal>
#include <cmath>

namespace game::ui {
namespace {

QRectF normalizedUiRect(const QRectF& n, const QSize& size)
{
    return QRectF(n.x()*size.width(), n.y()*size.height(),
                  n.width()*size.width(), n.height()*size.height());
}

const core::UiScreenStateSettings* initialScreenState(const UiTheme& theme, const QString& screen)
{
    for (auto it = theme.screenStates.cbegin(); it != theme.screenStates.cend(); ++it)
        if (it.value().screen == screen && it.value().initial) return &it.value();
    return nullptr;
}

const core::UiScreenStateElementSettings* screenStateElement(const core::UiScreenStateSettings* state, const QString& id)
{
    if (!state) return nullptr;
    const auto it = state->elements.constFind(id);
    return it == state->elements.cend() ? nullptr : &it.value();
}

QString screenStateVisual(const core::UiScreenStateSettings* state, const QString& id)
{
    const auto* ov = screenStateElement(state, id);
    if (!ov) return QStringLiteral("normal");
    if (ov->hasEnabled && !ov->enabled) return QStringLiteral("disabled");
    return ov->visualState.isEmpty() ? QStringLiteral("normal") : ov->visualState;
}

bool screenStateVisible(const core::UiScreenStateSettings* state, const QString& id, bool fallback)
{
    const auto* ov = screenStateElement(state, id);
    return ov && ov->hasVisible ? ov->visible : fallback;
}

QString screenStateClip(const core::UiScreenStateSettings* state, const QString& id)
{
    const auto* ov = screenStateElement(state, id);
    return ov ? ov->animationClip : QString();
}

bool screenUsesUiAction(const UiTheme& theme, const QString& screen, const QString& actionType)
{
    for(auto wit=theme.widgets.cbegin();wit!=theme.widgets.cend();++wit){
        if(wit.value().screen!=screen)continue;
        const auto meta=theme.layoutElements.value(wit.key());
        for(const auto&binding:meta.eventBindings)
            for(const auto&action:binding.actions)
                if(action.type==actionType)return true;
        for(const auto&graph:meta.visualLogicGraphs)
            for(const auto&node:graph.nodes)
                if(node.type==QLatin1String("action")&&node.action.type==actionType)return true;
    }
    return false;
}

int clipEndTime(const UiTheme& theme, const QString& id, const QString& clipName)
{
    if (clipName.isEmpty()) return 0;
    const auto meta = theme.layoutElements.value(id);
    for (const auto& clip : meta.animationClips) if (clip.name == clipName) return clip.durationMs;
    return 0;
}

qreal easedProgress(qreal progress, const QString& easing)
{
    const qreal p = qBound<qreal>(0.0, progress, 1.0);
    if (easing == QLatin1String("linear")) return p;
    if (easing == QLatin1String("ease-in")) return p * p * p;
    if (easing == QLatin1String("ease-in-out"))
        return p < 0.5 ? 4.0 * p * p * p
                       : 1.0 - std::pow(-2.0 * p + 2.0, 3.0) / 2.0;
    if (easing == QLatin1String("back")) {
        constexpr qreal c1 = 1.70158;
        constexpr qreal c3 = c1 + 1.0;
        const qreal t = p - 1.0;
        return 1.0 + c3 * t * t * t + c1 * t * t;
    }
    if (easing == QLatin1String("bounce")) {
        constexpr qreal n1 = 7.5625;
        constexpr qreal d1 = 2.75;
        qreal t = p;
        if (t < 1.0 / d1) return n1 * t * t;
        if (t < 2.0 / d1) { t -= 1.5 / d1; return n1 * t * t + 0.75; }
        if (t < 2.5 / d1) { t -= 2.25 / d1; return n1 * t * t + 0.9375; }
        t -= 2.625 / d1; return n1 * t * t + 0.984375;
    }
    if (easing == QLatin1String("elastic")) {
        if (p <= 0.0 || p >= 1.0) return p;
        constexpr qreal c4 = (2.0 * 3.14159265358979323846) / 3.0;
        return std::pow(2.0, -10.0 * p) * std::sin((p * 10.0 - 0.75) * c4) + 1.0;
    }
    // ease-out é o padrão porque funciona bem tanto para mouse quanto gamepad.
    const qreal inv = 1.0 - p;
    return 1.0 - inv * inv * inv;
}

QRectF transitionRect(const QRectF& base, const QString& animation, qreal rawProgress,
                      const QString& easing, qreal* opacity)
{
    if (animation == QLatin1String("none")) {
        *opacity = 1.0;
        return base;
    }
    const qreal p = easedProgress(rawProgress, easing);
    *opacity = qBound<qreal>(0.0, p, 1.0);
    if (rawProgress >= 0.999) return base;
    if (animation.contains(QStringLiteral("scale"))) {
        const qreal scale = 0.90 + 0.10 * p;
        const QSizeF size(base.width() * scale, base.height() * scale);
        return QRectF(base.center().x() - size.width() * 0.5,
                      base.center().y() - size.height() * 0.5,
                      size.width(), size.height());
    }
    const qreal distance = qBound<qreal>(24.0, qMax(base.width(), base.height()) * 0.08, 72.0);
    if (animation == QLatin1String("slide-up"))
        return base.translated(0, (1.0 - p) * distance);
    if (animation == QLatin1String("slide-down"))
        return base.translated(0, -(1.0 - p) * distance);
    if (animation == QLatin1String("slide-left"))
        return base.translated((1.0 - p) * distance, 0);
    if (animation == QLatin1String("slide-right"))
        return base.translated(-(1.0 - p) * distance, 0);
    return base; // fade
}

core::UiVisualStateSettings runtimeState(const core::UiLayoutElementSettings& meta, const QString& stateId)
{
    core::UiVisualStateSettings normal = meta.visualStates.value(QStringLiteral("normal"), core::UiVisualStateSettings());
    auto normalize = [](core::UiVisualStateSettings state) {
        state.offset.setX(qBound(-2.0, state.offset.x(), 2.0));
        state.offset.setY(qBound(-2.0, state.offset.y(), 2.0));
        state.scale.setWidth(qBound(0.05, state.scale.width(), 5.0));
        state.scale.setHeight(qBound(0.05, state.scale.height(), 5.0));
        state.opacity = qBound(0.0, state.opacity, 1.0);
        if (!state.tint.isValid()) state.tint = Qt::white;
        return state;
    };
    normal = normalize(normal);
    return stateId == QLatin1String("normal") ? normal
        : normalize(meta.visualStates.value(stateId, normal));
}

core::UiVisualStateSettings runtimeClipState(const core::UiAnimationClipSettings& clip, int timeMs)
{
    core::UiVisualStateSettings result;
    if (clip.keyframes.isEmpty()) return result;
    auto assign = [&](const core::UiAnimationKeyframeSettings& key) {
        result.offset = key.offset; result.scale = key.scale; result.opacity = key.opacity;
    };
    if (timeMs < clip.keyframes.first().timeMs) return result;
    if (clip.keyframes.size() == 1 || timeMs == clip.keyframes.first().timeMs) { assign(clip.keyframes.first()); return result; }
    if (timeMs >= clip.keyframes.last().timeMs) { assign(clip.keyframes.last()); return result; }
    for (int i = 0; i + 1 < clip.keyframes.size(); ++i) {
        const auto& a = clip.keyframes.at(i); const auto& b = clip.keyframes.at(i + 1);
        if (timeMs < a.timeMs || timeMs > b.timeMs) continue;
        const qreal p = easedProgress((timeMs - a.timeMs) / qreal(qMax(1, b.timeMs - a.timeMs)), b.easing);
        result.offset = a.offset + (b.offset - a.offset) * p;
        result.scale = QSizeF(a.scale.width() + (b.scale.width() - a.scale.width()) * p,
                              a.scale.height() + (b.scale.height() - a.scale.height()) * p);
        result.opacity = a.opacity + (b.opacity - a.opacity) * p;
        return result;
    }
    return result;
}

QColor multipliedColor(const QColor& base, const QColor& tint)
{
    QColor out((base.red() * tint.red()) / 255,
               (base.green() * tint.green()) / 255,
               (base.blue() * tint.blue()) / 255,
               (base.alpha() * tint.alpha()) / 255);
    return out;
}

QColor opacityColor(QColor color, qreal opacity)
{
    color.setAlpha(qBound(0, qRound(color.alpha() * qBound<qreal>(0.0, opacity, 1.0)), 255));
    return color;
}

UiPanelStyle tintedStyle(UiPanelStyle style, const QColor& tint)
{
    style.fill = multipliedColor(style.fill, tint);
    style.border = multipliedColor(style.border, tint);
    style.innerBorder = multipliedColor(style.innerBorder, tint);
    return style;
}

struct ElementVisual {
    QRectF rect;
    qreal opacity = 1.0;
    QColor tint = Qt::white;
    bool customClip = false;
    bool visible = true;
};

ElementVisual elementVisual(const UiTheme& theme, const QString& elementId, const QRectF& base,
                            const QSize& screenSize, const QString& trigger, qreal rawProgress,
                            const QString& visualStateId = QStringLiteral("normal"),
                            const QString& manualClipName = QString(), int manualClipTimeMs = 0,
                            const UiResolvedDataBindings* data = nullptr)
{
    ElementVisual visual{base, 1.0, Qt::white, false, true};
    const auto metaIt = theme.layoutElements.constFind(elementId);
    if (metaIt == theme.layoutElements.cend()) return visual;
    const core::UiLayoutElementSettings& meta = metaIt.value();
    visual.visible = data && data->hasVisible ? data->visible : meta.visible;
    const QString effectiveState = data && data->hasEnabled && !data->enabled
        ? QStringLiteral("disabled") : visualStateId;
    core::UiVisualStateSettings state = runtimeState(meta, effectiveState);

    for (const core::UiAnimationClipSettings& clip : meta.animationClips) {
        if (clip.trigger != trigger || clip.keyframes.isEmpty()) continue;
        visual.customClip = true;
        const qreal chronological = trigger == QLatin1String("close") ? 1.0 - rawProgress : rawProgress;
        const int timeMs = qRound(qBound<qreal>(0.0, chronological, 1.0) * qMax(1, clip.durationMs));
        const core::UiVisualStateSettings anim = runtimeClipState(clip, timeMs);
        state.offset += anim.offset;
        state.scale = QSizeF(state.scale.width() * anim.scale.width(),
                             state.scale.height() * anim.scale.height());
        state.opacity *= anim.opacity;
        break;
    }

    if (!manualClipName.isEmpty()) {
        for (const core::UiAnimationClipSettings& clip : meta.animationClips) {
            if (clip.name != manualClipName || clip.keyframes.isEmpty()) continue;
            const core::UiVisualStateSettings anim = runtimeClipState(clip, qBound(0, manualClipTimeMs, clip.durationMs));
            state.offset += anim.offset;
            state.scale = QSizeF(state.scale.width() * anim.scale.width(),
                                 state.scale.height() * anim.scale.height());
            state.opacity *= anim.opacity;
            visual.customClip = true;
            break;
        }
    }

    visual.rect.translate(state.offset.x() * screenSize.width(), state.offset.y() * screenSize.height());
    const QPointF pivot(visual.rect.left() + meta.pivot.x() * visual.rect.width(),
                        visual.rect.top() + meta.pivot.y() * visual.rect.height());
    const QSizeF scaled(visual.rect.width() * state.scale.width(), visual.rect.height() * state.scale.height());
    visual.rect = QRectF(pivot.x() - scaled.width() * meta.pivot.x(),
                         pivot.y() - scaled.height() * meta.pivot.y(), scaled.width(), scaled.height());
    visual.opacity = qBound<qreal>(0.0, state.opacity, 1.0);
    visual.tint = state.tint;
    if (data) {
        if (data->hasOpacity) visual.opacity *= qBound<qreal>(0.0, data->opacity, 1.0);
        if (data->hasColor) visual.tint = multipliedColor(visual.tint, data->color);
        if (data->hasEnabled && !data->enabled) visual.opacity *= 0.62;
    }
    visual.opacity = qBound<qreal>(0.0, visual.opacity, 1.0);
    return visual;
}

QRectF animatedRect(const QRectF& base, const UiTheme& theme,
                    const UiModalController& modal, qreal* opacity)
{
    const QString animation = modal.closing() ? theme.closeAnimation : theme.openAnimation;
    return transitionRect(base, animation, modal.visualProgress(), theme.animationEasing, opacity);
}

} // namespace

GameUiLayer::GameUiLayer()
    : m_theme(UiTheme::ludoDefault())
{
}

QRect GameUiLayer::messageGeometry(const MessageView& message, const MessageStyle& style,
                                   const QSize& viewSize, bool hudVisible, int paddingY)
{
    const int margin = 12;
    const QFontMetricsF fm(style.font);
    const int lines = qMax(1, style.maxLines);
    const int textHeight = int(fm.height() * lines);
    const int boxHeight = textHeight + paddingY * 2;
    const int footer = hudVisible ? 22 : 0;
    int y = viewSize.height() - boxHeight - margin - footer;
    if (message.position == BoxPosition::Top) y = margin;
    else if (message.position == BoxPosition::Middle) y = (viewSize.height() - boxHeight) / 2;
    return QRect(margin + message.offsetX, y + message.offsetY,
                 qMax(0, viewSize.width() - margin * 2), boxHeight);
}

QPair<QRect, int> GameUiLayer::choiceGeometry(const ChoiceView& choice,
                                               const MessageStyle& style,
                                               const QSize& viewSize,
                                               int paddingX, int paddingY)
{
    if (!choice.visible || choice.options.isEmpty()) return {};
    const QFontMetricsF metrics(style.font);
    const int rowHeight = qMax(28, int(metrics.height()) + 10);
    double textWidth = 140.0;
    if (choice.richPages.size() == choice.options.size()) {
        for (const TextPage& page : choice.richPages)
            textWidth = qMax(textWidth, measurePage(page, style.font, nullptr).width() + 42.0);
    } else {
        for (const QString& option : choice.options)
            textWidth = qMax(textWidth, metrics.horizontalAdvance(option) + 42.0);
    }
    for (int i = 0; i < choice.options.size(); ++i) {
        if (!choice.disabled.value(i, false)) continue;
        QString disabledText = choice.disabledLabels.value(i).trimmed();
        if (disabledText.isEmpty()) disabledText = choice.options.value(i);
        if (choice.showDisabledReason && !choice.disabledReasons.value(i).trimmed().isEmpty())
            disabledText += QStringLiteral(" — ") + choice.disabledReasons.value(i).trimmed();
        textWidth = qMax(textWidth, metrics.horizontalAdvance(disabledText) + 42.0);
    }
    const int count = choice.options.size();
    int columns = 1;
    if (choice.layout == QLatin1String("horizontal")) columns = count;
    else if (choice.layout == QLatin1String("grid")) columns = qBound(1, choice.columns, count);
    const int rows = qMax(1, int(std::ceil(count / double(columns))));
    const int maxContentW = qMax(120, viewSize.width() - 48 - paddingX * 2);
    int cellWidth = qMin(int(textWidth), qMax(90, (maxContentW - qMax(0, columns - 1) * choice.spacingX) / columns));
    int width = cellWidth * columns + qMax(0, columns - 1) * choice.spacingX + paddingX * 2;
    width = qMin(qMax(120, width), qMax(120, viewSize.width() - 24));
    const int timerHeight = choice.timeLimit > 0.0 ? 10 : 0;
    const int height = rowHeight * rows + qMax(0, rows - 1) * choice.spacingY + paddingY * 2 + timerHeight;
    const int margin = 18;
    int x=(viewSize.width()-width)/2, y=(viewSize.height()-height)/2;
    const QString pos=choice.position.trimmed().toLower();
    if(pos.contains(QStringLiteral("left"))) x=margin; else if(pos.contains(QStringLiteral("right"))) x=viewSize.width()-width-margin;
    if(pos.startsWith(QStringLiteral("top"))) y=margin; else if(pos.startsWith(QStringLiteral("bottom"))) y=viewSize.height()-height-margin;
    x=qBound(0,x+choice.offsetX,qMax(0,viewSize.width()-width));
    y=qBound(0,y+choice.offsetY,qMax(0,viewSize.height()-height));
    return {QRect(x,y,width,height), rowHeight};
}

QPair<QRect, int> GameUiLayer::choiceGeometry(const Interpreter* interpreter,
                                               const QSize& viewSize)
{
    if (!interpreter) return {};
    return choiceGeometry(interpreter->choice(), interpreter->style(), viewSize);
}

int GameUiLayer::choiceIndexAt(const ChoiceView& choice, const MessageStyle& style,
                               const QSize& viewSize, const QPointF& position,
                               int paddingX, int paddingY)
{
    const auto geometry = choiceGeometry(choice, style, viewSize, paddingX, paddingY);
    const QRect box = geometry.first;
    const int rowHeight = geometry.second;
    if (box.isNull() || rowHeight <= 0 || !QRectF(box).contains(position)) return -1;

    const int count = choice.options.size();
    int columns = 1;
    if (choice.layout == QLatin1String("horizontal")) columns = qMax(1, count);
    else if (choice.layout == QLatin1String("grid")) columns = qBound(1, choice.columns, qMax(1, count));

    const int contentW = qMax(1, box.width() - paddingX * 2 - qMax(0, columns - 1) * choice.spacingX);
    const int cellW = qMax(1, contentW / columns);
    const qreal localX = position.x() - box.left() - paddingX;
    const qreal localY = position.y() - box.top() - paddingY;
    if (localX < 0.0 || localY < 0.0) return -1;

    const int strideX = cellW + choice.spacingX;
    const int strideY = rowHeight + choice.spacingY;
    const int col = int(localX) / qMax(1, strideX);
    const int row = int(localY) / qMax(1, strideY);
    if (col < 0 || col >= columns) return -1;
    if ((int(localX) % qMax(1, strideX)) >= cellW) return -1;
    if ((int(localY) % qMax(1, strideY)) >= rowHeight) return -1;

    const int index = row * columns + col;
    return index >= 0 && index < count ? index : -1;
}

QRect GameUiLayer::modalGeometry(const UiModalController& modal, const QFont& font,
                                 const QSize& viewSize, int paddingX, int paddingY)
{
    const QFontMetricsF fm(font);
    const int lineH = qMax(16, int(fm.height()));
    const int rowH = qMax(30, lineH + 12);

    int width = qMax(360, int(fm.horizontalAdvance(modal.title())) + paddingX * 2);
    width = qMax(width, int(fm.horizontalAdvance(modal.prompt())) + paddingX * 2);
    for (const QString& label : modal.labels())
        width = qMax(width, int(fm.horizontalAdvance(label)) + paddingX * 2 + 42);
    width = qMin(width, qMax(280, viewSize.width() - 36));

    // O desenho usa: padding + título + 4px + prompt + 14px e só então o
    // conteúdo. A geometria segue exatamente essa conta para que Number Input,
    // Confirm (duas linhas) e listas não ultrapassem a Window Skin.
    int height = paddingY * 2 + (lineH + 4) + 4;
    if (!modal.prompt().isEmpty()) height += lineH + 14;
    else height += 8;

    switch (modal.type()) {
    case UiModalType::NumberInput:
        height += qMax(42, lineH + 18) + lineH + 14; // valor + faixa min/max
        break;
    case UiModalType::TextInput:
        height += qMax(42, lineH + 18) + lineH + 14; // campo + contador
        break;
    case UiModalType::Confirm:
        height += qMax(1, modal.labels().size()) * rowH + 4;
        break;
    case UiModalType::ItemSelection: {
        const int visible = qMax(1, qMin(6, modal.labels().size()));
        height += visible * rowH;
        if (modal.labels().size() > 6) height += lineH + 6;
        height += 4;
        break;
    }
    case UiModalType::None:
        height += rowH;
        break;
    }

    height = qMin(height, qMax(120, viewSize.height() - 36));
    return QRect((viewSize.width() - width) / 2, (viewSize.height() - height) / 2,
                 width, height);
}

QPair<QRect, int> GameUiLayer::modalListGeometry(const UiModalController& modal,
                                                  const QFont& font,
                                                  const QSize& viewSize,
                                                  int paddingX, int paddingY)
{
    const QRect box = modalGeometry(modal, font, viewSize, paddingX, paddingY);
    const QFontMetricsF fm(font);
    const int lineH = qMax(16, int(fm.height()));
    int top = box.top() + paddingY + (lineH + 4) + 4;
    if (!modal.prompt().isEmpty()) top += lineH + 14;
    else top += 8;
    const int row = qMax(30, lineH + 12);
    return {QRect(box.left() + paddingX, top, box.width() - paddingX * 2,
                  qMax(0, box.bottom() - paddingY - top)), row};
}

int GameUiLayer::modalFirstVisible(const UiModalController& modal, int maxRows)
{
    maxRows = qMax(1, maxRows);
    const int count = modal.labels().size();
    if (count <= maxRows) return 0;
    const int centered = modal.selected() - maxRows / 2;
    return qBound(0, centered, count - maxRows);
}

QPair<QRect, int> GameUiLayer::menuListGeometry(const QSize& viewSize, int paddingX, int paddingY)
{
    // 3.20: o cabeçalho/status e a barra de ajuda deixaram de ser elementos
    // obrigatórios. O menu funcional usa toda a área útil; quem quiser barras
    // pode criá-las como Widgets no UI Designer.
    const int margin = qMax(12, paddingX);
    const QRect body(margin, margin,
                     qMax(120, viewSize.width() - margin * 2),
                     qMax(120, viewSize.height() - margin * 2));
    const int listW = qBound(230, int(body.width() * 0.40), qMax(230, body.width() - 300));
    const QRect list(body.left() + paddingX, body.top() + paddingY,
                     qMax(100, listW - paddingX),
                     qMax(80, body.height() - paddingY * 2));
    const int row = qMax(32, list.height() / 9);
    return {list, row};
}

void GameUiLayer::rebuild(const Interpreter* interpreter, const UiModalController* modal,
                          const UiMenuController* menu, const UiBattleController* battle,
                          const UiShopController* shop, const QString& customScreen, const QSize& viewSize,
                          bool hudVisible, qint64 elapsedMs, const core::IconSet* icons)
{
    m_elapsedMs = qMax<qint64>(0, elapsedMs);
    m_canvas.beginFrame(viewSize);
    if (interpreter) {
        appendMessage(*interpreter, hudVisible, elapsedMs, icons);
        appendChoice(*interpreter, icons);
    }
    QFont font = interpreter ? interpreter->style().font : QFont();
    if (!m_theme.fontFamily.isEmpty()) font.setFamily(m_theme.fontFamily);
    font.setPixelSize(qMax(6, m_theme.fontSize));
    if (battle && battle->active()) appendBattle(*battle, font);
    else if (shop && shop->active()) appendShop(*shop, font);
    else {
        if (modal && modal->active()) appendModal(*modal, font);
        if (menu && menu->active()) appendMenu(*menu, font);
    }
    if (!customScreen.isEmpty() && menu) appendCustomScreen(*menu, customScreen, font);
}

void GameUiLayer::appendCustomScreen(const UiMenuController& dataSource, const QString& screen, const QFont& baseFont)
{
    UiDrawList& list = m_canvas.drawList();
    const QSize size = m_canvas.logicalSize();
    UiWidgetRenderer::appendScreen(list, m_theme, dataSource.editor(), dataSource.state(),
        screen, size, baseFont, QStringLiteral("open"), 1.0,
        [&](const QString& id){ return dataSource.runtimeVisualState(id); },
        [&](const QString& id){ return dataSource.runtimeClipName(id); },
        [&](const QString& id){ return dataSource.runtimeClipTimeMs(id); },
        [&](const QString& id, bool fallback){ return dataSource.runtimeVisibility(id, fallback); },
        dataSource.uiElapsedMs(),
        [&](const QString& id, const core::UiWidgetSettings& base){ auto w=base; dataSource.applyRuntimeWidget(id,w); return w; });
}

void GameUiLayer::appendWindow(UiDrawList& list, const QRectF& rect,
                               const UiPanelStyle& fallback, qreal opacity) const
{
    if (m_theme.hasWindowSkin())
        list.addNineSlice(rect, m_theme.windowSkin, m_theme.windowSkinSlices,
                          opacity * m_theme.windowOpacity, false);
    else
        list.addPanel(rect, fallback, opacity * m_theme.windowOpacity);
}

void GameUiLayer::appendCursor(UiDrawList& list, const QRectF& slot,
                               const QFont& font, qreal opacity, const QColor& color) const
{
    if (m_theme.hasCursorImage()) {
        const qreal side = qMin(slot.height() - 4.0, 24.0);
        list.addImage(QRectF(slot.left() + 2, slot.center().y() - side * 0.5, side, side),
                      m_theme.cursorImage, QRectF(), opacity, false);
    } else {
        list.addText(QRectF(slot.left() + 3, slot.top(), 20, slot.height()),
                     QStringLiteral("▶"), font, color.isValid() ? color : m_theme.selectedText,
                     Qt::AlignVCenter | Qt::AlignLeft, opacity);
    }
}

void GameUiLayer::appendMessage(const Interpreter& interpreter, bool hudVisible,
                                qint64 elapsedMs, const core::IconSet* icons)
{
    const MessageView& mv = interpreter.message();
    if (!mv.visible) return;

    MessageStyle style = interpreter.style();
    UiResolvedStyle messageStyle = resolveNativeStyle(m_theme, QStringLiteral("message"), QStringLiteral("normal"), style.font);
    style.font = messageStyle.font;
    if (!mv.fontFamily.isEmpty()) style.font.setFamily(mv.fontFamily);
    if (mv.fontSize > 0) style.font.setPixelSize(qBound(6, mv.fontSize, 96));
    const QRect box = messageGeometry(mv, style, m_canvas.logicalSize(), hudVisible, messageStyle.paddingY);
    UiDrawList& list = m_canvas.drawList();
    appendResolvedBackground(list, box, m_theme, messageStyle);

    qreal portraitInset=0.0;
    if(!mv.portraitImage.isNull()){
        const qreal maxH=qMax(32.0,box.height()-8.0);const qreal scale=qMin(maxH/mv.portraitImage.height(),qreal(1.0));
        const QSizeF size(mv.portraitImage.width()*scale,mv.portraitImage.height()*scale);
        const bool right=mv.portraitPosition==QLatin1String("right");
        const QRectF target(right?box.right()-size.width()-4:box.left()+4,box.bottom()-size.height()-4,size.width(),size.height());
        const qreal fade=qBound(0.0,mv.effectTimeSec/0.18,1.0);list.addImage(target,mv.portraitImage,QRectF(),fade,true);portraitInset=size.width()+8.0;
    }

    if (!mv.speaker.isEmpty()) {
        UiResolvedStyle nameStyle = resolveNativeStyle(m_theme, QStringLiteral("name-box"), QStringLiteral("normal"), style.font);
        const QFontMetricsF fm(nameStyle.font);
        const int nameH = qMax(30, int(fm.height()) + nameStyle.paddingY);
        const int nameW = qMin(box.width(), qMax(120, int(fm.horizontalAdvance(mv.speaker)) + nameStyle.paddingX * 2));
        QRect nameBox(box.left() + 10, box.top() - nameH + 5, nameW, nameH);
        if (nameBox.top() < 4) nameBox.moveTop(box.bottom() + 4);
        appendResolvedBackground(list, nameBox, m_theme, nameStyle);
        list.addText(QRectF(nameBox).adjusted(nameStyle.paddingX, 0, -nameStyle.paddingX, 0),
                     mv.speaker, nameStyle.font, mv.nameColor.isValid()?mv.nameColor:nameStyle.text, Qt::AlignVCenter | Qt::AlignLeft);
    }

    TextDrawOpts opts;
    opts.color = mv.textColor.isValid()?mv.textColor:messageStyle.text;
    opts.revealed = mv.drawableRevealed;
    opts.revealTimesSec = mv.revealTimesSec;
    opts.time = mv.effectTimeSec;
    opts.exitTime = mv.exitTimeSec;
    opts.icons = icons;
    opts.gradient = mv.gradient;
    opts.effects = mv.effects;
    const bool portraitRight=mv.portraitPosition==QLatin1String("right");
    list.addRichText(QRectF(box.left() + messageStyle.paddingX + (portraitInset>0&&!portraitRight?portraitInset:0), box.top() + messageStyle.paddingY,
                            box.width() - messageStyle.paddingX * 2 - portraitInset,
                            box.height() - messageStyle.paddingY * 2),
                     mv.page, style.font, opts);

    if (mv.waitingKey && (elapsedMs / 400) % 2 == 0) {
        list.addTriangle(QPointF(box.right() - 18, box.bottom() - 12),
                         QSizeF(12, 8), messageStyle.accent, 1.0, true);
    }
}

void GameUiLayer::appendChoice(const Interpreter& interpreter, const core::IconSet* icons)
{
    const ChoiceView& choice = interpreter.choice();
    if (!choice.visible || choice.options.isEmpty()) return;
    MessageStyle style = interpreter.style();
    UiResolvedStyle windowStyle = resolveNativeStyle(m_theme, QStringLiteral("choices"), QStringLiteral("normal"), style.font);
    style.font = windowStyle.font;
    if (!choice.fontFamily.isEmpty()) style.font.setFamily(choice.fontFamily);
    if (choice.fontSize > 0) style.font.setPixelSize(qBound(6, choice.fontSize, 96));
    const int px = windowStyle.paddingX + 2;
    const int py = windowStyle.paddingY + 2;
    const auto geometry = choiceGeometry(choice, style, m_canvas.logicalSize(), px, py);
    const QRect box = geometry.first;
    const int rowHeight = geometry.second;
    UiDrawList& list = m_canvas.drawList();
    if (choice.boxMode != QLatin1String("transparent")) appendResolvedBackground(list, box, m_theme, windowStyle);

    const int count = choice.options.size();
    int columns = 1;
    if (choice.layout == QLatin1String("horizontal")) columns = count;
    else if (choice.layout == QLatin1String("grid")) columns = qBound(1, choice.columns, count);
    const int contentW = qMax(1, box.width() - px * 2 - qMax(0, columns - 1) * choice.spacingX);
    const int cellW = qMax(1, contentW / columns);
    int align = Qt::AlignVCenter | Qt::AlignLeft;
    if (choice.alignment == QLatin1String("center")) align = Qt::AlignCenter;
    else if (choice.alignment == QLatin1String("right")) align = Qt::AlignVCenter | Qt::AlignRight;

    for (int index = 0; index < count; ++index) {
        const int col = index % columns;
        const int rowIndex = index / columns;
        const QRect row(box.left() + px + col * (cellW + choice.spacingX),
                        box.top() + py + rowIndex * (rowHeight + choice.spacingY),
                        cellW, rowHeight);
        const bool disabled = choice.disabled.value(index, false);
        const bool selected = index == choice.selected && !disabled;
        const QString state = disabled ? QStringLiteral("disabled") : (selected ? QStringLiteral("selected") : QStringLiteral("normal"));
        UiResolvedStyle itemStyle = resolveNativeStyle(m_theme, QStringLiteral("choice-item"), state, style.font);
        appendResolvedBackground(list, row, m_theme, itemStyle);
        if (selected) appendCursor(list, QRectF(row.left(), row.top(), 26, row.height()), itemStyle.font, 1.0, itemStyle.text);
        const qreal leftPad = selected ? 30.0 : 8.0;
        const QRectF textRect(row.left() + leftPad, row.top() + 4, row.width() - leftPad - 6, row.height() - 8);
        QString disabledText;
        if (disabled) {
            disabledText = choice.disabledLabels.value(index).trimmed();
            if (disabledText.isEmpty()) disabledText = choice.options.value(index);
            if (choice.showDisabledReason && !choice.disabledReasons.value(index).trimmed().isEmpty())
                disabledText += QStringLiteral(" — ") + choice.disabledReasons.value(index).trimmed();
        }
        if (!disabled && index < choice.richPages.size() && !choice.richPages[index].lines.isEmpty()) {
            TextDrawOpts opts; opts.color = itemStyle.text; opts.icons = icons;
            opts.time = choice.effectTimeSec; opts.exitTime = choice.exitTimeSec;
            opts.gradient = choice.gradient; opts.effects = choice.effects;
            opts.opacity = itemStyle.opacity;
            opts.align = choice.alignment == QLatin1String("center") ? TextAlign::Center
                       : choice.alignment == QLatin1String("right") ? TextAlign::Right
                                                                    : TextAlign::Left;
            list.addRichText(textRect, choice.richPages[index], itemStyle.font, opts);
        } else {
            list.addText(textRect, disabled ? disabledText : choice.options[index], itemStyle.font, itemStyle.text, align, itemStyle.opacity);
        }
    }
    if (choice.timeLimit > 0.0) {
        const qreal ratio = qBound(0.0, choice.timeRemaining / choice.timeLimit, 1.0);
        const QRectF track(box.left() + px, box.bottom() - 7,
                           qMax(1, box.width() - px * 2), 4);
        list.addGauge(track, ratio, QColor(255, 255, 255, 48),
                      windowStyle.accent, QColor(255, 255, 255, 72), 2.0);
    }
}

void GameUiLayer::appendModal(const UiModalController& modal, const QFont& baseFont)
{
    UiDrawList& list = m_canvas.drawList();
    UiResolvedStyle modalStyle = resolveNativeStyle(m_theme, QStringLiteral("modal"), QStringLiteral("normal"), baseFont);
    UiResolvedStyle selectedStyle = resolveNativeStyle(m_theme, QStringLiteral("selection"), QStringLiteral("selected"), modalStyle.font);
    const QRect base = modalGeometry(modal, modalStyle.font, m_canvas.logicalSize(),
                                     modalStyle.paddingX + 6, modalStyle.paddingY + 4);
    qreal opacity = 1.0;
    const QRectF box = animatedRect(base, m_theme, modal, &opacity);
    appendResolvedBackground(list, box, m_theme, modalStyle, opacity);

    const int px = modalStyle.paddingX + 6;
    const int py = modalStyle.paddingY + 4;
    const QFontMetricsF fm(modalStyle.font);
    QFont titleFont = modalStyle.font;
    titleFont.setBold(true);
    const qreal titleH = fm.height() + 4;
    list.addText(QRectF(box.left() + px, box.top() + py,
                        box.width() - px * 2, titleH),
                 modal.title(), titleFont, modalStyle.accent,
                 Qt::AlignLeft | Qt::AlignVCenter, opacity * modalStyle.opacity);
    qreal cursorY = box.top() + py + titleH + 4;
    if (!modal.prompt().isEmpty()) {
        list.addText(QRectF(box.left() + px, cursorY,
                            box.width() - px * 2, fm.height() + 6),
                     modal.prompt(), modalStyle.font, modalStyle.text,
                     Qt::AlignLeft | Qt::AlignVCenter, opacity * modalStyle.opacity);
        cursorY += fm.height() + 14;
    } else {
        cursorY += 8;
    }

    if (modal.type() == UiModalType::NumberInput) {
        const QRectF numberRect(box.left() + px, cursorY,
                                box.width() - px * 2, qMax<qreal>(42, fm.height() + 18));
        appendResolvedBackground(list, numberRect, m_theme, selectedStyle, opacity);
        list.addText(numberRect.adjusted(12, 0, -12, 0),
                     QString::number(modal.numberValue()), selectedStyle.font,
                     selectedStyle.text, Qt::AlignCenter, opacity * selectedStyle.opacity);
        const QString range = QStringLiteral("%1  —  %2").arg(modal.minimum()).arg(modal.maximum());
        list.addText(QRectF(numberRect.left(), numberRect.bottom() + 4,
                            numberRect.width(), fm.height() + 4),
                     range, modalStyle.font, modalStyle.text, Qt::AlignCenter, opacity * 0.75 * modalStyle.opacity);
        return;
    }
    if (modal.type() == UiModalType::TextInput) {
        const QRectF textRect(box.left() + px, cursorY,
                              box.width() - px * 2, qMax<qreal>(42, fm.height() + 18));
        appendResolvedBackground(list, textRect, m_theme, selectedStyle, opacity);
        const QString shown = modal.textValue() + QStringLiteral("▌");
        list.addText(textRect.adjusted(12, 0, -12, 0), shown, selectedStyle.font,
                     selectedStyle.text, Qt::AlignLeft | Qt::AlignVCenter, opacity * selectedStyle.opacity);
        const QString count = QStringLiteral("%1 / %2").arg(modal.textValue().size()).arg(modal.maximumTextLength());
        list.addText(QRectF(textRect.left(), textRect.bottom() + 4,
                            textRect.width(), fm.height() + 4),
                     count, modalStyle.font, modalStyle.text, Qt::AlignRight | Qt::AlignVCenter, opacity * 0.75 * modalStyle.opacity);
        return;
    }

    const int rowHeight = qMax(30, int(fm.height()) + 12);
    constexpr int maxVisibleRows = 6;
    const int first = modalFirstVisible(modal, maxVisibleRows);
    const int last = qMin(modal.labels().size(), first + maxVisibleRows);
    for (int i = first; i < last; ++i) {
        const int visualRow = i - first;
        QRectF row(box.left() + px, cursorY + visualRow * rowHeight,
                   box.width() - px * 2, rowHeight);
        const bool selected = i == modal.selected();
        UiResolvedStyle rowStyle = resolveNativeStyle(m_theme, QStringLiteral("choice-item"),
                                                       selected ? QStringLiteral("selected") : QStringLiteral("normal"), modalStyle.font);
        appendResolvedBackground(list, row, m_theme, rowStyle, opacity);
        if (selected) appendCursor(list, QRectF(row.left(), row.top(), 26, row.height()), rowStyle.font, opacity, rowStyle.text);
        list.addText(row.adjusted(selected ? 30 : 8, 0, -6, 0), modal.labels().at(i), rowStyle.font,
                     rowStyle.text, Qt::AlignLeft | Qt::AlignVCenter, opacity * rowStyle.opacity);
    }
    if (modal.labels().size() > maxVisibleRows) {
        const QString counter = QStringLiteral("%1 / %2").arg(modal.selected() + 1).arg(modal.labels().size());
        list.addText(QRectF(box.right() - 92, box.bottom() - py - fm.height(), 76, fm.height()),
                     counter, modalStyle.font, modalStyle.text, Qt::AlignRight | Qt::AlignVCenter, opacity * 0.75 * modalStyle.opacity);
    }
}

void GameUiLayer::appendMenu(const UiMenuController& menu, const QFont& baseFont)
{
    UiDrawList& list = m_canvas.drawList();
    const QSize size = m_canvas.logicalSize();
    UiResolvedStyle menuStyle = resolveNativeStyle(m_theme, QStringLiteral("menu"), QStringLiteral("normal"), baseFont);
    UiResolvedStyle selectionStyle = resolveNativeStyle(m_theme, QStringLiteral("selection"), QStringLiteral("selected"), menuStyle.font);
    const int margin = qMax(12, menuStyle.paddingX);
    const QRectF screen(0, 0, size.width(), size.height());

    const QString designedScreen = (menu.screen() == UiMenuScreen::Save || menu.screen() == UiMenuScreen::SaveOverwrite)
                                       ? QStringLiteral("save")
                                       : menu.screen() == UiMenuScreen::Load ? QStringLiteral("load")
                                                                             : QStringLiteral("menu");
    const bool fullyDesignedSave = designedScreen==QLatin1String("save") && screenUsesUiAction(m_theme,designedScreen,QStringLiteral("save-slot"));
    const bool fullyDesignedLoad = designedScreen==QLatin1String("load") && screenUsesUiAction(m_theme,designedScreen,QStringLiteral("load-slot"));
    const bool transparentBase = m_theme.screenTransparent.value(designedScreen, false);

    // Save/Load passam a ser telas verdadeiramente substituíveis. Assim que a
    // tela desenhada contém a ação funcional correspondente, o renderer legado
    // sai completamente do caminho. Projetos antigos continuam com fallback.
    if(fullyDesignedSave||fullyDesignedLoad){
        const qreal progress=qBound<qreal>(0.0,menu.transitionProgress(),1.0);
        const QString elementTrigger=menu.closing()?QStringLiteral("close"):QStringLiteral("open");
        UiWidgetRenderer::appendScreen(list,m_theme,menu.editor(),menu.state(),designedScreen,size,baseFont,elementTrigger,progress,
            [&](const QString&id){return menu.runtimeVisualState(id);},
            [&](const QString&id){return menu.runtimeClipName(id);},
            [&](const QString&id){return menu.runtimeClipTimeMs(id);},
            [&](const QString&id,bool fallback){return menu.runtimeVisibility(id,fallback);},menu.uiElapsedMs(),
            [&](const QString&id,const core::UiWidgetSettings&base){auto w=base;menu.applyRuntimeWidget(id,w);return w;});
        return;
    }

    UiPanelStyle shade = m_theme.window;
    shade.fill = QColor(0, 0, 0, 120);
    shade.border = Qt::transparent;
    shade.innerBorder = Qt::transparent;
    shade.borderWidth = 0;
    shade.innerBorderWidth = 0;
    shade.radius = 0;
    if (!transparentBase) list.addPanel(screen, shade, 1.0);

    const qreal progress = qBound<qreal>(0.0, menu.transitionProgress(), 1.0);
    const QString menuAnimation = menu.closing() ? m_theme.closeAnimation : m_theme.openAnimation;
    qreal opacity = 1.0;

    // Barras superior/inferior fixas removidas em 3.20. Tudo que for HUD de
    // menu agora é responsabilidade da tela construída no UI Designer.
    QRectF bodyBase(margin, margin,
                    size.width() - margin * 2,
                    qMax(120, size.height() - margin * 2));
    qreal bodyOpacity = 1.0;
    QRectF body = transitionRect(bodyBase, menuAnimation, progress,
                                 m_theme.animationEasing, &bodyOpacity);
    if (!transparentBase) appendResolvedBackground(list, body, m_theme, menuStyle, bodyOpacity);

    const QString elementTrigger = menu.closing() ? QStringLiteral("close") : QStringLiteral("open");
    const UiResolvedDataBindings listData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("menu.list")), menu.editor(), menu.state());
    const UiResolvedDataBindings detailData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("menu.detail")), menu.editor(), menu.state());
    ElementVisual listVisual = elementVisual(m_theme, QStringLiteral("menu.list"),
                                             normalizedUiRect(m_theme.menuListRect, size), size, elementTrigger, progress,
                                             menu.runtimeVisualState(QStringLiteral("menu.list")),
                                             menu.runtimeClipName(QStringLiteral("menu.list")), menu.runtimeClipTimeMs(QStringLiteral("menu.list")), &listData);
    ElementVisual detailVisual = elementVisual(m_theme, QStringLiteral("menu.detail"),
                                               normalizedUiRect(m_theme.menuDetailRect, size), size, elementTrigger, progress,
                                               menu.runtimeVisualState(QStringLiteral("menu.detail")),
                                               menu.runtimeClipName(QStringLiteral("menu.detail")), menu.runtimeClipTimeMs(QStringLiteral("menu.detail")), &detailData);
    listVisual.visible = menu.runtimeVisibility(QStringLiteral("menu.list"), listVisual.visible);
    detailVisual.visible = menu.runtimeVisibility(QStringLiteral("menu.detail"), detailVisual.visible);
    if (!listVisual.visible) listVisual.opacity = 0.0;
    if (!detailVisual.visible) detailVisual.opacity = 0.0;
    if (!listVisual.customClip) {
        qreal legacyOpacity = 1.0;
        listVisual.rect = transitionRect(listVisual.rect, menuAnimation, progress, m_theme.animationEasing, &legacyOpacity);
        listVisual.opacity *= legacyOpacity;
    }
    if (!detailVisual.customClip) {
        qreal legacyOpacity = 1.0;
        detailVisual.rect = transitionRect(detailVisual.rect, menuAnimation, progress, m_theme.animationEasing, &legacyOpacity);
        detailVisual.opacity *= legacyOpacity;
    }
    QRectF listRect = listVisual.rect.intersected(screen.adjusted(margin, margin, -margin, -margin));
    QRectF detailRect = detailVisual.rect.intersected(screen.adjusted(margin, margin, -margin, -margin));
    const qreal listOpacity = listVisual.opacity;
    const qreal detailOpacity = detailVisual.opacity;

    appendResolvedBackground(list, listRect, m_theme, menuStyle, listOpacity * 0.72);
    appendResolvedBackground(list, detailRect, m_theme, menuStyle, detailOpacity * 0.55);

    const int rowH = qMax(32, int(listRect.height()) / 9);
    const int first = menu.firstVisible(9);
    const int last = qMin(menu.entries().size(), first + 9);
    for (int i = first; i < last; ++i) {
        const UiMenuEntry& entry = menu.entries().at(i);
        const int vr = i - first;
        QRectF row(listRect.left() + 6, listRect.top() + vr * rowH + 4,
                   listRect.width() - 12, rowH - 4);
        const bool selected = i == menu.selected();
        if (selected) {
            appendResolvedBackground(list, row, m_theme, selectionStyle, listOpacity);
            appendCursor(list, QRectF(row.left(), row.top(), 26, row.height()), selectionStyle.font, listOpacity, selectionStyle.text);
        }
        QColor color = selected ? selectionStyle.text : menuStyle.text;
        if (!entry.enabled) color.setAlpha(105);
        list.addText(row.adjusted(30, 0, -8, 0), entry.label, menuStyle.font, color,
                     Qt::AlignLeft | Qt::AlignVCenter, listOpacity);
    }
    if (menu.entries().size() > 9) {
        const QString count = QStringLiteral("%1 / %2").arg(menu.selected() + 1).arg(menu.entries().size());
        list.addText(QRectF(listRect.right() - 90, listRect.bottom() - 24, 76, 20),
                     count, menuStyle.font, menuStyle.text, Qt::AlignRight | Qt::AlignVCenter,
                     listOpacity * 0.75);
    }

    QFont detailTitle = menuStyle.font;
    detailTitle.setBold(true);
    list.addText(QRectF(detailRect.left() + m_theme.paddingX, detailRect.top() + m_theme.paddingY,
                        detailRect.width() - m_theme.paddingX * 2, 28),
                 menu.screen() == UiMenuScreen::Main ? QStringLiteral("LUDO") : menu.title(),
                 detailTitle, menuStyle.accent, Qt::AlignLeft | Qt::AlignVCenter, detailOpacity);
    list.addText(QRectF(detailRect.left() + m_theme.paddingX,
                        detailRect.top() + m_theme.paddingY + 34,
                        detailRect.width() - m_theme.paddingX * 2,
                        detailRect.height() - m_theme.paddingY * 2 - 70),
                 (detailData.hasText ? detailData.text : menu.detail()), menuStyle.font, menuStyle.text,
                 int(Qt::AlignLeft | Qt::AlignTop) | int(Qt::TextWordWrap), detailOpacity);
    if (!menu.notice().isEmpty()) {
        list.addText(QRectF(detailRect.left() + m_theme.paddingX,
                            detailRect.bottom() - 38,
                            detailRect.width() - m_theme.paddingX * 2, 30),
                     menu.notice(), menuStyle.font, menuStyle.accent,
                     int(Qt::AlignLeft | Qt::AlignVCenter) | int(Qt::TextWordWrap), detailOpacity);
    }

    UiWidgetRenderer::appendScreen(list, m_theme, menu.editor(), menu.state(),
        designedScreen, size, baseFont, elementTrigger, progress,
        [&](const QString& id){ return menu.runtimeVisualState(id); },
        [&](const QString& id){ return menu.runtimeClipName(id); },
        [&](const QString& id){ return menu.runtimeClipTimeMs(id); },
        [&](const QString& id, bool fallback){ return menu.runtimeVisibility(id, fallback); }, menu.uiElapsedMs(),
        [&](const QString& id, const core::UiWidgetSettings& base){ auto w=base; menu.applyRuntimeWidget(id,w); return w; });
}


void GameUiLayer::appendBattle(const UiBattleController& battle, const QFont& baseFont)
{
    UiDrawList& list = m_canvas.drawList();
    const QSize size = m_canvas.logicalSize();
    const UiResolvedStyle battleStyle = resolveNativeStyle(m_theme, QStringLiteral("battle"), QStringLiteral("normal"), baseFont);
    const UiResolvedStyle battleSelection = resolveNativeStyle(m_theme, QStringLiteral("selection"), QStringLiteral("selected"), battleStyle.font);
    const QFont uiFont = battleStyle.font;
    const core::UiScreenStateSettings* screenState = initialScreenState(m_theme, QStringLiteral("battle"));
    const QRectF screen(0, 0, size.width(), size.height());
    if (!battle.background().isNull()) list.addImage(screen, battle.background(), QRectF(), 1.0, true);
    UiPanelStyle shade = m_theme.window;
    shade.fill = battle.background().isNull() ? QColor(8, 12, 24, 255) : QColor(0, 0, 0, 58);
    shade.border = Qt::transparent; shade.innerBorder = Qt::transparent;
    shade.borderWidth = shade.innerBorderWidth = 0; shade.radius = 0;
    if (!m_theme.screenTransparent.value(QStringLiteral("battle"), false)) list.addPanel(screen, shade);

    QFont title = uiFont; title.setBold(true); title.setPixelSize(qMax(uiFont.pixelSize() + 3, 18));
    if (battle.mode() == UiBattleMode::Result) {
        const QRectF box(size.width()*0.18, size.height()*0.23, size.width()*0.64, size.height()*0.52);
        appendResolvedBackground(list, box, m_theme, battleStyle);
        list.addText(box.adjusted(24,18,-24,-70), battle.resultSummary(), title, battleStyle.accent,
                     int(Qt::AlignCenter) | int(Qt::TextWordWrap));
        const QRectF row(box.left()+40,box.bottom()-60,box.width()-80,38);
        appendResolvedBackground(list,row,m_theme,battleSelection); appendCursor(list,QRectF(row.left(),row.top(),28,row.height()),battleSelection.font,1.0,battleSelection.text);
        list.addText(row.adjusted(30,0,-8,0), QStringLiteral("Continuar"), battleSelection.font, battleSelection.text, Qt::AlignCenter);
        UiWidgetRenderer::appendScreen(list, m_theme, battle.editor(), battle.state(),
            QStringLiteral("battle"), size, baseFont, QStringLiteral("none"), 1.0,
            [screenState](const QString& id){ return screenStateVisual(screenState, id); },
            [screenState](const QString& id){ return screenStateClip(screenState, id); },
            [this, screenState](const QString& id){ const QString name=screenStateClip(screenState,id); return clipEndTime(m_theme,id,name); },
            [screenState](const QString& id, bool fallback){ return screenStateVisible(screenState,id,fallback); }, m_elapsedMs);
        return;
    }

    const UiResolvedDataBindings arenaData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("battle.arena")), battle.editor(), battle.state());
    ElementVisual arenaVisual = elementVisual(m_theme, QStringLiteral("battle.arena"),
        normalizedUiRect(m_theme.battleArenaRect, size), size, QStringLiteral("none"), 1.0,
        screenStateVisual(screenState, QStringLiteral("battle.arena")),
        screenStateClip(screenState, QStringLiteral("battle.arena")),
        clipEndTime(m_theme, QStringLiteral("battle.arena"), screenStateClip(screenState, QStringLiteral("battle.arena"))), &arenaData);
    arenaVisual.visible = screenStateVisible(screenState, QStringLiteral("battle.arena"), arenaVisual.visible);
    const QRectF arena = arenaVisual.rect;
    UiResolvedStyle arenaStyle=battleStyle;arenaStyle.panel.fill.setAlpha(105);
    const qreal arenaOpacity = arenaVisual.visible ? arenaVisual.opacity : 0.0;
    appendResolvedBackground(list,arena,m_theme,arenaStyle,0.72*arenaOpacity);
    list.addText(QRectF(arena.left()+12,arena.top()+8,arena.width()-24,30), arenaData.hasText ? arenaData.text : battle.title(), title,
                 battleStyle.accent, Qt::AlignLeft|Qt::AlignVCenter, arenaOpacity);

    const bool reduceShake = m_reduceShake;
    const bool reduceFlash = m_reduceFlash;
    QPointF shakeOffset;
    if (battle.animationActive() && !reduceShake) {
        const int elapsed = battle.animationElapsedMs();
        for (const BattleAnimationCue& cue : battle.animationDefinition().cues) {
            if (cue.type != QLatin1String("shake") || elapsed < cue.timeMs || elapsed >= cue.timeMs + cue.durationMs) continue;
            const qreal t = qreal(elapsed - cue.timeMs) / qMax(1, cue.durationMs);
            const qreal fade = 1.0 - t;
            shakeOffset += QPointF(std::sin(t * 78.0) * cue.strength * fade,
                                   std::cos(t * 91.0) * cue.strength * 0.65 * fade);
        }
    }

    const auto& enemies = battle.enemies();
    const qreal automaticSlotW = enemies.isEmpty()?arena.width():arena.width()/enemies.size();
    QVector<QRectF> enemyRects; enemyRects.reserve(enemies.size());
    for(int i=0;i<enemies.size();++i){
        const auto& enemy=enemies[i];
        const qreal cx = enemy.arenaX >= 0.0 ? arena.left()+qBound(0.05,enemy.arenaX,0.95)*arena.width()
                                             : arena.left()+automaticSlotW*(i+0.5);
        const qreal cy = arena.top()+48+qBound(0.05,enemy.arenaY,0.95)*qMax<qreal>(40.0,arena.height()-112);
        const qreal width=qMin<qreal>(140.0, qMax<qreal>(60.0, automaticSlotW*.76));
        const qreal height=qMin<qreal>(qMax<qreal>(72.0, arena.height()*.52), 190.0);
        const QRectF sprite(cx-width*.5+shakeOffset.x(),cy-height*.5+shakeOffset.y(),width,height);
        enemyRects.push_back(sprite);
        if(enemy.hp<=0||arenaOpacity<=0.001)continue;
        if(battle.mode()==UiBattleMode::EnemyTarget && i==battle.selected()) appendResolvedBackground(list,sprite.adjusted(-5,-5,5,5),m_theme,battleSelection,0.82*arenaOpacity);
        if(!enemy.image.isNull()) list.addImage(sprite,enemy.image,QRectF(),arenaOpacity,true);
        else {UiResolvedStyle ep=battleStyle;ep.panel.fill.setAlpha(205);appendResolvedBackground(list,sprite,m_theme,ep,arenaOpacity);list.addText(sprite,enemy.name,uiFont,battleStyle.text,Qt::AlignCenter,arenaOpacity);}
        const QRectF gauge(sprite.left(),sprite.bottom()+5,sprite.width(),8);
        list.addGauge(gauge,qreal(enemy.hp)/qMax(1,enemy.maxHp),opacityColor(m_theme.gaugeBackground, arenaOpacity),opacityColor(QColor("#e35d6a"), arenaOpacity),opacityColor(m_theme.gaugeBorder, arenaOpacity),3);
        list.addText(QRectF(sprite.left(),gauge.bottom()+2,sprite.width(),20),QStringLiteral("%1  %2/%3").arg(enemy.name).arg(enemy.hp).arg(enemy.maxHp),uiFont,battleStyle.text,Qt::AlignCenter,arenaOpacity);
    }

    const UiResolvedDataBindings partyData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("battle.party")), battle.editor(), battle.state());
    const UiResolvedDataBindings commandData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("battle.commands")), battle.editor(), battle.state());
    ElementVisual partyVisual = elementVisual(m_theme, QStringLiteral("battle.party"),
        normalizedUiRect(m_theme.battlePartyRect, size), size, QStringLiteral("none"), 1.0,
        screenStateVisual(screenState, QStringLiteral("battle.party")), screenStateClip(screenState, QStringLiteral("battle.party")),
        clipEndTime(m_theme, QStringLiteral("battle.party"), screenStateClip(screenState, QStringLiteral("battle.party"))), &partyData);
    partyVisual.visible = screenStateVisible(screenState, QStringLiteral("battle.party"), partyVisual.visible);
    ElementVisual commandVisual = elementVisual(m_theme, QStringLiteral("battle.commands"),
        normalizedUiRect(m_theme.battleCommandRect, size), size, QStringLiteral("none"), 1.0,
        screenStateVisual(screenState, QStringLiteral("battle.commands")), screenStateClip(screenState, QStringLiteral("battle.commands")),
        clipEndTime(m_theme, QStringLiteral("battle.commands"), screenStateClip(screenState, QStringLiteral("battle.commands"))), &commandData);
    commandVisual.visible = screenStateVisible(screenState, QStringLiteral("battle.commands"), commandVisual.visible);
    const QRectF partyBox = partyVisual.rect;
    const QRectF cmdBox = commandVisual.rect;
    const qreal partyOpacity = partyVisual.visible ? partyVisual.opacity : 0.0;
    const qreal commandOpacity = commandVisual.visible ? commandVisual.opacity : 0.0;
    appendResolvedBackground(list,partyBox,m_theme,battleStyle,partyOpacity);
    appendResolvedBackground(list,cmdBox,m_theme,battleStyle,commandOpacity);

    const auto& party=battle.state().party();
    const int partyRow=qMax(34,int((partyBox.height()-battleStyle.paddingY*2)/qMax(1,party.size())));
    QVector<QRectF> partyRects; partyRects.reserve(party.size());
    for(int i=0;i<party.size();++i){const auto&actor=party[i];const CombatStats stats=memberStats(battle.editor(),actor);
        QRectF row(partyBox.left()+battleStyle.paddingX,partyBox.top()+battleStyle.paddingY+i*partyRow,partyBox.width()-battleStyle.paddingX*2,partyRow-4);partyRects.push_back(row);
        if (partyOpacity <= 0.001) continue;
        if((battle.mode()==UiBattleMode::AllyTarget&&i==battle.selected())||(battle.mode()==UiBattleMode::Commands&&i==battle.actorTurn()))appendResolvedBackground(list,row,m_theme,battleSelection,0.68*partyOpacity);
        const QString name=databaseRecordName(battle.editor(),QStringLiteral("actors"),actor.actorId,QStringLiteral("Personagem"));
        list.addText(QRectF(row.left()+6,row.top(),row.width()*0.34,row.height()),name,uiFont,actor.hp>0?battleStyle.text:QColor(150,150,150),Qt::AlignLeft|Qt::AlignVCenter,partyOpacity);
        const qreal gx=row.left()+row.width()*0.36, gw=row.width()*0.27;
        list.addGauge(QRectF(gx,row.center().y()-8,gw,7),qreal(actor.hp)/qMax(1,stats.maxHp),opacityColor(m_theme.gaugeBackground, partyOpacity),opacityColor(QColor("#79d17c"), partyOpacity),opacityColor(m_theme.gaugeBorder, partyOpacity),3);
        list.addText(QRectF(gx,row.center().y(),gw,18),QStringLiteral("HP %1/%2").arg(actor.hp).arg(stats.maxHp),uiFont,battleStyle.text,Qt::AlignCenter,partyOpacity);
        const qreal mx=gx+gw+8,mw=row.right()-mx-4;
        list.addGauge(QRectF(mx,row.center().y()-8,mw,7),stats.maxMp>0?qreal(actor.mp)/stats.maxMp:0,opacityColor(m_theme.gaugeBackground, partyOpacity),opacityColor(QColor("#63a9ff"), partyOpacity),opacityColor(m_theme.gaugeBorder, partyOpacity),3);
        list.addText(QRectF(mx,row.center().y(),mw,18),QStringLiteral("MP %1/%2").arg(actor.mp).arg(stats.maxMp),uiFont,battleStyle.text,Qt::AlignCenter,partyOpacity);
    }

    list.addText(QRectF(cmdBox.left()+battleStyle.paddingX,cmdBox.top()+8,cmdBox.width()-battleStyle.paddingX*2,26),commandData.hasText ? commandData.text : battle.prompt(),uiFont,battleStyle.accent,Qt::AlignLeft|Qt::AlignVCenter, commandOpacity);
    const int maxRows=6,rowH=qMax(28,int((cmdBox.height()-72)/maxRows));
    int first=0;if(battle.entries().size()>maxRows)first=qBound(0,battle.selected()-maxRows/2,battle.entries().size()-maxRows);
    for(int i=first;i<qMin(battle.entries().size(),first+maxRows);++i){if(commandOpacity<=0.001) break;const int vr=i-first;QRectF row(cmdBox.left()+8,cmdBox.top()+38+vr*rowH,cmdBox.width()-16,rowH-3);const bool selected=i==battle.selected();const UiResolvedStyle rowStyle=selected?battleSelection:battleStyle;if(selected){appendResolvedBackground(list,row,m_theme,battleSelection,commandOpacity);appendCursor(list,QRectF(row.left(),row.top(),26,row.height()),battleSelection.font,commandOpacity,battleSelection.text);}list.addText(row.adjusted(30,0,-5,0),battle.entries()[i],rowStyle.font,rowStyle.text,Qt::AlignLeft|Qt::AlignVCenter,commandOpacity);}

    if(!battle.logLines().isEmpty()){
        const int count=qMin(2,battle.logLines().size());QStringList recent;for(int i=battle.logLines().size()-count;i<battle.logLines().size();++i)recent.push_back(battle.logLines()[i]);
        UiResolvedStyle panel=battleStyle;panel.panel.fill.setAlpha(190);const QRectF log(arena.left()+12,arena.bottom()-54,arena.width()-24,44);appendResolvedBackground(list,log,m_theme,panel,0.88*arenaOpacity);list.addText(log.adjusted(8,3,-8,-3),recent.join(QLatin1Char('\n')),uiFont,battleStyle.text,int(Qt::AlignLeft|Qt::AlignVCenter)|int(Qt::TextWordWrap),arenaOpacity);
    }

    // Timeline da animação de batalha. Os cues visuais são consumidos pela
    // mesma UiDrawList usada em CPU e QRhi.
    if (battle.animationActive()) {
        const int elapsed=battle.animationElapsedMs();
        QPointF anchor=arena.center();int anchors=0;
        const QVector<int>& targets=battle.animationTargets();
        if(!targets.isEmpty()){
            QPointF sum;
            if(battle.animationTargetsParty())for(const int index:targets){if(index>=0&&index<partyRects.size()){sum+=partyRects[index].center();++anchors;}}
            else for(const int index:targets){if(index>=0&&index<enemyRects.size()){sum+=enemyRects[index].center();++anchors;}}
            if(anchors>0)anchor=sum/qreal(anchors);
        }
        for(const BattleAnimationCue& cue:battle.animationDefinition().cues){
            if(cue.type==QLatin1String("se")||cue.type==QLatin1String("shake"))continue;
            if(elapsed<cue.timeMs||elapsed>=cue.timeMs+cue.durationMs)continue;
            const int local=elapsed-cue.timeMs;const qreal progress=qBound(0.0,qreal(local)/qMax(1,cue.durationMs),1.0);
            const QPointF center=anchor+cue.offset;
            if(cue.type==QLatin1String("picture")){
                const QImage image=battle.animationImage(cue.assetPath);if(image.isNull())continue;
                const int cols=qMax(1,cue.columns),rows=qMax(1,cue.rows),frame=battleAnimationFrame(cue,local);
                const int fw=qMax(1,image.width()/cols),fh=qMax(1,image.height()/rows);const int fx=(frame%cols)*fw,fy=(frame/cols)*fh;
                const QSizeF drawSize(cue.size.width()*cue.scale,cue.size.height()*cue.scale);const QRectF target(center.x()-drawSize.width()/2,center.y()-drawSize.height()/2,drawSize.width(),drawSize.height());
                if(qAbs(cue.rotation)>0.001)list.pushTransform(target.center(),cue.rotation);
                list.addImage(target,image,QRectF(fx,fy,fw,fh),cue.opacity,true);
                if(qAbs(cue.rotation)>0.001)list.popTransform();
            }else if(cue.type==QLatin1String("particle")){
                const int count=qBound(1,cue.particleCount,500);const qreal spread=cue.spreadDegrees*3.14159265358979323846/180.0;
                for(int i=0;i<count;++i){const qreal unit=count<=1?0.5:qreal(i)/qreal(count-1);const qreal angle=(unit-.5)*spread+i*0.37;const qreal distance=cue.speed*(local/1000.0)*(0.55+0.45*std::sin(i*17.0+1.0));const QPointF pos=center+QPointF(std::cos(angle)*distance,std::sin(angle)*distance);QColor color=cue.color;color.setAlphaF(qBound(0.0,cue.opacity*(1.0-progress),1.0));const qreal sz=qMax<qreal>(2.0,3.0+cue.strength*.35);list.addParticle(pos,QSizeF(sz,sz),color,color.alphaF(),angle*180.0/3.14159265358979323846,cue.particleShape);}
            }else if(cue.type==QLatin1String("flash")){
                if (reduceFlash) continue;
                QColor color=cue.color;color.setAlphaF(qBound(0.0,cue.opacity*(1.0-progress),1.0));UiPanelStyle flash;flash.fill=color;flash.border=flash.innerBorder=Qt::transparent;flash.borderWidth=flash.innerBorderWidth=0;flash.radius=0;list.addPanel(screen,flash,color.alphaF());
            }
        }
    }

    UiWidgetRenderer::appendScreen(list, m_theme, battle.editor(), battle.state(),
        QStringLiteral("battle"), size, uiFont, QStringLiteral("none"), 1.0,
        [screenState](const QString& id){ return screenStateVisual(screenState, id); },
        [screenState](const QString& id){ return screenStateClip(screenState, id); },
        [this, screenState](const QString& id){ const QString name=screenStateClip(screenState,id); return clipEndTime(m_theme,id,name); },
        [screenState](const QString& id, bool fallback){ return screenStateVisible(screenState,id,fallback); }, m_elapsedMs);
}

void GameUiLayer::appendShop(const UiShopController& shop, const QFont& baseFont)
{
    UiDrawList& list=m_canvas.drawList();const QSize size=m_canvas.logicalSize();
    const UiResolvedStyle shopStyle=resolveNativeStyle(m_theme,QStringLiteral("shop"),QStringLiteral("normal"),baseFont);
    const UiResolvedStyle shopSelection=resolveNativeStyle(m_theme,QStringLiteral("selection"),QStringLiteral("selected"),shopStyle.font);
    const int margin=qMax(14,shopStyle.paddingX);
    const core::UiScreenStateSettings* screenState = initialScreenState(m_theme, QStringLiteral("shop"));
    const bool transparentBase = m_theme.screenTransparent.value(QStringLiteral("shop"), false);
    UiPanelStyle shade=m_theme.window;shade.fill=QColor(0,0,0,125);shade.border=shade.innerBorder=Qt::transparent;shade.borderWidth=shade.innerBorderWidth=0;shade.radius=0;if(!transparentBase)list.addPanel(QRectF(0,0,size.width(),size.height()),shade);
    const QRectF box(margin,margin,size.width()-margin*2,size.height()-margin*2);if(!transparentBase)appendResolvedBackground(list,box,m_theme,shopStyle);
    QFont title=shopStyle.font;title.setBold(true);title.setPixelSize(qMax(18,shopStyle.font.pixelSize()+3));
    list.addText(QRectF(box.left()+shopStyle.paddingX,box.top()+8,box.width()-shopStyle.paddingX*2,34),shop.title(),title,shopStyle.text,Qt::AlignLeft|Qt::AlignVCenter);
    list.addText(QRectF(box.left()+shopStyle.paddingX,box.top()+8,box.width()-shopStyle.paddingX*2,34),QStringLiteral("%1 G").arg(shop.gold()),title,shopStyle.accent,Qt::AlignRight|Qt::AlignVCenter);
    const UiResolvedDataBindings shopListData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("shop.list")), shop.editor(), shop.state());
    const UiResolvedDataBindings shopDetailData = UiDataBindingResolver::resolveRuntime(
        m_theme.layoutElements.value(QStringLiteral("shop.detail")), shop.editor(), shop.state());
    ElementVisual shopListVisual = elementVisual(m_theme, QStringLiteral("shop.list"),
                                                       normalizedUiRect(m_theme.shopListRect, size), size, QStringLiteral("none"), 1.0,
                                                       screenStateVisual(screenState, QStringLiteral("shop.list")),
                                                       screenStateClip(screenState, QStringLiteral("shop.list")),
                                                       clipEndTime(m_theme, QStringLiteral("shop.list"), screenStateClip(screenState, QStringLiteral("shop.list"))), &shopListData);
    shopListVisual.visible = screenStateVisible(screenState, QStringLiteral("shop.list"), shopListVisual.visible);
    ElementVisual shopDetailVisual = elementVisual(m_theme, QStringLiteral("shop.detail"),
                                                         normalizedUiRect(m_theme.shopDetailRect, size), size, QStringLiteral("none"), 1.0,
                                                         screenStateVisual(screenState, QStringLiteral("shop.detail")),
                                                         screenStateClip(screenState, QStringLiteral("shop.detail")),
                                                         clipEndTime(m_theme, QStringLiteral("shop.detail"), screenStateClip(screenState, QStringLiteral("shop.detail"))), &shopDetailData);
    shopDetailVisual.visible = screenStateVisible(screenState, QStringLiteral("shop.detail"), shopDetailVisual.visible);
    const QRectF listBox = shopListVisual.rect;
    const QRectF detail = shopDetailVisual.rect;
    const qreal shopListOpacity = shopListVisual.visible ? shopListVisual.opacity : 0.0;
    const qreal shopDetailOpacity = shopDetailVisual.visible ? shopDetailVisual.opacity : 0.0;
    appendResolvedBackground(list,listBox,m_theme,shopStyle,0.74*shopListOpacity);
    appendResolvedBackground(list,detail,m_theme,shopStyle,0.58*shopDetailOpacity);
    const int maxRows=9,rowH=qMax(30,int(listBox.height()/maxRows));int first=0;if(shop.entries().size()>maxRows)first=qBound(0,shop.selected()-maxRows/2,shop.entries().size()-maxRows);
    for(int i=first;i<qMin(shop.entries().size(),first+maxRows);++i){if(shopListOpacity<=0.001) break;QRectF row(listBox.left()+6,listBox.top()+(i-first)*rowH+3,listBox.width()-12,rowH-5);const bool selected=i==shop.selected();const UiResolvedStyle rowStyle=selected?shopSelection:shopStyle;if(selected){appendResolvedBackground(list,row,m_theme,shopSelection,shopListOpacity);appendCursor(list,QRectF(row.left(),row.top(),26,row.height()),shopSelection.font,shopListOpacity,shopSelection.text);}list.addText(row.adjusted(30,0,-6,0),shop.entries()[i],rowStyle.font,rowStyle.text,Qt::AlignLeft|Qt::AlignVCenter,shopListOpacity);}
    list.addText(detail.adjusted(shopStyle.paddingX,shopStyle.paddingY,-shopStyle.paddingX,-shopStyle.paddingY-44),shopDetailData.hasText ? shopDetailData.text : shop.detail(),shopStyle.font,shopStyle.text,int(Qt::AlignLeft|Qt::AlignTop)|int(Qt::TextWordWrap), shopDetailOpacity);
    if(shop.mode()!=UiShopMode::ModeSelect)list.addText(QRectF(detail.left()+shopStyle.paddingX,detail.bottom()-66,detail.width()-shopStyle.paddingX*2,28),QStringLiteral("◀  Quantidade: %1  ▶").arg(shop.quantity()),shopStyle.font,shopStyle.accent,Qt::AlignCenter,shopDetailOpacity);
    if(!shop.notice().isEmpty())list.addText(QRectF(detail.left()+shopStyle.paddingX,detail.bottom()-38,detail.width()-shopStyle.paddingX*2,28),shop.notice(),shopStyle.font,shopStyle.accent,Qt::AlignCenter,shopDetailOpacity);
    list.addText(QRectF(box.left()+shopStyle.paddingX,box.bottom()-34,box.width()-shopStyle.paddingX*2,24),QStringLiteral("Setas/WASD: navegar   ←/→: quantidade   Confirmar: escolher   Cancelar: voltar"),shopStyle.font,shopStyle.text,Qt::AlignCenter,0.78);
    UiWidgetRenderer::appendScreen(list, m_theme, shop.editor(), shop.state(),
        QStringLiteral("shop"), size, shopStyle.font, QStringLiteral("none"), 1.0,
        [screenState](const QString& id){ return screenStateVisual(screenState, id); },
        [screenState](const QString& id){ return screenStateClip(screenState, id); },
        [this, screenState](const QString& id){ const QString name=screenStateClip(screenState,id); return clipEndTime(m_theme,id,name); },
        [screenState](const QString& id, bool fallback){ return screenStateVisible(screenState,id,fallback); }, m_elapsedMs);
}

} // namespace game::ui
