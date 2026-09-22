// ============================================================================
//  PaintOps.h — Operacoes de pintura no mapa.
//  Porte de: paintStampAt, eraseAt, fillAt, rectFill, circleFill, linePoints,
//  pipetteAt, transformCells (espelhar/rodar), brushOffsets.
// ============================================================================
#pragma once

#include "Editor.h"
#include <QLineF>
#include <QPolygonF>
#include <QRegion>

namespace core { namespace paint {

/// Amostra do brush. `strength` vai de 0..1 e é usada para alpha masks.
struct BrushSample {
    QPoint point;
    qreal  strength = 1.0;
};

/// Carrega uma imagem como máscara de brush. PNGs com transparência usam o
/// canal alpha; imagens opacas usam luminância (preto=0, branco=255).
QImage loadBrushAlphaMask(const QString& path);

/// Amostras cobertas pelo brush, incluindo intensidade da alpha mask.
QVector<BrushSample> brushSamples(const BrushSettings& b);

/// Deslocamentos cobertos pelo pincel (compatibilidade com chamadas antigas).
QVector<QPoint> brushOffsets(const BrushSettings& b);

// ---------------------------------------------------------------- raster brush
/// Carrega a ponta RGBA usada pelo Paint Brush livre.
QImage loadRasterBrushTip(const QString& path);

/// Distância recomendada entre dabs, em pixels.
int rasterBrushSpacingPx(const RasterBrushSettings& b);

/// Imagem da ponta do pincel exatamente como será carimbada, usada pela
/// silhueta/preview sob o cursor. Não altera nenhuma camada.
QImage rasterBrushPreviewTip(const RasterBrushSettings& b, double strokeAngleDeg = 0.0);

/// Aplica um único dab numa Image Layer marcada como paintLayer. Retorna o
/// retângulo local afetado (coordenadas da própria imagem).
QRectF rasterBrushDab(const LayerPtr& layer, const RasterBrushSettings& b,
                      const QPointF& localCenter, bool erase, double strokeAngleDeg = 0.0);
/// Variante para um alvo raster arbitrário, usada pela máscara da camada.
QRectF rasterBrushDab(QImage* targetImage, const RasterBrushSettings& b,
                      const QPointF& localCenter, bool erase, double strokeAngleDeg = 0.0,
                      bool alphaMaskMode = false, bool alphaLock = false,
                      const QRegion* clipRegion = nullptr);

// ---------------------------------------------------------------- slope raster
/// Eixo da inclinacao progressiva. Horizontal desloca cada linha em X;
/// Vertical desloca cada coluna em Y.
enum class SlopeAxis { Horizontal, Vertical };

/// Inclina uma imagem de forma pixel-perfect sem interpolacao. O resultado
/// mantem exatamente o mesmo tamanho da fonte; pixels que saem da area sao
/// recortados e novas areas ficam transparentes. `step` representa pixels de
/// deslocamento por linha/coluna e aceita fracionarios (o deslocamento final
/// de cada linha/coluna e arredondado para pixel inteiro).
QImage slopeImage(const QImage& source, SlopeAxis axis, double step);

/// Aplica flip H/V do pincel a um stamp.
Stamp transformStamp(const Stamp& s, const BrushSettings& b);

/// Sorteio de densidade do Scatter. Fora do Random Mode sempre retorna true.
bool randomScatterPass(const Editor& ed);

/// Carimba o stamp atual (ou uma entrada do pool aleatorio) na celula (gx,gy).
void stampAt(Editor& ed, const LayerPtr& layer, int gx, int gy);

/// Apaga: remove a celula toda, ou so o topo da pilha em modo "por cima".
void eraseAt(Editor& ed, const LayerPtr& layer, int gx, int gy);

/// Balde de tinta (flood fill 4-vizinhos comparando o tile do topo).
void fillAt(Editor& ed, const LayerPtr& layer, int gx, int gy);

/// Retangulo cheio/oco, com stamp em mosaico ou pool aleatorio.
void rectFill(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1,
              bool hollow, bool erase);

/// Elipse cheia/oca inscrita no retangulo.
void circleFill(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1,
                bool hollow, bool erase);

/// Pontos de uma linha (Bresenham) em coordenadas de celula.
QVector<QPoint> linePoints(int x0, int y0, int x1, int y1);

/// Pinta uma linha de celulas usando o stamp atual.
void lineStroke(Editor& ed, const LayerPtr& layer, int x0, int y0, int x1, int y1, bool erase);

/// Conta-gotas: seleciona na paleta o tile do topo da celula do mapa.
/// Devolve false se nao havia tile.
bool pipetteAt(Editor& ed, const LayerPtr& layer, int px, int py);

/// Copia um retangulo de celulas do mapa para um stamp customizado.
CustomStamp stampFromMap(const LayerPtr& layer, int x0, int y0, int x1, int y1);

/// Aplica uma pincelada completa (respeitando tamanho/forma/densidade do pincel).
/// `scatterVisited`, quando informado, garante que cada celula receba somente
/// uma decisao de Scattering durante o mesmo gesto de mouse. Isso preserva
/// buracos do scatter ao arrastar sobre a grade normal.
void brushStroke(Editor& ed, const LayerPtr& layer, int gx, int gy, bool erase,
                 QSet<quint64>* scatterVisited = nullptr);

// ------------------------------------------------------------------ objetos
/// Objeto sob o ponto (px,py) em pixels do mapa, ou nullptr.
MapObject* hitObject(const LayerPtr& layer, double px, double py);
/// Poligono/limites visuais considerando rotacao ao redor do centro.
QPolygonF objectPolygon(const MapObject& o);
QRectF objectBounds(const MapObject& o);
/// Converte um ponto visual para o espaco nao rotacionado do objeto.
QPointF unrotateObjectPoint(const MapObject& o, const QPointF& p);
/// Alcas de redimensionamento de um objeto (8 alcas), ja rotacionadas.
QVector<QRectF> objectHandles(const MapObject& o, double zoom);
/// Indice da alca sob o cursor, ou -1.
int hitHandle(const MapObject& o, double px, double py, double zoom);
/// Haste e alca circular/retangular de rotacao acima do objeto.
QLineF objectRotationStem(const MapObject& o, double zoom);
QRectF objectRotationHandle(const MapObject& o, double zoom);
bool hitRotationHandle(const MapObject& o, double px, double py, double zoom);
/// Cria um objeto na posicao dada usando o stamp atual.
MapObject makeObjectFromStamp(Editor& ed, double px, double py);
/// Grid-Free Random: cria um objeto a partir de uma entrada aleatoria do pool,
/// aplicando jitter sub-grid. `valid` informa se a amostra passou o Scatter.
MapObject makeRandomObjectFromPool(Editor& ed, double px, double py, bool* valid = nullptr);

}} // namespace core::paint
