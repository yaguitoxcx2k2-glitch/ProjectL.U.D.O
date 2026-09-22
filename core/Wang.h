// ============================================================================
//  Wang.h — Logica de Wang Tiles / Terrain Brush (blob 47).
//
//  Tilesets/Autotiles 2.0: Wang continua sendo a autoridade das variantes,
//  enquanto AutotileTopology concentra a regra de vizinhanca/borda. O contexto
//  abaixo restringe a escolha visual ao recurso Autotile selecionado.
// ============================================================================
#pragma once

#include "Editor.h"
#include "AutotileTopology.h"

#include <QSet>

namespace core { namespace wang {

struct TerrainPaintContext {
    const Editor* sourceEditor = nullptr; ///< permite resolver o recurso dos vizinhos
    int tilesetIdx = -1;
    QString autotileId;
    bool extendAtMapBoundary = false;
    /// Empilhar tiles preserva o conteúdo já existente da célula e trabalha
    /// somente no topo. Isso também vale para Autotiles/Terrain.
    bool preserveCellStack = false;
    QSet<QString> allowedTileKeys;        ///< vazio = compatibilidade Wang legado

    bool restrictsTiles() const { return !allowedTileKeys.isEmpty(); }
};

/// Constroi o contexto canonico a partir do ID estavel do Autotile.
TerrainPaintContext contextForAutotile(const Editor& ed, int tilesetIdx,
                                       const QString& autotileId);

/// Regra dos cantos do "Tileset Roundup": wrapper de compatibilidade para a
/// autoridade canonica em AutotileTopology.
int filterMaskBlob(bool top, bool topRight, bool right, bool bottomRight,
                   bool bottom, bool bottomLeft, bool left, bool topLeft);

/// Mascara de um tile a partir dos seus rotulos, para uma cor-alvo.
int maskFromData(const WangTileData& d, int targetColorId,
                 const QString& type = QStringLiteral("mixed"));
int normalizeMaskForType(int mask, const QString& type);

/// O tile participa desta cor? Conta tanto rotulos nas 8 direcoes quanto a
/// marca de "tile isolado".
bool dataHasColor(const WangTileData& d, int colorId);

/// Posicoes validas conforme o tipo do Wang Set (mixed | corner | edge).
QStringList allowedPositions(const QString& type);

bool createTerrainFromPreset(WangSet& set, const WangPreset& preset, int tilesetIdx,
                             int originX, int originY, int* colorIdOut, int* countOut);
int removeColor(WangSet& set, int colorId);

/// A celula (x,y) pertence ao Wang Set/cor indicados? Fora da grade sempre
/// retorna false; continuidade de borda e responsabilidade de neighborMask().
bool isWangCell(const LayerPtr& layer, int x, int y, const QString& wangSetId, int colorId);
int cellColor(const LayerPtr& layer, int x, int y, const QString& wangSetId);

/// Mascara de vizinhanca da celula. Quando o contexto pede continuidade,
/// coordenadas fora do mapa contam como o mesmo terreno.
int neighborMask(const LayerPtr& layer, int x, int y, const QString& wangSetId, int colorId,
                 const TerrainPaintContext& context = TerrainPaintContext());

/// As buscas continuam publicas para testes/compatibilidade. Com contexto 2.0,
/// candidatos fisicos ficam restritos ao recurso Autotile selecionado.
bool findTileForMask(const WangSet& set, int colorId, int mask, TileRef* out,
                     const TerrainPaintContext& context = TerrainPaintContext());
bool findTileBestMatch(const WangSet& set, int colorId, int mask, TileRef* out,
                       const TerrainPaintContext& context = TerrainPaintContext());
bool setCellToMask(const LayerPtr& layer, int x, int y, const WangSet& set, int colorId, int mask,
                   const TerrainPaintContext& context = TerrainPaintContext());

/// Recalcula os 9 tiles da vizinhanca apos pintar/apagar. Vizinhos que ja
/// pertencem a outro Autotile tentam resolver seu proprio contexto por ID/regiao.
void updateNeighbors(const LayerPtr& layer, int cx, int cy, const WangSet& set,
                     int paintedColorId,
                     const TerrainPaintContext& context = TerrainPaintContext());

bool paintAt(const LayerPtr& layer, int gx, int gy, const WangSet& set, int colorId, bool erase,
             const TerrainPaintContext& context = TerrainPaintContext());
/// Pintura semântica em lote para Retângulo/Círculo/Linha/Balde. As células
/// são alteradas primeiro e a topologia é recomposta uma única vez ao final,
/// evitando o custo O(n×vizinhança) de chamar paintAt() para cada célula.
int paintCells(const Editor& ed, const LayerPtr& layer, const QVector<QPoint>& cells,
               const WangSet& set, int colorId, bool erase,
               const TerrainPaintContext& context = TerrainPaintContext());

/// Recalcula somente celulas de borda pertencentes ao recurso/contexto. Usado
/// ao mudar a propriedade "continuar nas bordas" e na migracao de projetos.
/// Retorna quantas celulas mudaram de variante.
int retileBoundaryCells(const LayerPtr& layer, const WangSet& set, int colorId,
                        const TerrainPaintContext& context);
/// Recalcula todas as celulas do recurso. Usado quando a geometria da grade
/// muda (ex.: resize): celulas que deixaram de ser borda tambem precisam ser
/// normalizadas. Variantes que ja batem com a mascara sao preservadas.
int retileTerrainCells(const LayerPtr& layer, const WangSet& set, int colorId,
                       const TerrainPaintContext& context);

/// Recalcula todos os Terrains existentes na vizinhanca de uma mudanca de
/// ocupacao. Nao importa se a mudanca veio de tile normal, outro Autotile ou
/// borracha: qualquer top tile diferente deve abrir/fechar as bordas vizinhas.
int retileTerrainNeighborhood(const Editor& ed, const LayerPtr& layer,
                              const QRect& changedCells);
/// Variante para mudancas de alcance desconhecido (ex.: flood fill).
int retileTerrainLayer(const Editor& ed, const LayerPtr& layer);

WangPreset builtinBlob47();
/// Preset canônico 12x4 fornecido pelo projeto para importação automática de
/// Autotiles. A geometria é embutida no binário para não depender de QSettings
/// nem da ordem dos presets personalizados da instalação.
WangPreset builtinTerrenos12x4();
/// Preset canônico 4x4 fornecido pelo projeto. A grade possui 15 variantes
/// normais; o tile (0,3) fica reservado para o isolado pelo pipeline de
/// importação automática.
WangPreset builtinAutotile4x4();
/// Versao canônica do Blob47 reorganizada em qualquer grade row-major de
/// exatamente 48 slots (ex.: 8x6 ou 12x4). Mantem a mesma ordem de variantes
/// do blob47::pickTile(); a mascara 0 fica vazia no preset bruto e o pipeline
/// automatico atribui a variante isolada pela convencao da ultima fileira.
WangPreset builtinBlob47ForGrid(int columns, int rows);
/// Um preset e auto-aplicavel quando a geometria coincide e cobre todas as
/// mascaras esperadas para seu tipo (incluindo a mascara isolada).
bool presetIsCompleteForGrid(const WangPreset& preset, int columns, int rows);
int applyPreset(WangSet& set, const WangPreset& preset, int tilesetIdx,
                int originX, int originY, int colorId,
                const QRect& allowedRegion = QRect(),
                const QSet<QString>& allowedTileKeys = QSet<QString>());
QVector<int> expectedMasks(const QString& type = QStringLiteral("mixed"));
QVector<int> missingMasks(const WangSet& set, int colorId,
                         const QSet<QString>& allowedTileKeys = QSet<QString>());
QHash<int, QStringList> duplicateMasks(const WangSet& set, int colorId,
                                       const QSet<QString>& allowedTileKeys = QSet<QString>());
double coverage(const WangSet& set, int colorId,
                const QSet<QString>& allowedTileKeys = QSet<QString>());

}} // namespace core::wang
