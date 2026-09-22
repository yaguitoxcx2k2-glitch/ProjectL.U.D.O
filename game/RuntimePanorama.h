// ============================================================================
// RuntimePanorama.h — contrato único de colocação para Panorama/Parallax.
//
// A arte já processada (frame/FX/flip) chega pronta. Este módulo decide apenas
// EM QUE espaço ela nasce e ONDE cada repetição fica. CPU e QRhi consomem o
// mesmo layout para não divergirem com câmera, zoom ou "Fixar na tela".
// ============================================================================
#pragma once

#include "game/RuntimeRenderState.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

namespace game {

struct RuntimePanoramaInput {
    QSizeF imageSize;          ///< tamanho da imagem já composta, em pixels lógicos
    QPointF scrollOffset;      ///< Screen px quando fixed; World px caso contrário
    double parallaxX = 1.0;    ///< 0 = cancela translação da câmera; 1 = preso ao mapa
    double parallaxY = 1.0;
    bool fixed = false;        ///< true = Screen Space real, sem câmera/zoom
};

struct RuntimePanoramaLayout {
    RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World;
    QRectF visibleRect;        ///< no MESMO espaço das tiles
    QPointF phase;             ///< origem lógica da repetição
    QVector<QRectF> tiles;     ///< destinos no espaço declarado

    bool valid = false;
    bool isEmpty() const { return tiles.isEmpty(); }
};

/// Normaliza e monta a geometria oficial de uma camada de panorama.
/// - fixed=true -> Screen Space: câmera/zoom não alteram posição nem tamanho;
/// - fixed=false -> World Space: RuntimeRenderState projeta uma única vez;
/// - a imagem continua tiled nos dois eixos para preservar a semântica clássica
///   da LUDO; loopX/loopY pertencem ao relógio de auto-scroll, não ao tiling.
RuntimePanoramaLayout buildRuntimePanoramaLayout(const RuntimeRenderState& state,
                                                 const RuntimePanoramaInput& input);

} // namespace game
