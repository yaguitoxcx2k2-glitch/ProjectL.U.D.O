// ============================================================================
// RuntimePictureTransform.h — transformação única de Pictures CPU/QRhi/editor.
//
// Pictures podem viver em Screen Space ou World Space, mas posição, pivot/
// âncora, escala, rotação, flip e offsets de animação precisam produzir a
// mesma geometria em todos os consumidores. Este contrato é a única fonte de
// verdade estrutural da Picture no runtime.
// ============================================================================
#pragma once

#include "core/Picture.h"
#include "game/RuntimeRenderState.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace game {

struct LivePicture;
struct PictureFrame;

struct RuntimePictureTransformInput {
    core::PictureSpace pictureSpace = core::PictureSpace::Screen;
    QPointF position;                 ///< X/Y efetivos no espaço declarado
    QSizeF baseSize;                  ///< imagem original; define âncora/pivot
    QSizeF composedSize;              ///< textura final após efeitos
    QPointF drawOffset;               ///< deslocamento local da textura composta
    QPointF screenOffset;             ///< offset visual em pixels lógicos de tela
    double scaleXPercent = 100.0;
    double scaleYPercent = 100.0;
    double scaleMulX = 1.0;           ///< transição/efeito
    double scaleMulY = 1.0;
    double angleDegrees = 0.0;
    double angleAddDegrees = 0.0;
    bool flipH = false;               ///< flip faz parte do transform, não dos pixels
    bool flipV = false;
    core::PictureAnchor anchor = core::PictureAnchor::TopLeft;
    double customAnchorX = 0.0;
    double customAnchorY = 0.0;
};

struct RuntimePictureTransform {
    RuntimeCoordinateSpace space = RuntimeCoordinateSpace::Screen;
    QRectF localRect;
    QPointF anchorFactor;
    QPointF anchorPixels;
    /// Nome explícito para deixar claro que a âncora é também o pivot de
    /// escala/rotação/flip. Mantido separado no diagnóstico/API por clareza.
    QPointF pivotPixels;
    double projectionScale = 1.0;
    double signedScaleX = 1.0;
    double signedScaleY = 1.0;
    bool flippedH = false;
    bool flippedV = false;
    /// Usada pelo SpriteBatcher/QRhi. Os vértices permanecem no espaço
    /// declarado e a projeção World -> Screen acontece depois, uma única vez.
    QTransform declaredSpaceTransform;
    /// Usada pelo QPainter/CPU. Já contém a projeção oficial da câmera/zoom.
    QTransform screenTransform;
    QRectF screenBounds;
    bool valid = false;
};

RuntimeCoordinateSpace runtimePictureSpace(core::PictureSpace space);
RuntimePictureTransform makeRuntimePictureTransform(
    const RuntimeRenderState& render,
    const RuntimePictureTransformInput& input);

/// Atalho oficial usado por CPU, QRhi e previews: monta a entrada diretamente
/// do estado vivo + quadro composto. Assim nenhum consumidor remonta a regra
/// de posição/âncora/transição/flip por conta própria.
RuntimePictureTransform makeRuntimePictureTransform(
    const RuntimeRenderState& render,
    const LivePicture& picture,
    const PictureFrame& frame);

} // namespace game
