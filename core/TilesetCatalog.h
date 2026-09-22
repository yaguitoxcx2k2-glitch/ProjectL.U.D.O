// ============================================================================
//  TilesetCatalog.h — Autoridade de autoria para Tilesets/Autotiles 2.0.
//
//  O catalogo NAO reimplementa Wang. Ele conecta a identidade persistente de
//  `TilesetAutotile` aos WangSet/WangColor canonicos e aos frames fisicos de
//  AnimatedAutotile. Gerenciador, paleta e selecao reversa passam a consultar
//  este ponto em vez de inferir "autotile" por imagem/posicao.
// ============================================================================
#pragma once

#include "Editor.h"

#include <QRect>
#include <QStringList>

namespace core {


struct TilesetAutotileRemovalResult {
    int terrainTilesCleared = 0;
    bool terrainColorRemoved = false;
    bool animationRemoved = false;
};

struct TilesetAutotileInfo {
    int tilesetIdx = -1;
    const Tileset* tileset = nullptr;
    const TilesetAutotile* autotile = nullptr;
    const WangSet* wangSet = nullptr;
    const WangColor* wangColor = nullptr;
    QVector<QPoint> tiles;
    QRect bounds;
};

/// Resultado semantico da selecao reversa Mapa -> Paleta. A coordenada e
/// sempre canonizada para o frame logico do atlas; `autotileId` identifica o
/// recurso de primeira classe quando a celula pertence a um Autotile.
/// Nenhuma UI precisa inferir origem por textura, nome ou indice visual.
struct TilesetReverseSelection {
    int tilesetIdx = -1;
    QPoint tile{-1, -1};
    QString autotileId;
    QString wangSetId;
    int wangColorId = -1;

    bool valid() const { return tilesetIdx >= 0 && tile.x() >= 0 && tile.y() >= 0; }
    bool isAutotile() const { return !autotileId.isEmpty(); }
};

const WangSet* wangSetById(const Editor& ed, const QString& id);
WangSet* wangSetById(Editor& ed, const QString& id);

/// Registra (ou atualiza) a identidade semantica de uma regiao fisica do
/// atlas. Importadores e geradores devem usar esta funcao em vez de montar
/// `TilesetAutotile` diretamente, mantendo uma unica regra para recursos
/// estaticos e animados. Retorna o ID estavel ou vazio se a regiao for invalida.
QString registerTilesetAutotile(Editor& ed, int tilesetIdx, const QString& name,
                                const QRect& logicalRegion,
                                const QString& animatedAutotileId = QString(),
                                const QString& wangSetId = QString(),
                                int wangColorId = -1,
                                bool extendAtMapBoundary = true,
                                const QPoint& previewTile = QPoint(-1, -1));

struct AutotileWangAutoConfigResult {
    bool configured = false;
    bool alreadyConfigured = false;
    bool usedCustomPreset = false;
    bool usedRequiredNamedPreset = false;
    bool usedBundledPreset = false;
    bool migratedGeneratedFallback = false;
    QString requiredPresetName;
    QString failureMessage;
    QString presetId;
    QString presetName;
    QString wangSetId;
    int wangColorId = -1;
    int labeledTiles = 0;
};

/// Uma única autoridade para decidir se a geometria de importação exige Wang
/// automático. Inclui presets canônicos fornecidos (12x4, 4x4) e o fallback
/// mixed de 48 variantes. Importadores não devem duplicar essas dimensões.
bool autotileGridRequiresAutomaticWang(int columns, int rows,
                                       const QString& type = QStringLiteral("mixed"));

/// Configura automaticamente Terrain/Wang para um Autotile criado ou para
/// um recurso interno detectado pela reconciliação/migração. Grades canônicas
/// fornecidas pelo projeto são reconhecidas por dimensão/tipo: 12x4 usa
/// `Terrenos (12x4)` e 4x4 usa `4x4`, ambos embutidos literalmente. Em toda
/// importação automática o primeiro tile da última fileira horizontal vira a
/// variante isolada; presets incompatíveis com esse slot são rejeitados.
/// Outras geometrias continuam usando seleção compatível/fallback canônico.
/// A operação somente altera a Wang Color do próprio recurso e nunca apaga
/// Terrains/Wang de outros Autotiles.
bool autoConfigureTilesetAutotileWang(Editor& ed, int tilesetIdx,
                                      const QString& autotileId,
                                      AutotileWangAutoConfigResult* result = nullptr,
                                      const QString& preferredType = QStringLiteral("mixed"));

/// Resolve o owner fisico de um Autotile global pelo ID estavel do Tileset.
int tilesetIndexForAutotile(const Editor& ed, const TilesetAutotile& autotile);

/// Tilesets internos existem apenas como backing de Autotiles e nunca aparecem
/// nas paletas/listas de Tileset normal.
bool isVisibleTileset(const Tileset& tileset);
QVector<int> visibleTilesetIndices(const Editor& ed);
/// Tilesets exibidos na paleta principal. Recursos ocultos por organização
/// continuam acessíveis pelo Gerenciador e por camadas já existentes.
QVector<int> paletteTilesetIndices(const Editor& ed);

/// Tiles logicos que formam o recurso. Terrain vem dos rotulos Wang; animacao
/// vem da area base do AnimatedAutotile. O resultado nunca inclui frames
/// fisicos auxiliares.
QVector<QPoint> tilesetAutotileTiles(const Editor& ed, int tilesetIdx,
                                     const TilesetAutotile& autotile);
QRect tilesetAutotileBounds(const Editor& ed, int tilesetIdx,
                            const TilesetAutotile& autotile);

/// Resolve o recurso semantico que contem a coordenada logica indicada.
const TilesetAutotile* tilesetAutotileAt(const Editor& ed, int tilesetIdx,
                                         int tx, int ty);

/// Resolve a origem real de um TileRef colocado no mapa. Metadados Wang da
/// propria celula vencem em casos ambiguos; a coordenada canonica do catalogo
/// e usada como fallback para Autotiles sem Terrain.
TilesetReverseSelection resolveTilesetReverseSelection(const Editor& ed,
                                                        const TileRef& tile);

/// Aplica a selecao resolvida ao estado de autoria. Autotile ativa Terrain e
/// seu WangSet/cor; tile comum seleciona exatamente 1x1 e retorna a Stamp.
bool applyTilesetReverseSelection(Editor& ed, const TilesetReverseSelection& selection);

/// Atalho atomico usado pelo MapView: resolve + aplica sem duplicar regras na UI.
bool selectPlacedTileInPalette(Editor& ed, const TileRef& tile,
                               TilesetReverseSelection* resolved = nullptr);

/// Snapshot leve usado por UI/diagnostico. Pointers permanecem validos enquanto
/// os vetores de tilesets/wangSets nao forem modificados.
QVector<TilesetAutotileInfo> tilesetAutotileCatalog(const Editor& ed);

/// Resolve uma identidade persistente do catalogo global. O overload com
/// tilesetIdx apenas valida o owner fisico e existe para compatibilidade dos
/// consumidores que ja conhecem o atlas.
const TilesetAutotile* tilesetAutotileById(const Editor& ed, int tilesetIdx,
                                            const QString& autotileId);
TilesetAutotile* tilesetAutotileById(Editor& ed, int tilesetIdx,
                                      const QString& autotileId);

/// Resolve um Autotile pelo ID estavel em toda a biblioteca, independentemente
/// do Tileset normal atualmente aberto na paleta. `ownerTilesetIdx` recebe o
/// Tileset fisico que contem o recurso.
const TilesetAutotile* tilesetAutotileById(const Editor& ed, const QString& autotileId,
                                            int* ownerTilesetIdx);


/// Ativa um Autotile como ferramenta de autoria no editor principal. Autotile
/// e Tileset normal sao selecoes independentes: ativar o recurso NAO troca
/// `activeTilesetIdx`; o owner fisico fica resolvido pelo ID/`tsSel`. O tipo
/// selecionado continua determinando o modo: Autotile => Terrain/Wang.
bool activateTilesetAutotileForPainting(Editor& ed, int tilesetIdx, const QString& autotileId);

/// Volta a autoria da paleta para tiles comuns. A selecao fisica continua em
/// `tsSel`, mas a identidade Autotile e aposentada e a ferramenta volta ao
/// pincel normal.
void activateRegularTilesetPainting(Editor& ed);

/// Operacoes de ciclo de vida do recurso de primeira classe. Elas preservam
/// a identidade durante rename e removem somente os filhos de configuracao
/// que pertencem exclusivamente ao recurso removido.
bool renameTilesetAutotile(Editor& ed, int tilesetIdx, const QString& autotileId,
                           const QString& name);
/// Categoria e metadado semantico do Autotile global, independente do backing
/// e do Wang Set. String vazia representa "Sem categoria".
bool setTilesetAutotileCategory(Editor& ed, int tilesetIdx, const QString& autotileId,
                                const QString& category);
/// Catalogo normalizado/ordenado usado por filtros de biblioteca.
QStringList autotileCategories(const Editor& ed);
bool setTilesetAutotileBoundaryExtension(Editor& ed, int tilesetIdx,
                                          const QString& autotileId, bool enabled);

/// Recalcula somente as celulas de borda que usam este Autotile, escolhendo
/// a variante Wang pela politica `extendAtMapBoundary`. A operacao usa a
/// identidade do recurso para nunca misturar variantes de outro Tileset.
int rebuildTilesetAutotileBoundaryTopology(Editor& ed, int tilesetIdx,
                                           const QString& autotileId, bool notify = true);
/// Recalcula o recurso inteiro. Necessario quando a grade muda e uma celula
/// pode deixar de ser borda sem estar na nova borda.
int rebuildTilesetAutotileTopology(Editor& ed, int tilesetIdx,
                                   const QString& autotileId, bool notify = true);
/// Normaliza todas as bordas do projeto. Usado ao carregar projetos anteriores
/// ao Topology 2.0 e por validacoes de paridade.
int rebuildAllAutotileBoundaryTopologies(Editor& ed, bool notify = true);
/// Normaliza todos os tiles Terrain. Reservado para mudancas estruturais da
/// grade (resize); pintura normal continua atualizando apenas a vizinhanca 3x3.
int rebuildAllAutotileTopologies(Editor& ed, bool notify = true);

/// Vincula a identidade do Autotile a um Terrain/Wang existente. O catalogo
/// continua sendo a autoridade da relacao; UI nenhuma deve escrever os campos
/// wangSetId/wangColorId diretamente. Ao trocar o vinculo, os rotulos antigos
/// exclusivos daquele recurso sao aposentados com seguranca.
bool setTilesetAutotileTerrain(Editor& ed, int tilesetIdx, const QString& autotileId,
                               const QString& wangSetId, int wangColorId);
/// Remove o vinculo Terrain/Wang e os rotulos exclusivos do recurso sem apagar
/// pixels do atlas nem destruir cores compartilhadas por outros Autotiles.
bool clearTilesetAutotileTerrain(Editor& ed, int tilesetIdx, const QString& autotileId);

bool removeTilesetAutotile(Editor& ed, int tilesetIdx, const QString& autotileId,
                           TilesetAutotileRemovalResult* result = nullptr);

/// Migra projetos antigos para o catalogo GLOBAL sem alterar Wang. Para cada
/// combinacao Tileset + WangSet + cor realmente utilizada, garante uma
/// identidade persistente. AnimatedAutotile sem Terrain tambem recebe identidade.
int reconcileTilesetAutotiles(Editor& ed);

/// Prepara uma copia real de um Tileset fisico: regenera IDs do atlas e das
/// animacoes. Identidades semanticas de Autotile vivem no Editor e nao sao
/// duplicadas implicitamente com um Tileset normal.
void regenerateTilesetResourceIds(Tileset& tileset);

/// Copia os rotulos Wang pertencentes a um tileset para outro indice. Usado
/// pela duplicacao para que Terrain/Autotile acompanhe o recurso copiado.
int copyTilesetTerrainLabels(Editor& ed, int sourceTilesetIdx, int targetTilesetIdx);

} // namespace core
