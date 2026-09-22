#include <QSizePolicy>
#include "CollaborationClient.h"
#include "TeamServerManager.h"
#include "MainWindow.h"
#include "ProjectRecoveryManager.h"
#include "WorkspaceProfiles.h"
#include "ProjectOpenWorkflow.h"
#include "ProjectManagerDialog.h"
#include "ProjectCreationWorkflow.h"
#include "RpgMakerExporter.h"
#include "RpgMakerProjectSync.h"
#include "ModuleManagerDialog.h"
#include "modules/EditorModuleRegistry.h"
#include "EditorDialogGeometry.h"
#include "PreferencesDialog.h"
#include "EditorUiPreferences.h"
#include "EditorSessionStore.h"
#include "EditorShortcutRegistry.h"
#include "MapAuthoringDialogs.h"
#include "maps/ResizeMapDialog.h"
#include "AssetBrowser.h"
#include "UniversalAssetPicker.h"
#include "Icons.h"
#include "LayerPanel.h"
#include "MapView.h"
#include "Minimap.h"
#include "PropertiesPanel.h"
#include "TilesetView.h"
#include "AutotilePaletteWidget.h"
#include "TilesetManagerDialog.h"
#include "TilesetSourceWatcher.h"
#include "AssetSourceWatcher.h"

#include "core/ProjectIO.h"
#include "core/ProjectBootstrap.h"
#include "core/ProjectReferenceIndex.h"
#include "core/AssetWorkflow.h"
#include "core/MapWorkflow.h"
#include "core/MapWorkspace.h"
#include "core/Version.h"
#include "core/Renderer.h"
#include "core/TilesetOps.h"
#include "core/TilesetCatalog.h"
#include "core/PaintOps.h"
#include "core/LayerTree.h"


#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QCloseEvent>
#include <QCheckBox>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QColorDialog>
#include <QListWidget>
#include <QListView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <QButtonGroup>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QFrame>
#include <QSettings>
#include <QSignalBlocker>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QDropEvent>
#include <QMenu>
#include <QStyle>
#include <QVBoxLayout>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

using namespace core;

namespace ui {

namespace {

/// QTreeWidget com um aviso simples depois do drop. Não precisa de Q_OBJECT:
/// a janela fornece uma callback e então transforma a árvore visual de volta
/// em parentId + ordem dos MapDoc.
class MapTreeWidget final : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> afterDrop;
    bool dragInProgress() const { return m_dragging; }

protected:
    void startDrag(Qt::DropActions actions) override
    {
        m_dragging = true;
        {
            const QSignalBlocker blocker(this);
            QTreeWidget::startDrag(actions);
        }
        m_dragging = false;
        if (m_dropAccepted) { m_dropAccepted = false; queueSync(); }
    }
    void dropEvent(QDropEvent* e) override
    {
        const QSignalBlocker block(this);
        QTreeWidget::dropEvent(e);
        if (!e->isAccepted()) return;
        if (m_dragging) { m_dropAccepted = true; return; }
        queueSync();
    }

private:
    void queueSync()
    {
        if (!afterDrop || m_syncQueued) return;
        // QTreeWidget ainda está finalizando a mutação do model dentro de
        // dropEvent. Reparentear itens / emitir docsChanged aqui podia destruir
        // QModelIndex/QTreeWidgetItem que o Qt ainda estava usando, sobretudo
        // ao soltar um mapa para fora do pai (raiz), fechando o editor.
        m_syncQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_syncQueued = false;
            if (afterDrop) afterDrop();
        });
    }

private:
    bool m_dragging = false;
    bool m_dropAccepted = false;
    bool m_syncQueued = false;
};

QStringList brushImageFiles(const QStringList& roots)
{
    const QStringList filters = { QStringLiteral("*.png"), QStringLiteral("*.webp"),
                                  QStringLiteral("*.bmp"), QStringLiteral("*.jpg"),
                                  QStringLiteral("*.jpeg") };
    QSet<QString> unique;
    QStringList files;
    for (const QString& root : roots) {
        if (root.isEmpty() || !QDir(root).exists()) continue;
        QDirIterator it(root, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString absolute = QFileInfo(it.next()).absoluteFilePath();
            const QString key = QDir::cleanPath(absolute).toLower();
            if (unique.contains(key)) continue;
            unique.insert(key);
            files.push_back(absolute);
        }
    }
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).completeBaseName().localeAwareCompare(QFileInfo(b).completeBaseName()) < 0;
    });
    return files;
}

QIcon brushThumbnail(const QString& path)
{
    QImage image(path);
    if (image.isNull()) return icons::get(QStringLiteral("paint-brush"));
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage canvas(40, 40, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    const QImage scaled = image.scaled(QSize(36, 36), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.drawImage(QPoint((40 - scaled.width()) / 2, (40 - scaled.height()) / 2), scaled);
    return QIcon(QPixmap::fromImage(canvas));
}

constexpr int MapIdRole = Qt::UserRole + 41;
constexpr int MapVariationRole = Qt::UserRole + 42;


} // namespace

MainWindow::MainWindow(Editor& editorRef, QWidget* parent)
    : QMainWindow(parent), ed(editorRef)
{
    setWindowTitle(tr("LUDO Map Editor"));
    resize(1500, 940);

    // O desktop é um editor de mapas para RPG Maker MV/MZ.

    // ---- centro: abas de mapa + canvas -----------------------------------
    auto* center = new QWidget(this);
    auto* cv = new QVBoxLayout(center);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    m_mapTabs = new QTabBar(center);
    m_mapTabs->setExpanding(false);
    m_mapTabs->setTabsClosable(true);
    m_mapTabs->setDocumentMode(true);
    m_mapTabs->setMovable(true);
    m_mapTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    cv->addWidget(m_mapTabs);

    m_view = new MapView(ed, center);
    cv->addWidget(m_view, 1);
    setCentralWidget(center);

    m_collaboration = new CollaborationClient(ed, this);
    m_teamServer = new TeamServerManager(m_collaboration, this);
    buildActions();
    buildMenus();
    buildToolbars();
    refreshRpgMakerUi();
    EditorShortcutRegistry::apply(menuBar());
    buildDocks();
    buildStatusBar();
    auto* teamStatus = new QLabel(tr("Equipe: desconectado"), this);
    teamStatus->setMaximumWidth(460);
    statusBar()->addPermanentWidget(teamStatus);
    connect(m_collaboration, &CollaborationClient::statusChanged, this, [this,teamStatus](const QString& text) {
        teamStatus->setText(teamStatus->fontMetrics().elidedText(text,Qt::ElideRight,450));
        teamStatus->setToolTip(text);
    });
    connect(m_collaboration, &CollaborationClient::projectReloaded, this, [this] {
        refreshAfterProjectTransition(tr("Projeto da equipe atualizado."));
    });
    wireSignals();
    refreshMapTabs();
    refreshMapTree();
    refreshTilesetCombo();
    refreshAutotileCategoryCombo();
    updateToolStates();
    updateWindowTitle();
    loadSettings();
    updateToolStates(); // restaura docking, mas o painel Pincel continua contextual
    updateSnapUi();

    // Recovery do editor vive em um componente próprio; MainWindow apenas o possui.
    m_recovery = new ProjectRecoveryManager(ed, this);

    // Fontes de tileset vinculadas são observadas em tempo real. Isso mantém
    // paleta + mapa usando os mesmos pixels assim que o arquivo é salvo em
    // um editor externo, sem recriar tileset nem remapear TileRef.
    new AssetSourceWatcher(ed, this);
    m_tilesetSourceWatcher = new TilesetSourceWatcher(ed, this);
    m_tilesetSourceWatcher->setStatusHandler([this](const QString& text) {
        statusBar()->showMessage(text, 6000);
        if (m_tileset) { m_tileset->updateGeometry(); m_tileset->resize(m_tileset->sizeHint()); m_tileset->update(); }
        if (m_view) m_view->update();
        if (m_minimap) m_minimap->update();
    });

    // Fase 9: MapInfos.json continua sendo uma árvore compartilhada, mas
    // alterações estruturais LOCAIS ficam pendentes até uma sincronização
    // explícita. Isso evita recargas inesperadas no RPG Maker durante a edição.
    m_rpgMakerSync = new RpgMakerProjectSync(ed, this, this);
    m_rpgMakerSync->setStatusHandler([this](const QString& text) {
        statusBar()->showMessage(text, 6000);
    });
    m_rpgMakerSync->setModelChangedHandler([this] {
        normalizeMapWorkspace();
        refreshMapTabs();
        refreshMapTree();
        updateWindowTitle();
    });
    m_rpgMakerSync->restoreLinkedProject();
}

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    if (m_firstShow) {
        m_firstShow = false;
        // Só agora os widgets têm tamanho real; antes disso "ajustar à janela"
        // calcularia o zoom com base numa área de 640x480 provisória.
        QMetaObject::invokeMethod(this, [this] {
            normalizeMapWorkspace();
            if (const MapDoc* d = ed.doc()) restoreViewport(d->id);
            if(ed.projectPath.isEmpty())openProjectManager();
        }, Qt::QueuedConnection);
    }
}

// Actions, menus e toolbars foram isolados em MainWindowActions.cpp.

QString MainWindow::primaryPaintBrushFolder() const
{
    const QString portable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Pinceis"));
    if (QDir().mkpath(portable) && QFileInfo(portable).isWritable()) return portable;

    const QString fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                 .filePath(QStringLiteral("Pinceis"));
    QDir().mkpath(fallback);
    return fallback;
}

QStringList MainWindow::paintBrushFolders() const
{
    QStringList folders;
    auto addUnique = [&folders](const QString& path, bool create = false) {
        if (path.trimmed().isEmpty()) return;
        if (create) QDir().mkpath(path);
        if (!QDir(path).exists()) return;
        const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        for (const QString& existing : folders)
            if (existing.compare(clean, Qt::CaseInsensitive) == 0) return;
        folders.push_back(clean);
    };

    const QString app = QCoreApplication::applicationDirPath();
    addUnique(primaryPaintBrushFolder(), true);
    addUnique(QDir(app).filePath(QStringLiteral("Brushes")));
    addUnique(QDir(app).filePath(QStringLiteral("Brushs")));

    const QString project = ed.projectRoot();
    if (!project.isEmpty()) {
        addUnique(QDir(project).filePath(QStringLiteral("Pinceis")));
        addUnique(QDir(project).filePath(QStringLiteral("Brushes")));
        addUnique(QDir(project).filePath(QStringLiteral("Brushs")));
    }
    return folders;
}

void MainWindow::rebuildPaintBrushWatcher()
{
    if (!m_paintBrushWatcher) return;
    const QStringList watched = m_paintBrushWatcher->directories() + m_paintBrushWatcher->files();
    if (!watched.isEmpty()) m_paintBrushWatcher->removePaths(watched);

    QStringList paths;
    const QStringList roots = paintBrushFolders();
    for (const QString& root : roots) {
        paths.push_back(root);
        QDirIterator dirs(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (dirs.hasNext()) paths.push_back(dirs.next());
    }
    paths += brushImageFiles(roots);
    paths.removeDuplicates();
    if (!paths.isEmpty()) m_paintBrushWatcher->addPaths(paths);
}

void MainWindow::refreshPaintBrushLibrary()
{
    if (!m_paintBrushCombo) return;
    // Se a imagem do pincel atual foi editada externamente, atualize-a sem
    // exigir que o usuário selecione o pincel novamente.
    auto& activeBrush = ed.session.rasterBrush;
    if (activeBrush.tipMode != QLatin1String("round") && !activeBrush.tipImagePath.isEmpty()) {
        const QImage reloaded = paint::loadRasterBrushTip(activeBrush.tipImagePath);
        if (!reloaded.isNull()) activeBrush.tipImage = reloaded;
    }
    const QStringList roots = paintBrushFolders();
    const QStringList files = brushImageFiles(roots);

    const QSignalBlocker blocker(m_paintBrushCombo);
    m_paintBrushCombo->clear();
    m_paintBrushCombo->addItem(icons::get(QStringLiteral("paint-brush")), tr("Redondo"), QString());
    m_paintBrushCombo->setItemData(0, tr("Pincel redondo criado pelo próprio editor. Use Suavidade da borda nas opções avançadas."), Qt::ToolTipRole);

    QHash<QString, int> basenameCounts;
    for (const QString& path : files) basenameCounts[QFileInfo(path).completeBaseName().toLower()] += 1;

    for (const QString& path : files) {
        const QFileInfo info(path);
        QString display = info.completeBaseName();
        for (const QString& root : roots) {
            const QString rootPrefix = QDir::cleanPath(root) + QDir::separator();
            if (!QDir::cleanPath(path).startsWith(rootPrefix, Qt::CaseInsensitive)) continue;
            QString rel = QDir(root).relativeFilePath(path);
            rel.chop(info.suffix().size() + 1);
            if (rel.contains(QLatin1Char('/')) || rel.contains(QLatin1Char('\\'))) display = rel;
            break;
        }
        if (basenameCounts.value(info.completeBaseName().toLower()) > 1 && display == info.completeBaseName()) {
            const bool projectBrush = !ed.projectRoot().isEmpty() &&
                QDir::cleanPath(path).startsWith(QDir::cleanPath(ed.projectRoot()) + QDir::separator(), Qt::CaseInsensitive);
            display = tr("%1 — %2").arg(display, projectBrush ? tr("Projeto") : tr("Editor"));
        }
        m_paintBrushCombo->addItem(brushThumbnail(path), display, path);
        const int row = m_paintBrushCombo->count() - 1;
        m_paintBrushCombo->setItemData(row, tr("Arquivo: %1").arg(QDir::toNativeSeparators(path)), Qt::ToolTipRole);
    }

    rebuildPaintBrushWatcher();
    syncPaintBrushToolbar();
    if (m_view) m_view->update();
}

void MainWindow::syncPaintBrushToolbar()
{
    if (!m_paintBrushCombo) return;
    const RasterBrushSettings& brush = ed.session.rasterBrush;

    const QSignalBlocker b0(m_paintBrushCombo);
    const QSignalBlocker b1(m_paintBrushUseCombo);
    const QSignalBlocker b2(m_paintBrushSizeSpin);
    const QSignalBlocker b3(m_paintBrushOpacitySpin);
    const QSignalBlocker b4(m_paintBrushFlowSpin);
    const QSignalBlocker b5(m_paintBrushEdgeSpin);
    const QSignalBlocker b6(m_paintBrushEdgeAction);
    const QSignalBlocker b7(m_paintBrushEdgeStrengthSpin);
    const QSignalBlocker b8(m_paintBrushEdgeIrregularitySpin);
    const QSignalBlocker b9(m_paintBrushPreserveCenterAction);

    int brushIndex = brush.tipMode == QLatin1String("round") ? 0 : m_paintBrushCombo->findData(brush.tipImagePath);
    if (brush.tipMode != QLatin1String("round") && brushIndex < 0 && !brush.tipImagePath.isEmpty()) {
        m_paintBrushCombo->addItem(brushThumbnail(brush.tipImagePath),
                                   tr("Atual: %1").arg(QFileInfo(brush.tipImagePath).completeBaseName()),
                                   brush.tipImagePath);
        brushIndex = m_paintBrushCombo->count() - 1;
        m_paintBrushCombo->setItemData(brushIndex,
            tr("Este pincel está fora das pastas Pinceis/Brushes/Brushs. Ele continua funcionando, mas não faz parte da biblioteca automática."),
            Qt::ToolTipRole);
    }
    m_paintBrushCombo->setCurrentIndex(qMax(0, brushIndex));
    m_paintBrushSizeSpin->setValue(brush.sizePx);
    m_paintBrushOpacitySpin->setValue(brush.opacity);
    m_paintBrushFlowSpin->setValue(brush.flow);
    m_paintBrushEdgeSpin->setValue(qBound(1, brush.edgeSoftnessPercent, 50));
    m_paintBrushEdgeStrengthSpin->setValue(qBound(0, brush.edgeSoftnessStrength, 100));
    m_paintBrushEdgeIrregularitySpin->setValue(qBound(0, brush.edgeIrregularityPercent, 100));
    m_paintBrushEdgeAction->setChecked(brush.softenImageEdges);
    m_paintBrushPreserveCenterAction->setChecked(brush.preserveEdgeCenter);

    const bool imageBrush = brush.tipMode != QLatin1String("round") && !brush.tipImage.isNull();
    const int modeIndex = m_paintBrushUseCombo->findData(brush.tipMode == QLatin1String("color")
                                                           ? QStringLiteral("color") : QStringLiteral("alpha"));
    m_paintBrushUseCombo->setCurrentIndex(qMax(0, modeIndex));
    m_paintBrushUseCombo->setEnabled(imageBrush);
    m_paintBrushColorButton->setEnabled(brush.tipMode != QLatin1String("color"));
    m_paintBrushEdgeAction->setEnabled(imageBrush);
    const bool edgeBlendEnabled = imageBrush && brush.softenImageEdges;
    m_paintBrushEdgeSpin->setEnabled(edgeBlendEnabled);
    m_paintBrushEdgeStrengthSpin->setEnabled(edgeBlendEnabled);
    m_paintBrushEdgeIrregularitySpin->setEnabled(edgeBlendEnabled);
    m_paintBrushPreserveCenterAction->setEnabled(edgeBlendEnabled);

    const QColor color = brush.color;
    m_paintBrushColorButton->setStyleSheet(QStringLiteral("QToolButton{background:%1;color:%2;padding:3px 8px;}")
        .arg(color.name(), color.lightness() > 135 ? QStringLiteral("#111") : QStringLiteral("#fff")));
    m_paintBrushColorButton->setText(color.name(QColor::HexRgb).toUpper());
}

// ------------------------------------------------------------------- docks
void MainWindow::buildDocks()
{
    // ---- esquerda, em cima: árvore de mapas ------------------------------
    m_mapDock = new QDockWidget(tr("Mapas"), this);
    m_mapDock->setObjectName(QStringLiteral("mapTreeDock"));
    m_mapDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* tree = new MapTreeWidget(m_mapDock);
    m_mapTree = tree;
    tree->setHeaderHidden(true);
    tree->setIndentation(18);
    tree->setAnimated(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setDropIndicatorShown(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->afterDrop = [this] { syncMapsFromTree(); };

    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        const QString id = item ? item->data(0, MapIdRole).toString() : QString();
        if (mapIndexById(id) >= 0) activateMap(id);
    });
    connect(tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        QTreeWidgetItem* item = m_mapTree ? m_mapTree->currentItem() : nullptr;
        const QString id = item ? item->data(0, MapIdRole).toString() : QString();
        QTimer::singleShot(0, this, [this, id] {
            auto* tree = static_cast<MapTreeWidget*>(m_mapTree);
            if (!tree || tree->dragInProgress()) return;
            const auto* current = tree->currentItem();
            if (current && current->data(0, MapIdRole).toString() == id && mapIndexById(id) >= 0) activateMap(id);
        });
    });
    connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_mapTree) return;
        QTreeWidgetItem* item = m_mapTree->itemAt(pos);
        const QString id = item ? item->data(0, MapIdRole).toString() : QString();
        const int idx = mapIndexById(id);
        const bool isVariation = idx >= 0 && !ed.docs[idx].variationBaseId.isEmpty();

        QMenu menu(m_mapTree);
        if (idx < 0 || !isVariation) {
            QAction* novo = menu.addAction(idx >= 0 ? tr("Novo mapa filho") : tr("Novo mapa"));
            connect(novo, &QAction::triggered, this, [this, id] {
                const int antes = ed.docs.size();
                newMapTab();
                if (ed.docs.size() > antes && ed.doc()) {
                    ed.doc()->parentId = id;
                    ed.markDirty();
                    emit ed.docsChanged();
                    if (!id.isEmpty() && m_mapTree) {
                        QTreeWidgetItemIterator it(m_mapTree);
                        while (*it) {
                            if ((*it)->data(0, MapIdRole).toString() == id) {
                                (*it)->setExpanded(true);
                                break;
                            }
                            ++it;
                        }
                    }
                }
            });
        }

        if (idx >= 0) {
            if (menu.actions().size() > 0) menu.addSeparator();

            QAction* variation = menu.addAction(isVariation
                ? tr("Duplicar como nova variação…")
                : tr("Criar variação deste mapa…"));
            connect(variation, &QAction::triggered, this, [this, id] {
                const int i = mapIndexById(id);
                if (i < 0) return;
                const MapDoc& source = ed.docs.at(i);
                const QString baseId = source.variationBaseId.isEmpty() ? source.id : source.variationBaseId;
                int count = 0;
                for (const MapDoc& map : std::as_const(ed.docs))
                    if (map.variationBaseId == baseId) ++count;
                const QString suggestion = source.variationBaseId.isEmpty()
                    ? tr("Variação %1").arg(count + 1)
                    : tr("%1 - Cópia").arg(source.variationName.isEmpty() ? tr("Variação") : source.variationName);
                bool ok = false;
                const QString label = QInputDialog::getText(this, tr("Nova variação do mapa"),
                    tr("Nome da variação:"), QLineEdit::Normal, suggestion, &ok).trimmed();
                if (!ok || label.isEmpty()) return;
                QString error;
                const QString variationId = core::mapworkflow::createVariation(ed, id, label, &error);
                if (variationId.isEmpty()) {
                    QMessageBox::warning(this, tr("Nova variação do mapa"), error);
                    return;
                }
                core::mapworkspace::openMap(ed, variationId);
                emit ed.docsChanged();
                emit ed.layersChanged();
                emit ed.mapChanged();
                statusBar()->showMessage(tr("Variação criada como mapa independente para o runtime, agrupada sob o cenário original no LUDO."), 5000);
            });

            QAction* ren = menu.addAction(isVariation ? tr("Renomear variação…") : tr("Renomear mapa…"));
            connect(ren, &QAction::triggered, this, [this, id] {
                const int i = mapIndexById(id);
                if (i < 0) return;
                MapDoc& current = ed.docs[i];
                const bool variation = !current.variationBaseId.isEmpty();
                bool ok = false;
                const QString initial = variation ? current.variationName : current.name;
                const QString nome = QInputDialog::getText(
                    this, variation ? tr("Renomear variação") : tr("Renomear mapa"),
                    variation ? tr("Nome da variação:") : tr("Nome:"), QLineEdit::Normal,
                    initial, &ok).trimmed();
                if (!ok || nome.isEmpty() || nome == initial) return;
                if (variation) {
                    const MapDoc* base = ed.mapById(current.variationBaseId);
                    const QString baseName = base ? base->name : tr("Mapa");
                    const QString desired = QStringLiteral("%1 — %2").arg(baseName, nome);
                    const QString scenarioBaseId = current.variationBaseId;
                    current.variationName = nome;
                    current.name = ed.uniqueMapName(desired);
                    for (MapDoc& map : ed.docs)
                        if (map.id == scenarioBaseId || map.variationBaseId == scenarioBaseId) map.dirty = true;
                    ed.markDirty();
                    emit ed.docsChanged();
                    emit ed.projectChanged();
                } else {
                    QString error;
                    if (!core::renameProjectSymbol(ed, core::ReferenceSymbolKind::Map,
                                                   id, nome, &error)) {
                        QMessageBox::warning(this, tr("Renomear mapa"), error);
                        return;
                    }
                    emit ed.docsChanged();
                }
            });

            if (!isVariation) {
                QAction* duplicar = menu.addAction(tr("Duplicar mapa"));
                connect(duplicar, &QAction::triggered, this, [this, id] {
                    QString error;
                    const QString copyId = core::mapworkflow::duplicateMap(ed, id, &error);
                    if (copyId.isEmpty()) {
                        QMessageBox::warning(this, tr("Duplicar mapa"), error);
                        return;
                    }
                    emit ed.docsChanged();
                    emit ed.layersChanged();
                    emit ed.mapChanged();
                    statusBar()->showMessage(tr("Mapa e subárvore duplicados com IDs internos independentes."), 4000);
                });

                const QString parentId = ed.docs[idx].parentId;
                QVector<QString> siblingIds;
                for (const MapDoc& doc : std::as_const(ed.docs))
                    if (doc.variationBaseId.isEmpty() && doc.parentId == parentId) siblingIds.push_back(doc.id);
                const int siblingIndex = siblingIds.indexOf(id);
                QAction* subir = menu.addAction(tr("Mover para cima"));
                QAction* descer = menu.addAction(tr("Mover para baixo"));
                subir->setEnabled(siblingIndex > 0);
                descer->setEnabled(siblingIndex >= 0 && siblingIndex + 1 < siblingIds.size());
                connect(subir, &QAction::triggered, this, [this, id] {
                    if (core::mapworkflow::moveSibling(ed, id, -1)) emit ed.docsChanged();
                });
                connect(descer, &QAction::triggered, this, [this, id] {
                    if (core::mapworkflow::moveSibling(ed, id, +1)) emit ed.docsChanged();
                });

                if (!ed.docs[idx].parentId.isEmpty()) {
                    QAction* raiz = menu.addAction(tr("Mover para a raiz"));
                    connect(raiz, &QAction::triggered, this, [this, id] {
                        QString error;
                        if (!core::mapworkflow::reparent(ed, id, QString(), &error)) {
                            QMessageBox::warning(this, tr("Mover mapa"), error);
                            return;
                        }
                        emit ed.docsChanged();
                    });
                }
            }

            menu.addSeparator();
            QAction* excluir = menu.addAction(isVariation ? tr("Excluir variação") : tr("Excluir mapa"));
            connect(excluir, &QAction::triggered, this, [this, id] { requestDeleteMap(id); });
        }
        menu.exec(m_mapTree->viewport()->mapToGlobal(pos));
    });

    m_mapDock->setWidget(tree);
    addDockWidget(Qt::LeftDockWidgetArea, m_mapDock);

    // ---- esquerda, embaixo: paleta de tilesets ---------------------------
    m_tilesetDock = new QDockWidget(tr("Tilesets"), this);
    m_tilesetDock->setObjectName(QStringLiteral("tilesetDock"));
    m_tilesetDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* leftBody = new QWidget(m_tilesetDock);
    auto* lv = new QVBoxLayout(leftBody);
    lv->setContentsMargins(4, 4, 4, 4);
    lv->setSpacing(4);

    auto* autotileLabel = new QLabel(tr("<b>Autotiles</b>"), leftBody);
    autotileLabel->setToolTip(tr("Escolha um Autotile pela faixa de miniaturas abaixo."));
    lv->addWidget(autotileLabel);
    m_autotileHeading = autotileLabel;

    auto* autotileCategoryRow = new QWidget(leftBody);
    auto* autotileCategoryLayout = new QHBoxLayout(autotileCategoryRow);
    autotileCategoryLayout->setContentsMargins(0, 0, 0, 0);
    autotileCategoryLayout->addWidget(new QLabel(tr("Categoria:"), autotileCategoryRow));
    m_autotileCategoryCombo = new QComboBox(autotileCategoryRow);
    m_autotileCategoryCombo->setObjectName(QStringLiteral("autotileCategoryFilter"));
    m_autotileCategoryCombo->setAccessibleName(tr("Filtrar Autotiles por categoria"));
    m_autotileCategoryCombo->setToolTip(tr("Filtrar autotiles por categoria."));
    autotileCategoryLayout->addWidget(m_autotileCategoryCombo, 1);
    lv->addWidget(autotileCategoryRow);
    m_autotileCategories = autotileCategoryRow;

    m_autotiles = new AutotilePaletteWidget(ed, leftBody);
    lv->addWidget(m_autotiles);

    // O seletor de Tileset pertence exclusivamente à paleta de tiles normais.
    // Por isso fica imediatamente acima dela, e nunca acima da biblioteca de Autotiles.
    m_tilesetCategoryCombo = new QComboBox(leftBody);
    m_tilesetCategoryCombo->setAccessibleName(tr("Filtrar tilesets por categoria"));
    m_tilesetCategoryCombo->setToolTip(tr("Categorias de tilesets — organize vários recursos pelo botão direito no Gerenciador."));
    lv->addWidget(m_tilesetCategoryCombo);
    connect(m_tilesetCategoryCombo, &QComboBox::activated, this, [this](int) {
        refreshTilesetCombo();
        const int idx = m_tilesetCombo->currentData().toInt();
        if (idx >= 0) {
            ed.session.activeTilesetIdx = idx;
            ed.session.tsSel = TilesetSelection{idx, 0, 0, 1, 1};
            ed.session.customStamp.clear();
            core::activateRegularTilesetPainting(ed);
        } else {
            ed.session.activeTilesetIdx = -1;
            ed.session.tsSel = TilesetSelection{};
            ed.session.customStamp.clear();
        }
        emit ed.selectionChanged();
    });
    auto* tsBar = new QWidget(leftBody);
    auto* tsBarLayout = new QHBoxLayout(tsBar);
    tsBarLayout->setContentsMargins(0, 0, 0, 0);
    tsBarLayout->addWidget(new QLabel(tr("Tileset:"), tsBar));
    m_tilesetCombo = new QComboBox(tsBar);
    m_tilesetCombo->setToolTip(tr("Tileset ativo — use o Gerenciador de Tilesets para criar, renomear ou excluir."));
    tsBarLayout->addWidget(m_tilesetCombo, 1);
    lv->addWidget(tsBar);

    auto* normalTilesLabel = new QLabel(tr("<b>Tiles normais</b>"), leftBody);
    lv->addWidget(normalTilesLabel);
    m_tilesetScroll = new QScrollArea(leftBody);
    m_tilesetScroll->setWidgetResizable(false);
    m_tilesetScroll->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_tilesetScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tileset = new TilesetView(ed, m_tilesetScroll);
    m_tileset->setHideAutotileTiles(true);
    m_tilesetScroll->setWidget(m_tileset);
    m_tileset->setAutoFitWidth(true);
    lv->addWidget(m_tilesetScroll, 1);

    // Um Tileset lógico pode possuir vários atlas físicos de até 4096×4096.
    // A paleta mostra um único nome e navega pelas páginas aqui, sem expor
    // "parte 1/2" como se fossem Tilesets diferentes.
    auto* pageRow = new QWidget(leftBody);
    auto* pageLayout = new QHBoxLayout(pageRow);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(new QLabel(tr("Pag:"), pageRow));
    m_tilesetPagePrev = new QToolButton(pageRow);
    m_tilesetPagePrev->setText(QStringLiteral("◀"));
    m_tilesetPagePrev->setToolTip(tr("Página anterior do mesmo Tileset."));
    m_tilesetPageSpin = new QSpinBox(pageRow);
    m_tilesetPageSpin->setRange(1, 1);
    m_tilesetPageSpin->setAlignment(Qt::AlignCenter);
    m_tilesetPageSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_tilesetPageSpin->setFixedWidth(48);
    m_tilesetPageCount = new QLabel(tr("/ 1"), pageRow);
    m_tilesetPageNext = new QToolButton(pageRow);
    m_tilesetPageNext->setText(QStringLiteral("▶"));
    m_tilesetPageNext->setToolTip(tr("Próxima página do mesmo Tileset."));
    pageLayout->addWidget(m_tilesetPagePrev);
    pageLayout->addWidget(m_tilesetPageSpin);
    pageLayout->addWidget(m_tilesetPageCount);
    pageLayout->addWidget(m_tilesetPageNext);
    pageLayout->addStretch(1);
    lv->addWidget(pageRow);

    auto activatePage = [this](int oneBased) {
        const QVector<int> pages = core::tilesetPageIndices(ed, ed.session.activeTilesetIdx);
        if (pages.isEmpty()) return;
        const int pos = qBound(0, oneBased - 1, pages.size() - 1);
        const int idx = pages.at(pos);
        if (idx == ed.session.activeTilesetIdx) return;
        ed.session.activeTilesetIdx = idx;
        ed.session.tsSel = TilesetSelection{idx, 0, 0, 1, 1};
        ed.session.customStamp.clear();
        core::activateRegularTilesetPainting(ed);
        emit ed.selectionChanged();
        refreshTilesetPageControls();
        m_tileset->refreshAutoFit();
        m_tileset->update();
    };
    connect(m_tilesetPagePrev, &QToolButton::clicked, this, [this, activatePage] {
        activatePage(qMax(1, m_tilesetPageSpin->value() - 1));
    });
    connect(m_tilesetPageNext, &QToolButton::clicked, this, [this, activatePage] {
        activatePage(qMin(m_tilesetPageSpin->maximum(), m_tilesetPageSpin->value() + 1));
    });
    connect(m_tilesetPageSpin, qOverload<int>(&QSpinBox::valueChanged), this, [activatePage](int value) {
        activatePage(value);
    });

    auto* zoomRow = new QWidget(leftBody);
    auto* zr = new QHBoxLayout(zoomRow);
    zr->setContentsMargins(0, 0, 0, 0);
    auto* autoFit = new QCheckBox(tr("Ajustar ao painel"), zoomRow);
    autoFit->setChecked(true);
    autoFit->setToolTip(tr("Mantém 8 tiles por linha e adapta automaticamente o tamanho visual à largura da janela de Tilesets."));
    auto* zoomOut = new QToolButton(zoomRow);
    zoomOut->setIcon(icons::get(QStringLiteral("zoomout")));
    zoomOut->setToolTip(tr("Diminuir zoom manual. Desative Ajustar ao painel para usar."));
    auto* zoomIn = new QToolButton(zoomRow);
    zoomIn->setIcon(icons::get(QStringLiteral("zoomin")));
    zoomIn->setToolTip(tr("Aumentar zoom manual. Desative Ajustar ao painel para usar."));
    zoomOut->setEnabled(false);
    zoomIn->setEnabled(false);
    connect(autoFit, &QCheckBox::toggled, this, [this, zoomOut, zoomIn](bool on) {
        m_tileset->setAutoFitWidth(on);
        zoomOut->setEnabled(!on);
        zoomIn->setEnabled(!on);
        if (on) m_tileset->refreshAutoFit();
    });
    connect(zoomOut, &QToolButton::clicked, this, [this] {
        m_tileset->setPaletteZoom(m_tileset->paletteZoom() / 1.25);
    });
    connect(zoomIn, &QToolButton::clicked, this, [this] {
        m_tileset->setPaletteZoom(m_tileset->paletteZoom() * 1.25);
    });
    zr->addWidget(autoFit);
    zr->addStretch(1);
    zr->addWidget(zoomOut);
    zr->addWidget(zoomIn);
    lv->addWidget(zoomRow);

    m_tilesetDock->setWidget(leftBody);
    addDockWidget(Qt::LeftDockWidgetArea, m_tilesetDock);
    splitDockWidget(m_mapDock, m_tilesetDock, Qt::Vertical);

    // ---- direita: camadas + propriedades + minimapa -----------------------
    m_rightDock = new QDockWidget(tr("Camadas e propriedades"), this);
    m_rightDock->setObjectName(QStringLiteral("rightDock"));
    auto* splitter = new QSplitter(Qt::Vertical, m_rightDock);

    m_layers = new LayerPanel(ed, splitter);
    splitter->addWidget(m_layers);
    m_props = new PropertiesPanel(ed, splitter);
    splitter->addWidget(m_props);
    m_minimap = new Minimap(ed, splitter);
    splitter->addWidget(m_minimap);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);

    m_rightDock->setWidget(splitter);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
    if (m_paintBrushDock) {
        splitDockWidget(m_paintBrushDock, m_rightDock, Qt::Horizontal);
        resizeDocks({ m_paintBrushDock, m_rightDock }, { 280, 340 }, Qt::Horizontal);
    }

    // ---- inferior: problemas navegáveis ---------------------------------
    resizeDocks({ m_tilesetDock, m_rightDock }, { 300, 340 }, Qt::Horizontal);
    resizeDocks({ m_mapDock, m_tilesetDock }, { 220, 650 }, Qt::Vertical);
}

void MainWindow::buildStatusBar()
{
    m_statusTool = new QLabel(this);
    m_statusLayer = new QLabel(this);
    m_statusPos = new QLabel(this);
    m_statusZoom = new QLabel(this);
    m_statusHint = new QLabel(this);
    m_statusHint->setStyleSheet(QStringLiteral("color:#8fd18f"));
    statusBar()->addWidget(m_statusTool);
    statusBar()->addWidget(m_statusLayer);
    statusBar()->addWidget(m_statusPos);
    statusBar()->addWidget(m_statusZoom);
    statusBar()->addPermanentWidget(m_statusHint);
}

// ----------------------------------------------------------------- signals
void MainWindow::wireSignals()
{
    connect(m_view, &MapView::cursorMoved, this, [this](const QPoint& px, const QPoint& cell) {
        m_statusPos->setText(tr("  px %1,%2  ·  célula %3,%4  ").arg(px.x()).arg(px.y()).arg(cell.x()).arg(cell.y()));
    });
    connect(m_view, &MapView::zoomChanged, this, [this](double z) {
        m_statusZoom->setText(tr("  zoom %1%  ").arg(int(z * 100)));
        if (m_zoomCombo) m_zoomCombo->setCurrentText(QStringLiteral("%1%").arg(int(z * 100)));
    });
    connect(m_view, &MapView::statusMessage, this, [this](const QString& s) {
        m_statusHint->setText(s);
        statusBar()->showMessage(s, 6000);
    });
    connect(m_view, &MapView::slopeSelectionRequested, this, &MainWindow::openSlopeDialog);
    connect(m_view, &MapView::viewportChanged, this, [this] {
        rememberActiveViewport();
        const double z = m_view->zoom();
        const QPointF c = m_view->viewCenterInMap();
        m_minimap->setViewport(QRectF(c.x() - m_view->width() / (2 * z), c.y() - m_view->height() / (2 * z),
                                      m_view->width() / z, m_view->height() / z));
    });
    connect(m_minimap, &Minimap::navigateRequested, m_view, &MapView::centerOn);
    // Eventos são editados no RPG Maker; o Map Editor não conecta mais o editor legado.

    connect(m_tileset, &TilesetView::statusMessage, this, [this](const QString& s) {
        statusBar()->showMessage(s, 4000);
    });
    connect(m_tileset, &TilesetView::regularTilePicked, this, [this](int, int, int) {
        core::activateRegularTilesetPainting(ed);
        setTool(Tool::Stamp);
        statusBar()->showMessage(tr("Tile comum selecionado — modo normal de pintura."), 2500);
    });
    connect(m_autotiles, &AutotilePaletteWidget::autotileActivated, this, [this](int tilesetIdx, const QString& autotileId) {
        if (ed.activeLayer() && ed.activeLayer()->type == LayerType::Object) return;
        if (!core::activateTilesetAutotileForPainting(ed, tilesetIdx, autotileId)) return;
        setTool(Tool::Terrain);
        for (QAction* action : m_toolGroup->actions()) action->setChecked(false);
        updateToolStates();
        const core::TilesetAutotile* autotile = core::tilesetAutotileById(ed, tilesetIdx, autotileId);
        statusBar()->showMessage(autotile && autotile->hasTerrain()
            ? tr("Autotile selecionado — modo Terreno automático.")
            : tr("Configure o terreno deste autotile no gerenciador de tilesets."), 5000);
        m_view->update();
    });
    connect(m_autotiles, &AutotilePaletteWidget::statusMessage, this, [this](const QString& s) {
        statusBar()->showMessage(s, 4000);
    });
    connect(m_autotileCategoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_autotiles || !m_autotileCategoryCombo) return;
        m_autotiles->setCategoryFilter(m_autotileCategoryCombo->currentData().toString());
    });
    connect(m_props, &PropertiesPanel::statusMessage, this, [this](const QString& s) {
        statusBar()->showMessage(s, 5000);
    });
    connect(m_layers, &LayerPanel::statusMessage, this, [this](const QString& s) {
        statusBar()->showMessage(s, 4000);
    });
    connect(m_layers, &LayerPanel::requestAddLayer, this, [this](int kind) {
        if (kind == 0) addTileLayer(0);
        else if (kind == -1) addObjectLayer();
        else if (kind == -2) addGroupLayer();
        else if (kind == -3) addImageLayer(false);
        else if (kind == -4) addImageLayer(true);
        else if (kind == -5) addPaintLayer();
        else if(kind==-6){
            const DocSnapshot before=ed.snapshotDoc();
            LayerPtr layer=makePaintLayer(QSize(qMax(1,ed.mapInfo().pixelWidth()),qMax(1,ed.mapInfo().pixelHeight())),tr("Reflexo"));
            layer->reflectionLayer=true;layer->imageReferenceOnly=true;layer->opacity=.42;
            const LayerPtr selected=ed.selectedLayer();const QString parentId=(selected&&selected->type==LayerType::Group)?selected->id:QString();
            ed.addLayer(layer,parentId);ed.pushDocHistory(before,tr("Nova Camada de Reflexo"));
            statusBar()->showMessage(tr("Camada de Reflexo criada. Pinte nela a área da poça ou superfície refletiva."),5000);
        }
    });

    connect(m_tilesetCombo, &QComboBox::activated, this, [this](int i) {
        const int tilesetIdx = m_tilesetCombo ? m_tilesetCombo->itemData(i).toInt() : -1;
        const Tileset* ts = ed.tilesetAt(tilesetIdx);
        if (tilesetIdx < 0 || !ts || !core::isVisibleTileset(*ts)) return;
        ed.session.authoringContext = AuthoringContext::Tileset;
        ed.session.activeTilesetIdx = tilesetIdx;
        ed.session.tsSel = TilesetSelection{ tilesetIdx, 0, 0, 1, 1 };
        ed.session.customStamp.clear();
        core::activateRegularTilesetPainting(ed);
        setTool(Tool::Stamp);
        emit ed.selectionChanged();
    });

    connect(m_mapTabs, &QTabBar::currentChanged, this, [this](int i) {
        const QString id = mapIdForTab(i);
        if (!id.isEmpty()) activateMap(id, false);
    });
    connect(m_mapTabs, &QTabBar::tabCloseRequested, this, [this](int i) { closeMapTab(i); });
    connect(m_mapTabs, &QTabBar::tabMoved, this, [this](int, int) {
        QVector<QString> ordered;
        ordered.reserve(m_mapTabs->count());
        for (int i = 0; i < m_mapTabs->count(); ++i) {
            const QString id = mapIdForTab(i);
            if (!id.isEmpty() && !ordered.contains(id)) ordered.push_back(id);
        }
        core::mapworkspace::setOpenOrder(ed, ordered);
    });
    connect(m_mapTabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int index = m_mapTabs->tabAt(pos);
        if (index < 0) return;
        const QString id = mapIdForTab(index);
        if (id.isEmpty()) return;

        QMenu menu(m_mapTabs);
        QAction* close = menu.addAction(tr("Fechar"));
        close->setEnabled(m_mapTabs->count() > 1);
        connect(close, &QAction::triggered, this, [this, index] { closeMapTab(index); });

        QAction* closeOthers = menu.addAction(tr("Fechar outras"));
        closeOthers->setEnabled(m_mapTabs->count() > 1);
        connect(closeOthers, &QAction::triggered, this, [this, id] {
            rememberActiveViewport();
            const QVector<QString> opened = ed.session.openMapIds;
            for (const QString& otherId : opened) {
                if (otherId != id) core::mapworkspace::closeMap(ed, otherId);
            }
            activateMap(id, false);
        });

        QAction* closeRight = menu.addAction(tr("Fechar abas à direita"));
        closeRight->setEnabled(index + 1 < m_mapTabs->count());
        connect(closeRight, &QAction::triggered, this, [this, index, id] {
            QVector<QString> toClose;
            for (int i = index + 1; i < m_mapTabs->count(); ++i) {
                const QString tabId = mapIdForTab(i);
                if (!tabId.isEmpty()) toClose.push_back(tabId);
            }
            for (const QString& tabId : toClose) core::mapworkspace::closeMap(ed, tabId);
            if (!ed.doc() || !ed.session.openMapIds.contains(ed.doc()->id)) activateMap(id, false);
            else refreshMapTabs();
        });

        menu.addSeparator();
        QAction* locate = menu.addAction(tr("Localizar na árvore"));
        connect(locate, &QAction::triggered, this, [this, id] {
            activateMap(id);
            if (m_mapTree && m_mapTree->currentItem()) m_mapTree->scrollToItem(m_mapTree->currentItem());
        });
        menu.exec(m_mapTabs->mapToGlobal(pos));
    });

    auto* nextMap = new QAction(tr("Próxima aba de mapa"), this);
    nextMap->setShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")));
    nextMap->setShortcutContext(Qt::ApplicationShortcut);
    nextMap->setProperty("paletteId", QStringLiteral("navigation.nextMapTab"));
    addAction(nextMap);
    connect(nextMap, &QAction::triggered, this, [this] {
        if (m_mapTabs->count() < 2) return;
        m_mapTabs->setCurrentIndex((m_mapTabs->currentIndex() + 1) % m_mapTabs->count());
    });

    auto* previousMap = new QAction(tr("Aba de mapa anterior"), this);
    previousMap->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    previousMap->setShortcutContext(Qt::ApplicationShortcut);
    previousMap->setProperty("paletteId", QStringLiteral("navigation.previousMapTab"));
    addAction(previousMap);
    connect(previousMap, &QAction::triggered, this, [this] {
        if (m_mapTabs->count() < 2) return;
        const int current = m_mapTabs->currentIndex();
        m_mapTabs->setCurrentIndex((current - 1 + m_mapTabs->count()) % m_mapTabs->count());
    });

    auto* reopenMap = new QAction(tr("Reabrir última aba de mapa"), this);
    reopenMap->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    reopenMap->setShortcutContext(Qt::ApplicationShortcut);
    reopenMap->setProperty("paletteId", QStringLiteral("navigation.reopenMapTab"));
    addAction(reopenMap);
    connect(reopenMap, &QAction::triggered, this, [this] {
        const QString id = core::mapworkspace::takeRecentlyClosed(ed);
        if (!id.isEmpty()) activateMap(id);
    });

    connect(&ed, &Editor::tilesetsChanged, this, [this] { refreshTilesetCombo(); refreshAutotileCategoryCombo(); });
    connect(&ed, &Editor::selectionChanged, this, [this] {
        if (m_regionSpin && m_regionSpin->value() != ed.session.activeRegionId) {
            QSignalBlocker blocker(m_regionSpin);
            m_regionSpin->setValue(qBound(0, ed.session.activeRegionId, 255));
        }
        updateToolStates();
        if (ed.session.authoringContext != AuthoringContext::Tileset) return;

        // Seleção reversa Mapa -> Paleta pode trocar o Tileset sem modificar
        // a biblioteca. Sincronizamos somente a seleção visual; não emitimos
        // tilesetsChanged para fingir uma alteração de dados.
        bool switchedTileset = false;
        if (m_tilesetCombo && ed.session.activeTilesetIdx >= 0) {
            const Tileset* activePage = ed.tilesetAt(ed.session.activeTilesetIdx);
            const QString activeGroup = activePage ? core::tilesetPageGroupKey(*activePage) : QString();
            int comboIdx = -1;
            for (int i = 0; i < m_tilesetCombo->count(); ++i) {
                const Tileset* representative = ed.tilesetAt(m_tilesetCombo->itemData(i).toInt());
                if (representative && core::tilesetPageGroupKey(*representative) == activeGroup) { comboIdx = i; break; }
            }
            if (comboIdx >= 0 && m_tilesetCombo->currentIndex() != comboIdx) {
                QSignalBlocker blocker(m_tilesetCombo);
                m_tilesetCombo->setCurrentIndex(comboIdx);
                switchedTileset = true;
            }
            refreshTilesetPageControls();
        }

        if (m_tileset && ed.session.activeAutotileId.isEmpty() && ed.session.tsSel.valid() &&
            ed.session.tsSel.tilesetIdx == ed.session.activeTilesetIdx) {
            if (switchedTileset) {
                m_tileset->updateGeometry();
                m_tileset->resize(m_tileset->sizeHint());
            }
            m_tileset->locateTile(ed.session.tsSel.tilesetIdx, ed.session.tsSel.x, ed.session.tsSel.y);
        }

        // Terrain contextual não possui botão próprio na toolbar; ao voltar
        // para tile comum, o botão Stamp acompanha novamente o estado real.
        if (ed.session.tool == Tool::Terrain) {
            for (QAction* action : m_toolGroup->actions()) action->setChecked(false);
        } else if (QAction* action = m_toolActions.value(ed.session.tool)) {
            action->setChecked(true);
        }
        updateToolStates();
        if (m_view) m_view->update();
    });
    connect(&ed, &Editor::docsChanged, this, [this] {
        normalizeMapWorkspace();
        refreshMapTabs();
        refreshMapTree();
        updateWindowTitle();
    });
    connect(&ed, &Editor::projectChanged, this, [this] {
        refreshMapTree();
        refreshRpgMakerUi();
        updateWindowTitle();
    });
    connect(&ed, &Editor::historyChanged, this, [this] {
        m_actUndo->setEnabled(ed.canUndo());
        m_actRedo->setEnabled(ed.canRedo());
    });
    connect(&ed, &Editor::layersChanged, this, [this] { updateToolStates(); });
    connect(&ed, &Editor::status, this, [this](const QString& s, int ms) {
        statusBar()->showMessage(s, ms);
    });
}

// ------------------------------------------------------------------ acoes
void MainWindow::setTool(Tool tool)
{
    // RC2.84: Autotile não é mais uma ferramenta separada. A seleção do
    // Autotile permanece ativa ao trocar entre pincel, borracha, balde e
    // formas; cada ferramenta delega a topologia ao Wang/Terrain.
    const bool autotileCapable = tool == Tool::Terrain || tool == Tool::Stamp ||
                                 tool == Tool::Eraser || tool == Tool::Fill ||
                                 tool == Tool::Rect || tool == Tool::Circle ||
                                 tool == Tool::Line;
    if (!autotileCapable && !ed.session.activeAutotileId.isEmpty()) {
        ed.session.activeAutotileId.clear();
        ed.session.wangBrushActive = false;
        emit ed.selectionChanged();
    }
    ed.session.tool = tool;
    if (QAction* a = m_toolActions.value(tool)) a->setChecked(true);
    ed.session.wangBrushActive = (tool == Tool::Terrain) || (autotileCapable && !ed.session.activeAutotileId.isEmpty());
    updateToolStates();
    if (tool == Tool::Slope)
        statusBar()->showMessage(tr("Inclinação: arraste uma área e solte para ajustar o efeito."), 5000);
    else
        statusBar()->showMessage(tr("Ferramenta: %1").arg(toolLabel(tool)), 2500);
    m_view->update();
}

void MainWindow::updateToolStates()
{
    const LayerPtr active = ed.activeLayer();
    const bool tiles = ed.session.regionMarkMode || (active && active->type == LayerType::Tile);
    const bool objects = !ed.session.regionMarkMode && active && active->type == LayerType::Object;
    const bool imageLayer = !ed.session.regionMarkMode && active && active->type == LayerType::Image;
    const bool tileLayer = !ed.session.regionMarkMode && active && active->type == LayerType::Tile;
    const bool maskEditingRaster = active && (imageLayer || tileLayer) &&
        ed.session.selectedMaskLayerId == active->id && !active->imageMask.isNull();
    const bool paintLayer = imageLayer && active->imagePaintLayer;
    const bool imageAlphaPaint = imageLayer && active->alphaLock;
    const bool rasterAuthoring = paintLayer || imageAlphaPaint || maskEditingRaster;
    const bool tileContent = tiles && !maskEditingRaster;
    for (auto it = m_toolActions.begin(); it != m_toolActions.end(); ++it) {
        const bool selectTool = it.key() == Tool::Select;
        const bool objectTool = it.key() == Tool::Object;
        const bool rasterTool = it.key() == Tool::Paint;
        const bool slopeTool = it.key() == Tool::Slope;
        const bool sharedEraser = it.key() == Tool::Eraser;
        bool available = false;
        if (maskEditingRaster) {
            // Contexto Máscara: não mostre ferramentas de tiles/objetos que
            // não podem atuar no alvo atual. Pincel e Borracha bastam.
            available = rasterTool || sharedEraser;
        } else {
            available = slopeTool ? (!ed.session.regionMarkMode && (tileContent || imageLayer))
                      : (rasterTool ? rasterAuthoring
                      : (sharedEraser ? (tileContent || rasterAuthoring)
                      : (selectTool ? (objects || imageLayer)
                      : (objectTool ? objects : tileContent))));
        }
        it.value()->setVisible(available);
        it.value()->setEnabled(available);
    }
    if (m_tileBrushSettingsAction) m_tileBrushSettingsAction->setVisible(tileContent && !ed.session.regionMarkMode);
    if (m_paintBrushSettingsAction) m_paintBrushSettingsAction->setVisible(false);
    if (m_autotiles) m_autotiles->setVisible(tileContent);
    if (m_autotileHeading) m_autotileHeading->setVisible(tileContent);
    if (m_autotileCategories) m_autotileCategories->setVisible(tileContent);
    if (objects) {
        bool blocked = !ed.session.activeAutotileId.isEmpty();
        const Stamp stamp = ed.currentStamp();
        for (const TileRef& tile : stamp.tiles)
            if (core::tilesetAutotileAt(ed,tile.tilesetIdx,tile.tx,tile.ty)) { blocked=true;break; }
        if (blocked) { ed.session.tsSel=TilesetSelection{}; ed.session.customStamp.clear(); }
        ed.session.activeAutotileId.clear(); ed.session.wangBrushActive=false;
    }
    if (rasterAuthoring) {
        ed.session.activeAutotileId.clear();
        ed.session.wangBrushActive = false;
        if (maskEditingRaster) {
            if (ed.session.tool != Tool::Paint && ed.session.tool != Tool::Eraser) ed.session.tool = Tool::Paint;
        } else if (ed.session.tool != Tool::Paint && ed.session.tool != Tool::Eraser &&
                   ed.session.tool != Tool::Slope && ed.session.tool != Tool::Select) {
            ed.session.tool = Tool::Paint;
        }
    } else if (imageLayer) {
        ed.session.activeAutotileId.clear();
        ed.session.wangBrushActive = false;
        // Image Layers, inclusive Slope gerado, entram no modo Seleção para
        // poder mover/copiar imediatamente. Slope continua disponível na toolbar.
        if (ed.session.tool != Tool::Slope && ed.session.tool != Tool::Select) ed.session.tool = Tool::Select;
    }
    if (objects && ed.session.tool != Tool::Select && ed.session.tool != Tool::Object) ed.session.tool = Tool::Object;
    if (tiles && !rasterAuthoring && (ed.session.tool == Tool::Select || ed.session.tool == Tool::Object || ed.session.tool == Tool::Paint))
        ed.session.tool = Tool::Stamp;
    if (QAction* action = m_toolActions.value(ed.session.tool)) action->setChecked(true);
    if (m_actFocus) {
        QSignalBlocker blocker(m_actFocus);
        m_actFocus->setChecked(ed.session.highlightCurrent);
    }
    if (m_actRegion) { QSignalBlocker b(m_actRegion); m_actRegion->setChecked(ed.session.regionMarkMode); }
    if (m_actRandom) { QSignalBlocker b(m_actRandom); m_actRandom->setChecked(ed.session.randomMode); }
    if (m_actStar) { QSignalBlocker b(m_actStar); m_actStar->setChecked(ed.session.starMarkMode); }
    if (m_actCollision) { QSignalBlocker b(m_actCollision); m_actCollision->setChecked(ed.session.collisionMarkMode); }
    if (m_regionSpinAction) m_regionSpinAction->setVisible(ed.session.regionMarkMode);
    if (m_regionGradientAction) m_regionGradientAction->setVisible(ed.session.regionMarkMode);
    if (m_paintBrushDock) {
        const LayerPtr brushSelected = ed.selectedLayer();
        const bool selectedPaintLayer = brushSelected && brushSelected->type == LayerType::Image &&
                                        brushSelected->imagePaintLayer;
        const bool selectedMask = brushSelected &&
            (brushSelected->type == LayerType::Image || brushSelected->type == LayerType::Tile) &&
            ed.session.selectedMaskLayerId == brushSelected->id && !brushSelected->imageMask.isNull();
        const bool showBrushPanel = selectedPaintLayer || selectedMask;
        m_paintBrushDock->setVisible(showBrushPanel);
        if (showBrushPanel) syncPaintBrushToolbar();
    }
    if (m_actOnTop) m_actOnTop->setVisible(tileContent && !ed.session.regionMarkMode);
    if (m_actRandom) m_actRandom->setVisible(tileContent && !ed.session.regionMarkMode);
    if (m_actStar) m_actStar->setVisible(tileContent && !ed.session.regionMarkMode);
    if (m_actCollision) m_actCollision->setVisible(tileContent && !ed.session.regionMarkMode);
    // Snap é compartilhado por objetos e Image Layers/Slope.
    if (m_actSnap) m_actSnap->setVisible(!maskEditingRaster && (objects || imageLayer));
    if (m_snapComboAction) m_snapComboAction->setVisible(!maskEditingRaster && (objects || imageLayer) && ed.session.snapObjects);
    if (ed.session.regionMarkMode)
        m_statusTool->setText(tr("  Regiões %1: %2  ").arg(core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper()).arg(ed.session.activeRegionId));
    else
        m_statusTool->setText(tr("  Ferramenta: %1  ").arg(toolLabel(ed.session.tool)));
    const LayerPtr l = ed.activeLayer();
    const LayerPtr selectedNode = ed.selectedLayer();
    if (selectedNode && selectedNode->type == LayerType::Group)
        m_statusLayer->setText(tr("  Pasta: %1%2  ").arg(selectedNode->name, selectedNode->locked ? tr(" (bloqueada)") : QString()));
    else
        m_statusLayer->setText(l ? tr("  Camada: %1%2  ").arg(l->name, l->locked ? tr(" (bloqueada)") : QString())
                                 : tr("  Camada: (nenhuma)  "));
    if (m_actFilled) m_actFilled->setVisible(ed.session.tool == Tool::Rect || ed.session.tool == Tool::Circle);
}

void MainWindow::updateSnapUi()
{
    if (!m_snapCombo) return;
    QSignalBlocker block(m_snapCombo);
    const QString text = tr("%1 px").arg(ed.session.snapGridSize);
    if (m_snapCombo->currentText() != text) m_snapCombo->setCurrentText(text);
    if (m_actSnap) m_actSnap->setChecked(ed.session.snapObjects);
    const LayerPtr active = ed.activeLayer();
    const bool snapTarget = active && (active->type == LayerType::Object || active->type == LayerType::Image);
    if (m_snapComboAction) m_snapComboAction->setVisible(snapTarget && ed.session.snapObjects);
}

void MainWindow::refreshAutotileCategoryCombo()
{
    if (!m_autotileCategoryCombo || !m_autotiles) return;
    const QString keep = m_autotileCategoryCombo->currentData().toString();
    const QSignalBlocker blocker(m_autotileCategoryCombo);
    m_autotileCategoryCombo->clear();
    m_autotileCategoryCombo->addItem(tr("Todas as categorias"), QString());
    if (m_autotiles->hasUncategorized())
        m_autotileCategoryCombo->addItem(tr("Sem categoria"), AutotilePaletteWidget::uncategorizedCategoryToken());
    for (const QString& category : m_autotiles->categories())
        m_autotileCategoryCombo->addItem(category, category);
    int index = m_autotileCategoryCombo->findData(keep);
    if (index < 0) index = 0;
    m_autotileCategoryCombo->setCurrentIndex(index);
    m_autotiles->setCategoryFilter(m_autotileCategoryCombo->currentData().toString());
}

void MainWindow::refreshTilesetCombo()
{
    if (!m_tilesetCombo) return;
    const Tileset* keepPage = ed.tilesetAt(ed.session.activeTilesetIdx);
    const QString keepGroup = keepPage ? core::tilesetPageGroupKey(*keepPage) : QString();

    m_tilesetCombo->blockSignals(true);
    m_tilesetCombo->clear();

    // A lista principal contém Tilesets LÓGICOS. As páginas físicas ficam
    // acessíveis pelo controle Pag: ◀ [n] ▶ logo abaixo da paleta.
    QVector<int> palettePages = core::paletteTilesetIndices(ed);
    QVector<int> groups;
    QSet<QString> seenGroups;
    for (int idx : palettePages) {
        const Tileset* ts = ed.tilesetAt(idx);
        if (!ts) continue;
        const QString key = core::tilesetPageGroupKey(*ts);
        if (seenGroups.contains(key)) continue;
        seenGroups.insert(key);
        const QVector<int> pages = core::tilesetPageIndices(ed, idx);
        int representative = idx;
        for (int pageIdx : pages) {
            const Tileset* page = ed.tilesetAt(pageIdx);
            if (page && page->paletteVisible && core::isVisibleTileset(*page)) { representative = pageIdx; break; }
        }
        groups.push_back(representative);
    }

    QString category;
    if (m_tilesetCategoryCombo) {
        const QSignalBlocker block(m_tilesetCategoryCombo);
        const QString keepCategory = m_tilesetCategoryCombo->currentData().toString();
        QStringList categories;
        for (int idx : groups) {
            const QString value = ed.tilesets[idx].category;
            if (!value.isEmpty() && !categories.contains(value)) categories << value;
        }
        categories.sort(Qt::CaseInsensitive);
        m_tilesetCategoryCombo->clear();
        m_tilesetCategoryCombo->addItem(tr("Todas as categorias de tilesets"), QString());
        m_tilesetCategoryCombo->addItem(tr("Sem categoria"), QStringLiteral("__uncategorized__"));
        for (const QString& value : categories) m_tilesetCategoryCombo->addItem(value, value);
        m_tilesetCategoryCombo->setCurrentIndex(qMax(0, m_tilesetCategoryCombo->findData(keepCategory)));
        category = m_tilesetCategoryCombo->currentData().toString();
    }
    if (!category.isEmpty()) {
        for (int i = groups.size() - 1; i >= 0; --i) {
            const QString value = ed.tilesets[groups[i]].category;
            if (category == QLatin1String("__uncategorized__") ? !value.isEmpty() : value != category) groups.removeAt(i);
        }
    }

    int selectedCombo = -1;
    for (int tilesetIdx : groups) {
        const Tileset* ts = ed.tilesetAt(tilesetIdx);
        if (!ts) continue;
        const QVector<int> pages = core::tilesetPageIndices(ed, tilesetIdx);
        const QString label = ts->category.isEmpty() ? ts->name : QStringLiteral("[%1] %2").arg(ts->category, ts->name);
        m_tilesetCombo->addItem(label, tilesetIdx);
        m_tilesetCombo->setItemData(m_tilesetCombo->count() - 1,
            tr("%1 página(s) · Tile %2×%3 px").arg(qMax(1, pages.size())).arg(ts->tilewidth).arg(ts->tileheight),
            Qt::ToolTipRole);
        if (core::tilesetPageGroupKey(*ts) == keepGroup) selectedCombo = m_tilesetCombo->count() - 1;
    }
    if (groups.isEmpty()) m_tilesetCombo->addItem(tr("(nenhum tileset)"), -1);

    if (selectedCombo < 0 && !groups.isEmpty()) {
        selectedCombo = 0;
        const int rep = m_tilesetCombo->itemData(0).toInt();
        if (!keepPage || !core::isVisibleTileset(*keepPage) || !keepPage->paletteVisible)
            ed.session.activeTilesetIdx = rep;
    }
    if (selectedCombo >= 0) m_tilesetCombo->setCurrentIndex(selectedCombo);
    m_tilesetCombo->blockSignals(false);
    refreshTilesetPageControls();
    m_tileset->refreshAutoFit();
}

void MainWindow::refreshTilesetPageControls()
{
    if (!m_tilesetPageSpin || !m_tilesetPagePrev || !m_tilesetPageNext || !m_tilesetPageCount) return;
    const QVector<int> pages = core::tilesetPageIndices(ed, ed.session.activeTilesetIdx);
    int pos = pages.indexOf(ed.session.activeTilesetIdx);
    if (pos < 0) pos = 0;
    const int count = qMax(1, pages.size());
    {
        QSignalBlocker blocker(m_tilesetPageSpin);
        m_tilesetPageSpin->setRange(1, count);
        m_tilesetPageSpin->setValue(qBound(1, pos + 1, count));
    }
    m_tilesetPageCount->setText(tr("/ %1").arg(count));
    const bool hasPages = !pages.isEmpty();
    m_tilesetPageSpin->setEnabled(hasPages && count > 1);
    m_tilesetPagePrev->setEnabled(hasPages && pos > 0);
    m_tilesetPageNext->setEnabled(hasPages && pos + 1 < count);
}

void MainWindow::normalizeMapWorkspace()
{
    core::mapworkspace::normalize(ed);
}

QString MainWindow::mapIdForTab(int index) const
{
    if (!m_mapTabs || index < 0 || index >= m_mapTabs->count()) return QString();
    return m_mapTabs->tabData(index).toString();
}

int MainWindow::tabIndexForMapId(const QString& mapId) const
{
    if (!m_mapTabs || mapId.isEmpty()) return -1;
    for (int i = 0; i < m_mapTabs->count(); ++i)
        if (m_mapTabs->tabData(i).toString() == mapId) return i;
    return -1;
}

void MainWindow::rememberActiveViewport()
{
    if (!m_view) return;
    const MapDoc* d = ed.doc();
    if (!d || d->id.isEmpty()) return;
    core::mapworkspace::recordViewport(ed, d->id, m_view->zoom(), m_view->viewCenterInMap());
}

void MainWindow::restoreViewport(const QString& mapId)
{
    if (!m_view || mapId.isEmpty()) return;
    core::MapViewportState state;
    if (!core::mapworkspace::viewport(ed, mapId, &state)) {
        m_view->fitToView();
        return;
    }
    m_view->setZoom(state.zoom);
    m_view->centerOn(state.center);
}

void MainWindow::activateMap(QString mapId, bool ensureOpen)
{
    const int docIndex = mapIndexById(mapId);
    if (docIndex < 0) return;

    const QString previousId = ed.doc() ? ed.doc()->id : QString();
    if (!previousId.isEmpty() && previousId != mapId) rememberActiveViewport();

    if (ensureOpen) core::mapworkspace::openMap(ed, mapId);

    if (docIndex != ed.activeDocIdx) ed.switchDoc(docIndex);
    refreshMapTabs();
    refreshMapTree();

    const int tabIndex = tabIndexForMapId(mapId);
    if (tabIndex >= 0 && m_mapTabs->currentIndex() != tabIndex) {
        QSignalBlocker block(m_mapTabs);
        m_mapTabs->setCurrentIndex(tabIndex);
    }

    if (previousId != mapId) restoreViewport(mapId);
}

void MainWindow::refreshMapTabs()
{
    if (!m_mapTabs) return;
    normalizeMapWorkspace();

    const QString activeId = ed.doc() ? ed.doc()->id : QString();
    QSignalBlocker block(m_mapTabs);
    while (m_mapTabs->count()) m_mapTabs->removeTab(0);
    for (const QString& id : std::as_const(ed.session.openMapIds)) {
        const int docIndex = mapIndexById(id);
        if (docIndex < 0) continue;
        const MapDoc& d = ed.docs.at(docIndex);
        const QString rpgMakerLabel = d.rpgMakerMapId > 0
            ? QStringLiteral("MAP%1 · %2").arg(d.rpgMakerMapId, 3, 10, QLatin1Char('0')).arg(d.name)
            : (m_rpgMakerSync && m_rpgMakerSync->isLinked()
                   ? QStringLiteral("● %1").arg(d.name) : d.name);
        const int tab = m_mapTabs->addTab(d.dirty ? rpgMakerLabel + QStringLiteral(" •") : rpgMakerLabel);
        m_mapTabs->setTabData(tab, d.id);
        m_mapTabs->setTabToolTip(tab, tr("Mapa aberto: %1").arg(d.name));
    }
    const int activeTab = tabIndexForMapId(activeId);
    if (activeTab >= 0) m_mapTabs->setCurrentIndex(activeTab);
    m_mapTabs->setTabsClosable(m_mapTabs->count() > 1);
}

int MainWindow::mapIndexById(const QString& id) const
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < ed.docs.size(); ++i)
        if (ed.docs[i].id == id) return i;
    return -1;
}

void MainWindow::refreshMapTree()
{
    if (!m_mapTree) return;

    QSet<QString> expandidos;
    QTreeWidgetItemIterator velho(m_mapTree);
    while (*velho) {
        const QString id = (*velho)->data(0, MapIdRole).toString();
        if (!id.isEmpty() && (*velho)->isExpanded()) expandidos.insert(id);
        ++velho;
    }

    QSignalBlocker bloqueio(m_mapTree);
    m_mapTree->clear();
    auto* raiz = new QTreeWidgetItem(m_mapTree);
    QString rootLabel = ed.projectName.isEmpty() ? tr("Projeto") : ed.projectName;
    const bool structurePending = m_rpgMakerSync && m_rpgMakerSync->hasPendingStructure();
    if (structurePending) rootLabel += QStringLiteral("  ●");
    raiz->setText(0, rootLabel);
    raiz->setToolTip(0, structurePending
        ? tr("Há alterações da árvore ainda não enviadas ao RPG Maker. Use Salvar/Atualizar RPG Maker quando estiver pronto.")
        : QString());
    raiz->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    Qt::ItemFlags raizFlags = raiz->flags();
    raizFlags.setFlag(Qt::ItemIsDropEnabled, true);
    raizFlags.setFlag(Qt::ItemIsDragEnabled, false);
    raiz->setFlags(raizFlags);
    raiz->setExpanded(true);

    QSet<QString> visitados;
    auto addVariation = [&](const MapDoc& variation, QTreeWidgetItem* baseItem) {
        if (!baseItem || visitados.contains(variation.id)) return;
        visitados.insert(variation.id);
        auto* item = new QTreeWidgetItem(baseItem);
        const QString label = variation.variationName.trimmed().isEmpty()
            ? tr("Variação") : variation.variationName.trimmed();
        const QString rpgMakerLabel = variation.rpgMakerMapId > 0
            ? QStringLiteral("↳ MAP%1 · %2").arg(variation.rpgMakerMapId, 3, 10, QLatin1Char('0')).arg(label)
            : (m_rpgMakerSync && m_rpgMakerSync->isLinked()
                   ? QStringLiteral("↳ ● %1").arg(label) : QStringLiteral("↳ %1").arg(label));
        item->setText(0, variation.dirty ? rpgMakerLabel + QStringLiteral(" •") : rpgMakerLabel);
        item->setData(0, MapIdRole, variation.id);
        item->setData(0, MapVariationRole, true);
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setToolTip(0, variation.rpgMakerMapId > 0
            ? tr("Variação “%1” · MAP%2 · mapa independente no RPG Maker · %3×%4 tiles")
                  .arg(label).arg(variation.rpgMakerMapId, 3, 10, QLatin1Char('0'))
                  .arg(variation.map.width).arg(variation.map.height)
            : tr("Variação “%1” · receberá um Map ID próprio ao sincronizar · %2×%3 tiles")
                  .arg(label).arg(variation.map.width).arg(variation.map.height));
        Qt::ItemFlags flags = item->flags();
        flags.setFlag(Qt::ItemIsDragEnabled, false);
        flags.setFlag(Qt::ItemIsDropEnabled, false);
        flags.setFlag(Qt::ItemIsSelectable, true);
        flags.setFlag(Qt::ItemIsEnabled, true);
        item->setFlags(flags);
    };

    std::function<void(int, QTreeWidgetItem*)> inserir = [&](int idx, QTreeWidgetItem* pai) {
        if (idx < 0 || idx >= ed.docs.size()) return;
        const MapDoc& d = ed.docs[idx];
        if (visitados.contains(d.id)) return;
        // Variações válidas são inseridas junto do mapa-base, não como ramo da
        // hierarquia comum. Isso evita confundir variação com mapa-filho.
        if (!d.variationBaseId.isEmpty() && mapIndexById(d.variationBaseId) >= 0) return;
        visitados.insert(d.id);

        auto* item = new QTreeWidgetItem(pai);
        const QString rpgMakerLabel = d.rpgMakerMapId > 0
            ? QStringLiteral("MAP%1 · %2").arg(d.rpgMakerMapId, 3, 10, QLatin1Char('0')).arg(d.name)
            : (m_rpgMakerSync && m_rpgMakerSync->isLinked()
                   ? QStringLiteral("● %1").arg(d.name) : d.name);
        item->setText(0, d.dirty ? rpgMakerLabel + QStringLiteral(" •") : rpgMakerLabel);
        item->setData(0, MapIdRole, d.id);
        item->setData(0, MapVariationRole, false);
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        int variationCount = 0;
        for (const MapDoc& candidate : std::as_const(ed.docs))
            if (candidate.variationBaseId == d.id) ++variationCount;
        QString tip = d.rpgMakerMapId > 0
            ? tr("MAP%1 — %2 — %3×%4 tiles").arg(d.rpgMakerMapId, 3, 10, QLatin1Char('0')).arg(d.name).arg(d.map.width).arg(d.map.height)
            : tr("%1 — %2×%3 tiles").arg(d.name).arg(d.map.width).arg(d.map.height);
        if (variationCount > 0) tip += tr(" · %1 variação(ões)").arg(variationCount);
        item->setToolTip(0, tip);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
                       Qt::ItemIsSelectable | Qt::ItemIsEnabled);

        // Estados alternativos ficam imediatamente sob o cenário principal.
        for (const MapDoc& candidate : std::as_const(ed.docs))
            if (candidate.variationBaseId == d.id) addVariation(candidate, item);

        // Depois vêm os mapas-filhos reais.
        for (int filho = 0; filho < ed.docs.size(); ++filho)
            if (ed.docs[filho].variationBaseId.isEmpty() && ed.docs[filho].parentId == d.id)
                inserir(filho, item);
        item->setExpanded(expandidos.contains(d.id) || variationCount > 0);
    };

    for (int i = 0; i < ed.docs.size(); ++i) {
        const MapDoc& d = ed.docs[i];
        if (!d.variationBaseId.isEmpty()) continue;
        const QString p = d.parentId;
        if (p.isEmpty() || p == d.id || mapIndexById(p) < 0) inserir(i, raiz);
    }
    // Arquivos antigos/malformados nunca escondem mapas. Relações de variação
    // órfãs aparecem como itens simples até normalizeHierarchy repará-las.
    for (int i = 0; i < ed.docs.size(); ++i) {
        if (visitados.contains(ed.docs[i].id)) continue;
        MapDoc& d = ed.docs[i];
        if (!d.variationBaseId.isEmpty() && mapIndexById(d.variationBaseId) < 0) {
            d.variationBaseId.clear();
            d.variationName.clear();
        }
        inserir(i, raiz);
    }

    raiz->setExpanded(true);
    if (ed.activeDocIdx >= 0 && ed.activeDocIdx < ed.docs.size()) {
        const QString ativo = ed.docs[ed.activeDocIdx].id;
        QTreeWidgetItemIterator it(m_mapTree);
        while (*it) {
            if ((*it)->data(0, MapIdRole).toString() == ativo) {
                m_mapTree->setCurrentItem(*it);
                if ((*it)->parent()) (*it)->parent()->setExpanded(true);
                break;
            }
            ++it;
        }
    }
}

void MainWindow::syncMapsFromTree()
{
    if (!m_mapTree || m_mapTree->topLevelItemCount() == 0) return;
    QSignalBlocker treeSignals(m_mapTree);
    QTreeWidgetItem* raiz = nullptr;
    for (int i = 0; i < m_mapTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* candidato = m_mapTree->topLevelItem(i);
        if (candidato && candidato->data(0, MapIdRole).toString().isEmpty()) {
            raiz = candidato;
            break;
        }
    }
    if (!raiz) return;

    for (int i = m_mapTree->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* solto = m_mapTree->topLevelItem(i);
        if (solto == raiz) continue;
        solto = m_mapTree->takeTopLevelItem(i);
        if (solto) raiz->addChild(solto);
    }

    QVector<core::mapworkflow::MapTreePlacement> placements;
    QSet<QString> vistos;
    QHash<QString, QString> baseParents;
    std::function<void(QTreeWidgetItem*, const QString&)> recolher =
        [&](QTreeWidgetItem* pai, const QString& parentId) {
            if (!pai) return;
            for (int n = 0; n < pai->childCount(); ++n) {
                QTreeWidgetItem* item = pai->child(n);
                const QString id = item->data(0, MapIdRole).toString();
                const int mapIndex = mapIndexById(id);
                if (mapIndex < 0 || vistos.contains(id)) continue;

                const bool variation = item->data(0, MapVariationRole).toBool() ||
                    !ed.docs[mapIndex].variationBaseId.isEmpty();
                if (variation) {
                    const QString baseId = ed.docs[mapIndex].variationBaseId;
                    const MapDoc* base = ed.mapById(baseId);
                    const QString effectiveParent = baseParents.contains(baseId)
                        ? baseParents.value(baseId)
                        : (base ? base->parentId : QString());
                    placements.push_back({id, effectiveParent});
                    vistos.insert(id);
                    // Variação não é um pai estrutural; filhos visuais são
                    // propositalmente ignorados (a UI também bloqueia drop).
                    continue;
                }

                placements.push_back({id, parentId});
                baseParents.insert(id, parentId);
                vistos.insert(id);
                recolher(item, id);
            }
        };
    recolher(raiz, QString());
    for (const MapDoc& doc : std::as_const(ed.docs)) {
        if (vistos.contains(doc.id)) continue;
        const QString parent = !doc.variationBaseId.isEmpty() && baseParents.contains(doc.variationBaseId)
            ? baseParents.value(doc.variationBaseId) : doc.parentId;
        placements.push_back({doc.id, parent});
    }

    QString error;
    if (!core::mapworkflow::applyTreeOrder(ed, placements, &error)) {
        refreshMapTree();
        statusBar()->showMessage(tr("Não foi possível reorganizar os mapas: %1").arg(error), 5000);
        return;
    }
    core::mapworkflow::normalizeHierarchy(ed);
    emit ed.docsChanged();
    emit ed.layersChanged();
    emit ed.mapChanged();
}

void MainWindow::refreshRpgMakerUi()
{
    const QString engineName = core::rpgMakerEngineName(ed.rpgMakerEngine);
    const QString shortName = core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper();
    if (m_rpgMakerMenu) m_rpgMakerMenu->setTitle(tr("RPG Maker &%1").arg(shortName));
    if (m_rpgLinkAction) m_rpgLinkAction->setText(tr("Vincular projeto %1…").arg(engineName));
    if (m_rpgSyncAction) m_rpgSyncAction->setText(tr("Reparar vínculo %1…").arg(shortName));
    if (m_rpgExportAction) m_rpgExportAction->setText(tr("Exportação manual avançada para %1…").arg(shortName));
    if (m_rpgReflectionAction)
        m_rpgReflectionAction->setVisible(ed.rpgMakerEngine == core::RpgMakerEngine::MZ);
    if (m_teamPublishRpgAction) {
        m_teamPublishRpgAction->setText(tr("Atualizar %1…").arg(engineName));
        m_teamPublishRpgAction->setToolTip(tr("Atualizar mapas e recursos no %1").arg(engineName));
    }
    if (m_rpgSyncAction)
        m_rpgSyncAction->setToolTip(tr("Sincronizar árvore de mapas com o %1 (Ctrl+Alt+S)").arg(engineName));
    if (m_actRegion) m_actRegion->setText(tr("Modo Regiões %1").arg(shortName));
}

void MainWindow::updateWindowTitle()
{
    const MapDoc* d = ed.doc();
    setWindowTitle(tr("%1%2 — %3 — LUDO Map Editor")
                       .arg(ed.projectDirty ? QStringLiteral("• ") : QString(),
                            ed.projectName,
                            d ? d->name : tr("sem mapa")));
}

// ------------------------------------------------------------- arquivo
void MainWindow::newProject()
{
    if (!maybeSave()) return;

    ProjectCreationWorkflowResult creation;
    if (!runProjectCreationWorkflow(ed, this, &creation)) return;

    m_collaboration->detachProject();
    refreshAfterProjectTransition(tr("Projeto criado em %1.").arg(creation.project.projectRoot));
    if (m_rpgMakerSync && !m_rpgMakerSync->isLinked()) m_rpgMakerSync->linkProjectInteractive();
}

void MainWindow::refreshAfterProjectTransition(const QString& statusMessage)
{
    // O Map Editor sempre volta ao modo de edição visual do cenário.
    EditorSessionStore::restore(ed);
    pixmapCache().invalidate();
    normalizeMapWorkspace();
    refreshMapTabs();
    refreshMapTree();
    refreshTilesetCombo();
    refreshPaintBrushLibrary();
    updateToolStates();
    updateWindowTitle();
    if (m_rpgMakerSync) m_rpgMakerSync->restoreLinkedProject();
    if (m_tilesetSourceWatcher) m_tilesetSourceWatcher->rescan();
    QMetaObject::invokeMethod(this, [this] {
        if (const MapDoc* d = ed.doc()) restoreViewport(d->id);
    }, Qt::QueuedConnection);
    if (!statusMessage.isEmpty()) statusBar()->showMessage(statusMessage, 5000);
}

bool MainWindow::loadProjectPath(const QString& path)
{
    if (path.trimmed().isEmpty()) return false; EditorSessionStore::save(ed);
    ProjectOpenResult openResult;
    QString error;
    if (!openProjectWorkflow(ed, path, this, &openResult, &error)) {
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Erro ao abrir"), error);
        return false;
    }
    m_collaboration->detachProject();
    ProjectManagerDialog::rememberProject(openResult.projectPath);
    refreshAfterProjectTransition(tr("Projeto “%1” carregado.").arg(ed.projectName));
    return true;
}

void MainWindow::openProject()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Abrir projeto LUDO Map Editor"), QString(),
                                                      tr("Projeto Ludo (*.ludo);;Projeto antigo (*.json);;Todos (*)"));
    if (path.isEmpty()) return;
    loadProjectPath(path);
}

void MainWindow::openProjectManager()
{
    if (!ed.projectPath.isEmpty() && !maybeSave()) return;
    ProjectManagerDialog manager(ed, this);
    if (manager.exec() != QDialog::Accepted) return;
    if(manager.teamRequested()){
        if(m_teamServer)m_teamServer->showTeamHub();
        return;
    }
    if(!manager.selectedRpgMakerRoot().isEmpty()){
        core::ProjectCreationResult opened;QString error;
        if(!core::openRpgMakerProject(ed,manager.selectedRpgMakerRoot(),&opened,&error)){
            QMessageBox::warning(this,tr("Abrir RPG Maker"),error);return;
        }
        m_collaboration->detachProject();ProjectManagerDialog::rememberProject(opened.projectPath);
        refreshAfterProjectTransition(tr("%1 aberto como projeto LUDO.").arg(ed.projectName));
        if(m_rpgMakerSync)m_rpgMakerSync->restoreLinkedProject();
        return;
    }
    const QString path = manager.selectedProjectPath();
    if (path.isEmpty()) return;
    if (manager.selectedProjectAlreadyLoaded()) {
        m_collaboration->detachProject();
        ProjectManagerDialog::rememberProject(path);
        refreshAfterProjectTransition(tr("Projeto “%1” pronto para edição.").arg(ed.projectName));
        return;
    }
    loadProjectPath(path);
}

bool MainWindow::saveProject(bool saveAs)
{
    QString path = ed.projectPath;
    if(QFileInfo(path).suffix().compare(QStringLiteral("json"),Qt::CaseInsensitive)==0)saveAs=true;
    if (saveAs || path.isEmpty()) {
        QString safe = ed.projectName;
        safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        path = QFileDialog::getSaveFileName(this, tr("Salvar projeto Ludo"),
                                            safe + QStringLiteral(".ludo"),
                                            tr("Projeto Ludo (*.ludo)"));
        if (path.isEmpty()) return false;
        if(QFileInfo(path).suffix().isEmpty())path+=QStringLiteral(".ludo");
    }
    QString err;
    if (!io::saveProject(ed, path, &err)) {
        QMessageBox::warning(this, tr("Erro ao salvar"), err);
        return false;
    }
    ProjectManagerDialog::rememberProject(path, false);
    // Um save confirmado torna a recuperação daquele mesmo projeto obsoleta.
    // Recovery continua sendo rede de segurança, não uma segunda versão paralela.
    ProjectRecoveryManager::discardRecovery(path, nullptr);
    core::AssetWorkflow::ensureProjectFolders(ed.projectRoot(), nullptr);
    refreshPaintBrushLibrary();
    refreshMapTabs();
    statusBar()->showMessage(tr("Projeto salvo em %1 · mapas: %2.").arg(path).arg(ed.docs.size()), 6000);
    return true;
}

bool MainWindow::saveCurrentMap()
{
    // Um único pipeline: contêiner local -> equipe -> destino RPG Maker.
    if(!saveProject(false))return false;
    if(m_collaboration&&m_collaboration->attached()&&!m_collaboration->synchronize())return false;
    if(m_rpgMakerSync&&m_rpgMakerSync->isLinked()&&!m_rpgMakerSync->saveAllDirtyMaps())return false;
    statusBar()->showMessage(tr("Projeto salvo e destinos atualizados."),5000);return true;
}

bool MainWindow::maybeSave()
{
    if (!ed.projectDirty) return true;
    QMessageBox box(QMessageBox::Question, tr("Mapas não salvos"),
                    tr("Salvar e sincronizar os mapas alterados de “%1” antes de continuar?").arg(ed.projectName),
                    QMessageBox::NoButton, this);
    QPushButton* save = box.addButton(tr("Salvar"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(tr("Descartar"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == save) {
        return saveCurrentMap();
    }
    return box.clickedButton() == discard;
}

void MainWindow::exportPng()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar PNG"),
                                                      QStringLiteral("map.png"), tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    QString err;
    if (!io::exportPNG(ed, path, &err)) QMessageBox::warning(this, tr("Erro"), err);
    else statusBar()->showMessage(tr("PNG exportado para %1.").arg(path), 5000);
}

void MainWindow::newTileset()
{
    int selected = -1;
    if (!TilesetManagerDialog::runCreateTilesetFlow(ed, this, &selected)) return;
    if (selected >= 0) ed.session.activeTilesetIdx = selected;
    pixmapCache().invalidate();
    refreshTilesetCombo();
    emit ed.mapChanged();
}


void MainWindow::openTilesetManager()
{
    TilesetManagerDialog dlg(ed, this);
    dlg.exec();
    pixmapCache().invalidate();
    refreshTilesetCombo();
    emit ed.mapChanged();
}


void MainWindow::openAutoTileConverter()
{
    QString createdId;
    if (!TilesetManagerDialog::runImportAutotileFlow(ed, this, &createdId)) return;
    pixmapCache().invalidate();
    refreshTilesetCombo();
    statusBar()->showMessage(tr("Autotile importado para a biblioteca do projeto."), 5000);
}

// ------------------------------------------------------- camadas / mapas
void MainWindow::addTileLayer(int tileSize)
{
    const MapInfo& info = ed.mapInfo();
    const int tw = tileSize > 0 ? tileSize : info.tileWidth;
    const int th = tileSize > 0 ? tileSize : info.tileHeight;
    const int cols = qMax(1, (info.pixelWidth() + tw - 1) / tw);
    const int rows = qMax(1, (info.pixelHeight() + th - 1) / th);
    const DocSnapshot before = ed.snapshotDoc();
    const LayerPtr selected = ed.selectedLayer();
    const QString parentId = (selected && selected->type == LayerType::Group) ? selected->id : QString();
    int nextLayerNumber = 1; const QRegularExpression generatedName(QStringLiteral("^Camada\\s+(\\d+)$"));
    for (const LayerPtr& layer : ed.flatLayers()) { if (!layer) continue; const auto match=generatedName.match(layer->name.trimmed());
        if (match.hasMatch()) nextLayerNumber=qMax(nextLayerNumber,match.captured(1).toInt()+1); }
    const QString layerName=QStringLiteral("Camada %1").arg(nextLayerNumber,3,10,QLatin1Char('0'));
    ed.addLayer(makeTileLayer(layerName, tw, th, cols, rows), parentId);
    ed.pushDocHistory(before, tr("Nova camada de tiles"));
}


void MainWindow::openBrushSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Formato do pincel de tiles"));
    dialog.resize(620, 520);

    auto* root = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        tr("Escolha o formato e o tamanho da área que será pintada de uma vez. Você também pode usar uma imagem para criar um formato irregular, útil para vegetação, sujeira e detalhes menos repetitivos."), &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* body = new QHBoxLayout;
    root->addLayout(body, 1);

    auto* formBox = new QGroupBox(tr("Pincel"), &dialog);
    auto* form = new QFormLayout(formBox);
    body->addWidget(formBox, 1);

    auto* preset = new QComboBox(formBox);
    preset->addItem(tr("Personalizado"), QStringLiteral("custom"));
    preset->addItem(tr("Pixel 1×1"), QStringLiteral("pixel"));
    preset->addItem(tr("Quadrado 3×3"), QStringLiteral("square3"));
    preset->addItem(tr("Círculo 5×5"), QStringLiteral("circle5"));
    preset->addItem(tr("Losango 5×5"), QStringLiteral("diamond5"));
    preset->addItem(tr("Formato de imagem atual"), QStringLiteral("alpha"));

    auto* shape = new QComboBox(formBox);
    shape->addItem(tr("Quadrado"), QStringLiteral("square"));
    shape->addItem(tr("Círculo"), QStringLiteral("circle"));
    shape->addItem(tr("Losango"), QStringLiteral("diamond"));
    shape->addItem(tr("Formato criado por imagem"), QStringLiteral("alpha"));
    shape->setCurrentIndex(qMax(0, shape->findData(ed.session.brush.shape)));

    auto* diameter = new QSpinBox(formBox);
    diameter->setRange(1, 63);
    diameter->setSingleStep(2);
    diameter->setSuffix(tr(" tiles"));
    diameter->setValue(qMax(1, ed.session.brush.size * 2 - 1));
    diameter->setToolTip(tr("O pincel sempre usa um centro. Valores pares são ajustados para o próximo ímpar."));

    auto* density = new QSpinBox(formBox);
    density->setRange(0, 100);
    density->setSuffix(QStringLiteral("%"));
    density->setValue(ed.session.brush.density);
    density->setToolTip(tr("Controla quanto do formato do pincel é usado. Em formatos criados por imagem, valores menores deixam as bordas e detalhes mais leves."));

    auto* spacing = new QSpinBox(formBox);
    spacing->setRange(1, 64);
    spacing->setValue(qMax(1, ed.session.brush.spacing));
    spacing->setSuffix(tr(" passo(s)"));

    auto* rotation = new QSpinBox(formBox);
    rotation->setRange(-180, 180);
    rotation->setSuffix(QStringLiteral("°"));
    rotation->setValue(ed.session.brush.alphaMaskRotation);

    auto* invert = new QCheckBox(tr("Inverter claro e escuro"), formBox);
    invert->setChecked(ed.session.brush.alphaMaskInvert);

    form->addRow(tr("Modelo pronto:"), preset);
    form->addRow(tr("Formato:"), shape);
    form->addRow(tr("Diâmetro:"), diameter);
    form->addRow(tr("Cobertura:"), density);
    form->addRow(tr("Distância entre marcas:"), spacing);
    spacing->setToolTip(tr("Controla a distância entre cada marca do pincel durante o movimento. Valores menores criam um traço mais contínuo."));
    form->addRow(tr("Rotação do formato:"), rotation);
    form->addRow(QString(), invert);

    auto* maskBox = new QGroupBox(tr("Imagens usadas como formato do pincel"), formBox);
    auto* maskLayout = new QVBoxLayout(maskBox);
    auto* maskCombo = new QComboBox(maskBox);
    maskCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    maskCombo->setMinimumContentsLength(22);
    maskLayout->addWidget(maskCombo);
    auto* maskButtons = new QHBoxLayout;
    auto* addMask = new QPushButton(tr("Adicionar imagem…"), maskBox);
    auto* removeMask = new QPushButton(tr("Remover da lista"), maskBox);
    maskButtons->addWidget(addMask);
    maskButtons->addWidget(removeMask);
    maskLayout->addLayout(maskButtons);
    form->addRow(maskBox);

    QSettings settings;
    QStringList library = settings.value(QStringLiteral("editor/brushMaskLibrary")).toStringList();
    library.removeDuplicates();
    if (!ed.session.brush.alphaMaskPath.isEmpty() && !library.contains(ed.session.brush.alphaMaskPath))
        library.prepend(ed.session.brush.alphaMaskPath);

    auto refillMasks = [&] {
        const QString previous = maskCombo->currentData().toString();
        maskCombo->clear();
        maskCombo->addItem(tr("Nenhuma imagem"), QString());
        for (const QString& path : library) {
            QFileInfo info(path);
            maskCombo->addItem(info.fileName().isEmpty() ? path : info.fileName(), path);
            const int row = maskCombo->count() - 1;
            maskCombo->setItemData(row, path, Qt::ToolTipRole);
        }
        QString wanted = previous;
        if (wanted.isEmpty()) wanted = ed.session.brush.alphaMaskPath;
        const int idx = maskCombo->findData(wanted);
        maskCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    refillMasks();

    auto* previewBox = new QGroupBox(tr("Prévia"), &dialog);
    auto* previewLayout = new QVBoxLayout(previewBox);
    auto* preview = new QLabel(previewBox);
    preview->setFixedSize(220, 220);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameShape(QFrame::StyledPanel);
    previewLayout->addWidget(preview, 0, Qt::AlignCenter);
    auto* previewInfo = new QLabel(previewBox);
    previewInfo->setAlignment(Qt::AlignCenter);
    previewInfo->setWordWrap(true);
    previewLayout->addWidget(previewInfo);
    body->addWidget(previewBox);

    auto normalizedDiameter = [&]() {
        int d = qBound(1, diameter->value(), 63);
        if ((d % 2) == 0) ++d;
        return qMin(d, 63);
    };

    auto buildBrush = [&]() {
        BrushSettings b = ed.session.brush;
        const int d = normalizedDiameter();
        b.size = (d + 1) / 2;
        b.shape = shape->currentData().toString();
        b.density = density->value();
        b.spacing = spacing->value();
        b.alphaMaskRotation = rotation->value();
        b.alphaMaskInvert = invert->isChecked();
        b.alphaMaskPath = maskCombo->currentData().toString();
        b.alphaMask = paint::loadBrushAlphaMask(b.alphaMaskPath);
        return b;
    };

    auto refreshPreview = [&] {
        const BrushSettings b = buildBrush();
        const int d = normalizedDiameter();
        QImage img(200, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(31, 34, 39));
        QPainter painter(&img);
        const int cell = qMax(4, 180 / qMax(1, d));
        const int total = cell * d;
        const int ox = (img.width() - total) / 2;
        const int oy = (img.height() - total) / 2;
        painter.setPen(QPen(QColor(255,255,255,28), 1));
        for (int i = 0; i <= d; ++i) {
            painter.drawLine(ox + i * cell, oy, ox + i * cell, oy + total);
            painter.drawLine(ox, oy + i * cell, ox + total, oy + i * cell);
        }
        const QVector<paint::BrushSample> samples = paint::brushSamples(b);
        painter.setPen(Qt::NoPen);
        for (const paint::BrushSample& sample : samples) {
            const int gx = sample.point.x() + b.size - 1;
            const int gy = sample.point.y() + b.size - 1;
            QColor c(107, 187, 255);
            const qreal strength = qBound<qreal>(0.0, sample.strength * (b.density / 100.0), 1.0);
            c.setAlpha(qBound(18, int(std::lround(235 * strength)), 235));
            painter.fillRect(QRect(ox + gx * cell + 1, oy + gy * cell + 1,
                                   qMax(1, cell - 1), qMax(1, cell - 1)), c);
        }
        painter.end();
        preview->setPixmap(QPixmap::fromImage(img));
        previewInfo->setText(b.shape == QLatin1String("alpha")
            ? (b.alphaMask.isNull()
               ? tr("Escolha uma imagem para definir o formato do pincel.")
               : tr("%1 × %2 tiles · %3 células ativas")
                    .arg(d).arg(d).arg(samples.size()))
            : tr("%1 × %2 tiles · %3 células")
                  .arg(d).arg(d).arg(samples.size()));

        const bool alpha = shape->currentData().toString() == QLatin1String("alpha");
        maskBox->setEnabled(alpha);
        rotation->setEnabled(alpha);
        invert->setEnabled(alpha);
    };

    auto applyPreset = [&](const QString& id) {
        if (id == QLatin1String("custom")) return;
        if (id == QLatin1String("pixel")) {
            shape->setCurrentIndex(shape->findData(QStringLiteral("square")));
            diameter->setValue(1); density->setValue(100); spacing->setValue(1);
        } else if (id == QLatin1String("square3")) {
            shape->setCurrentIndex(shape->findData(QStringLiteral("square")));
            diameter->setValue(3); density->setValue(100); spacing->setValue(1);
        } else if (id == QLatin1String("circle5")) {
            shape->setCurrentIndex(shape->findData(QStringLiteral("circle")));
            diameter->setValue(5); density->setValue(100); spacing->setValue(1);
        } else if (id == QLatin1String("diamond5")) {
            shape->setCurrentIndex(shape->findData(QStringLiteral("diamond")));
            diameter->setValue(5); density->setValue(100); spacing->setValue(1);
        } else if (id == QLatin1String("alpha")) {
            shape->setCurrentIndex(shape->findData(QStringLiteral("alpha")));
            if (diameter->value() < 3) diameter->setValue(5);
        }
        refreshPreview();
    };

    connect(preset, &QComboBox::currentIndexChanged, &dialog,
            [&](int) { applyPreset(preset->currentData().toString()); });
    connect(shape, &QComboBox::currentIndexChanged, &dialog, [&](int) {
        preset->setCurrentIndex(0);
        refreshPreview();
    });
    connect(diameter, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int value) {
        if ((value % 2) == 0) {
            QSignalBlocker blocker(diameter);
            diameter->setValue(qMin(63, value + 1));
        }
        preset->setCurrentIndex(0);
        refreshPreview();
    });
    connect(density, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) { preset->setCurrentIndex(0); refreshPreview(); });
    connect(spacing, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) { preset->setCurrentIndex(0); refreshPreview(); });
    connect(rotation, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int) { preset->setCurrentIndex(0); refreshPreview(); });
    connect(invert, &QCheckBox::toggled, &dialog, [&](bool) { preset->setCurrentIndex(0); refreshPreview(); });
    connect(maskCombo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
        if (!maskCombo->currentData().toString().isEmpty())
            shape->setCurrentIndex(shape->findData(QStringLiteral("alpha")));
        preset->setCurrentIndex(0);
        refreshPreview();
    });

    connect(addMask, &QPushButton::clicked, &dialog, [&] {
        const QString path = UniversalAssetPickerDialog::chooseOne(
            ed,&dialog,QStringLiteral("brush.mask"),QStringLiteral("image"),
            tr("Adicionar imagem para o formato do pincel"));
        if (path.isEmpty()) return;
        const QImage mask = paint::loadBrushAlphaMask(path);
        if (mask.isNull()) {
            QMessageBox::warning(&dialog, tr("Formato do pincel"), tr("Não foi possível carregar esta imagem."));
            return;
        }
        if (!library.contains(path)) library.prepend(path);
        settings.setValue(QStringLiteral("editor/brushMaskLibrary"), library);
        refillMasks();
        const int idx = maskCombo->findData(path);
        if (idx >= 0) maskCombo->setCurrentIndex(idx);
        shape->setCurrentIndex(shape->findData(QStringLiteral("alpha")));
        refreshPreview();
    });

    connect(removeMask, &QPushButton::clicked, &dialog, [&] {
        const QString path = maskCombo->currentData().toString();
        if (path.isEmpty()) return;
        library.removeAll(path);
        settings.setValue(QStringLiteral("editor/brushMaskLibrary"), library);
        refillMasks();
        refreshPreview();
    });

    refreshPreview();

    auto* hint = new QLabel(
        tr("A imagem escolhida serve somente para definir o formato do pincel no editor. O mapa continua usando tiles normais, e a pintura pode ser desfeita com Ctrl+Z."), &dialog);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        BrushSettings b = buildBrush();
        if (b.shape == QLatin1String("alpha") && b.alphaMask.isNull()) {
            QMessageBox::warning(&dialog, tr("Formato do pincel"),
                                 tr("Escolha uma imagem para o formato do pincel ou selecione um formato pronto."));
            return;
        }
        ed.session.brush = b;
        EditorSessionStore::save(ed);
        if (m_view) m_view->update();
        statusBar()->showMessage(
            b.shape == QLatin1String("alpha")
                ? tr("Pincel atualizado: formato por imagem · %1×%1 tiles.").arg(b.size * 2 - 1)
                : tr("Pincel atualizado: %1 · %2×%2 tiles.")
                      .arg(shape->currentText()).arg(b.size * 2 - 1),
            3500);
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::openPaintBrushSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pincel de pintura"));
    dialog.resize(700, 560);

    auto* root = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        tr("Pinte livremente sem depender da grade. Você pode usar o pincel redondo ou escolher imagens da sua biblioteca. "
           "Para texturas alpha com borda dura, use Mesclar bordas: o editor segue o contorno real da transparência, quebra a transição de forma orgânica e preserva o centro da textura."), &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* body = new QHBoxLayout;
    root->addLayout(body, 1);
    auto* formBox = new QGroupBox(tr("Pincel"), &dialog);
    auto* form = new QFormLayout(formBox);
    body->addWidget(formBox, 1);

    auto* mode = new QComboBox(formBox);
    mode->addItem(tr("Redondo"), QStringLiteral("round"));
    mode->addItem(tr("Usar formato de uma imagem"), QStringLiteral("alpha"));
    mode->addItem(tr("Pintar com a própria imagem"), QStringLiteral("color"));
    mode->setCurrentIndex(qMax(0, mode->findData(ed.session.rasterBrush.tipMode)));

    auto* size = new QSpinBox(formBox); size->setRange(1, 2048); size->setSuffix(tr(" px"));
    size->setValue(ed.session.rasterBrush.sizePx);
    size->setToolTip(tr("Define o tamanho do pincel em pixels. A silhueta mostrada no mapa acompanha este tamanho."));
    auto* opacity = new QSpinBox(formBox); opacity->setRange(1,100); opacity->setSuffix(QStringLiteral("%"));
    opacity->setValue(ed.session.rasterBrush.opacity);
    opacity->setToolTip(tr("Controla o limite máximo de transparência da pintura. 100% permite uma marca totalmente visível; valores menores deixam o resultado mais leve."));
    auto* flow = new QSpinBox(formBox); flow->setRange(1,100); flow->setSuffix(QStringLiteral("%"));
    flow->setValue(ed.session.rasterBrush.flow);
    auto* hardness = new QSpinBox(formBox); hardness->setRange(0,100); hardness->setSuffix(QStringLiteral("%"));
    hardness->setValue(ed.session.rasterBrush.hardness);
    auto* spacing = new QSpinBox(formBox); spacing->setRange(1,400); spacing->setSuffix(tr("% do tamanho"));
    spacing->setValue(ed.session.rasterBrush.spacingPercent);
    auto* rotation = new QSpinBox(formBox); rotation->setRange(-360,360); rotation->setSuffix(QStringLiteral("°"));
    rotation->setValue(ed.session.rasterBrush.rotation);
    rotation->setToolTip(tr("Gira o formato do pincel antes de pintar."));
    auto* rotateStroke = new QCheckBox(tr("Rotacionar acompanhando o traço"), formBox);
    rotateStroke->setChecked(ed.session.rasterBrush.rotateToStroke);
    auto* scatter = new QSpinBox(formBox); scatter->setRange(0,400); scatter->setSuffix(QStringLiteral("%"));
    scatter->setValue(ed.session.rasterBrush.scatterPercent);
    auto* sizeJitter = new QSpinBox(formBox); sizeJitter->setRange(0,100); sizeJitter->setSuffix(QStringLiteral("%"));
    sizeJitter->setValue(ed.session.rasterBrush.sizeJitter);
    sizeJitter->setToolTip(tr("Faz cada marca sair com um tamanho um pouco diferente, reduzindo repetição."));
    auto* rotationJitter = new QSpinBox(formBox); rotationJitter->setRange(0,360); rotationJitter->setSuffix(QStringLiteral("°"));
    rotationJitter->setValue(ed.session.rasterBrush.rotationJitter);
    rotationJitter->setToolTip(tr("Gira cada marca de forma diferente, útil para folhas, pedras, sujeira e detalhes orgânicos."));
    auto* blend = new QComboBox(formBox);
    blend->addItem(tr("Normal"), QStringLiteral("source-over"));
    blend->addItem(tr("Multiplicar"), QStringLiteral("multiply"));
    blend->addItem(tr("Tela"), QStringLiteral("screen"));
    blend->addItem(tr("Sobrepor"), QStringLiteral("overlay"));
    blend->addItem(tr("Escurecer"), QStringLiteral("darken"));
    blend->addItem(tr("Clarear"), QStringLiteral("lighten"));
    blend->setCurrentIndex(qMax(0, blend->findData(ed.session.rasterBrush.blendMode)));

    auto* softenEdges = new QCheckBox(tr("Mesclar bordas da textura"), formBox);
    softenEdges->setChecked(ed.session.rasterBrush.softenImageEdges);
    softenEdges->setToolTip(tr("Segue o contorno alpha real da textura e mistura apenas a borda. Diferente de um desfoque, os detalhes internos não ficam borrados."));
    auto* edgeArea = new QSpinBox(formBox); edgeArea->setRange(1, 50); edgeArea->setSuffix(QStringLiteral("%"));
    edgeArea->setValue(ed.session.rasterBrush.edgeSoftnessPercent);
    edgeArea->setToolTip(tr("Largura da faixa de mesclagem medida para dentro do contorno real da textura."));
    auto* edgeStrength = new QSpinBox(formBox); edgeStrength->setRange(0, 100); edgeStrength->setSuffix(QStringLiteral("%"));
    edgeStrength->setValue(ed.session.rasterBrush.edgeSoftnessStrength);
    edgeStrength->setToolTip(tr("Controla quanto a borda se mistura com o fundo. 50–75% costuma manter detalhes sem parecer um carimbo."));
    auto* edgeIrregularity = new QSpinBox(formBox); edgeIrregularity->setRange(0, 100); edgeIrregularity->setSuffix(QStringLiteral("%"));
    edgeIrregularity->setValue(ed.session.rasterBrush.edgeIrregularityPercent);
    edgeIrregularity->setToolTip(tr("Cria pequenas variações na largura da transição para a borda não parecer perfeitamente uniforme."));
    auto* preserveCenter = new QCheckBox(tr("Preservar centro da textura"), formBox);
    preserveCenter->setChecked(ed.session.rasterBrush.preserveEdgeCenter);
    preserveCenter->setToolTip(tr("Mantém o miolo e os detalhes internos 100% intactos; somente a área próxima ao contorno é alterada."));

    auto* colorButton = new QPushButton(formBox);
    QColor selectedColor = ed.session.rasterBrush.color;
    auto refreshColorButton = [&] {
        colorButton->setText(selectedColor.name(QColor::HexArgb));
        colorButton->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;}")
                                   .arg(selectedColor.name(), selectedColor.lightness() > 130 ? QStringLiteral("#111") : QStringLiteral("#fff")));
    };
    refreshColorButton();
    connect(colorButton, &QPushButton::clicked, &dialog, [&] {
        const QColor picked = QColorDialog::getColor(selectedColor, &dialog, tr("Cor do pincel"), QColorDialog::ShowAlphaChannel);
        if (picked.isValid()) { selectedColor = picked; refreshColorButton(); }
    });

    auto* tipRow = new QWidget(formBox);
    auto* tipLayout = new QHBoxLayout(tipRow); tipLayout->setContentsMargins(0,0,0,0);
    auto* tipPath = new QLineEdit(ed.session.rasterBrush.tipImagePath, tipRow); tipPath->setReadOnly(true);
    auto* chooseTip = new QPushButton(tr("Escolher arquivo…"), tipRow);
    auto* openTipFolder = new QPushButton(tr("Pasta Pinceis"), tipRow);
    auto* clearTip = new QPushButton(tr("Limpar"), tipRow);
    tipLayout->addWidget(tipPath,1); tipLayout->addWidget(chooseTip); tipLayout->addWidget(openTipFolder); tipLayout->addWidget(clearTip);

    form->addRow(tr("Tipo:"), mode);
    form->addRow(tr("Tamanho:"), size);
    form->addRow(tr("Opacidade:"), opacity);
    form->addRow(tr("Quantidade de tinta por passada:"), flow);
    flow->setToolTip(tr("Valores baixos fazem a tinta aparecer aos poucos enquanto você passa o pincel várias vezes. Valores altos aplicam a cor mais rapidamente."));
    form->addRow(tr("Suavidade da borda:"), hardness);
    hardness->setToolTip(tr("Valores altos deixam a borda mais marcada. Valores baixos deixam a borda mais suave e difusa."));
    form->addRow(tr("Distância entre marcas:"), spacing);
    spacing->setToolTip(tr("Controla a distância entre cada marca do pincel durante o movimento. Valores menores criam um traço mais contínuo."));
    form->addRow(tr("Cor:"), colorButton);
    form->addRow(tr("Imagem do pincel:"), tipRow);
    form->addRow(tr("Rotação inicial:"), rotation);
    form->addRow(QString(), rotateStroke);
    form->addRow(tr("Espalhar ao redor do traço:"), scatter);
    scatter->setToolTip(tr("Espalha as marcas para os lados do caminho do pincel. Útil para folhas, sujeira, pedras e vegetação."));
    form->addRow(tr("Variar tamanho:"), sizeJitter);
    form->addRow(tr("Variar rotação:"), rotationJitter);
    form->addRow(tr("Mistura:"), blend);
    form->addRow(QString(), softenEdges);
    form->addRow(tr("Área da borda:"), edgeArea);
    form->addRow(tr("Força da mesclagem:"), edgeStrength);
    form->addRow(tr("Irregularidade:"), edgeIrregularity);
    form->addRow(QString(), preserveCenter);

    auto* previewBox = new QGroupBox(tr("Prévia"), &dialog);
    auto* previewLayout = new QVBoxLayout(previewBox);
    auto* preview = new QLabel(previewBox); preview->setFixedSize(240,240); preview->setAlignment(Qt::AlignCenter);
    preview->setFrameShape(QFrame::StyledPanel); previewLayout->addWidget(preview,0,Qt::AlignCenter);
    auto* info = new QLabel(previewBox); info->setWordWrap(true); info->setAlignment(Qt::AlignCenter); previewLayout->addWidget(info);
    body->addWidget(previewBox);

    QImage selectedTip = ed.session.rasterBrush.tipImage;
    auto currentSettings = [&] {
        RasterBrushSettings b = ed.session.rasterBrush;
        b.sizePx = size->value(); b.opacity = opacity->value(); b.flow = flow->value();
        b.hardness = hardness->value(); b.spacingPercent = spacing->value(); b.color = selectedColor;
        b.tipMode = mode->currentData().toString(); b.tipImagePath = tipPath->text(); b.tipImage = selectedTip;
        b.rotation = rotation->value(); b.rotateToStroke = rotateStroke->isChecked();
        b.scatterPercent = scatter->value(); b.sizeJitter = sizeJitter->value();
        b.rotationJitter = rotationJitter->value(); b.blendMode = blend->currentData().toString();
        b.softenImageEdges = softenEdges->isChecked();
        b.edgeSoftnessPercent = edgeArea->value();
        b.edgeSoftnessStrength = edgeStrength->value();
        b.edgeIrregularityPercent = edgeIrregularity->value();
        b.preserveEdgeCenter = preserveCenter->isChecked();
        return b;
    };

    auto refreshPreview = [&] {
        RasterBrushSettings b = currentSettings();
        QImage canvas(220,220,QImage::Format_ARGB32_Premultiplied); canvas.fill(QColor(35,38,44));
        LayerPtr temp = makePaintLayer(canvas.size(), QStringLiteral("preview")); temp->image = canvas;
        RasterBrushSettings pb = b; pb.sizePx = qMin(180, qMax(8, b.sizePx)); pb.scatterPercent = 0; pb.sizeJitter = 0; pb.rotationJitter = 0;
        paint::rasterBrushDab(temp, pb, QPointF(110,110), false, 0.0);
        QPainter pp(&temp->image); pp.setPen(QPen(QColor(255,255,255,35),1)); pp.drawLine(0,110,220,110); pp.drawLine(110,0,110,220); pp.end();
        preview->setPixmap(QPixmap::fromImage(temp->image));
        info->setText(tr("%1 px · marcas a cada %2 px · %3")
                      .arg(b.sizePx).arg(paint::rasterBrushSpacingPx(b)).arg(mode->currentText()));
        const bool imageMode = b.tipMode != QLatin1String("round");
        tipRow->setEnabled(true);
        clearTip->setEnabled(imageMode && !selectedTip.isNull());
        hardness->setEnabled(!imageMode);
        colorButton->setEnabled(b.tipMode != QLatin1String("color"));
        softenEdges->setEnabled(imageMode);
        const bool canBlendEdge = imageMode && softenEdges->isChecked();
        edgeArea->setEnabled(canBlendEdge);
        edgeStrength->setEnabled(canBlendEdge);
        edgeIrregularity->setEnabled(canBlendEdge);
        preserveCenter->setEnabled(canBlendEdge);
    };

    connect(chooseTip, &QPushButton::clicked, &dialog, [&] {
        const QString path = UniversalAssetPickerDialog::chooseOne(
            ed,&dialog,QStringLiteral("brush.tip"),QStringLiteral("image"),tr("Escolher imagem do pincel"));
        if (path.isEmpty()) return;
        QImage image = paint::loadRasterBrushTip(path);
        if (image.isNull()) { QMessageBox::warning(&dialog, tr("Pincel"), tr("Não foi possível carregar esta imagem.")); return; }
        selectedTip = image; tipPath->setText(path);
        if (mode->currentData().toString() == QLatin1String("round")) mode->setCurrentIndex(mode->findData(QStringLiteral("alpha")));
        refreshPreview();
    });
    connect(openTipFolder, &QPushButton::clicked, &dialog, [this] {
        const QString folder = primaryPaintBrushFolder();
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(clearTip, &QPushButton::clicked, &dialog, [&] { selectedTip = QImage(); tipPath->clear(); mode->setCurrentIndex(mode->findData(QStringLiteral("round"))); refreshPreview(); });

    for (QSpinBox* spin : {size, opacity, flow, hardness, spacing, rotation, scatter, sizeJitter, rotationJitter, edgeArea, edgeStrength, edgeIrregularity})
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&](int){ refreshPreview(); });
    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int){ refreshPreview(); });
    connect(blend, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int){ refreshPreview(); });
    connect(rotateStroke, &QCheckBox::toggled, &dialog, [&](bool){ refreshPreview(); });
    connect(softenEdges, &QCheckBox::toggled, &dialog, [&](bool){ refreshPreview(); });
    connect(preserveCenter, &QCheckBox::toggled, &dialog, [&](bool){ refreshPreview(); });
    connect(colorButton, &QPushButton::clicked, &dialog, [&] { QTimer::singleShot(0, &dialog, refreshPreview); });
    refreshPreview();

    auto* hint = new QLabel(tr("Dica: segure Ctrl enquanto pinta para esconder/apagar usando o mesmo formato do pincel. Para sujeira e sombras, experimente Multiplicar. Para caminhos e texturas, use uma imagem e ative “Rotacionar acompanhando o traço”."), &dialog);
    hint->setWordWrap(true); root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        RasterBrushSettings b = currentSettings();
        if (b.tipMode != QLatin1String("round") && b.tipImage.isNull()) {
            QMessageBox::warning(&dialog, tr("Pincel de pintura"), tr("Escolha uma imagem para o pincel ou use o formato Redondo."));
            return;
        }
        ed.session.rasterBrush = b;
        EditorSessionStore::save(ed);
        setTool(Tool::Paint);
        if (m_view) m_view->update();
        refreshPaintBrushLibrary();
        syncPaintBrushToolbar();
        statusBar()->showMessage(tr("Pincel de pintura atualizado: %1 px · %2.").arg(b.sizePx).arg(mode->currentText()), 4000);
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}


void MainWindow::openSlopeDialog(const QString& layerId, const QRect& localPixelRect)
{
    LayerPtr layer = ed.findNode(layerId);
    if (!layer || (layer->type != LayerType::Tile && layer->type != LayerType::Image)) return;

    const LayerEffectiveState effective = layerEffectiveState(ed.layers(), layer->id);
    if (!effective.visible || effective.locked) {
        QMessageBox::information(this, tr("Inclinação"),
                                 effective.locked
                                     ? tr("A camada está bloqueada pela própria camada ou por um grupo pai.")
                                     : tr("A camada está oculta."));
        return;
    }
    if (layer->type == LayerType::Tile && (layer->isMask || !layer->children.isEmpty())) {
        QMessageBox::information(this, tr("Inclinação"),
                                 tr("A Inclinação precisa de uma camada de tiles simples para gerar um recurso reutilizável. "
                                    "Separe máscaras estruturais antes de aplicar a ferramenta."));
        return;
    }

    QRect rect = localPixelRect;
    QImage source;
    const int outputTileW = layer->type == LayerType::Tile ? qMax(1, layer->tileWidth)
                                                            : qMax(1, ed.mapInfo().tileWidth);
    const int outputTileH = layer->type == LayerType::Tile ? qMax(1, layer->tileHeight)
                                                            : qMax(1, ed.mapInfo().tileHeight);
    if (layer->type == LayerType::Image) {
        rect = rect.intersected(layer->image.rect());
        if (rect.isEmpty()) return;
        source = layer->image.copy(rect).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
        const QRect layerPixels(0, 0, layer->cols * layer->tileWidth, layer->rows * layer->tileHeight);
        rect = rect.intersected(layerPixels);
        if (rect.isEmpty()) return;

        source = QImage(rect.size(), QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::transparent);
        LayerPtr rasterSource = cloneLayer(layer, false);
        rasterSource->visible = true;
        rasterSource->opacity = 1.0;
        rasterSource->blendMode = QStringLiteral("source-over");
        rasterSource->isMask = false;
        rasterSource->children.clear();
        // Inclinação cria um recurso gráfico; sombras de contato continuam
        // sendo processamento da camada e não devem ser gravadas no Tileset.
        rasterSource->imageFilters.clear();

        QPainter painter(&source);
        const QRectF mapRect(rect.x() + layer->offsetx, rect.y() + layer->offsety,
                             rect.width(), rect.height());
        painter.translate(-mapRect.topLeft());
        painter.setClipRect(mapRect);
        RenderOptions options;
        options.fillBackground = false;
        options.drawObjectFrames = false;
        options.suppressContactShadows = true;
        drawLayer(painter, ed, rasterSource, 1.0, options);
        painter.end();
    }

    if (source.isNull() || source.width() <= 0 || source.height() <= 0) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Inclinação"));
    dialog.resize(800, 570);
    auto* root = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(
        tr("A Inclinação cria novas peças inclinadas para você reutilizar no mapa. A camada original não é apagada. Você pode guardar o resultado em um conjunto novo ou acrescentá-lo a um conjunto de Inclinações que já existe."), &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* body = new QHBoxLayout;
    root->addLayout(body, 1);

    auto* previewBox = new QGroupBox(tr("Prévia"), &dialog);
    auto* previewLayout = new QVBoxLayout(previewBox);
    auto* preview = new QLabel(previewBox);
    preview->setMinimumSize(390, 340);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameShape(QFrame::StyledPanel);
    previewLayout->addWidget(preview, 1);
    auto* previewInfo = new QLabel(previewBox);
    previewInfo->setAlignment(Qt::AlignCenter);
    previewInfo->setWordWrap(true);
    previewLayout->addWidget(previewInfo);
    body->addWidget(previewBox, 1);

    auto* controlsBox = new QGroupBox(tr("Ajustes da inclinação"), &dialog);
    auto* form = new QFormLayout(controlsBox);
    auto* axis = new QComboBox(controlsBox);
    axis->addItem(tr("Horizontal — linhas deslocam em X"), int(paint::SlopeAxis::Horizontal));
    axis->addItem(tr("Vertical — colunas deslocam em Y"), int(paint::SlopeAxis::Vertical));
    auto* step = new QDoubleSpinBox(controlsBox);
    step->setRange(-16.0, 16.0);
    step->setDecimals(2);
    step->setSingleStep(0.25);
    step->setValue(1.0);
    step->setSuffix(tr(" px / linha"));
    auto* pixelPerfect = new QCheckBox(tr("Manter pixels nítidos (sem suavização)"), controlsBox);
    pixelPerfect->setChecked(true);
    pixelPerfect->setEnabled(false);
    auto* direction = new QLabel(controlsBox);
    direction->setWordWrap(true);
    auto* selectionInfo = new QLabel(tr("Seleção: %1 × %2 px · grade %3 × %4")
                                         .arg(source.width()).arg(source.height())
                                         .arg(outputTileW).arg(outputTileH), controlsBox);

    auto* destination = new QComboBox(controlsBox);
    destination->addItem(tr("Criar novo Tileset de Inclinações"), -1);
    for (int i = 0; i < ed.tilesets.size(); ++i) {
        const Tileset& ts = ed.tilesets.at(i);
        if (!ts.generatedFromSlope || ts.internalAutotileAtlas) continue;
        if (ts.tilewidth != outputTileW || ts.tileheight != outputTileH) continue;
        if (ts.spacing != 0 || ts.margin != 0) continue;
        destination->addItem(tr("Adicionar em: %1 (%2×%3 tiles)").arg(ts.name).arg(ts.columns).arg(ts.rows), i);
    }
    auto* placement = new QComboBox(controlsBox);
    placement->addItem(tr("Ao lado — acrescentar à direita"), QStringLiteral("right"));
    placement->addItem(tr("Abaixo — acrescentar uma nova faixa"), QStringLiteral("below"));
    placement->setEnabled(false);

    auto* newName = new QLineEdit(tr("Inclinação - %1").arg(layer->name), controlsBox);

    form->addRow(tr("Direção:"), axis);
    form->addRow(tr("Deslocamento:"), step);
    form->addRow(QString(), pixelPerfect);
    form->addRow(tr("Área:"), selectionInfo);
    form->addRow(tr("Salvar em:"), destination);
    form->addRow(tr("Posição:"), placement);
    form->addRow(tr("Nome novo:"), newName);
    form->addRow(tr("Resultado:"), direction);
    body->addWidget(controlsBox);

    auto refreshDestinationUi = [&] {
        const bool append = destination->currentData().toInt() >= 0;
        placement->setEnabled(append);
        newName->setEnabled(!append);
    };
    connect(destination, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
            [&](int) { refreshDestinationUi(); });
    refreshDestinationUi();

    QImage result;
    auto refreshPreview = [&] {
        const auto slopeAxis = axis->currentData().toInt() == int(paint::SlopeAxis::Vertical)
            ? paint::SlopeAxis::Vertical : paint::SlopeAxis::Horizontal;
        result = paint::slopeImage(source, slopeAxis, step->value());

        const QSize boxSize(qMax(80, preview->width() - 18), qMax(80, preview->height() - 18));
        QImage canvas(boxSize, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(30, 32, 36));
        QPainter checker(&canvas);
        const int cell = 12;
        for (int y = 0; y < canvas.height(); y += cell)
            for (int x = 0; x < canvas.width(); x += cell)
                checker.fillRect(QRect(x, y, cell, cell), ((x / cell + y / cell) & 1)
                    ? QColor(54, 57, 62) : QColor(42, 45, 49));
        QImage scaled = result.scaled(canvas.size() - QSize(20, 20), Qt::KeepAspectRatio, Qt::FastTransformation);
        const QPoint topLeft((canvas.width() - scaled.width()) / 2, (canvas.height() - scaled.height()) / 2);
        checker.drawImage(topLeft, scaled);
        checker.end();
        preview->setPixmap(QPixmap::fromImage(canvas));

        const int travel = slopeAxis == paint::SlopeAxis::Horizontal
            ? qRound(step->value() * qMax(0, source.height() - 1))
            : qRound(step->value() * qMax(0, source.width() - 1));
        previewInfo->setText(tr("Deslocamento %1 · total no último %2: %3 px")
            .arg(step->value(), 0, 'f', 2)
            .arg(slopeAxis == paint::SlopeAxis::Horizontal ? tr("linha") : tr("coluna"))
            .arg(travel));
        direction->setText(slopeAxis == paint::SlopeAxis::Horizontal
            ? (step->value() >= 0.0 ? tr("Positivo: linhas descendo avançam para a direita.")
                                    : tr("Negativo: linhas descendo avançam para a esquerda."))
            : (step->value() >= 0.0 ? tr("Positivo: colunas à direita avançam para baixo.")
                                    : tr("Negativo: colunas à direita avançam para cima.")));
        step->setSuffix(slopeAxis == paint::SlopeAxis::Horizontal ? tr(" px / linha") : tr(" px / coluna"));
    };
    connect(axis, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { refreshPreview(); });
    connect(step, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, [&](double) { refreshPreview(); });
    QTimer::singleShot(0, &dialog, refreshPreview);

    auto* hint = new QLabel(
        tr("Organização: use “Adicionar em” para juntar várias inclinações no mesmo Tileset. "
           "O editor só oferece Tilesets de Inclinações com a mesma grade para evitar referências quebradas."), &dialog);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton* apply = buttons->button(QDialogButtonBox::Ok)) apply->setText(tr("Gerar Tileset"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const auto slopeAxis = axis->currentData().toInt() == int(paint::SlopeAxis::Vertical)
        ? paint::SlopeAxis::Vertical : paint::SlopeAxis::Horizontal;
    result = paint::slopeImage(source, slopeAxis, step->value());
    if (result.isNull()) return;

    auto resultHasAlpha = [](const QImage& image) {
        const QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < rgba.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
            for (int x = 0; x < rgba.width(); ++x)
                if (qAlpha(row[x]) > 0) return true;
        }
        return false;
    };
    if (!resultHasAlpha(result)) {
        QMessageBox::information(this, tr("Inclinação"), tr("A seleção não possui pixels visíveis para inclinar."));
        return;
    }

    // Tileset sempre trabalha em células completas. O padding transparente
    // evita perder a última borda quando a inclinação aumenta a imagem.
    const int resultCols = qMax(1, (result.width() + outputTileW - 1) / outputTileW);
    const int resultRows = qMax(1, (result.height() + outputTileH - 1) / outputTileH);
    QImage packed(resultCols * outputTileW, resultRows * outputTileH, QImage::Format_ARGB32_Premultiplied);
    packed.fill(Qt::transparent);
    {
        QPainter pp(&packed);
        pp.drawImage(QPoint(0, 0), result);
    }

    const DocSnapshot beforeDoc = ed.snapshotDoc();
    const QVector<Tileset> beforeTilesets = ed.tilesets;
    const int beforeActiveTilesetIdx = ed.session.activeTilesetIdx;
    const TilesetSelection beforeTsSel = ed.session.tsSel;
    const CustomStamp beforeCustomStamp = ed.session.customStamp;

    int targetIndex = destination->currentData().toInt();
    int startTx = 0, startTy = 0;
    bool appended = targetIndex >= 0 && targetIndex < ed.tilesets.size();

    if (!appended) {
        QString baseName = newName->text().trimmed();
        if (baseName.isEmpty()) baseName = tr("Inclinação - %1").arg(layer->name);
        QString tilesetName = baseName;
        int suffix = 2;
        auto nameExists = [&](const QString& wanted) {
            for (const Tileset& ts : ed.tilesets)
                if (ts.name.compare(wanted, Qt::CaseInsensitive) == 0) return true;
            return false;
        };
        while (nameExists(tilesetName)) tilesetName = tr("%1 %2").arg(baseName).arg(suffix++);

        Tileset generated = makeTileset(packed, tilesetName, outputTileW, outputTileH, 0, 0);
        generated.category = QStringLiteral("Inclinações");
        generated.generatedFromSlope = true;
        generated.slopeSourceLayerId = layer->id;
        generated.slopeAxis = slopeAxis == paint::SlopeAxis::Vertical
            ? QStringLiteral("vertical") : QStringLiteral("horizontal");
        generated.slopeStep = step->value();
        targetIndex = ed.addTileset(generated);
    } else {
        Tileset& target = ed.tilesets[targetIndex];
        const int oldCols = target.columns;
        const int oldRows = target.rows;
        const bool below = placement->currentData().toString() == QLatin1String("below");
        const int newCols = below ? qMax(oldCols, resultCols) : oldCols + resultCols;
        const int newRows = below ? oldRows + resultRows : qMax(oldRows, resultRows);
        QImage expanded(newCols * outputTileW, newRows * outputTileH,
                        QImage::Format_ARGB32_Premultiplied);
        expanded.fill(Qt::transparent);
        QPainter ep(&expanded);
        ep.drawImage(QPoint(0, 0), target.image);
        if (below) {
            startTx = 0;
            startTy = oldRows;
        } else {
            startTx = oldCols;
            startTy = 0;
        }
        ep.drawImage(QPoint(startTx * outputTileW, startTy * outputTileH), packed);
        ep.end();
        target.image = expanded;
        target.generatedFromSlope = true;
        if (target.category.isEmpty()) target.category = QStringLiteral("Inclinações");
        target.recomputeGrid();
        ed.session.activeTilesetIdx = targetIndex;
        ed.reindexTilesetGids();
        ed.markDirty();
        emit ed.tilesetsChanged();
    }

    CustomStamp generatedStamp;
    generatedStamp.w = resultCols;
    generatedStamp.h = resultRows;
    for (int ty = 0; ty < resultRows; ++ty) {
        for (int tx = 0; tx < resultCols; ++tx) {
            TileRef ref;
            ref.tilesetIdx = targetIndex;
            ref.tx = startTx + tx;
            ref.ty = startTy + ty;
            if (isBlankTile(ed, ref)) continue;
            generatedStamp.tiles.push_back(ref);
            generatedStamp.offsets.push_back(QPoint(tx, ty));
        }
    }
    ed.session.tsSel = TilesetSelection{targetIndex, startTx, startTy, resultCols, resultRows};
    if (generatedStamp.valid()) ed.session.customStamp = generatedStamp;
    emit ed.selectionChanged();

    ed.pushDocTilesetHistory(beforeDoc, beforeTilesets, beforeActiveTilesetIdx,
                             beforeTsSel, beforeCustomStamp,
                             appended ? tr("Adicionar inclinação ao Tileset")
                                      : tr("Criar Tileset de inclinação"));
    setTool(Tool::Stamp);
    statusBar()->showMessage(appended
        ? tr("Inclinação adicionada ao Tileset existente. Nenhuma camada foi criada ou alterada.")
        : tr("Tileset de Inclinação criado. Nenhuma camada foi criada ou alterada."), 7000);

    if (m_view) m_view->update();
    updateToolStates();
}

void MainWindow::openReflectionSettings()
{
    if (ed.rpgMakerEngine != core::RpgMakerEngine::MZ) {
        statusBar()->showMessage(tr("Reflexos dependem do LudoReflectionSystem e não fazem parte de projetos RPG Maker MV."), 5000);
        return;
    }
    MapDoc* doc = ed.doc();
    if (!doc) return;

    const DocSnapshot before = ed.snapshotDoc();
    const QJsonObject current = doc->reflectionSettings;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Reflexos do mapa — avançado"));
    dialog.resize(660, 840);
    auto* root = new QVBoxLayout(&dialog);

    auto* intro = new QLabel(tr("Configura opções avançadas de reflexo do mapa atual. O céu/ambiente agora é editado diretamente na aba Mapa → Reflexos; para água, poças e piso molhado, prefira marcar o Tile/Autotile no Gerenciador de Tilesets."), &dialog);
    intro->setWordWrap(true);
    intro->setProperty("uiRole", QStringLiteral("hint"));
    root->addWidget(intro);

    auto* generalBox = new QGroupBox(tr("Runtime"), &dialog);
    auto* general = new QFormLayout(generalBox);
    auto* enabled = new QCheckBox(tr("Ativar reflexos neste mapa"), generalBox);
    enabled->setChecked(current.value("enabled").toBool(true));
    auto* quality = new QComboBox(generalBox);
    quality->addItem(tr("Performance"), QStringLiteral("performance"));
    quality->addItem(tr("Equilibrado"), QStringLiteral("balanced"));
    quality->addItem(tr("Alta qualidade"), QStringLiteral("quality"));
    const QString qualityValue = current.value("quality").toString(QStringLiteral("balanced"));
    quality->setCurrentIndex(qMax(0, quality->findData(qualityValue)));
    auto* renderMode = new QComboBox(generalBox);
    renderMode->addItem(tr("Automático (shader + fallback)"), QStringLiteral("auto"));
    renderMode->addItem(tr("Shader"), QStringLiteral("shader"));
    renderMode->addItem(tr("Faixas compatíveis"), QStringLiteral("strips"));
    const QString renderValue = current.value("renderMode").toString(QStringLiteral("auto"));
    renderMode->setCurrentIndex(qMax(0, renderMode->findData(renderValue)));
    auto* reflectEvents = new QCheckBox(tr("Refletir eventos / NPCs"), generalBox);
    reflectEvents->setChecked(current.value("reflectEvents").toBool(true));
    auto* reflectFollowers = new QCheckBox(tr("Refletir seguidores"), generalBox);
    reflectFollowers->setChecked(current.value("reflectFollowers").toBool(true));
    auto* reflectObjects = new QCheckBox(tr("Refletir objetos marcados no inspetor"), generalBox);
    reflectObjects->setChecked(current.value("reflectObjects").toBool(true));
    auto* reflectLights = new QCheckBox(tr("Refletir luzes dinâmicas do LudoLightEngine"), generalBox);
    reflectLights->setChecked(current.value("reflectLights").toBool(true));
    auto* weather = new QCheckBox(tr("Integrar com LudoWeatherSystem"), generalBox);
    weather->setChecked(current.value("weatherIntegration").toBool(true));
    auto* maxReflections = new QSpinBox(generalBox);
    maxReflections->setRange(1, 128);
    maxReflections->setValue(qBound(1, current.value("maxReflections").toInt(32), 128));
    auto* cullingMargin = new QSpinBox(generalBox);
    cullingMargin->setRange(0, 1024);
    cullingMargin->setSuffix(tr(" px"));
    cullingMargin->setValue(qBound(0, current.value("cullingMargin").toInt(160), 1024));
    general->addRow(enabled);
    general->addRow(tr("Qualidade"), quality);
    general->addRow(tr("Renderização"), renderMode);
    general->addRow(reflectEvents);
    general->addRow(reflectFollowers);
    general->addRow(reflectObjects);
    general->addRow(reflectLights);
    general->addRow(weather);
    general->addRow(tr("Máximo visível"), maxReflections);
    general->addRow(tr("Margem de culling"), cullingMargin);
    root->addWidget(generalBox);

    auto* tileMaskBox = new QGroupBox(tr("Limitar efeito ao desenho do Tile / Autotile"), &dialog);
    auto* tileMaskForm = new QFormLayout(tileMaskBox);
    auto* tileAlphaThreshold = new QSpinBox(tileMaskBox);
    tileAlphaThreshold->setRange(0, 254);
    tileAlphaThreshold->setValue(qBound(0, current.value("tileAlphaThreshold").toInt(4), 254));
    tileAlphaThreshold->setToolTip(tr("Ignora pixels quase transparentes nas bordas do desenho. O valor 4 costuma funcionar bem; aumente apenas se aparecerem pequenas sobras nas extremidades."));
    auto* tileMaskLayer = new QComboBox(tileMaskBox);
    tileMaskLayer->addItem(tr("Automático (tile mais alto)"), -1);
    tileMaskLayer->addItem(tr("Camada 1"), 0);
    tileMaskLayer->addItem(tr("Camada 2"), 1);
    tileMaskLayer->addItem(tr("Camada 3"), 2);
    tileMaskLayer->addItem(tr("Camada 4"), 3);
    const int savedMaskLayer = qBound(-1, current.value("tileMaskLayer").toInt(-1), 3);
    tileMaskLayer->setCurrentIndex(qMax(0, tileMaskLayer->findData(savedMaskLayer)));
    tileMaskLayer->setToolTip(tr("Automático acompanha o tile que aparece por cima. Se uma decoração estiver cobrindo o efeito, escolha manualmente a posição da pilha onde está o tile que deve definir o formato."));
    tileMaskForm->addRow(tr("Ignorar transparência muito fraca"), tileAlphaThreshold);
    tileMaskForm->addRow(tr("De qual tile usar o formato"), tileMaskLayer);
    root->addWidget(tileMaskBox);

    auto* surfacesBox = new QGroupBox(tr("Configurações por região (avançado)"), &dialog);
    auto* surfaces = new QFormLayout(surfacesBox);
    struct SurfaceRow { const char* id; const char* label; int defaultRegion; const char* defaultMask; QSpinBox* spin = nullptr; QComboBox* mask = nullptr; };
    QVector<SurfaceRow> rows{
        {"still",  "Água parada", 20, "region"},
        {"dirty",  "Água suja", 21, "region"},
        {"rough",  "Água agitada", 22, "region"},
        {"puddle", "Poça", 23, "regionTileAlpha"},
        {"wet",    "Piso molhado", 24, "regionTileAlpha"},
        {"mirror", "Espelho / superfície limpa", 25, "region"}
    };
    QHash<QString, QJsonObject> savedSurfaces;
    for (const QJsonValue& value : current.value("surfaces").toArray()) {
        const QJsonObject surface = value.toObject();
        savedSurfaces.insert(surface.value("preset").toString(), surface);
    }
    for (SurfaceRow& row : rows) {
        const QString presetId = QString::fromLatin1(row.id);
        const bool hadSavedSurface = savedSurfaces.contains(presetId);
        const QJsonObject saved = savedSurfaces.value(presetId);
        row.spin = new QSpinBox(surfacesBox);
        row.spin->setRange(0, 255);
        row.spin->setSpecialValueText(tr("Desativado"));
        row.spin->setValue(qBound(0, hadSavedSurface ? saved.value("regionId").toInt(row.defaultRegion) : row.defaultRegion, 255));
        row.spin->setToolTip(tr("0 desativa esta configuração. Use números diferentes para separar superfícies com comportamentos diferentes."));
        row.mask = new QComboBox(surfacesBox);
        row.mask->addItem(tr("Região"), QStringLiteral("region"));
        row.mask->addItem(tr("Transparência do Tile / Autotile"), QStringLiteral("tileAlpha"));
        row.mask->addItem(tr("Região + transparência do Tile"), QStringLiteral("regionTileAlpha"));
        const QString defaultMask = hadSavedSurface ? QStringLiteral("region") : QString::fromLatin1(row.defaultMask);
        const QString savedMask = saved.value("maskMode").toString(defaultMask);
        row.mask->setCurrentIndex(qMax(0, row.mask->findData(savedMask)));
        row.mask->setToolTip(tr("Região: usa a célula inteira. Transparência do Tile: o efeito acompanha somente as partes visíveis do gráfico. Região + transparência: combina as duas opções e é útil para poças e formas irregulares."));
        auto* rowWidget = new QWidget(surfacesBox);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(row.spin);
        rowLayout->addWidget(row.mask, 1);
        surfaces->addRow(tr(row.label), rowWidget);
    }
    root->addWidget(surfacesBox);

    auto* hint = new QLabel(tr("O céu/ambiente é configurado em Mapa → Reflexos e continua sendo salvo por mapa. A opacidade de cada Tile/Autotile refletivo fica no Gerenciador de Tilesets → Efeitos e multiplica tudo que aparece naquela superfície, inclusive o céu. Regions continuam disponíveis para mapas antigos e superfícies especiais."), &dialog);
    hint->setWordWrap(true);
    hint->setProperty("uiRole", QStringLiteral("hint"));
    root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    QJsonObject next;
    next.insert("enabled", enabled->isChecked());
    next.insert("quality", quality->currentData().toString());
    next.insert("renderMode", renderMode->currentData().toString());
    next.insert("reflectEvents", reflectEvents->isChecked());
    next.insert("reflectFollowers", reflectFollowers->isChecked());
    next.insert("reflectObjects", reflectObjects->isChecked());
    next.insert("reflectLights", reflectLights->isChecked());
    next.insert("weatherIntegration", weather->isChecked());
    next.insert("maxReflections", maxReflections->value());
    next.insert("cullingMargin", cullingMargin->value());
    if (current.contains(QStringLiteral("offsetY"))) next.insert(QStringLiteral("offsetY"), current.value(QStringLiteral("offsetY")));
    next.insert("tileAlphaThreshold", tileAlphaThreshold->value());
    next.insert("tileMaskLayer", tileMaskLayer->currentData().toInt());

    // O céu/ambiente pertence à aba Mapa → Reflexos. Preserve-o ao salvar
    // as opções avançadas para que esta janela nunca sobrescreva essa configuração.
    const QJsonObject environment = current.value(QStringLiteral("environment")).toObject();
    if (!environment.isEmpty()) next.insert(QStringLiteral("environment"), environment);

    QJsonArray surfaceArray;
    QSet<int> used;
    for (const SurfaceRow& row : rows) {
        const int regionId = row.spin->value();
        if (regionId <= 0 || used.contains(regionId)) continue;
        used.insert(regionId);
        surfaceArray.append(QJsonObject{
            {"regionId", regionId},
            {"preset", QString::fromLatin1(row.id)},
            {"maskMode", row.mask->currentData().toString()}
        });
    }
    next.insert("surfaces", surfaceArray);
    doc->reflectionSettings = next;
    ed.pushDocHistory(before, tr("Configurar superfícies refletivas"));
    emit ed.mapChanged();
    statusBar()->showMessage(tr("Configuração de reflexos atualizada. Exporte/salve o mapa para enviar ao runtime."), 5000);
}

void MainWindow::addObjectLayer()
{
    const DocSnapshot before = ed.snapshotDoc();
    const LayerPtr selected = ed.selectedLayer();
    const QString parentId = (selected && selected->type == LayerType::Group) ? selected->id : QString();
    ed.addLayer(makeObjectLayer(tr("Camada de objetos")), parentId);
    ed.pushDocHistory(before, tr("Nova camada de objetos"));
}

void MainWindow::addImageLayer(bool referenceOnly)
{
    const QString folder = referenceOnly ? QStringLiteral("References") : QStringLiteral("Pictures");
    const QString path = AssetBrowserDialog::chooseImage(ed, this, folder);
    if (path.isEmpty()) return;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Erro"), tr("Não foi possível ler a imagem."));
        return;
    }
    const DocSnapshot before = ed.snapshotDoc();
    const QString stored = ed.projectRelativePath(path);
    const QString source = stored.isEmpty() ? path : stored;
    const LayerPtr selected = ed.selectedLayer();
    const QString parentId = (selected && selected->type == LayerType::Group) ? selected->id : QString();
    ed.addLayer(makeImageLayer(img, QFileInfo(path).completeBaseName(), source, referenceOnly), parentId);
    ed.pushDocHistory(before, referenceOnly ? tr("Imagem de referência") : tr("Nova camada de imagem"));
    statusBar()->showMessage(referenceOnly
        ? tr("Imagem adicionada como referência editorial; ela não será exportada no jogo.")
        : tr("Camada de imagem adicionada. Use o Inspector para posição, escala, rotação, filtro e mistura."), 5000);
}

void MainWindow::addPaintLayer()
{
    const DocSnapshot before = ed.snapshotDoc();
    const QSize size(qMax(1, ed.mapInfo().pixelWidth()), qMax(1, ed.mapInfo().pixelHeight()));
    LayerPtr layer = makePaintLayer(size, tr("Pintura"));
    const LayerPtr selected = ed.selectedLayer();
    const QString parentId = (selected && selected->type == LayerType::Group) ? selected->id : QString();
    ed.addLayer(layer, parentId);
    ed.pushDocHistory(before, tr("Nova camada de pintura"));
    setTool(Tool::Paint);
    statusBar()->showMessage(tr("Camada de pintura criada. Pinte livremente em pixels; Ctrl funciona como borracha temporária."), 5000);
}

void MainWindow::addGroupLayer()
{
    const DocSnapshot before = ed.snapshotDoc();
    const LayerPtr selected = ed.selectedLayer();

    // UX: criar Grupo sobre uma camada normal significa agrupar aquela camada,
    // não criar uma pasta vazia ao lado. Isso faz a hierarquia aparecer de
    // imediato e corresponde ao comportamento esperado de editores gráficos.
    if (selected && selected->type != LayerType::Group) {
        QVector<LayerPtr>* parent = nullptr;
        int index = -1;
        if (ed.findNode(selected->id, &parent, &index) && parent && index >= 0) {
            LayerPtr group = makeGroupLayer(tr("Grupo"));
            group->collapsed = false;
            parent->replace(index, group);
            group->children.push_back(selected);
            ed.setSelectedLayerById(group->id);
            ed.markDirty();
            emit ed.layersChanged();
            emit ed.mapChanged();
            ed.pushDocHistory(before, tr("Agrupar camada"));
            statusBar()->showMessage(tr("Grupo criado com “%1” dentro dele.").arg(selected->name), 3500);
            return;
        }
    }

    // Com um Grupo selecionado, um novo Grupo é criado dentro dele. Sem
    // seleção de Grupo, ele nasce na raiz/ao lado da seleção atual.
    const QString parentId = (selected && selected->type == LayerType::Group) ? selected->id : QString();
    if (selected && selected->type == LayerType::Group) selected->collapsed = false;
    ed.addLayer(makeGroupLayer(tr("Grupo")), parentId);
    ed.pushDocHistory(before, tr("Novo grupo"));
}

void MainWindow::newMapTab()
{
    rememberActiveViewport();
    MapInfo info = ed.mapInfo();
    if (ed.rpgMakerEngine == core::RpgMakerEngine::MV) {
        info.tileWidth = 48;
        info.tileHeight = 48;
    }
    const int idx = ed.addMapDoc(ed.uniqueMapName(tr("Mapa")), info, false);
    MapDoc& d = ed.docs[idx];
    const int cols = info.pixelWidth() / info.tileWidth;
    const int rows = info.pixelHeight() / info.tileHeight;
    d.layers.push_back(makeTileLayer(tr("Chão"), info.tileWidth, info.tileHeight, cols, rows));
    d.activeLayerIdx = 0;
    d.activeLayerId = d.layers.first()->id;
    ed.markDirty();

    core::mapworkspace::openMap(ed, d.id);
    activateMap(d.id, false);
    ed.session.selectedLayerId = d.activeLayerId;
    emit ed.layersChanged();
    emit ed.mapChanged();
}

void MainWindow::closeMapTab(int index)
{
    if (!m_mapTabs || m_mapTabs->count() <= 1) return;
    const QString closingId = mapIdForTab(index);
    if (closingId.isEmpty()) return;

    rememberActiveViewport();
    const bool closingActive = ed.doc() && ed.doc()->id == closingId;
    const core::mapworkspace::MapCloseResult closeResult =
        core::mapworkspace::closeMap(ed, closingId, index);
    if (!closeResult.closed) return;

    if (closingActive && !closeResult.fallbackMapId.isEmpty())
        activateMap(closeResult.fallbackMapId, false);
    else
        refreshMapTabs();
    statusBar()->showMessage(tr("Mapa fechado no workspace. O conteúdo continua no projeto."), 3000);
}

void MainWindow::requestDeleteMap(const QString& mapId)
{
    if (ed.docs.size() <= 1) {
        QMessageBox::information(this, tr("Excluir mapa"),
                                 tr("O projeto precisa manter pelo menos um mapa."));
        return;
    }
    const int index = mapIndexById(mapId);
    if (index < 0) return;
    const QString closingId = ed.docs.at(index).id;
    const QString closingName = ed.docs.at(index).name;

    core::mapworkflow::MapDeletePolicy policy;
    policy.children = core::mapworkflow::MapDeleteChildrenPolicy::PromoteChildren;
    policy.references = core::mapworkflow::MapDeleteReferencePolicy::RejectReferenced;

    QStringList subtreeIds;
    std::function<void(const QString&)> collectSubtree = [&](const QString& parentId) {
        if (subtreeIds.contains(parentId)) return;
        subtreeIds.push_back(parentId);
        for (const MapDoc& map : std::as_const(ed.docs))
            if (map.parentId == parentId) collectSubtree(map.id);
    };
    collectSubtree(closingId);
    const int childCount = qMax(0, subtreeIds.size() - 1);
    int directVariationCount = 0;
    for (const MapDoc& map : std::as_const(ed.docs))
        if (map.variationBaseId == closingId) ++directVariationCount;

    if (childCount > 0) {
        QMessageBox choice(this);
        choice.setWindowTitle(tr("Excluir mapa"));
        choice.setIcon(QMessageBox::Warning);
        choice.setText(tr("O mapa “%1” possui %2 mapa(s) descendente(s). Como deseja tratar a subárvore?")
                           .arg(closingName).arg(childCount));
        auto* promote = choice.addButton(tr("Excluir mapa e subir filhos"), QMessageBox::DestructiveRole);
        auto* subtree = choice.addButton(tr("Excluir mapa e subárvore"), QMessageBox::DestructiveRole);
        choice.addButton(QMessageBox::Cancel);
        choice.exec();
        if (choice.clickedButton() == subtree)
            policy.children = core::mapworkflow::MapDeleteChildrenPolicy::DeleteSubtree;
        else if (choice.clickedButton() != promote)
            return;
    }

    QSet<QString> deleting;
    if (policy.children == core::mapworkflow::MapDeleteChildrenPolicy::DeleteSubtree)
        for (const QString& id : std::as_const(subtreeIds)) deleting.insert(id);
    else deleting.insert(closingId);

    // O core remove automaticamente as variações de todo mapa-base apagado.
    // Espelhamos isso aqui para fechar abas, limpar arquivos RPG Maker e avisar
    // sobre alterações não salvas corretamente.
    bool addedVariation = true;
    while (addedVariation) {
        addedVariation = false;
        for (const MapDoc& map : std::as_const(ed.docs)) {
            if (map.variationBaseId.isEmpty() || !deleting.contains(map.variationBaseId) || deleting.contains(map.id)) continue;
            deleting.insert(map.id);
            addedVariation = true;
        }
    }

    QVector<int> deletingRpgMakerIds;
    for (const MapDoc& map : std::as_const(ed.docs))
        if (deleting.contains(map.id) && map.rpgMakerMapId > 0) deletingRpgMakerIds.push_back(map.rpgMakerMapId);

    QStringList dirtyNames;
    for (const MapDoc& map : std::as_const(ed.docs))
        if (deleting.contains(map.id) && map.dirty) dirtyNames.push_back(map.name);

    QString confirmation = tr("Excluir permanentemente o mapa “%1” do projeto?").arg(closingName);
    if (directVariationCount > 0)
        confirmation += tr("\n\nAs %1 variação(ões) deste cenário também serão excluídas.").arg(directVariationCount);
    if (!dirtyNames.isEmpty())
        confirmation += tr("\n\nHá alterações não salvas em: %1.").arg(dirtyNames.join(QStringLiteral(", ")));
    confirmation += tr("\n\nFechar uma aba não exclui mapas; esta operação é a exclusão estrutural explícita.");
    if (QMessageBox::warning(this, tr("Excluir mapa"), confirmation,
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes) return;

    core::mapworkflow::MapDeleteResult result;
    QString error;
    bool removed = core::mapworkflow::deleteMap(ed, closingId, policy, &result, &error);
    if (!removed && result.externalReferenceCount > 0) {
        const auto answer = QMessageBox::warning(
            this, tr("Mapa em uso"),
            tr("A exclusão deixará %1 referência(s) legada(s) inválida(s) dentro deste projeto antigo.\n\n"
               "Essas referências pertencem à antiga camada de gameplay e não são mais editadas pelo LUDO Map Editor.\n\nExcluir mesmo assim?")
                .arg(result.externalReferenceCount),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        policy.references = core::mapworkflow::MapDeleteReferencePolicy::AllowDangling;
        error.clear();
        removed = core::mapworkflow::deleteMap(ed, closingId, policy, &result, &error);
    }
    if (!removed) {
        QMessageBox::warning(this, tr("Excluir mapa"), error);
        return;
    }

    if (m_rpgMakerSync && !deletingRpgMakerIds.isEmpty()) {
        QString syncError;
        if (!m_rpgMakerSync->deleteMaps(deletingRpgMakerIds, &syncError))
            QMessageBox::warning(this, tr("Sincronização RPG Maker"), syncError);
    }
    core::mapworkspace::forgetRemovedMaps(ed, deleting);
    normalizeMapWorkspace();
    emit ed.docsChanged();
    emit ed.layersChanged();
    emit ed.mapChanged();
    refreshMapTabs();
    if (const MapDoc* active = ed.doc()) restoreViewport(active->id);
    statusBar()->showMessage(m_rpgMakerSync && m_rpgMakerSync->hasPendingStructure()
        ? tr("Mapa removido do LUDO. A exclusão no RPG Maker está pendente até a próxima sincronização.")
        : tr("Mapa removido do projeto com política estrutural explícita."), 5000);
}

// ------------------------------------------------------------------ misc
void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        // Esc sai dos modos de marcacao/aleatorio, como no original.
        if (ed.session.starMarkMode || ed.session.collisionMarkMode || ed.session.regionMarkMode || ed.session.randomMode) {
            if (m_actStar) m_actStar->setChecked(false);
            if (m_actCollision) m_actCollision->setChecked(false);
            if (m_actRegion) m_actRegion->setChecked(false);
            if (m_actRandom) m_actRandom->setChecked(false);
            statusBar()->showMessage(tr("Modos especiais desligados."), 2500);
            e->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::rebuildWorkspaceMenu()
{
    if (!m_workspaceMenu) return;
    m_workspaceMenu->clear();
    const QString current = WorkspaceProfiles::currentProfileId();
    const auto profiles = WorkspaceProfiles::catalog();
    bool insertedCustomSeparator = false;
    for (const auto& profile : profiles) {
        if (!profile.builtIn && !insertedCustomSeparator) {
            m_workspaceMenu->addSeparator();
            insertedCustomSeparator = true;
        }
        QAction* action = m_workspaceMenu->addAction(profile.name);
        action->setToolTip(profile.description);
        action->setCheckable(true);
        action->setChecked(profile.id == current);
        connect(action, &QAction::triggered, this, [this, id = profile.id] {
            applyWorkspaceProfile(id);
        });
    }
    m_workspaceMenu->addSeparator();
    m_workspaceMenu->addAction(tr("Salvar espaço atual como…"), this, [this] {
        saveCurrentWorkspaceProfile();
    });

    bool hasCustom = false;
    for (const auto& profile : profiles) hasCustom |= !profile.builtIn;
    QAction* remove = m_workspaceMenu->addAction(tr("Remover perfil personalizado…"), this, [this] {
        removeWorkspaceProfile();
    });
    remove->setEnabled(hasCustom);
}

void MainWindow::applyWorkspaceProfile(const QString& id)
{
    if (!m_mapDock || !m_tilesetDock || !m_rightDock) return;

    if (!WorkspaceProfiles::isBuiltIn(id)) {
        const QByteArray state = WorkspaceProfiles::customProfileState(id);
        if (state.isEmpty() || !restoreState(state)) {
            QMessageBox::warning(this, tr("Espaço de trabalho"),
                                 tr("Não foi possível restaurar este perfil."));
            return;
        }
    } else {
        m_mapDock->show();
        m_tilesetDock->show();
        m_rightDock->show();
        addDockWidget(Qt::LeftDockWidgetArea, m_mapDock);
        addDockWidget(Qt::LeftDockWidgetArea, m_tilesetDock);
        addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

        if (id == QLatin1String("builtin/focus")) {
            m_mapDock->hide();
            m_tilesetDock->hide();
            m_rightDock->hide();
        } else {
            splitDockWidget(m_mapDock, m_tilesetDock, Qt::Vertical);
            if (id == QLatin1String("builtin/mapping")) {
                resizeDocks({m_tilesetDock, m_rightDock}, {330, 310}, Qt::Horizontal);
                resizeDocks({m_mapDock, m_tilesetDock}, {170, 720}, Qt::Vertical);
            } else {
                resizeDocks({m_tilesetDock, m_rightDock}, {300, 340}, Qt::Horizontal);
                resizeDocks({m_mapDock, m_tilesetDock}, {220, 650}, Qt::Vertical);
            }
        }
    }

    WorkspaceProfiles::setCurrentProfileId(id);
    rebuildWorkspaceMenu();
    statusBar()->showMessage(tr("Espaço de trabalho aplicado."), 2500);
}

void MainWindow::saveCurrentWorkspaceProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Salvar espaço de trabalho"),
                                                tr("Nome do perfil:"), QLineEdit::Normal,
                                                QString(), &ok).simplified();
    if (!ok || name.isEmpty()) return;
    QString error;
    const QString id = WorkspaceProfiles::saveCustomProfile(name, saveState(), &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Espaço de trabalho"), error);
        return;
    }
    WorkspaceProfiles::setCurrentProfileId(id);
    rebuildWorkspaceMenu();
    statusBar()->showMessage(tr("Perfil “%1” salvo neste computador.").arg(name), 3000);
}

void MainWindow::removeWorkspaceProfile()
{
    QVector<WorkspaceProfileDescriptor> custom;
    for (const auto& profile : WorkspaceProfiles::catalog())
        if (!profile.builtIn) custom.push_back(profile);
    if (custom.isEmpty()) return;

    QStringList names;
    for (const auto& profile : custom) names << profile.name;
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Remover perfil"),
                                                    tr("Perfil personalizado:"),
                                                    names, 0, false, &ok);
    if (!ok || selected.isEmpty()) return;

    for (const auto& profile : custom) {
        if (profile.name != selected) continue;
        if (QMessageBox::question(this, tr("Remover perfil"),
                                  tr("Remover o perfil “%1”?").arg(profile.name),
                                  QMessageBox::Yes | QMessageBox::Cancel,
                                  QMessageBox::Cancel) != QMessageBox::Yes) return;
        QString error;
        if (!WorkspaceProfiles::removeCustomProfile(profile.id, &error)) {
            QMessageBox::warning(this, tr("Remover perfil"), error);
            return;
        }
        rebuildWorkspaceMenu();
        statusBar()->showMessage(tr("Perfil removido."), 2500);
        return;
    }
}

void MainWindow::loadSettings()
{
    // Geometria/docking pertencem à janela. O restante das preferências locais
    // passa por EditorUiPreferences para existir uma única autoridade.
    QSettings windowSettings;
    restoreGeometry(windowSettings.value(QStringLiteral("geometry")).toByteArray());
    QByteArray workspaceState = WorkspaceProfiles::lastSessionState();
    if (workspaceState.isEmpty())
        workspaceState = windowSettings.value(QStringLiteral("windowState")).toByteArray(); // migração 4.0 RC
    if (!workspaceState.isEmpty()) restoreState(workspaceState);

    const EditorUiPreferences prefs = EditorUiPreferences::load();
    ed.session.showGrid = prefs.showGrid;
    ed.session.multigrid = prefs.multigrid;
    ed.session.gridColor = prefs.gridColor;
    ed.session.checkerboardBackground = prefs.checkerboardBackground;
    ed.session.checkerColorA = prefs.checkerColorA;
    ed.session.checkerColorB = prefs.checkerColorB;
    ed.session.ghostPreview = prefs.ghostPreview;
    ed.session.snapGridSize = prefs.snapGridSize;
    ed.session.focusDim = prefs.focusDim; EditorSessionStore::restore(ed);
    if (m_actGrid) m_actGrid->setChecked(ed.session.showGrid);
    if (m_actMultigrid) m_actMultigrid->setChecked(ed.session.multigrid);
    if (m_actGhost) m_actGhost->setChecked(ed.session.ghostPreview);
}

void MainWindow::saveSettings()
{
    EditorSessionStore::save(ed); QSettings windowSettings;
    windowSettings.setValue(QStringLiteral("geometry"), saveGeometry());
    WorkspaceProfiles::setLastSessionState(saveState());

    EditorUiPreferences prefs = EditorUiPreferences::load();
    prefs.showGrid = ed.session.showGrid;
    prefs.multigrid = ed.session.multigrid;
    prefs.gridColor = ed.session.gridColor;
    prefs.checkerboardBackground = ed.session.checkerboardBackground;
    prefs.checkerColorA = ed.session.checkerColorA;
    prefs.checkerColorB = ed.session.checkerColorB;
    prefs.ghostPreview = ed.session.ghostPreview;
    prefs.snapGridSize = ed.session.snapGridSize;
    prefs.focusDim = ed.session.focusDim;
    prefs.save();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave()) { e->ignore(); return; }
    if (m_teamServer && !m_teamServer->prepareForEditorClose()) { e->ignore(); return; }
    if (m_collaboration) m_collaboration->detachProject();
    saveSettings();
    e->accept();
}

} // namespace ui
