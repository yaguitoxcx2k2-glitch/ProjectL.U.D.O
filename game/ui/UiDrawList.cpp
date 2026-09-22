#include "UiDrawList.h"

#include <QtGlobal>

namespace game::ui {

void UiDrawList::addPanel(const QRectF& rect, const UiPanelStyle& style, qreal opacity)
{
    m_commands.emplace_back(UiPanelCommand{rect, style, qBound<qreal>(0.0, opacity, 1.0)});
}

void UiDrawList::addText(const QRectF& rect, const QString& text, const QFont& font,
                         const QColor& color, int flags, qreal opacity)
{
    m_commands.emplace_back(UiTextCommand{rect, text, font, color, flags,
                                           qBound<qreal>(0.0, opacity, 1.0)});
}

void UiDrawList::addRichText(const QRectF& rect, const TextPage& page, const QFont& font,
                             const TextDrawOpts& options)
{
    m_commands.emplace_back(UiRichTextCommand{rect, page, font, options});
}

void UiDrawList::addImage(const QRectF& target, const QImage& image,
                          const QRectF& source, qreal opacity, bool smooth)
{
    m_commands.emplace_back(UiImageCommand{target, source, image,
                                            qBound<qreal>(0.0, opacity, 1.0), smooth});
}

void UiDrawList::addNineSlice(const QRectF& target, const QImage& image,
                              const QMargins& slices, qreal opacity, bool smooth)
{
    m_commands.emplace_back(UiNineSliceCommand{target, image, slices,
                                                qBound<qreal>(0.0, opacity, 1.0), smooth});
}

void UiDrawList::pushClip(const QRectF& rect, const QString& shape, qreal radius, const QImage& maskImage)
{
    m_commands.emplace_back(UiPushClipCommand{rect, shape, qMax<qreal>(0.0, radius), maskImage});
}

void UiDrawList::popClip()
{
    m_commands.emplace_back(UiPopClipCommand{});
}

void UiDrawList::pushTransform(const QPointF& pivot, qreal rotationDegrees,
                               qreal scaleX, qreal scaleY,
                               qreal skewXDegrees, qreal skewYDegrees)
{
    UiPushTransformCommand cmd;
    cmd.pivot = pivot;
    cmd.rotationDegrees = rotationDegrees;
    cmd.scaleX = qBound<qreal>(0.05, scaleX, 10.0);
    cmd.scaleY = qBound<qreal>(0.05, scaleY, 10.0);
    cmd.skewXDegrees = qBound<qreal>(-80.0, skewXDegrees, 80.0);
    cmd.skewYDegrees = qBound<qreal>(-80.0, skewYDegrees, 80.0);
    m_commands.emplace_back(cmd);
}

void UiDrawList::popTransform()
{
    m_commands.emplace_back(UiPopTransformCommand{});
}

void UiDrawList::pushEffect(const QRectF& bounds, const QVector<UiEffectSpec>& effects,
                            const QString& blendMode)
{
    UiPushEffectCommand cmd;
    cmd.bounds = bounds;
    cmd.effects = effects;
    cmd.blendMode = blendMode.trimmed().toLower();
    m_commands.emplace_back(cmd);
}

void UiDrawList::popEffect()
{
    m_commands.emplace_back(UiPopEffectCommand{});
}

void UiDrawList::addParticle(const QPointF& center, const QSizeF& size, const QColor& color,
                             qreal opacity, qreal rotationDegrees,
                             const QString& shape, const QImage& image)
{
    UiParticleCommand cmd;
    cmd.center = center;
    cmd.size = QSizeF(qMax<qreal>(0.1, size.width()), qMax<qreal>(0.1, size.height()));
    cmd.color = color;
    cmd.opacity = qBound<qreal>(0.0, opacity, 1.0);
    cmd.rotationDegrees = rotationDegrees;
    cmd.shape = shape.trimmed().toLower();
    cmd.image = image;
    m_commands.emplace_back(cmd);
}

void UiDrawList::addGauge(const QRectF& rect, qreal value, const QColor& background,
                          const QColor& fill, const QColor& border, qreal radius)
{
    m_commands.emplace_back(UiGaugeCommand{rect, qBound<qreal>(0.0, value, 1.0),
                                            background, fill, border, radius});
}

void UiDrawList::addRadialProgress(const QRectF& rect, qreal value, const QColor& track,
                                   const QColor& fill, qreal thickness, qreal opacity)
{
    m_commands.emplace_back(UiRadialProgressCommand{rect, qBound<qreal>(0.0, value, 1.0),
                                                     track, fill, qMax<qreal>(1.0, thickness),
                                                     qBound<qreal>(0.0, opacity, 1.0)});
}

void UiDrawList::addTriangle(const QPointF& center, const QSizeF& size,
                             const QColor& color, qreal opacity, bool pointsDown)
{
    m_commands.emplace_back(UiTriangleCommand{center, size, color,
                                               qBound<qreal>(0.0, opacity, 1.0),
                                               pointsDown});
}

} // namespace game::ui
