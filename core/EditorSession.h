// ============================================================================
// EditorSession.h — estado VOLATIL da interface do editor.
//
// Nada daqui pertence ao arquivo .ludo nem ao runtime. A separacao evita que
// selecao, zoom, ferramenta ativa e clipboard se misturem com o modelo do jogo.
// Editor mantem aliases temporarios para a API antiga (ed.session.zoom, ed.session.tool, ...),
// permitindo uma migracao incremental dos widgets sem quebra em plugins/testes.
// ============================================================================
#pragma once

#include "Model.h"

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

namespace core {

/// Estado de câmera por mapa. É estritamente volátil: não pertence ao .ludo.
struct MapViewportState
{
    double  zoom = 1.0;
    QPointF center;
    bool    initialized = false;
};

enum class AuthoringContext
{
    Map,
    Layer,
    Tileset,
    Object,
    Pattern,
    Region
};

struct EditorSession
{
    Tool             tool = Tool::Stamp;
    TilesetSelection tsSel;
    CustomStamp      customStamp;
    int              activeTilesetIdx = -1;
    QString          activeAutotileId; ///< recurso Autotile selecionado na paleta; vazio = tile comum
    bool             showGrid = true;
    bool             multigrid = false;
    QColor           gridColor = QColor(18, 20, 24, 190); // escura por padrão
    bool             checkerboardBackground = true;
    QColor           checkerColorA = QColor(74, 78, 84);
    QColor           checkerColorB = QColor(46, 49, 54);
    bool             highlightCurrent = false;
    double           focusDim = 0.25;
    bool             shapeFilled = true;
    bool             ghostPreview = true;
    bool heldStack = false, heldSnap = false, heldErase = false;
    bool animateAutotiles = true;
    int objectInsertionIndex = -1; // -1: front; otherwise a persistent insertion position.
    bool             placeOnTop = false;
    bool             snapObjects = false;
    int              snapGridSize = 32;
    bool             randomMode = false;
    // Random Tile / Scatter. O peso de cada tile continua em Tileset; estas
    // opcoes controlam COMO o pool e distribuido no mapa.
    bool             randomScattering = false;
    int              randomScatterPercent = 55;       ///< chance por ponto, 0..100
    bool             randomGridFree = false;           ///< cria objetos livres em vez de celulas
    int              randomGridFreeJitter = 70;        ///< deslocamento aleatorio, % de um tile
    int              randomGridFreeSpacing = 20;       ///< distancia minima entre amostras, px
    bool             starMarkMode = false;
    bool             collisionMarkMode = false;
    bool             regionMarkMode = false;
    bool regionGradient = false;
    bool regionGradientMerge = false;
    bool regionGradientPingPong = false;
    bool regionGradientVertical = false;
    int regionGradientEnd = 5;
    int              activeRegionId = 1;   ///< RPG Maker MV/MZ: 0 = nenhuma, 1..255 = região
    /// Nó atualmente selecionado na árvore de camadas. Pode ser um Grupo;
    /// não substitui a camada ativa de pintura do MapDoc.
    QString          selectedLayerId;
    /// Seleção múltipla da árvore. selectedLayerId continua sendo o item
    /// principal mostrado no Inspector; os demais recebem edições em lote.
    QSet<QString>    selectedLayerIds;
    double           zoom = 1.0;
    BrushSettings    brush;
    RasterBrushSettings rasterBrush;
    /// Se aponta para uma Image Layer com máscara, Paint/Eraser editam a
    /// máscara em vez do conteúdo raster da camada.
    QString          selectedMaskLayerId;
    /// Clipboard interno de camada para Ctrl+C/Ctrl+V (especialmente útil
    /// para Slope, que é uma Image Layer normal após a rasterização).
    LayerPtr          clipboardLayer;

    // Workspace de mapas -------------------------------------------------
    // A árvore representa a estrutura do projeto; esta lista representa
    // somente os mapas atualmente abertos em abas. IDs estáveis evitam
    // acoplamento entre ordem visual das tabs e posição em Editor::docs.
    QVector<QString>                 openMapIds;
    QVector<QString>                 recentlyClosedMapIds;
    QHash<QString, MapViewportState> mapViewports;

    // Contexto do Inspector. Não duplica IDs de seleção: apenas registra qual
    // superfície de authoring o usuário está manipulando quando não há uma
    // seleção específica de objeto/evento.
    AuthoringContext authoringContext = AuthoringContext::Map;
    bool             inspectorPinned = false;

    // Wang / terreno
    int  activeWangSetIdx = -1;
    int  activeWangColorId = 0;
    bool wangBrushActive = false;
    bool wangEraseMode = false;
    bool terrainManual = false;

    // Objetos
    QString            selectedObjectId;
    QSet<QString>      selectedObjectIds;
    QVector<MapObject> clipboardObjects;

    void resetWorkspace()
    {
        openMapIds.clear();
        recentlyClosedMapIds.clear();
        mapViewports.clear();
    }

    void resetSelection()
    {
        selectedLayerId.clear();
        selectedLayerIds.clear();
        selectedMaskLayerId.clear();
        selectedObjectId.clear();
        selectedObjectIds.clear();
    }

    /// Mesmo reset que Editor::newProject fazia antes da separacao: preserva
    /// preferencias de visualizacao/ferramenta que historicamente atravessam
    /// a criacao de um projeto novo.
    void resetForProject()
    {
        tsSel = TilesetSelection();
        customStamp.clear();
        activeTilesetIdx = -1;
        activeAutotileId.clear();
        activeWangSetIdx = -1;
        authoringContext = AuthoringContext::Map;
        clipboardObjects.clear();
        clipboardLayer.clear();
        resetWorkspace();
        resetSelection();
    }

    /// Runtime nao carrega estado de UI. Como o objeto e somente value-types,
    /// voltar ao valor default e suficiente e nao toca no ProjectModel.
    void resetForRuntime()
    {
        *this = EditorSession();
    }
};

} // namespace core
