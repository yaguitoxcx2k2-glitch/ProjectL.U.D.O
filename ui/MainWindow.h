// ============================================================================
//  MainWindow.h — Janela principal.
//  Reproduz a estrutura da ferramenta web:
//    #menubar    -> QMenuBar
//    #toolbar    -> QToolBar de ferramentas
//    #mapToolbar -> QToolBar de opcoes do mapa (grade, foco, random, snap...)
//    #mapTabsBar -> QTabBar de mapas
//    #leftPanel  -> dock com a paleta de tilesets
//    #rightPanel -> dock com camadas + propriedades + minimapa
//    #statusbar  -> QStatusBar
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QGroupBox;
class QFileSystemWatcher;
class QMenu;
class QLabel;
class QScrollArea;
class QSpinBox;
class QTabBar;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace ui {

class CollaborationClient;
class TeamServerManager;
class LayerPanel;
class MapView;
class Minimap;
class RpgMakerProjectSync;
class PropertiesPanel;
class ProjectRecoveryManager;
class TilesetView;
class TilesetSourceWatcher;
class AutotilePaletteWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(core::Editor& ed, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbars();
    void buildDocks();
    void buildStatusBar();
    void wireSignals();
    void loadSettings();
    void saveSettings();
    void rebuildWorkspaceMenu();
    void applyWorkspaceProfile(const QString& id);
    void saveCurrentWorkspaceProfile();
    void removeWorkspaceProfile();

    // acoes de arquivo
    void newProject();
    void openProject();
    void openProjectManager();
    bool loadProjectPath(const QString& path);
    void refreshAfterProjectTransition(const QString& statusMessage);
    bool saveProject(bool saveAs);
    bool saveCurrentMap();
    bool maybeSave();
    void exportPng();

    // tilesets
    void newTileset();
    void openAutoTileConverter();
    void openTilesetManager();
    void openBrushSettings();
    void openPaintBrushSettings();
    void refreshPaintBrushLibrary();
    void syncPaintBrushToolbar();
    QString primaryPaintBrushFolder() const;
    QStringList paintBrushFolders() const;
    void rebuildPaintBrushWatcher();
    void openSlopeDialog(const QString& layerId, const QRect& localPixelRect);
    void openReflectionSettings();

    // camadas / mapas
    void addTileLayer(int tileSize);
    void addObjectLayer();
    void addImageLayer(bool referenceOnly = false);
    void addPaintLayer();
    void addGroupLayer();
    void newMapTab();
    /// Fecha somente a visualização no workspace. Nunca remove MapDoc.
    void closeMapTab(int index);
    /// Exclusão estrutural explícita, usada por Árvore/Menu de Mapa.
    void requestDeleteMap(const QString& mapId);
    void activateMap(QString mapId, bool ensureOpen = true);
    void rememberActiveViewport();
    void restoreViewport(const QString& mapId);
    void normalizeMapWorkspace();
    QString mapIdForTab(int index) const;
    int tabIndexForMapId(const QString& mapId) const;

    void setTool(core::Tool tool);
    void updateWindowTitle();
    void refreshRpgMakerUi();
    void updateToolStates();
    void refreshTilesetCombo();
    void refreshTilesetPageControls();
    void refreshAutotileCategoryCombo();
    /// Mantem o combo da barra e o valor de Preferencias em sincronia.
    void updateSnapUi();
    void refreshMapTabs();
    void refreshMapTree();
    void syncMapsFromTree();
    int  mapIndexById(const QString& id) const;

    core::Editor& ed;

    MapView*         m_view = nullptr;
    TilesetView*     m_tileset = nullptr;
    AutotilePaletteWidget* m_autotiles = nullptr;
    QWidget* m_autotileHeading = nullptr;
    QWidget* m_autotileCategories = nullptr;
    QScrollArea*     m_tilesetScroll = nullptr;
    LayerPanel*      m_layers = nullptr;
    PropertiesPanel* m_props = nullptr;
    Minimap*         m_minimap = nullptr;
    QTabBar*         m_mapTabs = nullptr;
    QDockWidget*      m_mapDock = nullptr;
    QDockWidget*      m_tilesetDock = nullptr;
    QDockWidget*      m_rightDock = nullptr;
    QMenu*            m_workspaceMenu = nullptr;
    QMenu*            m_rpgMakerMenu = nullptr;
    QTreeWidget*     m_mapTree = nullptr;
    QComboBox* m_tilesetCategoryCombo = nullptr;
    QComboBox*       m_tilesetCombo = nullptr;
    QToolButton*     m_tilesetPagePrev = nullptr;
    QSpinBox*        m_tilesetPageSpin = nullptr;
    QLabel*          m_tilesetPageCount = nullptr;
    QToolButton*     m_tilesetPageNext = nullptr;
    QComboBox*       m_autotileCategoryCombo = nullptr;
    QComboBox*       m_zoomCombo = nullptr;
    QComboBox*       m_snapCombo = nullptr;
    QDockWidget*     m_paintBrushDock = nullptr;
    QComboBox*       m_paintBrushAuthoringModeCombo = nullptr;
    QComboBox*       m_paintBrushCombo = nullptr;
    QComboBox*       m_paintBrushUseCombo = nullptr;
    QGroupBox*       m_paintBrushPixelBox = nullptr;
    QGroupBox*       m_paintBrushEdgeBox = nullptr;
    QSpinBox*        m_paintBrushPixelScaleSpin = nullptr;
    QComboBox*       m_paintBrushPixelShapeCombo = nullptr;
    QComboBox*       m_paintBrushPixelDitherCombo = nullptr;
    QCheckBox*       m_paintBrushPixelMirrorH = nullptr;
    QCheckBox*       m_paintBrushPixelMirrorV = nullptr;
    QCheckBox*       m_paintBrushPixelReplace = nullptr;
    QToolButton*     m_paintBrushReplaceColorButton = nullptr;
    QSpinBox*        m_paintBrushSizeSpin = nullptr;
    QSpinBox*        m_paintBrushOpacitySpin = nullptr;
    QSpinBox*        m_paintBrushFlowSpin = nullptr;
    QSpinBox*        m_paintBrushEdgeSpin = nullptr;
    QSpinBox*        m_paintBrushEdgeStrengthSpin = nullptr;
    QSpinBox*        m_paintBrushEdgeIrregularitySpin = nullptr;
    QToolButton*     m_paintBrushColorButton = nullptr;
    QFileSystemWatcher* m_paintBrushWatcher = nullptr;
    QAction* m_regionGradientAction = nullptr;
    QSpinBox*        m_regionSpin = nullptr;
    QAction*         m_snapComboAction = nullptr;
    QAction*         m_regionSpinAction = nullptr;

    QAction *m_actNewProject = nullptr, *m_actOpenProject = nullptr,
            *m_actProjectManager = nullptr, *m_actSaveAll = nullptr,
            *m_actSaveAs = nullptr, *m_actQuit = nullptr,
            *m_actTilesetManager = nullptr, *m_actZoomReset = nullptr,
            *m_actFitView = nullptr;

    QActionGroup* m_toolGroup = nullptr;
    QHash<core::Tool, QAction*> m_toolActions;
    QAction *m_actUndo = nullptr, *m_actRedo = nullptr;
    QAction *m_tileBrushSettingsAction = nullptr, *m_paintBrushSettingsAction = nullptr;
    QAction *m_paintBrushEdgeAction = nullptr, *m_paintBrushPreserveCenterAction = nullptr,
            *m_paintBrushOpenFolderAction = nullptr, *m_paintBrushRefreshAction = nullptr;
    QAction *m_rpgReflectionAction = nullptr, *m_rpgLinkAction = nullptr,
            *m_rpgSyncAction = nullptr, *m_rpgExportAction = nullptr,
            *m_rpgHelpAction = nullptr, *m_teamPublishRpgAction = nullptr;
    QAction *m_actGrid = nullptr, *m_actMultigrid = nullptr, *m_actFocus = nullptr,
            *m_actGhost = nullptr, *m_actOnTop = nullptr, *m_actRandom = nullptr,
            *m_actSnap = nullptr, *m_actFilled = nullptr, *m_actStar = nullptr,
            *m_actCollision = nullptr, *m_actRegion = nullptr;

    bool m_firstShow = true;

    CollaborationClient* m_collaboration = nullptr;
    TeamServerManager* m_teamServer = nullptr;
    ProjectRecoveryManager* m_recovery = nullptr;
    RpgMakerProjectSync* m_rpgMakerSync = nullptr;
    TilesetSourceWatcher* m_tilesetSourceWatcher = nullptr;

    QLabel *m_statusTool = nullptr, *m_statusLayer = nullptr, *m_statusPos = nullptr,
           *m_statusHint = nullptr, *m_statusZoom = nullptr;
};

} // namespace ui
