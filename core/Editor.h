// ============================================================================
//  Editor.h — Estado central da aplicacao (equivalente ao `state` + `mapDocs`
//  globais do JS) exposto como QObject com sinais, para que os widgets se
//  atualizem por connect() em vez das chamadas manuais drawMap()/refreshX().
// ============================================================================
#pragma once

#include "RpgMakerTarget.h"
#include "AssetDatabase.h"
#include "EditorSession.h"
#include "LayerTree.h"
#include "Model.h"

#include <QObject>
#include <QColor>
#include <QStack>
#include <QStringList>
#include <QMargins>
#include <QRectF>
#include <QHash>
#include <QPointF>
#include <QSizeF>
#include <QImage>
#include <QJsonObject>

namespace core {


class ResourceManager;

// ------------------------------------------------------------------ Historico
/// Snapshot de uma unica camada (paint ops) — equivalente a snapshotLayer().
struct LayerSnapshot {
    QVector<QVector<Cell>> data2D;
    QVector<MapObject>     objects;
    QImage                 image;       ///< COW: barato até a camada raster ser modificada
    QImage                 imageMask;   ///< máscara raster de Image/Tile Layer
    bool                   imageMaskEnabled = false;
    int                    offsetx = 0;
    int                    offsety = 0;
};

/// Snapshot do documento inteiro (operacoes estruturais: adicionar/remover
/// camada, redimensionar mapa, renomear...) — equivalente a snapshotDocumentState().
struct DocSnapshot {
    QString           name;
    QString           parentId;
    // Metadados estruturais também pertencem ao estado do documento.
    // Mantê-los no snapshot evita Undo/Redo parcial e permite que o canal
    // colaborativo compare a mesma unidade lógica usada pelo Editor.
    QString           variationBaseId;
    QString           variationName;
    int               rpgMakerMapId = 0;
    bool              rpgMakerImported = false;
    MapInfo           map;
    QVector<LayerPtr> layers;
    int               activeLayerIdx = -1;
    /// ID estável da camada ativa. activeLayerIdx permanece para compatibilidade
    /// com projetos antigos e como fallback de migração.
    QString           activeLayerId;
    QHash<quint64, quint8> rpgMakerRegions;
    bool             rpgMakerRegionsAuthored = false;
    QJsonObject       reflectionSettings;
};

struct TileHistoryChange {
    int x = 0;
    int y = 0;
    Cell before;
    Cell after;
};

/// Alteração esparsa da camada nativa de Regiões do RPG Maker MV/MZ.
/// Região 0 significa "sem região"; valores válidos exportados são 1..255.
struct RegionHistoryChange {
    int x = 0;
    int y = 0;
    int before = 0;
    int after = 0;
};

struct HistoryEntry {
    bool          document = false;
    bool          tileDiff = false;
    bool          regionDiff = false;
    QString       label;
    QString       layerId;
    QVector<TileHistoryChange> tileChanges;
    QVector<RegionHistoryChange> regionChanges;
    bool          beforeRegionsAuthored = false;
    bool          afterRegionsAuthored = false;
    LayerSnapshot beforeLayer, afterLayer;
    DocSnapshot   beforeDoc,   afterDoc;

    // Operacoes estruturais que tambem criam/removem recursos globais (ex.:
    // Bake de Object Layer -> Tileset) precisam desfazer o mapa E os tilesets
    // de forma atomica. O snapshot e opcional para nao inflar historico comum.
    bool              tilesetSnapshot = false;
    QVector<Tileset>  beforeTilesets, afterTilesets;
    int               beforeActiveTilesetIdx = -1, afterActiveTilesetIdx = -1;
    TilesetSelection  beforeTsSel, afterTsSel;
    CustomStamp       beforeCustomStamp, afterCustomStamp;
};

// ---------------------------------------------------------------- Documento
/// Um mapa aberto (aba). Equivalente a um item de mapDocs.
struct MapDoc {
    QString              id = idGen();
    QString              name;
    /// Organização visual da árvore de mapas. Vazio = mapa na raiz do
    /// projeto; caso contrário contém o id de outro MapDoc.
    /// A hierarquia não muda a renderização nem o formato interno do mapa.
    QString              parentId;

    // Variações representam estados alternativos do mesmo cenário (por
    // exemplo: quarto limpo / quarto destruído). Cada variação continua sendo
    // um mapa completo e independente no RPG Maker; estes campos servem para
    // agrupá-la sob o mapa-base apenas na organização do LUDO.
    QString              variationBaseId;
    QString              variationName;

    MapInfo              map;
    QVector<LayerPtr>    layers;
    int                  activeLayerIdx = -1;
    /// Fonte de verdade estável para a camada de pintura após reordenação.
    /// Projetos antigos sem este campo continuam usando activeLayerIdx.
    QString              activeLayerId;

    // Regiões nativas do RPG Maker MV/MZ (camada z=5 de MapXXX.data).
    // Armazenamento esparso: apenas IDs 1..255 são mantidos. O booleano
    // diferencia "ainda não administrado pelo LUDO" de "administrado, mas
    // propositalmente vazio", evitando apagar regiões antigas do RPG Maker ao migrar.
    QHash<quint64, quint8> rpgMakerRegions;
    bool                  rpgMakerRegionsAuthored = false;

    // Configuração integrada do LudoReflectionSystem para este mapa.
    // Vazio = usar os padrões do plugin/runtime.
    QJsonObject           reflectionSettings;

    // Vínculo estável com MapXXX.json. Zero = ainda não vinculado.
    int                   rpgMakerMapId = 0;
    // Mapas criados no RPG Maker entram como placeholders visuais vazios até o usuário
    // começar a desenhá-los no LUDO.
    bool                  rpgMakerImported = false;

    static quint64 regionKey(int x, int y) {
        return (quint64(quint32(y)) << 32) | quint64(quint32(x));
    }
    static int regionX(quint64 key) { return int(quint32(key & 0xffffffffu)); }
    static int regionY(quint64 key) { return int(quint32(key >> 32)); }
    bool regionInBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < map.width && y < map.height;
    }
    int regionIdAt(int x, int y) const {
        if (!regionInBounds(x, y)) return 0;
        return int(rpgMakerRegions.value(regionKey(x, y), quint8(0)));
    }
    void setRegionIdAt(int x, int y, int regionId) {
        if (!regionInBounds(x, y)) return;
        rpgMakerRegionsAuthored = true;
        const quint64 key = regionKey(x, y);
        const int normalized = qBound(0, regionId, 255);
        if (normalized == 0) rpgMakerRegions.remove(key);
        else rpgMakerRegions.insert(key, quint8(normalized));
    }
    void pruneRegions() {
        QVector<quint64> remove;
        remove.reserve(rpgMakerRegions.size());
        for (auto it = rpgMakerRegions.cbegin(); it != rpgMakerRegions.cend(); ++it) {
            const int x = regionX(it.key()), y = regionY(it.key());
            if (!regionInBounds(x, y) || it.value() == 0) remove.push_back(it.key());
        }
        for (quint64 key : remove) rpgMakerRegions.remove(key);
    }

    bool                 dirty = false;
    QVector<HistoryEntry> history;
    int                  historyPtr = -1;
    /// Monotonic in-memory transaction serial. Unlike historyPtr it keeps
    /// changing after the 150-entry history window starts pruning old items.
    quint64              historyRevision = 0;
};

// ------------------------------------------------------------------- Editor
class Editor : public QObject
{
    Q_OBJECT
public:
    explicit Editor(QObject* parent = nullptr);

    // ---- projeto ---------------------------------------------------------
    QString projectName = QStringLiteral("Meu projeto");
    /// Identidade persistente usada para separar os saves de cada jogo.
    QString projectId;
    QString projectPath;                    ///< arquivo .json dentro da pasta do projeto
    bool    projectDirty = false;

    // Engine-alvo persistente do projeto. Projetos antigos sem esse campo
    // continuam abrindo como MZ para preservar compatibilidade.
    RpgMakerEngine rpgMakerEngine = RpgMakerEngine::MZ;

    // Projeto RPG Maker de destino vinculado. MV e MZ compartilham a mesma
    // árvore/IDs via data/MapInfos.json.
    QString rpgMakerProjectRoot;

    /// Pasta-base do projeto e seu navegador de conteúdo estilo editores modernos.
    QString projectRoot() const;
    QString assetsRoot() const;
    /// Converte um arquivo da pasta do projeto para caminho relativo portátil.
    QString projectRelativePath(const QString& absolutePath) const;
    ResourceManager& resources();
    const ResourceManager& resources() const;

    /// Bloco B / 3.24.0 — inventário persistente de arquivos em Assets/.
    AssetDatabase assetDatabase;

    // ---- recursos globais (compartilhados por todos os mapas) ------------
    QVector<Tileset>     tilesets;          ///< atlases fisicos; inclui backing oculto de Autotiles
    QVector<TilesetAutotile> autotiles;     ///< catalogo semantico global de Autotiles
    QVector<WangSet>     wangSets;
    QVector<WangPreset>  wangPresets;
    QHash<QString, bool> starTiles;         ///< espelho legado ★; fonte moderna = Tileset::tilePriorities
    QVector<RandomEntry> randomPool;
    QVector<SavedStamp>  savedStamps;
    QVector<TileRef>     recentTiles;


    // ---- documentos ------------------------------------------------------
    QVector<MapDoc> docs;
    int             activeDocIdx = -1;

    // ---- estado de edicao VOLATIL ----------------------------------------
    // Fonte única de verdade para seleção, ferramenta, zoom, clipboard e
    // demais estados transitórios da UI. Nada daqui pertence ao .ludo.
    EditorSession session;

    // ---- acesso ao documento ativo ---------------------------------------
    MapDoc*        doc();
    const MapDoc*  doc() const;
    int            mapIndexById(const QString& id) const;
    MapDoc*        mapById(const QString& id);
    const MapDoc*  mapById(const QString& id) const;
    MapInfo&       mapInfo();
    const MapInfo& mapInfo() const;
    QVector<LayerPtr>& layers();
    const QVector<LayerPtr>& layers() const;

    /// Lista achatada na ordem de renderizacao (equivalente a state._flat).
    QVector<LayerPtr> flatLayers() const;
    LayerPtr activeLayer() const;
    int      activeLayerIdx() const;
    void     setActiveLayerIdx(int idx);
    void     setActiveLayerById(const QString& id);

    /// Seleção visual da árvore. Grupos podem ser selecionados sem trocar a
    /// camada de pintura; isso evita operações estruturais no alvo errado.
    LayerPtr selectedLayer() const;
    QVector<LayerPtr> selectedLayers() const;
    void     setSelectedLayerById(const QString& id);
    void     setSelectedLayerIds(const QSet<QString>& ids, const QString& primaryId = QString());

    /// Busca um no na arvore; devolve tambem o vetor-pai e o indice.
    LayerPtr findNode(const QString& id, QVector<LayerPtr>** parentOut = nullptr,
                      int* indexOut = nullptr);
    LayerPtr findNode(const QString& id) const;

    // ---- documentos ------------------------------------------------------
    void newProject();
    int  addMapDoc(const QString& name, const MapInfo& info, bool activate = true);
    void switchDoc(int idx);
    QString uniqueMapName(const QString& base) const;

    // ---- camadas ---------------------------------------------------------
    /// Cria a camada e a insere no grupo ativo (ou na raiz), acima da ativa.
    LayerPtr addLayer(LayerPtr layer, const QString& parentGroupId = QString());
    void     removeLayer(const QString& id);
    void     duplicateLayer(const QString& id);
    /// Mescla a camada com a irmã imediatamente abaixo, preservando pilhas
    /// de tiles/objetos. Retorna false quando a operação seria destrutiva.
    bool     mergeDown(const QString& id, QString* error = nullptr);
    /// Rasteriza uma Object Layer exatamente como aparece, corta o resultado
    /// pela grade-base, cria um Tileset gerado e substitui a origem por uma
    /// Tile Layer. O resultado continua compatível com Patterns/Empilhar/etc.
    /// A operação registra seu próprio Undo/Redo porque também cria tilesets.
    /// Faz Bake de qualquer camada/subárvore visual para um Tileset e troca a
    /// origem por uma Tile Layer. `showInPalette=false` mantém o recurso apenas
    /// no Gerenciador/projeto, evitando poluir a paleta principal.
    bool     bakeLayerToTileset(const QString& id, bool showInPalette, QString* error = nullptr);
    bool     bakeObjectLayerToTileset(const QString& id, QString* error = nullptr);
    /// Move o conteúdo do mapa em tiles da grade base, no estilo RPG Maker.
    bool     shiftMapContents(int dxTiles, int dyTiles, bool shiftRegions = true,
                              QString* error = nullptr);
    void     moveLayer(const QString& id, int delta);
    /// Reancora `id` dentro de `newParentId` (vazio = raiz) na posicao `index`.
    bool     reparentLayer(const QString& id, const QString& newParentId, int index);
    /// Reorganiza a árvore inteira por IDs estáveis, após validação de ciclos,
    /// pais e unicidade. Nenhum widget deve reescrever Layer::children direto.
    bool     applyLayerTreeOrder(const QVector<LayerTreePlacement>& placements, QString* error = nullptr);
    /// Recria as grades de todas as camadas de tiles apos mudar o tamanho do mapa.
    void     resyncLayerGrids();

    // ---- tilesets --------------------------------------------------------
    int  addTileset(const Tileset& ts);
    void removeTileset(int idx);
    void reindexTilesetGids();
    int  nextFirstGid() const;
    const Tileset* tilesetAt(int idx) const;
    Tileset*       tilesetAt(int idx);
    /// gid global (Tiled) de um tile.
    int  gidFor(int tilesetIdx, int tx, int ty) const;
    /// Tileset dono de um gid.
    int  tilesetIdxForGid(int gid) const;

    // ---- marcadores / probabilidade --------------------------------------
    /// Lados de um tile, como bits de uma mascara.
    enum Side { SideTop = 1, SideRight = 2, SideBottom = 4, SideLeft = 8, SideAll = 15 };
    /// Lado oposto (para checar a celula vizinha ao atravessar a fronteira).
    static int oppositeSide(int side);

    /// Prioridade visual do tile: 0 = normal; 1..5 = ordenacao Y com atores.
    int    tilePriority(int tilesetIdx, int tx, int ty) const;
    void   setTilePriority(int tilesetIdx, int tx, int ty, int priority);
    void   cycleTilePriority(int tilesetIdx, int tx, int ty);
    /// Aplica prioridades por linha, da base para o topo: 1,2,3,4,5.
    /// Linhas acima do quinto nivel permanecem em 5 (limite do formato).
    /// Retorna a quantidade de linhas logicas selecionadas.
    int    applyTilePrioritiesBottomUp(int tilesetIdx, const QVector<QPoint>& tiles);
    /// Compatibilidade com o antigo marcador ★: qualquer prioridade > 0.
    bool   isStarMarked(int tilesetIdx, int tx, int ty) const;
    /// Compatibilidade historica: alterna 0 <-> 1. Novas UIs usam cycle/set.
    void   toggleStar(int tilesetIdx, int tx, int ty);
    /// true se o tile bloqueia ao menos um lado.
    bool   isCollisionMarked(int tilesetIdx, int tx, int ty) const;
    /// true apenas quando os quatro lados estao bloqueados. Esta e a
    /// semantica simples exposta pelo Editor principal.
    bool   isTileFullyBlocked(int tilesetIdx, int tx, int ty) const;
    /// Mascara detalhada de lados bloqueados (0 = livre, 15 = inteiro).
    /// A UI detalhada pertence ao Gerenciador de Tilesets.
    int    collisionMask(int tilesetIdx, int tx, int ty) const;
    void   setCollisionMask(int tilesetIdx, int tx, int ty, int mask);
    /// Contrato simples do Editor: nunca produz mascara parcial.
    void   setTileBlocked(int tilesetIdx, int tx, int ty, bool blocked);
    void   toggleTileBlocked(int tilesetIdx, int tx, int ty);
    /// Compatibilidade historica: delega ao contrato simples.
    void   toggleCollision(int tilesetIdx, int tx, int ty);
    double tileProb(int tilesetIdx, int tx, int ty) const;
    void   setTileProb(int tilesetIdx, int tx, int ty, double v);
    QString tileReflectionPreset(int tilesetIdx, int tx, int ty) const;
    void    setTileReflectionPreset(int tilesetIdx, int tx, int ty, const QString& preset);
    /// -1 usa a opacidade nativa do preset; 0..100 multiplica toda a reflexão
    /// produzida por este recurso (céu, personagens, objetos e luzes).
    int     tileReflectionOpacityPercent(int tilesetIdx, int tx, int ty) const;
    void    setTileReflectionOpacityPercent(int tilesetIdx, int tx, int ty, int percent);
    QString tileReflectionBlurMode(int tilesetIdx, int tx, int ty) const;
    int     tileReflectionBlurStrength(int tilesetIdx, int tx, int ty) const;
    void    setTileReflectionBlur(int tilesetIdx, int tx, int ty, const QString& mode, int strength);

    // ---- pool aleatorio --------------------------------------------------
    void addRectToRandomPool(int tilesetIdx, int x, int y, int w, int h);
    bool isInRandomPool(int tilesetIdx, int tx, int ty) const;
    void clearRandomPool();
    /// Sorteia uma entrada inteira do pool (multi-tile), respeitando pesos.
    const RandomEntry* pickRandomEntry() const;
    /// Sorteia um unico tile "achatado" do pool (usado por rect/circle/linha).
    TileRef pickRandomSingleTile() const;

    // ---- stamp / patterns -----------------------------------------------
    /// Equivalente a currentStampTiles(): customStamp tem prioridade sobre tsSel.
    Stamp currentStamp() const;
    QString savePattern(const QString& name, const Stamp& stamp);
    bool    applyPattern(const QString& id);
    bool    renamePattern(const QString& id, const QString& name);
    bool    removePattern(const QString& id);


    // ---- Wang ------------------------------------------------------------
    WangSet*       activeWangSet();
    const WangSet* activeWangSet() const;
    void           setActiveWangSet(int idx);

    // ---- historico -------------------------------------------------------
    LayerSnapshot snapshotLayer(const LayerPtr& l) const;
    void          applySnapshot(const LayerPtr& l, const LayerSnapshot& s);
    DocSnapshot   snapshotDoc() const;
    void          applyDocSnapshot(const DocSnapshot& s);

    /// Inicia uma edicao de camada; guarde o retorno e passe a commitLayerEdit.
    struct EditSession { LayerPtr layer; LayerSnapshot before; bool valid = false; };
    EditSession beginLayerEdit(const LayerPtr& layer = LayerPtr());
    void        commitLayerEdit(EditSession& s, const QString& label = QString());

    /// Historico de operacao estrutural.
    void pushDocHistory(const DocSnapshot& before, const QString& label);
    /// Histórico estrutural que também inclui os Tilesets globais. Útil para
    /// operações como Bake/Inclinação que criam uma camada e um Tileset juntos.
    void pushDocTilesetHistory(const DocSnapshot& beforeDoc, const QVector<Tileset>& beforeTilesets,
                               int beforeActiveTilesetIdx, const TilesetSelection& beforeTsSel,
                               const CustomStamp& beforeCustomStamp, const QString& label);
    void pushRegionHistory(const QVector<RegionHistoryChange>& changes, bool beforeAuthored,
                           const QString& label = QString());

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    QStringList historyLabels(int* currentPtr) const;
    void jumpHistory(int index);

    void markDirty();
    void markSaved();

    /// Revisão monotônica do conteúdo do mapa. Caches derivados do runtime
    /// podem comparar este valor em O(1) e reconstruir-se somente quando o
    /// documento realmente mudou. Não é persistida no projeto/save.
    quint64 mapRevision() const { return m_mapRevision; }

signals:
    void mapChanged();          ///< conteudo das camadas mudou (redesenhar canvas)
    void layersChanged();       ///< arvore/propriedades de camadas mudou
    void tilesetsChanged();
    void wangChanged();
    void selectionChanged();    ///< selecao de tileset/objeto mudou
    void patternsChanged();     ///< biblioteca de patterns mudou
    void historyChanged();
    void docsChanged();         ///< abas de mapa
    void projectChanged();      ///< nome/dirty do projeto
    void status(const QString& message, int timeoutMs = 4000);

private:
    void pruneHistory(MapDoc* d);
    ResourceManager* m_resources = nullptr;
    quint64 m_mapRevision = 1;
};

/// Instancia global (a ferramenta web tambem tem um unico `state`).
Editor& editor();

} // namespace core
