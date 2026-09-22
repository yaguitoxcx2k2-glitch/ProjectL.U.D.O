// ============================================================================
//  Renderer.h — Desenho do mapa com QPainter.
//  Porte de: drawLayerTree, drawLayerToCtx, drawObjectToCtx, drawGrid,
//  drawMini, exportPNG (render offscreen) e do cache de mascaras (_maskCanvases).
// ============================================================================
#pragma once

#include "Editor.h"
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <functional>

namespace core {

/// Cache de QPixmap por tileset (evita converter QImage a cada frame).
class TilesetPixmapCache
{
public:
    const QPixmap& pixmap(const Editor& ed, int tilesetIdx);
    void invalidate(int tilesetIdx = -1);
private:
    QHash<int, QPixmap> m_cache;
    QHash<int, qint64>  m_keys;
};

TilesetPixmapCache& pixmapCache();

/// Transformação canônica de uma Image Layer. Canvas, máscaras e exportadores
/// usam a mesma matriz para evitar divergência visual.
QTransform imageLayerTransform(const LayerPtr& layer);

/// Geometria da imagem exibida quando a Camada Visual usa spritesheet.
/// O retângulo retornado aponta para o quadro dentro da imagem-fonte; o
/// tamanho local sempre começa em (0,0) para transformação e seleção.
QRect visualLayerFrameSourceRect(const LayerPtr& layer, qint64 animationTimeMs = 0);
QSize visualLayerFrameSize(const LayerPtr& layer);

/// Silhueta usada por Clipping Mask. Tile Layers usam células ocupadas;
/// Image/Paint Layers usam o alpha real da imagem transformada.
QPainterPath layerClipPath(const LayerPtr& layer, const QRectF& clip = QRectF());

/// Invalida o cache editorial de geometrias de mascara. Vazio = todas.
void invalidateMaskPathCache(const QString& layerId = QString());


/// Contadores opcionais usados por testes/diagnóstico do editor.
/// Não participam do estado do projeto e não alteram o desenho.
struct RendererViewportStats {
    qint64 tileCellsVisited = 0;
    qint64 objectCandidates = 0;
    qint64 objectsCulled = 0;
    qint64 maskChunksBuilt = 0;
    qint64 maskChunksReused = 0;
};

struct RenderOptions {
    QString previewLayerId;
    MapObject previewObject;
    int previewIndex = -1;
    bool     skipReferenceLayers = false;  ///< exportadores ignoram apenas Image Layers marcadas como referência
    /// Filtro por tile (equivale ao `tileFilter` de drawLayerTreeFiltered):
    /// devolva false para omitir o tile. Vazio = desenha todos.
    std::function<bool(const TileRef&)> tileFilter;
    /// Filtro por célula com contexto da camada. Usado por renderizações seletivas.
    /// para retirar do chunk estatico apenas as celulas que viram overlays.
    std::function<bool(const LayerPtr&, int, int)> cellFilter;
    /// Pintar a cor de fundo do mapa antes das camadas (false = PNG transparente).
    bool     fillBackground = true;
    /// Filtro por camada (usado pelo runtime para separar o que fica atras e
    /// o que fica na frente do jogador). Vazio = desenha todas.
    std::function<bool(const LayerPtr&)> layerFilter;
    /// Filtro editorial por unidade realmente desenhavel. Diferente de
    /// layerFilter, containers continuam sendo percorridos e uma mascara pode
    /// ter sua base omitida sem perder o recorte dos filhos. Isso permite ao
    /// modo Focar separar a pilha em abaixo / foco / acima sem quebrar grupos.
    std::function<bool(const LayerPtr&)> drawableFilter;
    /// Runtime Map Management: permite que o player desenhe a célula efetiva
    /// sem modificar o MapDoc do projeto. Editor/export deixam vazio.
    std::function<Cell(const LayerPtr&, int, int)> cellResolver;
    bool     highlightActive = false;      ///< legado: reduz alpha de camadas nao ativas
    double   focusDim = 0.25;
    QString  activeLayerId;
    bool     drawObjectFrames = true;      ///< contorno dos objetos (nao vai no export)
    /// O editor pode reutilizar paths de mascara por chunk entre repaints.
    /// Runtime/export permanecem opt-in para evitar estado de cache invisivel.
    bool     cacheMaskPaths = false;
    RendererViewportStats* viewportStats = nullptr; ///< opcional; nullptr = zero custo de contagem
    qint64   animationTimeMs = 0;          ///< relógio visual compartilhado dos autotiles animados
    qint64   visualAnimationTimeMs = -1;   ///< relógio das Camadas Visuais; -1 reutiliza animationTimeMs
    bool     previewParallax = false;
    QPointF  cameraOffset;                 ///< câmera editorial em pixels do mapa
    bool     previewVisualEffects = false; ///< aproximação leve dos efeitos MZ no Editor
    /// Usado ao montar a silhueta de um Grupo: evita sombra dentro de sombra.
    bool     suppressContactShadows = false;
};

/// Desenha uma arvore de camadas no painter (coordenadas em pixels do mapa).
void drawLayerTree(QPainter& p, const Editor& ed, const QVector<LayerPtr>& nodes,
                   double parentAlpha, const RenderOptions& opt);

/// Desenha uma unica camada.
void drawLayer(QPainter& p, const Editor& ed, const LayerPtr& layer,
               double extraAlpha, const RenderOptions& opt);

/// Desenha o panorama configurado no MapInfo, atrás das camadas do mapa.
void drawMapPanorama(QPainter& p, const MapInfo& info);

/// Renderiza o mapa inteiro numa imagem (usado por exportar PNG e pelo minimapa).
QImage renderMapToImage(const Editor& ed, const RenderOptions& opt, const QSize& target = QSize());

/// Renderiza um documento sem precisar torna-lo o mapa ativo do editor.
/// Util para previas (por exemplo, escolher visualmente o destino de um
/// teletransporte) que nao podem alterar abas, selecao ou historico.
QImage renderMapToImage(const Editor& ed, const MapDoc& doc,
                        const RenderOptions& opt, const QSize& target = QSize());

/// Desenha a grade (base + multigrade da camada ativa).
void drawGrid(QPainter& p, const Editor& ed, const QRectF& visibleMapRect, double zoom);

/// Modo de composicao Qt equivalente ao blend-mode do canvas HTML.
QPainter::CompositionMode compositionFor(const QString& blendMode);

/// Aplica a tonalidade global da tela usando a mesma equacao do runtime.
/// R/G/B aceitam -255..255; gray aceita 0..255 (255 = dessaturacao total).
/// Esta funcao compartilhada mantem CPU e previews do editor visualmente
/// identicos. O caminho QRhi usa a mesma equacao no shader.
void applyScreenTone(QImage& image, int red, int green, int blue, int gray);

} // namespace core
