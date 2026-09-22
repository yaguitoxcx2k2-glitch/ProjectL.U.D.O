#pragma once

#include "UiCanvas.h"
#include "UiTheme.h"
#include "UiModalController.h"
#include "UiMenuController.h"
#include "UiBattleController.h"
#include "UiShopController.h"
#include "game/Interpreter.h"

#include <QPair>
#include <QRect>

namespace core { class IconSet; }

namespace game::ui {

/// Camada funcional da UI do jogo. Mensagens, escolhas e os modais da Fase 2
/// viram comandos do mesmo canvas, portanto CPU e QRhi recebem exatamente o
/// mesmo layout e a mesma aparência.
class GameUiLayer {
public:
    GameUiLayer();

    void setTheme(const UiTheme& theme) { m_theme = theme; }
    const UiTheme& theme() const { return m_theme; }
    void setReducedMotion(bool reduceShake, bool reduceFlash)
    { m_reduceShake = reduceShake; m_reduceFlash = reduceFlash; }

    void rebuild(const Interpreter* interpreter, const UiModalController* modal,
                 const UiMenuController* menu, const UiBattleController* battle,
                 const UiShopController* shop, const QString& customScreen, const QSize& viewSize, bool hudVisible,
                 qint64 elapsedMs, const core::IconSet* icons);

    const UiCanvas& canvas() const { return m_canvas; }
    UiCanvas& canvas() { return m_canvas; }

    static QRect messageGeometry(const MessageView& message, const MessageStyle& style,
                                 const QSize& viewSize, bool hudVisible,
                                 int paddingY = 12);
    static QPair<QRect, int> choiceGeometry(const ChoiceView& choice,
                                             const MessageStyle& style,
                                             const QSize& viewSize,
                                             int paddingX = 14,
                                             int paddingY = 14);
    static QPair<QRect, int> choiceGeometry(const Interpreter* interpreter,
                                             const QSize& viewSize);
    static int choiceIndexAt(const ChoiceView& choice, const MessageStyle& style,
                             const QSize& viewSize, const QPointF& position,
                             int paddingX = 14, int paddingY = 14);
    static QRect modalGeometry(const UiModalController& modal, const QFont& font,
                               const QSize& viewSize, int paddingX = 18,
                               int paddingY = 16);
    static QPair<QRect, int> modalListGeometry(const UiModalController& modal,
                                                const QFont& font,
                                                const QSize& viewSize,
                                                int paddingX = 18,
                                                int paddingY = 16);
    static int modalFirstVisible(const UiModalController& modal, int maxRows = 6);
    static QPair<QRect, int> menuListGeometry(const QSize& viewSize, int paddingX = 12,
                                              int paddingY = 12);

private:
    void appendWindow(UiDrawList& list, const QRectF& rect, const UiPanelStyle& fallback,
                      qreal opacity = 1.0) const;
    void appendCursor(UiDrawList& list, const QRectF& slot, const QFont& font,
                      qreal opacity = 1.0, const QColor& color = QColor()) const;
    void appendMessage(const Interpreter& interpreter, bool hudVisible,
                       qint64 elapsedMs, const core::IconSet* icons);
    void appendChoice(const Interpreter& interpreter, const core::IconSet* icons);
    void appendModal(const UiModalController& modal, const QFont& baseFont);
    void appendMenu(const UiMenuController& menu, const QFont& baseFont);
    void appendBattle(const UiBattleController& battle, const QFont& baseFont);
    void appendShop(const UiShopController& shop, const QFont& baseFont);
    void appendCustomScreen(const UiMenuController& dataSource, const QString& screen, const QFont& baseFont);

    UiCanvas m_canvas;
    UiTheme m_theme;
    qint64 m_elapsedMs = 0;
    bool m_reduceShake = false;
    bool m_reduceFlash = false;
};

} // namespace game::ui
