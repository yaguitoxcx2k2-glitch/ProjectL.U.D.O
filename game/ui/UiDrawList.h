#pragma once

#include "game/TextDraw.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QMargins>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>
#include <QString>

#include <variant>
#include <vector>

namespace game::ui {

/// Visual de um painel in-game. Sem QWidget: apenas dados que qualquer backend
/// de renderizacao pode interpretar.
struct UiPanelStyle {
    QColor fill = QColor(12, 16, 32, 225);
    QColor border = QColor("#cfd8ff");
    QColor innerBorder = QColor(90, 110, 180, 180);
    qreal borderWidth = 2.0;
    qreal innerBorderWidth = 1.0;
    qreal radius = 8.0;
    qreal innerInset = 4.0;
};

struct UiPanelCommand {
    QRectF rect;
    UiPanelStyle style;
    qreal opacity = 1.0;
};

struct UiTextCommand {
    QRectF rect;
    QString text;
    QFont font;
    QColor color = Qt::white;
    int flags = int(Qt::AlignLeft | Qt::AlignVCenter);
    qreal opacity = 1.0;
};

/// Texto rico da LUDO (\FS, \FC, icones, efeitos etc.). Mantemos TextPage
/// como dado do comando; o backend de QPainter usa o mesmo TextDraw de antes.
struct UiRichTextCommand {
    QRectF rect;
    TextPage page;
    QFont font;
    TextDrawOpts options;
};

struct UiImageCommand {
    QRectF target;
    QRectF source;
    QImage image;
    qreal opacity = 1.0;
    bool smooth = true;
};

/// Janela escalavel por 9-slice. `slices` informa, em pixels da imagem fonte,
/// quanto preservar de cada borda.
struct UiNineSliceCommand {
    QRectF target;
    QImage image;
    QMargins slices;
    qreal opacity = 1.0;
    bool smooth = false;
};

/// Inicia uma região de clipping/máscara persistente até UiPopClipCommand.
/// `shape`: rect, rounded, circle ou image-alpha.
struct UiPushClipCommand {
    QRectF rect;
    QString shape = QStringLiteral("rect");
    qreal radius = 0.0;
    QImage maskImage;
};

struct UiPopClipCommand {};

/// Transformação hierárquica 2D. Push/Pop seguem a mesma pilha do QPainter,
/// portanto pais transformam filhos sem alterar os retângulos persistidos.
struct UiPushTransformCommand {
    QPointF pivot;
    qreal rotationDegrees = 0.0;
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    qreal skewXDegrees = 0.0;
    qreal skewYDegrees = 0.0;
};
struct UiPopTransformCommand {};

/// Especificação de material/efeito pós-processado. `timeMs` permite efeitos
/// animados (glitch/wave/VHS etc.) sem estado mutável no renderer.
struct UiEffectSpec {
    QString type;
    qreal intensity = 1.0;
    qreal amount = 0.5;
    qreal speed = 1.0;
    QColor color = QColor("#67d6ff");
    int seed = 0;
    qint64 timeMs = 0;
};

/// Delimita um grupo de comandos a ser renderizado em layer offscreen e
/// pós-processado. `bounds` está em coordenadas lógicas.
struct UiPushEffectCommand {
    QRectF bounds;
    QVector<UiEffectSpec> effects;
    QString blendMode = QStringLiteral("normal");
};
struct UiPopEffectCommand {};

/// Partícula 2D declarativa. O emitter gera estas primitivas determinísticas
/// e o backend só precisa desenhá-las.
struct UiParticleCommand {
    QPointF center;
    QSizeF size;
    QColor color = Qt::white;
    qreal opacity = 1.0;
    qreal rotationDegrees = 0.0;
    QString shape = QStringLiteral("circle"); // circle/square/image
    QImage image;
};

struct UiGaugeCommand {
    QRectF rect;
    qreal value = 0.0; // 0..1
    QColor background = QColor(0, 0, 0, 150);
    QColor fill = QColor("#79d17c");
    QColor border = QColor(255, 255, 255, 150);
    qreal radius = 4.0;
};

struct UiRadialProgressCommand {
    QRectF rect;
    qreal value = 0.0; // 0..1
    QColor track = QColor(0, 0, 0, 150);
    QColor fill = QColor("#79d17c");
    qreal thickness = 5.0;
    qreal opacity = 1.0;
};

struct UiTriangleCommand {
    QPointF center;
    QSizeF size = QSizeF(12, 8);
    QColor color = Qt::white;
    qreal opacity = 1.0;
    bool pointsDown = true;
};

using UiCommand = std::variant<UiPanelCommand, UiTextCommand, UiRichTextCommand,
                               UiImageCommand, UiNineSliceCommand,
                               UiPushClipCommand, UiPopClipCommand,
                               UiPushTransformCommand, UiPopTransformCommand,
                               UiPushEffectCommand, UiPopEffectCommand,
                               UiParticleCommand,
                               UiGaugeCommand, UiRadialProgressCommand, UiTriangleCommand>;

/// Lista backend-agnostic de comandos de UI. A ordem de insercao e a ordem de
/// composicao (o ultimo comando fica por cima), como em um canvas 2D.
class UiDrawList {
public:
    void clear() { m_commands.clear(); }
    bool empty() const { return m_commands.empty(); }
    int size() const { return int(m_commands.size()); }

    void addPanel(const QRectF& rect, const UiPanelStyle& style, qreal opacity = 1.0);
    void addText(const QRectF& rect, const QString& text, const QFont& font,
                 const QColor& color, int flags = int(Qt::AlignLeft | Qt::AlignVCenter),
                 qreal opacity = 1.0);
    void addRichText(const QRectF& rect, const TextPage& page, const QFont& font,
                     const TextDrawOpts& options);
    void addImage(const QRectF& target, const QImage& image,
                  const QRectF& source = QRectF(), qreal opacity = 1.0,
                  bool smooth = true);
    void addNineSlice(const QRectF& target, const QImage& image,
                      const QMargins& slices, qreal opacity = 1.0,
                      bool smooth = false);
    void pushClip(const QRectF& rect, const QString& shape = QStringLiteral("rect"),
                  qreal radius = 0.0, const QImage& maskImage = QImage());
    void popClip();
    void pushTransform(const QPointF& pivot, qreal rotationDegrees = 0.0,
                       qreal scaleX = 1.0, qreal scaleY = 1.0,
                       qreal skewXDegrees = 0.0, qreal skewYDegrees = 0.0);
    void popTransform();
    void pushEffect(const QRectF& bounds, const QVector<UiEffectSpec>& effects,
                    const QString& blendMode = QStringLiteral("normal"));
    void popEffect();
    void addParticle(const QPointF& center, const QSizeF& size, const QColor& color,
                     qreal opacity = 1.0, qreal rotationDegrees = 0.0,
                     const QString& shape = QStringLiteral("circle"),
                     const QImage& image = QImage());
    void addGauge(const QRectF& rect, qreal value, const QColor& background,
                  const QColor& fill, const QColor& border, qreal radius = 4.0);
    void addRadialProgress(const QRectF& rect, qreal value, const QColor& track,
                           const QColor& fill, qreal thickness = 5.0, qreal opacity = 1.0);
    void addTriangle(const QPointF& center, const QSizeF& size, const QColor& color,
                     qreal opacity = 1.0, bool pointsDown = true);

    const std::vector<UiCommand>& commands() const { return m_commands; }

private:
    std::vector<UiCommand> m_commands;
};

} // namespace game::ui
