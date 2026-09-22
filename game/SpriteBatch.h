// ============================================================================
//  SpriteBatch.h — Monta a cena do jogo como QUADS, sem tocar em GPU nenhuma.
//
//  Este arquivo é a fronteira do porte para GPU: ele percorre as camadas do
//  mapa exatamente como o renderizador de CPU faz, mas em vez de chamar
//  `drawPixmap` ele **acumula vértices**. Quem sobe isso para a placa de vídeo
//  é a janela (`RhiGameWindow`), que não conhece regra de camada nenhuma.
//
//  Por que separado assim: dá para TESTAR a cena inteira sem GPU, sem driver e
//  sem tela — conferindo quantos lotes saíram, em que ordem e com quais UVs.
//  Um bug de "tile errado na tela" vira uma falha de teste, não uma caçada
//  visual.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/Picture.h"
#include "game/RuntimeRenderState.h"

#include <QHash>
#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QString>
#include <QVector>
#include <functional>

namespace game {

/// Vértice: posição em World/Camera/Screen/Ui conforme DrawBatch::space, UV 0..1 e cor/alfa.
struct QuadVertex {
    float x = 0, y = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};

/// Um lote = tudo que usa a MESMA textura E a mesma mistura, e pode ir numa
/// chamada só. A mistura entra na chave do lote porque na GPU ela é ESTADO de
/// pipeline: dois quads com misturas diferentes nunca vão juntos.
struct DrawBatch {
    QString texture;              ///< chave da textura (ver TextureKey)
    core::PictureBlend blend = core::PictureBlend::Normal;
    /// Se true, o shader da GPU aplica a tonalidade global da tela a este lote.
    /// Mantemos isto no lote (e não no alfa do vértice) para preservar os
    /// valores 0..1 usados pelos testes e pelo restante do renderer.
    bool affectedByScreenTone = true;
    /// Filtro da textura: Pictures podem pedir suavização sem desfocar tiles pixel-art.
    bool smooth = false;
    /// Espaço declarado dos vértices. O renderer projeta World via
    /// World->Camera->Screen, Camera via Camera->Screen e mantém Screen/Ui
    /// literais. Nunca há compensação manual ou segunda projeção.
    RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World;
    /// Profundidade World-Y usada somente pelo compositor ★/atores. Lotes
    /// comuns permanecem em zero e continuam seguindo a ordem de inserção.
    double depth = 0.0;
    QVector<QuadVertex> verts;    ///< 6 vértices por quad (dois triângulos)
    int quads() const { return verts.size() / 6; }
};

/// Chaves de textura: o tileset já é um atlas, então uma textura por tileset
/// cobre o mapa inteiro. Imagens soltas (objetos, charsets) ganham a sua.
namespace TextureKey {
QString tileset(int idx);
QString image(const QString& id);
}

/// Acumula quads preservando a ORDEM de inserção (quem entra depois desenha
/// por cima) e junta vizinhos que usam a mesma textura.
class SpriteBatcher
{
public:
    void clear();
    /// `src` em pixels da textura, `texSize` o tamanho dela e `dst` no espaço
    /// informado. `opacity` multiplica o alfa.
    void add(const QString& texture, const QRectF& dst, const QRectF& src,
             const QSize& texSize, double opacity = 1.0, const QColor& tint = QColor(),
             core::PictureBlend blend = core::PictureBlend::Normal,
             bool affectedByScreenTone = true,
             RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World,
             double depth = 0.0);
    /// Quad de cor sólida (sem textura): usado para sombra e caixas.
    void addSolid(const QRectF& dst, const QColor& cor, bool affectedByScreenTone = true,
                  RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World);
    /// Quad sólido arbitrário. Usado por primitivas World-Space que não são
    /// axis-aligned (ex.: traços inclinados do clima) sem criar textura/pipeline
    /// paralela no backend QRhi. Os pontos devem vir em ordem TL,TR,BR,BL.
    void addSolidQuad(const QPolygonF& quad, const QColor& cor,
                      bool affectedByScreenTone = true,
                      RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World);
    /// Quad com transformação livre (rotação, escala, âncora) — é o que as
    /// imagens de tela precisam: `t` leva o retângulo local `local` para os
    /// espaço declarado. `src` em pixels da textura.
    void addTransformed(const QString& texture, const QTransform& t, const QRectF& local,
                        const QRectF& src, const QSize& texSize, double opacity,
                        core::PictureBlend blend, bool affectedByScreenTone = true,
                        RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World,
                        double depth = 0.0, bool smooth = false);
    /// Acrescenta lotes já montados, preservando ordem e fundindo vizinhos compatíveis.
    void append(const SpriteBatcher& other);

    const QVector<DrawBatch>& batches() const { return m_batches; }
    int totalQuads() const;
    bool isEmpty() const { return m_batches.isEmpty(); }

private:
    QVector<DrawBatch> m_batches;
};

/// Imagens que a cena precisa, por chave — a janela sobe para a GPU e guarda
/// em cache. O mapa é somente leitura durante o jogo, então isso é montado uma
/// vez e reaproveitado.
using ImageProvider = QHash<QString, QImage>;

/// Filtros da passada (mesma ideia do RenderOptions do renderizador de CPU).
struct ScenePass {
    bool starTiles = false;     ///< nome legado da passada: true = prioridades 1..5; false = prioridade 0
    bool aboveLayers = false;   ///< true = só camadas "acima"; false = as de baixo
    /// Pintar a cor de fundo do mapa antes das camadas. Só a primeira
    /// passada deve fazer isso — é o que o renderizador de CPU faz.
    bool drawBackground = false;
    /// Retângulo visível em pixels do mapa. Vazio = mapa inteiro.
    /// Sem isto, um mapa 200×150 com 6 camadas geraria 180 mil quads POR
    /// QUADRO — mais dado do que qualquer GPU merece receber à toa.
    QRectF visible;
    qint64 animationTimeMs = 0; ///< mesmo relógio visual usado pelo caminho CPU
    /// Se true, cada linha/célula mantém a profundidade da borda inferior do ★
    /// para ser intercalada com os pés de jogador/eventos.
    bool depthSortedTiles = false;
    /// Bloco I: célula efetiva de runtime. Vazio = usa MapDoc imutável.
    std::function<core::Cell(const core::LayerPtr&, int, int)> cellResolver;
};

/// Percorre as camadas e enche o batcher. Devolve as imagens necessárias em
/// `provider` (só as que ainda não estiverem lá).
void buildSceneBatches(const core::Editor& ed, const ScenePass& pass,
                       SpriteBatcher& out, ImageProvider& provider);

/// Um sprite avulso (jogador, evento) na posição em pixels do mapa.
void addSpriteQuad(SpriteBatcher& out, ImageProvider& provider,
                   const QString& id, const QImage& img, const QRectF& src,
                   const QRectF& dst, double opacity = 1.0);

} // namespace game
