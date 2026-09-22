#include "RuntimePictureTransform.h"

#include "game/PictureFx.h"
#include "game/Pictures.h"

#include <QtGlobal>
#include <cmath>

namespace game {
namespace {

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

QPointF finitePoint(const QPointF& value)
{
    return QPointF(finiteOr(value.x(), 0.0), finiteOr(value.y(), 0.0));
}

QSizeF finiteSize(const QSizeF& value)
{
    return QSizeF(qMax(0.0, finiteOr(value.width(), 0.0)),
                  qMax(0.0, finiteOr(value.height(), 0.0)));
}

QPointF finiteAnchor(const QPointF& value)
{
    // Custom anchors podem ficar fora de 0..1 de propósito. Apenas impedimos
    // NaN/Inf de contaminar os vértices e o QRhi.
    return finitePoint(value);
}

} // namespace

RuntimeCoordinateSpace runtimePictureSpace(core::PictureSpace space)
{
    return space == core::PictureSpace::Map
        ? RuntimeCoordinateSpace::World
        : RuntimeCoordinateSpace::Screen;
}

RuntimePictureTransform makeRuntimePictureTransform(
    const RuntimeRenderState& render,
    const RuntimePictureTransformInput& input)
{
    RuntimePictureTransform out;
    out.space = runtimePictureSpace(input.pictureSpace);
    out.projectionScale = render.scaleFor(out.space);
    out.flippedH = input.flipH;
    out.flippedV = input.flipV;

    const QSizeF composed = finiteSize(input.composedSize);
    if (composed.isEmpty()) return out;
    out.localRect = QRectF(QPointF(0.0, 0.0), composed);

    QSizeF base = finiteSize(input.baseSize);
    if (base.isEmpty()) base = composed;

    const QPointF custom(finiteOr(input.customAnchorX, 0.0),
                         finiteOr(input.customAnchorY, 0.0));
    out.anchorFactor = finiteAnchor(core::pictureAnchorFactor(
        input.anchor, custom.x(), custom.y()));
    out.anchorPixels = QPointF(out.anchorFactor.x() * base.width(),
                               out.anchorFactor.y() * base.height());
    out.pivotPixels = out.anchorPixels;

    const QPointF position = finitePoint(input.position);
    const QPointF screenOffset = finitePoint(input.screenOffset);
    const QPointF drawOffset = finitePoint(input.drawOffset);
    const double baseScaleX = finiteOr(input.scaleXPercent, 100.0) / 100.0
                            * finiteOr(input.scaleMulX, 1.0);
    const double baseScaleY = finiteOr(input.scaleYPercent, 100.0) / 100.0
                            * finiteOr(input.scaleMulY, 1.0);
    out.signedScaleX = baseScaleX * (input.flipH ? -1.0 : 1.0);
    out.signedScaleY = baseScaleY * (input.flipV ? -1.0 : 1.0);
    if (!std::isfinite(out.signedScaleX) || !std::isfinite(out.signedScaleY) ||
        qFuzzyIsNull(out.signedScaleX) || qFuzzyIsNull(out.signedScaleY))
        return out;

    const double angle = finiteOr(input.angleDegrees, 0.0)
                       + finiteOr(input.angleAddDegrees, 0.0);
    const double projectionScale = qMax(0.01, finiteOr(out.projectionScale, 1.0));

    // QRhi: screenOffset é uma animação visual em pixels lógicos. Quando a
    // Picture está no mapa, convertemos somente esse offset para World Space.
    // O batch continua declarando World e a câmera/zoom não são compensados.
    const QPointF declaredPosition(
        position.x() + screenOffset.x() / projectionScale,
        position.y() + screenOffset.y() / projectionScale);
    out.declaredSpaceTransform.translate(declaredPosition.x(), declaredPosition.y());
    out.declaredSpaceTransform.rotate(angle);
    out.declaredSpaceTransform.scale(out.signedScaleX, out.signedScaleY);
    out.declaredSpaceTransform.translate(-out.pivotPixels.x(), -out.pivotPixels.y());
    out.declaredSpaceTransform.translate(drawOffset.x(), drawOffset.y());

    // CPU: a mesma transformação é expressa já em Screen Space. O zoom entra
    // uma única vez na posição e na escala de Pictures presas ao mapa.
    const QPointF screenPosition = render.projectPoint(position, out.space) + screenOffset;
    out.screenTransform.translate(screenPosition.x(), screenPosition.y());
    out.screenTransform.rotate(angle);
    out.screenTransform.scale(out.signedScaleX * projectionScale,
                              out.signedScaleY * projectionScale);
    out.screenTransform.translate(-out.pivotPixels.x(), -out.pivotPixels.y());
    out.screenTransform.translate(drawOffset.x(), drawOffset.y());

    out.screenBounds = out.screenTransform.mapRect(out.localRect);
    out.valid = true;
    return out;
}

RuntimePictureTransform makeRuntimePictureTransform(
    const RuntimeRenderState& render,
    const LivePicture& picture,
    const PictureFrame& frame)
{
    const core::PictureDef& d = picture.def;
    RuntimePictureTransformInput input;
    input.pictureSpace = d.space;
    input.position = QPointF(picture.effectiveX(), picture.effectiveY());
    input.baseSize = frame.baseSize;
    input.composedSize = QSizeF(frame.image.size());
    input.drawOffset = frame.drawOffset;
    input.screenOffset = frame.posOffset;
    input.scaleXPercent = picture.effectiveScaleX();
    input.scaleYPercent = picture.effectiveScaleY();
    input.scaleMulX = frame.scaleMulX;
    input.scaleMulY = frame.scaleMulY;
    input.angleDegrees = picture.effectiveAngle();
    input.angleAddDegrees = frame.angleAdd;
    input.flipH = d.flipH;
    input.flipV = d.flipV;
    input.anchor = d.anchor;
    input.customAnchorX = d.anchorX;
    input.customAnchorY = d.anchorY;
    return makeRuntimePictureTransform(render, input);
}

} // namespace game
