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
#include "Icons.h"
#include "LayerPanel.h"
#include "MapView.h"
#include "Minimap.h"
#include "PropertiesPanel.h"
#include "TilesetView.h"
#include "AutotilePaletteWidget.h"
#include "TilesetManagerDialog.h"
#include "TilesetSourceWatcher.h"

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

// ------------------------------------------------------------ shared actions
void MainWindow::buildActions()
{
    // Uma ação representa um comando. Menu, toolbar, paleta de ações e atalhos
    // reutilizam o mesmo QAction para não duplicar enable/visible/text/shortcut.
    m_actNewProject = new QAction(icons::get(QStringLiteral("new")), tr("Novo projeto de mapas"), this);
    m_actNewProject->setShortcut(QKeySequence(QKeySequence::New));
    m_actNewProject->setToolTip(tr("Novo projeto de mapas (Ctrl+N)"));
    connect(m_actNewProject, &QAction::triggered, this, [this] { newProject(); });

    m_actOpenProject = new QAction(icons::get(QStringLiteral("open")), tr("Abrir projeto…"), this);
    m_actOpenProject->setShortcut(QKeySequence(QKeySequence::Open));
    m_actOpenProject->setToolTip(tr("Abrir projeto (Ctrl+O)"));
    connect(m_actOpenProject, &QAction::triggered, this, [this] { openProject(); });

    m_actProjectManager = new QAction(tr("Início…"), this);
    m_actProjectManager->setIcon(icons::get(QStringLiteral("open")));
    m_actProjectManager->setProperty("paletteId", QStringLiteral("project.manager"));
    connect(m_actProjectManager, &QAction::triggered, this, [this] { openProjectManager(); });

    m_actSaveAll = new QAction(icons::get(QStringLiteral("save")), tr("Salvar e atualizar"), this);
    m_actSaveAll->setShortcut(QKeySequence(QKeySequence::Save));
    m_actSaveAll->setToolTip(tr("Salvar e sincronizar todos os mapas alterados (Ctrl+S)"));
    connect(m_actSaveAll, &QAction::triggered, this, [this] { saveCurrentMap(); });

    m_actSaveAs = new QAction(tr("Salvar contêiner do projeto como…"), this);
    m_actSaveAs->setShortcut(QKeySequence(QKeySequence::SaveAs));
    connect(m_actSaveAs, &QAction::triggered, this, [this] { saveProject(true); });

    m_actQuit = new QAction(tr("Sair"), this);
    m_actQuit->setShortcut(QKeySequence(QKeySequence::Quit));
    connect(m_actQuit, &QAction::triggered, this, [this] { close(); });

    m_actTilesetManager = new QAction(icons::get(QStringLiteral("tileset-manager")), tr("Gerenciador de Tilesets…"), this);
    m_actTilesetManager->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
    m_actTilesetManager->setToolTip(tr("Gerenciador de Tilesets (Ctrl+Alt+T)"));
    connect(m_actTilesetManager, &QAction::triggered, this, [this] { openTilesetManager(); });

    m_actZoomReset = new QAction(icons::get(QStringLiteral("one-to-one")), tr("Zoom 1:1"), this);
    m_actZoomReset->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    m_actZoomReset->setToolTip(tr("Zoom 1:1 — 100% (Ctrl+1)"));
    connect(m_actZoomReset, &QAction::triggered, this, [this] { if (m_view) m_view->zoomReset(); });

    m_actFitView = new QAction(icons::get(QStringLiteral("fit-view")), tr("Ajustar à janela"), this);
    m_actFitView->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    m_actFitView->setToolTip(tr("Ajustar mapa à janela (Ctrl+0)"));
    connect(m_actFitView, &QAction::triggered, this, [this] { if (m_view) m_view->fitToView(); });

    auto& moduleRegistry = modules::EditorModuleRegistry::instance();
    if (moduleRegistry.isEnabled(modules::EditorModuleId::RpgMakerIntegration)) {
        m_teamPublishRpgAction = new QAction(QIcon(QStringLiteral(":/ludo/icons/rpg-maker.png")),
                                             tr("Atualizar RPG Maker…"), this);
        connect(m_teamPublishRpgAction, &QAction::triggered, this,
                [this] { if (m_rpgMakerSync) m_rpgMakerSync->publishProject(m_collaboration); });

        m_rpgSyncAction = new QAction(icons::get(QStringLiteral("folder")), tr("Reparar vínculo RPG Maker…"), this);
        m_rpgSyncAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+S")));
        m_rpgSyncAction->setProperty("paletteId", QStringLiteral("rpgmaker.sync"));
        connect(m_rpgSyncAction, &QAction::triggered, this,
                [this] { if (m_rpgMakerSync) m_rpgMakerSync->synchronizeNow(true); });
    }
}

// ------------------------------------------------------------------- menus
void MainWindow::buildMenus()
{
    auto* team = menuBar()->addMenu(tr("Equipe"));
    team->setObjectName(QStringLiteral("teamMenu"));
    team->addAction(icons::get(QStringLiteral("team-online")), tr("Abrir Hub da equipe…"), this, [this] {
        if (m_teamServer) m_teamServer->showTeamHub();
    });
    team->addSeparator();
    team->addAction(tr("Pessoas neste projeto…"),m_collaboration,&CollaborationClient::people);
    team->addAction(tr("Histórico de versões…"),m_collaboration,&CollaborationClient::history);
    team->addSeparator();
    team->addAction(tr("Reparar sincronização…"),this,[this]{m_collaboration->synchronize();});
    team->addAction(tr("Configurações avançadas do servidor…"), this, [this] {
        if (m_teamServer) m_teamServer->showHostDialog();
    });
    team->addAction(tr("Sair da equipe"),m_collaboration,&CollaborationClient::disconnectServer);

    auto& moduleRegistry = modules::EditorModuleRegistry::instance();

    // ---- Arquivo ---------------------------------------------------------
    QMenu* file = menuBar()->addMenu(tr("&Arquivo"));
    file->addAction(m_actProjectManager);
    file->addSeparator();
    file->addAction(m_actSaveAll);
    QMenu* fileAdvanced=file->addMenu(tr("Avançado"));
    fileAdvanced->addAction(m_actNewProject);fileAdvanced->addAction(m_actOpenProject);
    fileAdvanced->addSeparator();fileAdvanced->addAction(m_actSaveAs);
    file->addSeparator();
    file->addAction(m_actQuit);

    // ---- Imagem ----------------------------------------------------------
    if (moduleRegistry.isEnabled(modules::EditorModuleId::ImageTools)) {
        QMenu* imageMenu = menuBar()->addMenu(tr("&Imagem"));
        imageMenu->addAction(tr("Exportar PNG…"), this, [this] { exportPng(); });
        imageMenu->addAction(tr("Imagem do tileset ativo…"), this, [this] {
            const Tileset* ts = ed.tilesetAt(ed.session.activeTilesetIdx);
            if (!ts) { statusBar()->showMessage(tr("Nenhum tileset ativo.")); return; }
            const QString path = QFileDialog::getSaveFileName(this, tr("Salvar imagem do tileset"),
                                                              ts->name + QStringLiteral(".png"), tr("PNG (*.png)"));
            if (path.isEmpty()) return;
            QString err;
            if (!io::exportTilesetImage(ed, ed.session.activeTilesetIdx, path, &err))
                QMessageBox::warning(this, tr("Erro"), err);
        });
    }

    // ---- Editar ----------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(tr("&Editar"));
    m_actUndo = edit->addAction(icons::get(QStringLiteral("undo")), tr("Desfazer"),
                                QKeySequence(QKeySequence::Undo), this, [this] { ed.undo(); });
    m_actRedo = edit->addAction(icons::get(QStringLiteral("redo")), tr("Refazer"),
                                QKeySequence(QKeySequence::Redo), this, [this] { ed.redo(); });
    edit->addSeparator();

    // Comandos básicos globais. Se o foco estiver em um campo de texto,
    // preservamos o comportamento nativo do Qt; nos demais contextos o
    // MapView decide o que Copiar/Colar/Apagar significa para Tile, Região,
    // Paint/Mask, Object ou Image Layer.
    auto textCopy = []() -> bool {
        QWidget* w = QApplication::focusWidget();
        if (auto* e = qobject_cast<QLineEdit*>(w)) { e->copy(); return true; }
        if (auto* e = qobject_cast<QTextEdit*>(w)) { e->copy(); return true; }
        if (auto* e = qobject_cast<QPlainTextEdit*>(w)) { e->copy(); return true; }
        return false;
    };
    auto textCut = []() -> bool {
        QWidget* w = QApplication::focusWidget();
        if (auto* e = qobject_cast<QLineEdit*>(w)) { e->cut(); return true; }
        if (auto* e = qobject_cast<QTextEdit*>(w)) { e->cut(); return true; }
        if (auto* e = qobject_cast<QPlainTextEdit*>(w)) { e->cut(); return true; }
        return false;
    };
    auto textPaste = []() -> bool {
        QWidget* w = QApplication::focusWidget();
        if (auto* e = qobject_cast<QLineEdit*>(w)) { e->paste(); return true; }
        if (auto* e = qobject_cast<QTextEdit*>(w)) { e->paste(); return true; }
        if (auto* e = qobject_cast<QPlainTextEdit*>(w)) { e->paste(); return true; }
        return false;
    };
    auto textSelectAll = []() -> bool {
        QWidget* w = QApplication::focusWidget();
        if (auto* e = qobject_cast<QLineEdit*>(w)) { e->selectAll(); return true; }
        if (auto* e = qobject_cast<QTextEdit*>(w)) { e->selectAll(); return true; }
        if (auto* e = qobject_cast<QPlainTextEdit*>(w)) { e->selectAll(); return true; }
        if (auto* view = qobject_cast<QAbstractItemView*>(w)) { view->selectAll(); return true; }
        return false;
    };
    auto textDelete = []() -> bool {
        QWidget* w = QApplication::focusWidget();
        if (auto* e = qobject_cast<QLineEdit*>(w)) { e->del(); return true; }
        if (auto* e = qobject_cast<QTextEdit*>(w)) {
            QTextCursor c = e->textCursor(); if (c.hasSelection()) c.removeSelectedText(); else c.deleteChar(); e->setTextCursor(c); return true;
        }
        if (auto* e = qobject_cast<QPlainTextEdit*>(w)) {
            QTextCursor c = e->textCursor(); if (c.hasSelection()) c.removeSelectedText(); else c.deleteChar(); e->setTextCursor(c); return true;
        }
        return false;
    };

    edit->addAction(tr("Selecionar tudo"), QKeySequence(QKeySequence::SelectAll), this, [this, textSelectAll] {
        if (!textSelectAll() && m_view) m_view->editSelectAll();
    });
    edit->addSeparator();
    edit->addAction(tr("Recortar"), QKeySequence(QKeySequence::Cut), this, [this, textCut] {
        if (!textCut() && m_view) m_view->editCut();
    });
    edit->addAction(tr("Copiar"), QKeySequence(QKeySequence::Copy), this, [this, textCopy] {
        if (!textCopy() && m_view) m_view->editCopy();
    });
    edit->addAction(tr("Colar"), QKeySequence(QKeySequence::Paste), this, [this, textPaste] {
        if (!textPaste() && m_view) m_view->editPaste();
    });
    edit->addAction(tr("Apagar seleção"), QKeySequence(QKeySequence::Delete), this, [this, textDelete] {
        if (!textDelete() && m_view) m_view->editDelete();
    });
    edit->addAction(tr("Limpar seleção (Esc)"), this, [this] {
        if (m_view) m_view->editClearSelection();
    });
    edit->addSeparator();
    edit->addAction(icons::get(QStringLiteral("preferences")), tr("Preferências do Editor…"), this, [this] {
        PreferencesDialog dlg(ed, menuBar(), this);
        if (dlg.exec() != QDialog::Accepted) return;
        updateSnapUi();
        updateToolStates();
        icons::refreshApplicationIcons(this);
        emit ed.layersChanged(); // reaplica também badges [AL]/[M] do QTreeWidget
        if (dlg.restartRequired())
            statusBar()->showMessage(tr("A escala da interface será aplicada ao reiniciar o Editor."), 7000);
        else
            statusBar()->showMessage(tr("Preferências do Editor atualizadas."), 3500);
    });
    edit->addAction(icons::get(QStringLiteral("modules")), tr("Módulos do Editor…"), this, [this] {
        ModuleManagerDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted && dlg.changed()) {
            QMessageBox::information(this, tr("Módulos atualizados"),
                                     tr("As alterações dos módulos serão aplicadas quando o LUDO Map Editor for reiniciado."));
        }
    });

    // ---- Mapa ------------------------------------------------------------
    QMenu* map = menuBar()->addMenu(tr("&Mapa"));
    map->addAction(icons::get(QStringLiteral("layer-add")), tr("Novo mapa"), QKeySequence(QStringLiteral("Ctrl+M")), this, [this] { newMapTab(); });
    map->addAction(icons::get(QStringLiteral("resize")), tr("Redimensionar mapa…"), this, [this] { ResizeMapDialog(ed, this).exec(); });
    map->addAction(icons::get(QStringLiteral("map-move")), tr("Mover conteúdo do mapa…"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), this, [this] {
        if (!ed.doc()) return;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Mover conteúdo do mapa"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* hint = new QLabel(tr("Desloque tiles, objetos, imagens e regiões pela grade-base, como o comando Shift do RPG Maker.\nConteúdo que sair dos limites é descartado nas camadas de tiles/Regiões."), &dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto* form = new QFormLayout;
        auto* dx = new QSpinBox(&dialog); dx->setRange(-999, 999); dx->setSuffix(tr(" tiles"));
        auto* dy = new QSpinBox(&dialog); dy->setRange(-999, 999); dy->setSuffix(tr(" tiles"));
        form->addRow(tr("Horizontal (+ direita)"), dx);
        form->addRow(tr("Vertical (+ baixo)"), dy);
        layout->addLayout(form);
        auto* arrows = new QHBoxLayout;
        auto* left = new QPushButton(tr("← 1"), &dialog);
        auto* right = new QPushButton(tr("1 →"), &dialog);
        auto* up = new QPushButton(tr("↑ 1"), &dialog);
        auto* down = new QPushButton(tr("1 ↓"), &dialog);
        arrows->addWidget(left); arrows->addWidget(right); arrows->addWidget(up); arrows->addWidget(down);
        layout->addLayout(arrows);
        connect(left, &QPushButton::clicked, &dialog, [dx] { dx->setValue(dx->value() - 1); });
        connect(right, &QPushButton::clicked, &dialog, [dx] { dx->setValue(dx->value() + 1); });
        connect(up, &QPushButton::clicked, &dialog, [dy] { dy->setValue(dy->value() - 1); });
        connect(down, &QPushButton::clicked, &dialog, [dy] { dy->setValue(dy->value() + 1); });
        auto* regions = new QCheckBox(tr("Mover Regiões RPG Maker junto"), &dialog);
        regions->setChecked(true);
        layout->addWidget(regions);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        if (dialog.exec() != QDialog::Accepted || (dx->value() == 0 && dy->value() == 0)) return;
        const DocSnapshot before = ed.snapshotDoc();
        QString error;
        if (!ed.shiftMapContents(dx->value(), dy->value(), regions->isChecked(), &error)) {
            QMessageBox::warning(this, tr("Mover conteúdo do mapa"), error);
            return;
        }
        ed.pushDocHistory(before, tr("Mover conteúdo do mapa (%1,%2)").arg(dx->value()).arg(dy->value()));
        statusBar()->showMessage(tr("Conteúdo movido %1 tile(s) em X e %2 em Y.").arg(dx->value()).arg(dy->value()), 4000);
    });
    map->addAction(icons::get(QStringLiteral("map-rename")), tr("Renomear mapa…"), this, [this] {
        MapDoc* d = ed.doc();
        if (!d) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Renomear mapa"), tr("Nome:"),
                                                   QLineEdit::Normal, d->name, &ok);
        if (ok && !name.trimmed().isEmpty()) {
            QString error;
            const QString mapId = d->id;
            if (!core::renameProjectSymbol(ed, core::ReferenceSymbolKind::Map, mapId, name, &error)) {
                QMessageBox::warning(this, tr("Renomear mapa"), error);
                return;
            }
            emit ed.docsChanged();
        }
    });

    // ---- Camada ----------------------------------------------------------
    QMenu* layer = menuBar()->addMenu(tr("&Camada"));
    layer->addAction(icons::get(QStringLiteral("layer-tile")), tr("Nova camada"), this, [this] { addTileLayer(0); });
    layer->addAction(icons::get(QStringLiteral("objectlayer")), tr("Camada de objetos"), this, [this] { addObjectLayer(); });
    // Camada de imagem agora faz parte do fluxo principal de camadas. O antigo
    // modulo opcional ImageTools continua controlando utilitarios como Exportar
    // PNG, mas nao pode esconder uma primitiva estrutural do mapa.
    layer->addAction(icons::get(QStringLiteral("layer-image")), tr("Camada de imagem…"), this, [this] { addImageLayer(false); });
    layer->addAction(icons::get(QStringLiteral("layer-paint")), tr("Camada de pintura"), this, [this] { addPaintLayer(); });
    layer->addAction(icons::get(QStringLiteral("layer-reference")), tr("Imagem de referência…"), this, [this] { addImageLayer(true); });
    layer->addAction(icons::get(QStringLiteral("group")), tr("Grupo"), this, [this] { addGroupLayer(); });
    layer->addSeparator();
    layer->addAction(icons::get(QStringLiteral("slope")), tr("Criar inclinação…"), this, [this] {
        const LayerPtr active = ed.selectedLayer();
        if (!active || (active->type != LayerType::Tile && active->type != LayerType::Image)) {
            QMessageBox::information(this, tr("Inclinação"),
                                     tr("Selecione uma Camada de tiles, imagem ou pintura."));
            return;
        }
        setTool(Tool::Slope);
        statusBar()->showMessage(tr("Inclinação: arraste a área, ajuste o resultado e escolha em qual Tileset salvar."), 6000);
    });
    layer->addSeparator();
    layer->addAction(icons::get(QStringLiteral("duplicate")), tr("Duplicar camada"), this, [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.duplicateLayer(l->id);
            ed.pushDocHistory(before, tr("Duplicar camada"));
        }
    });
    layer->addAction(icons::get(QStringLiteral("merge-down")), tr("Mesclar com a de baixo"), this, [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            auto needsBake = [](const LayerPtr& node) {
                return node && (node->isMask || !node->children.isEmpty() ||
                    ((node->type == LayerType::Tile || node->type == LayerType::Image) && !node->imageMask.isNull()));
            };
            QVector<LayerPtr>* parent = nullptr;
            int index = -1;
            ed.findNode(l->id, &parent, &index);
            const LayerPtr below = (parent && index > 0) ? parent->at(index - 1) : LayerPtr();
            const bool bake = needsBake(l) || needsBake(below);
            if (bake && QMessageBox::question(this, tr("Mesclar máscara com Bake"),
                    tr("A máscara e o resultado visual das duas camadas serão transformados em pixels dentro de uma única Camada de pintura.\n\n"
                       "Ctrl+Z restaura as camadas originais. Continuar?")) != QMessageBox::Yes)
                return;
            const DocSnapshot before = ed.snapshotDoc();
            QString error;
            if (!ed.mergeDown(l->id, &error)) {
                QMessageBox::information(this, tr("Mesclar camadas"), error);
                return;
            }
            ed.pushDocHistory(before, bake ? tr("Mesclar camadas com Bake") : tr("Mesclar camadas"));
            statusBar()->showMessage(bake ? tr("Bake concluído — máscara consolidada em uma Camada de pintura.")
                                          : tr("Camadas mescladas."), 4500);
        }
    });
    layer->addAction(tr("Bake da camada → Tileset…"), this, [this] {
        LayerPtr l = ed.selectedLayer();
        if (!l) {
            QMessageBox::information(this, tr("Bake para Tileset"), tr("Selecione uma camada ou grupo."));
            return;
        }
        QMessageBox choice(this);
        choice.setWindowTitle(tr("Bake para Tileset"));
        choice.setIcon(QMessageBox::Question);
        choice.setText(tr("A aparência atual da camada será consolidada em um novo Tileset e substituída por uma Camada de tiles.\n\n"
                          "Deseja deixar o Tileset visível na paleta principal?"));
        choice.setInformativeText(tr("Não mostrar mantém o recurso organizado no Gerenciador e ainda permite que a camada baked funcione normalmente."));
        auto* show = choice.addButton(tr("Bake e mostrar na paleta"), QMessageBox::AcceptRole);
        auto* hide = choice.addButton(tr("Bake sem mostrar"), QMessageBox::ActionRole);
        choice.addButton(QMessageBox::Cancel);
        choice.exec();
        if (choice.clickedButton() != show && choice.clickedButton() != hide) return;
        QString error;
        if (!ed.bakeLayerToTileset(l->id, choice.clickedButton() == show, &error)) {
            QMessageBox::warning(this, tr("Bake para Tileset"), error);
            return;
        }
        statusBar()->showMessage(choice.clickedButton() == show
            ? tr("Bake concluído — Tileset disponível na paleta.")
            : tr("Bake concluído — Tileset oculto da paleta e disponível no Gerenciador."), 6000);
    });
    layer->addAction(icons::get(QStringLiteral("remove")), tr("Excluir camada"), this, [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.removeLayer(l->id);
            ed.pushDocHistory(before, tr("Excluir camada"));
        }
    });

    // ---- Tileset ---------------------------------------------------------
    // O menu principal apenas encaminha para os mesmos fluxos centrais usados
    // pelo Gerenciador. Assim Ctrl+T, menu e janela nunca divergem.
    QMenu* tsMenu = menuBar()->addMenu(tr("&Tileset"));
    tsMenu->addAction(icons::get(QStringLiteral("tileset")), tr("Novo Tileset…"),
                      QKeySequence(QStringLiteral("Ctrl+T")), this, [this] { newTileset(); });
    tsMenu->addAction(m_actTilesetManager);
    tsMenu->addSeparator();

    QMenu* autotilesMenu = tsMenu->addMenu(icons::get(QStringLiteral("convert")), tr("Autotiles"));
    autotilesMenu->addAction(tr("Importar Autotile…"), this, [this] { openAutoTileConverter(); });
    autotilesMenu->addAction(tr("Importar A1 animado…"), this, [this] {
        QString createdId;
        if (!TilesetManagerDialog::runImportA1Flow(ed, this, &createdId)) return;
        pixmapCache().invalidate();
        refreshTilesetCombo();
        statusBar()->showMessage(tr("A1 importado para a biblioteca de Autotiles."), 5000);
    });

    QMenu* connectionModels = tsMenu->addMenu(tr("Modelos de conexão"));
    connectionModels->addAction(tr("Importar modelos…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar modelos de conexão"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty()) return;
        QString err; int count = 0;
        if (!io::importWangPresets(ed, path, &err, &count)) {
            QMessageBox::warning(this, tr("Não foi possível importar"), err);
        } else {
            if (!err.isEmpty()) QMessageBox::information(this, tr("Importado com avisos"), err);
            statusBar()->showMessage(tr("Modelos de conexão importados: %1.").arg(count), 6000);
        }
    });
    connectionModels->addAction(tr("Exportar modelos…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar modelos de conexão"),
                                                          QStringLiteral("wang-presets.json"), tr("JSON (*.json)"));
        if (path.isEmpty()) return;
        QString err; int count = 0;
        if (!io::exportWangPresets(ed, path, &err, &count))
            QMessageBox::warning(this, tr("Não foi possível exportar"), err);
        else statusBar()->showMessage(tr("Modelos de conexão exportados: %1 · destino: %2.").arg(count).arg(path), 6000);
    });

    // ---- RPG Maker MV / MZ ----------------------------------------------
    if (moduleRegistry.isEnabled(modules::EditorModuleId::RpgMakerIntegration)) {
        m_rpgMakerMenu = menuBar()->addMenu(tr("RPG Maker"));
        QMenu* rpgMakerMenu = m_rpgMakerMenu;
        if (moduleRegistry.isEnabled(modules::EditorModuleId::Regions)) {
            m_actRegion = rpgMakerMenu->addAction(icons::get(QStringLiteral("region")), tr("Regiões"));
            m_actRegion->setCheckable(true);
            m_actRegion->setChecked(ed.session.regionMarkMode);
            m_actRegion->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+R")));
            m_actRegion->setProperty("paletteId", QStringLiteral("rpgmaker.regions"));
            m_actRegion->setToolTip(tr("Pintar regiões. Use o ID 0 para apagar."));
            connect(m_actRegion, &QAction::toggled, this, [this](bool on) {
                ed.session.regionMarkMode = on;
                if (on) {
                    ed.session.authoringContext = AuthoringContext::Region;
                    if (m_actStar) m_actStar->setChecked(false);
                    if (m_actCollision) m_actCollision->setChecked(false);
                    if (m_actRandom) m_actRandom->setChecked(false);
                } else if (ed.session.authoringContext == AuthoringContext::Region) {
                    ed.session.authoringContext = AuthoringContext::Map;
                }
                if (m_regionSpinAction) m_regionSpinAction->setVisible(on);
                updateToolStates();
                emit ed.mapChanged();
            });
            rpgMakerMenu->addSeparator();
        }
        m_rpgReflectionAction = rpgMakerMenu->addAction(tr("Reflexos do mapa (avançado)…"), this, [this] { openReflectionSettings(); });
        m_rpgReflectionAction->setProperty("paletteId", QStringLiteral("rpgmaker.reflections"));
        m_rpgReflectionAction->setToolTip(tr("Configura quais regiões refletem, a qualidade dos reflexos, objetos refletidos e a integração com o clima do mapa atual."));
        rpgMakerMenu->addSeparator();
        m_rpgLinkAction = rpgMakerMenu->addAction(tr("Vincular projeto RPG Maker…"), this, [this] {
            if (m_rpgMakerSync) m_rpgMakerSync->linkProjectInteractive();
        });
        m_rpgLinkAction->setProperty("paletteId", QStringLiteral("rpgmaker.link"));
        if (m_rpgSyncAction) rpgMakerMenu->addAction(m_rpgSyncAction);
        rpgMakerMenu->addSeparator();
        if(m_teamPublishRpgAction)rpgMakerMenu->addAction(m_teamPublishRpgAction);
        m_rpgExportAction = rpgMakerMenu->addAction(tr("Exportação manual avançada…"), this, [this] { rpgMaker::run(ed, this); });
        m_rpgExportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+E")));
        m_rpgExportAction->setProperty("paletteId", QStringLiteral("rpgmaker.export"));
        rpgMakerMenu->addSeparator();
        m_rpgHelpAction = rpgMakerMenu->addAction(tr("Como funciona a integração…"), this, [this] {
            const QString engineName = core::rpgMakerEngineName(ed.rpgMakerEngine);
            QString details = tr("O LUDO e o %1 compartilham a mesma árvore de mapas por MapInfos.json. Novos mapas recebem Map ID automaticamente e Ctrl+S salva/sincroniza todos os mapas alterados.\n\n").arg(engineName);
            if (ed.rpgMakerEngine == core::RpgMakerEngine::MV)
                details += tr("No modo MV, recursos que exigem plugins LUDO extras ficam desativados. A exceção é o LudoMapSystem, que executa o cenário, colisões, prioridades e recursos exportados suportados pelo mapa.");
            else
                details += tr("Eventos, jogador, switches, variáveis, menus, áudio, saves e a lógica do jogo permanecem no RPG Maker selecionado.");
            QMessageBox::information(this, tr("Integração com %1").arg(engineName), details);
        });
    }

    // ---- Ver -------------------------------------------------------------
    QMenu* view = menuBar()->addMenu(tr("&Ver"));
    m_actGrid = view->addAction(tr("Grade"));
    m_actGrid->setCheckable(true);
    m_actGrid->setChecked(ed.session.showGrid);
    m_actGrid->setShortcut(QKeySequence(QStringLiteral("G")));
    connect(m_actGrid, &QAction::toggled, this, [this](bool on) { ed.session.showGrid = on; emit ed.mapChanged(); });
    m_actMultigrid = view->addAction(tr("Multigrade"));
    m_actMultigrid->setCheckable(true);
    connect(m_actMultigrid, &QAction::toggled, this, [this](bool on) { ed.session.multigrid = on; emit ed.mapChanged(); });
    auto* animations = view->addAction(tr("Animar autotiles na viewport"));
    animations->setCheckable(true); animations->setChecked(ed.session.animateAutotiles);
    connect(animations, &QAction::toggled, this, [this](bool on) { ed.session.animateAutotiles = on; m_view->update(); });
    m_actFocus = view->addAction(tr("Focar camada ativa"));
    m_actFocus->setCheckable(true);
    m_actFocus->setShortcutContext(Qt::WindowShortcut);
    m_actFocus->setToolTip(tr("Destacar a camada ou grupo selecionado e atenuar os demais (H)."));
    addAction(m_actFocus);
    m_actFocus->setShortcut(QKeySequence(QStringLiteral("H")));
    connect(m_actFocus, &QAction::toggled, this, [this](bool on) { ed.session.highlightCurrent = on; emit ed.mapChanged(); });
    view->addSeparator();
    view->addAction(tr("Aumentar zoom"), QKeySequence(QKeySequence::ZoomIn), this, [this] { m_view->zoomIn(); });
    view->addAction(tr("Diminuir zoom"), QKeySequence(QKeySequence::ZoomOut), this, [this] { m_view->zoomOut(); });
    view->addAction(m_actZoomReset);
    view->addAction(m_actFitView);
    view->addSeparator();
    m_workspaceMenu = view->addMenu(tr("Espaço de trabalho"));
    rebuildWorkspaceMenu();

    // ---- Ajuda -----------------------------------------------------------
    QMenu* help = menuBar()->addMenu(tr("A&juda"));
    help->addAction(tr("Atalhos principais"), QKeySequence(QKeySequence::HelpContents), this, [this] {
        QMessageBox::information(
            this, tr("Atalhos — LUDO Map Editor"),
            tr("B — Pincel\nE — Borracha\nU — Balde\nR — Retângulo\nC — Círculo\nL — Linha\nV — Seleção\n"
               "G — Grade\nCtrl+S — Salvar/sincronizar mapas alterados\nCtrl+1 — Zoom 1:1\nCtrl+0 — Ajustar mapa\nCtrl+Alt+R — Regiões RPG Maker\nCtrl+Alt+T — Tilesets\nCtrl+Alt+S — Sincronizar árvore RPG Maker\nCtrl+Alt+E — Exportação manual avançada"));
    });
    help->addAction(tr("Sobre"), this, [this] {
        QMessageBox::about(
            this, tr("Sobre"),
            tr("<h3>LUDO Map Editor %1</h3>"
               "<p>Editor visual de mapas para <b>RPG Maker MV e MZ</b>.</p>"
               "<p>O LUDO cuida do cenário, tilesets, camadas, colisões, prioridades, animações de tiles e exportação. "
               "A lógica do jogo permanece no RPG Maker escolhido.</p>")
                .arg(QString::fromLatin1(core::version::Editor)));
    });
    help->addAction(tr("Sobre o Qt"), this, [] { QApplication::aboutQt(); });
}

// ---------------------------------------------------------------- toolbars
void MainWindow::buildToolbars()
{
    auto& moduleRegistry = modules::EditorModuleRegistry::instance();

    auto* tools = addToolBar(tr("Ferramentas"));
    tools->setObjectName(QStringLiteral("toolsToolbar"));
    tools->setMovable(false);
    tools->setIconSize(QSize(icons::toolbarIconSize(), icons::toolbarIconSize()));
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);

    tools->addAction(m_actProjectManager);
    tools->addAction(m_actSaveAll);

    if (moduleRegistry.isEnabled(modules::EditorModuleId::RpgMakerIntegration)) {
        // Salvar e atualizar é o único comando normal da toolbar. Reparos e
        // revisão detalhada do RPG Maker ficam no menu avançado correspondente.
    }

    tools->addAction(m_actTilesetManager);
    tools->addSeparator();

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    struct ToolDef { Tool tool; const char* icon; const char* label; const char* key; const char* help; };
    static const ToolDef defs[] = {
        { Tool::Stamp,   "tile-brush",  "Pincel de tiles",      "B", "Pinta os tiles selecionados no mapa. Arraste para pintar uma área contínua." },
        { Tool::Eraser,  "eraser",      "Borracha",             "E", "Apaga o conteúdo onde você passar. Com Empilhar Tiles, remove apenas o que está por cima." },
        { Tool::Fill,    "fill",        "Preencher área",       "U", "Preenche de uma vez uma área conectada que possui o mesmo conteúdo." },
        { Tool::Rect,    "rect",        "Retângulo",            "R", "Desenhe uma área retangular usando os tiles ou a região selecionada." },
        { Tool::Circle,  "circle",      "Círculo / Elipse",     "C", "Desenhe círculos e elipses arrastando do início até o tamanho desejado." },
        { Tool::Line,    "line",        "Linha / Caminho",      "L", "Cria uma linha entre dois pontos. Útil para caminhos, bordas e marcações." },
        { Tool::Select,  "select",      "Selecionar e mover",   "V", "Selecione imagens, pinturas e objetos para mover, duplicar ou ajustar." },
        { Tool::Object,  "object",      "Colocar objeto",       "F", "Coloca a seleção como um objeto livre, sem ficar presa às células do mapa." },
        { Tool::Paint,   "paint-brush", "Pincel de pintura",    "D", "Pinte livremente em pixels. Ao editar uma máscara, branco mostra e preto esconde." },
        { Tool::Slope,   "slope",       "Criar inclinação",     "S", "Transforma a área selecionada em novas peças inclinadas e guarda o resultado em um Tileset de Inclinações." }
    };
    for (const ToolDef& d : defs) {
        auto* a = tools->addAction(icons::get(QString::fromUtf8(d.icon)), tr(d.label));
        a->setCheckable(true);
        a->setToolTip(tr("%1 (%2)\n%3")
                          .arg(QString::fromUtf8(d.label), QString::fromUtf8(d.key), tr(d.help)));
        a->setShortcut(QKeySequence(QString::fromUtf8(d.key)));
        a->setProperty("paletteId", QStringLiteral("tool.%1").arg(QString::fromUtf8(d.icon)));
        m_toolGroup->addAction(a);
        m_toolActions.insert(d.tool, a);
        connect(a, &QAction::triggered, this, [this, t = d.tool] { setTool(t); });
    }
    m_toolActions[Tool::Stamp]->setChecked(true);

    m_tileBrushSettingsAction = tools->addAction(icons::get(QStringLiteral("tile-brush-settings")), tr("Formato do pincel de tiles…"));
    m_tileBrushSettingsAction->setToolTip(tr("Escolha o formato, o tamanho e como o pincel distribui os tiles. Você também pode usar uma imagem para criar formatos irregulares."));
    m_tileBrushSettingsAction->setProperty("paletteId", QStringLiteral("tool.tileBrushSettings"));
    connect(m_tileBrushSettingsAction, &QAction::triggered, this, [this] { openBrushSettings(); });
    m_paintBrushSettingsAction = tools->addAction(icons::get(QStringLiteral("paint-brush-settings")), tr("Configurar pincel de pintura…"));
    m_paintBrushSettingsAction->setToolTip(tr("Escolha a aparência e o comportamento do pincel: tamanho, cor, suavidade da borda, quantidade de tinta, espaçamento e variações do traço."));
    m_paintBrushSettingsAction->setProperty("paletteId", QStringLiteral("tool.paintBrushSettings"));
    connect(m_paintBrushSettingsAction, &QAction::triggered, this, [this] { openPaintBrushSettings(); });

    tools->addSeparator();
    m_actUndo->setIcon(icons::get(QStringLiteral("undo")));
    m_actRedo->setIcon(icons::get(QStringLiteral("redo")));
    m_actUndo->setToolTip(tr("Desfazer (Ctrl+Z)"));
    m_actRedo->setToolTip(tr("Refazer (Ctrl+Shift+Z)"));
    tools->addAction(m_actUndo);
    tools->addAction(m_actRedo);

    auto* historyButton = new QToolButton(tools);
    historyButton->setIcon(icons::get(QStringLiteral("history")));
    historyButton->setText(tr("Histórico"));
    historyButton->setToolTip(tr("Histórico de alterações do mapa"));
    historyButton->setAccessibleName(tr("Histórico de alterações do mapa"));
    historyButton->setPopupMode(QToolButton::InstantPopup);
    auto* historyMenu = new QMenu(historyButton);
    historyButton->setMenu(historyMenu);
    connect(historyMenu, &QMenu::aboutToShow, this, [this, historyMenu] {
        historyMenu->clear();
        int current = -1;
        const QStringList labels = ed.historyLabels(&current);
        if (labels.isEmpty()) {
            QAction* empty = historyMenu->addAction(tr("Nenhuma alteração neste mapa"));
            empty->setEnabled(false);
            return;
        }
        for (int i = labels.size() - 1; i >= 0; --i) {
            QAction* action = historyMenu->addAction(labels.at(i));
            action->setCheckable(true);
            action->setChecked(i == current);
            connect(action, &QAction::triggered, this, [this, i] { ed.jumpHistory(i); });
        }
        historyMenu->addSeparator();
        QAction* initial = historyMenu->addAction(tr("Estado inicial"));
        initial->setCheckable(true);
        initial->setChecked(current < 0);
        connect(initial, &QAction::triggered, this, [this] { ed.jumpHistory(-1); });
    });
    tools->addWidget(historyButton);
    auto* spacer = new QWidget(tools);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(spacer);
    auto* teamButton = new QToolButton(tools);
    teamButton->setAccessibleName(tr("Equipe"));
    teamButton->setIconSize(QSize(icons::toolbarIconSize(), icons::toolbarIconSize()));
    teamButton->setIcon(icons::get(QStringLiteral("team-offline")));
    teamButton->setToolTip(tr("Equipe: desconectado"));
    teamButton->setPopupMode(QToolButton::InstantPopup);
    teamButton->setMenu(menuBar()->findChild<QMenu*>(QStringLiteral("teamMenu")));
    tools->addWidget(teamButton);
    connect(m_collaboration, &CollaborationClient::connectionStateChanged, teamButton,
            [teamButton](const QString& state) {
        teamButton->setIcon(icons::get(QStringLiteral("team-") + state));
    });
    connect(m_collaboration, &CollaborationClient::statusChanged, teamButton,
            [teamButton](const QString& text) { teamButton->setToolTip(text); });

    // ---- barra de opções do mapa ----------------------------------------
    addToolBarBreak();
    auto* opts = addToolBar(tr("Opções"));
    opts->setObjectName(QStringLiteral("optionsToolbar"));
    opts->setIconSize(QSize(icons::toolbarIconSize(), icons::toolbarIconSize()));
    opts->setToolButtonStyle(Qt::ToolButtonIconOnly);
    opts->setMovable(false);

    auto addToggle = [&](const QString& iconName, const QString& text, const QString& tip,
                         const QString& key, bool initial, std::function<void(bool)> fn) {
        auto* a = opts->addAction(icons::get(iconName), text);
        a->setCheckable(true);
        a->setChecked(initial);
        a->setToolTip(key.isEmpty() ? tip : tr("%1 (%2)").arg(tip, key));
        if (!key.isEmpty()) a->setShortcut(QKeySequence(key));
        connect(a, &QAction::toggled, this, [fn](bool on) { fn(on); });
        return a;
    };

    m_actFilled = addToggle(QStringLiteral("filled"), tr("Preencher"), tr("Formas preenchidas ou apenas contorno"),
                            QString(), true, [this](bool on) { ed.session.shapeFilled = on; });
    m_actGhost = addToggle(QStringLiteral("ghost"), tr("Prévia"), tr("Mostrar prévia da pintura"),
                           QStringLiteral(";"), true, [this](bool on) { ed.session.ghostPreview = on; emit ed.mapChanged(); });
    connect(m_actGhost, &QAction::toggled, this, [this](bool on) {
        m_actGhost->setIcon(icons::get(on ? QStringLiteral("ghost") : QStringLiteral("ghost-off")));
    });
    m_actOnTop = addToggle(QStringLiteral("ontop"), tr("Empilhar tiles"), tr("Coloca novos tiles por cima sem apagar o que já existe na mesma posição. Autotiles que ficaram embaixo mantêm exatamente a forma que já tinham."),
                           QStringLiteral("P"), false, [this](bool on) { ed.session.placeOnTop = on; });
    m_actRandom = addToggle(QStringLiteral("random"), tr("Aleatório"), tr("Pintar tiles aleatórios. Ajuste as opções no painel de propriedades."),
                            QStringLiteral("Q"), false, [this](bool on) { ed.session.randomMode = on; emit ed.selectionChanged(); });
    m_actSnap = addToggle(QStringLiteral("snap"), tr("Encaixar na grade"), tr("Ao mover objetos ou imagens, encaixa a posição nos espaços da grade para facilitar o alinhamento."),
                          QStringLiteral("Shift+S"), false, [this](bool on) {
                              ed.session.snapObjects = on;
                              if (m_snapComboAction) m_snapComboAction->setVisible(on);
                          });

    m_snapCombo = new QComboBox(opts);
    m_snapCombo->setEditable(true);
    m_snapCombo->setInsertPolicy(QComboBox::NoInsert);
    m_snapCombo->setFixedWidth(78);
    m_snapCombo->setToolTip(tr("Tamanho da grade em pixels."));
    for (int v : { 4, 8, 16, 24, 32, 48, 64 }) m_snapCombo->addItem(tr("%1 px").arg(v), v);
    m_snapComboAction = opts->addWidget(m_snapCombo);
    m_snapComboAction->setVisible(false);
    connect(m_snapCombo, &QComboBox::activated, this, [this](int i) {
        ed.session.snapGridSize = qMax(1, m_snapCombo->itemData(i).toInt());
        statusBar()->showMessage(tr("Encaixe da grade: %1 px").arg(ed.session.snapGridSize), 2500);
    });
    connect(m_snapCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const int v = m_snapCombo->currentText().remove(QRegularExpression(QStringLiteral("[^0-9]"))).toInt();
        if (v > 0) ed.session.snapGridSize = v;
        updateSnapUi();
    });

    const bool hasPriority = moduleRegistry.isEnabled(modules::EditorModuleId::Priority);
    const bool hasCollision = moduleRegistry.isEnabled(modules::EditorModuleId::Collision);
    if (hasPriority || hasCollision) opts->addSeparator();

    if (hasPriority) {
        m_actStar = addToggle(QStringLiteral("star"), tr("Modo Prioridade"),
                              tr("Define quais tiles podem aparecer na frente do personagem. Clique para alternar os níveis de 0 a 5."), QString(), false,
                              [this](bool on) {
                                  ed.session.starMarkMode = on;
                                  if (on && m_actCollision) m_actCollision->setChecked(false);
                                  if (on && m_actRegion) m_actRegion->setChecked(false);
                                  emit ed.mapChanged();
                              });
    }
    if (hasCollision) {
        m_actCollision = addToggle(QStringLiteral("collision"), tr("Colisão"),
                                   tr("Clique para liberar ou bloquear o tile. Ajuste os lados no gerenciador de tilesets."),
                                   QString(), false, [this](bool on) {
                                       ed.session.collisionMarkMode = on;
                                       if (on && m_actStar) m_actStar->setChecked(false);
                                       if (on && m_actRegion) m_actRegion->setChecked(false);
                                       emit ed.mapChanged();
                                   });
    }

    if (m_actRegion && moduleRegistry.isEnabled(modules::EditorModuleId::Regions)) {
        opts->addSeparator();
        opts->addAction(m_actRegion);
        m_regionSpin = new QSpinBox(opts);
        m_regionSpin->setRange(0, 255);
        m_regionSpin->setValue(qBound(0, ed.session.activeRegionId, 255));
        m_regionSpin->setPrefix(tr("R "));
        m_regionSpin->setFixedWidth(74);
        m_regionSpin->setToolTip(tr("ID da região. Use 0 para apagar ou clique direito para copiar um ID."));
        m_regionGradientAction = opts->addAction(tr("Degradê de região…"));
        connect(m_regionGradientAction, &QAction::triggered, this, [this] {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Degradê de região"));
            auto* layout = new QFormLayout(&dialog);
            auto* enabled = new QCheckBox(tr("Ativar degradê"), &dialog);
            enabled->setChecked(ed.session.regionGradient);
            auto* merge = new QCheckBox(tr("Mesclar"), &dialog);
            merge->setChecked(ed.session.regionGradientMerge);
            merge->setToolTip(tr("Une o traço às regiões vizinhas entre os IDs inicial e final."));
            merge->setEnabled(enabled->isChecked());
            connect(enabled, &QCheckBox::toggled, merge, &QCheckBox::setEnabled);
            auto* start = new QSpinBox(&dialog); start->setRange(1, 255); start->setValue(qMax(1, ed.session.activeRegionId));
            auto* end = new QSpinBox(&dialog); end->setRange(1, 255); end->setValue(ed.session.regionGradientEnd);
            auto* mode = new QComboBox(&dialog); mode->addItems({tr("Linear"), tr("Bordas ao centro")}); mode->setCurrentIndex(ed.session.regionGradientPingPong ? 1 : 0);
            auto* axis = new QComboBox(&dialog); axis->addItems({tr("Horizontal"), tr("Vertical")}); axis->setCurrentIndex(ed.session.regionGradientVertical ? 1 : 0);
            layout->addRow(enabled); layout->addRow(merge); layout->addRow(tr("ID inicial"), start); layout->addRow(tr("ID final"), end);
            layout->addRow(tr("Tipo"), mode); layout->addRow(tr("Direção (linear)"), axis);
            axis->setEnabled(mode->currentIndex() == 0);
            connect(mode, &QComboBox::currentIndexChanged, &dialog, [axis](int index) { axis->setEnabled(index == 0); });
            auto* hint = new QLabel(tr("O degradê acompanha a área pintada. No modo Bordas ao centro, os IDs avançam para dentro até o valor final."), &dialog); hint->setWordWrap(true); layout->addRow(hint);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted) return;
            ed.session.regionGradient = enabled->isChecked(); ed.session.regionGradientMerge = merge->isChecked(); ed.session.activeRegionId = start->value();
            ed.session.regionGradientEnd = end->value(); ed.session.regionGradientPingPong = mode->currentIndex() == 1;
            ed.session.regionGradientVertical = axis->currentIndex() == 1;
            emit ed.selectionChanged(); emit ed.mapChanged();
        });
        m_regionSpinAction = opts->addWidget(m_regionSpin);
        m_regionSpinAction->setVisible(ed.session.regionMarkMode);
        connect(m_regionSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            ed.session.activeRegionId = qBound(0, value, 255);
            ed.session.authoringContext = AuthoringContext::Region;
            if (!ed.session.regionMarkMode && m_actRegion) m_actRegion->setChecked(true);
            statusBar()->showMessage(value == 0
                ? tr("Região %1 0: apagar região.").arg(core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper())
                : tr("Região %1 selecionada: %2.").arg(core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper()).arg(value), 2500);
            updateToolStates();
            emit ed.mapChanged();
        });
    }

    opts->addSeparator();
    m_zoomCombo = new QComboBox(opts);
    m_zoomCombo->setToolTip(tr("Zoom do mapa"));
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->setFixedWidth(80);
    for (int pct : { 25, 50, 75, 100, 150, 200, 300, 400, 800 })
        m_zoomCombo->addItem(QStringLiteral("%1%").arg(pct), pct / 100.0);
    m_zoomCombo->setCurrentText(QStringLiteral("100%"));
    connect(m_zoomCombo, &QComboBox::activated, this, [this](int i) { m_view->setZoom(m_zoomCombo->itemData(i).toDouble()); });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const double pct = m_zoomCombo->currentText().remove(QLatin1Char('%')).trimmed().toDouble();
        if (pct > 0) m_view->setZoom(pct / 100.0);
    });
    opts->addWidget(m_zoomCombo);
    opts->addAction(m_actFitView);
    opts->addAction(m_actZoomReset);

    // ---- painel lateral contextual do Pincel -----------------------------
    // Inspirado no fluxo lateral do Blender: as opções ficam fora da toolbar
    // principal e o painel só aparece quando uma Camada de pintura ou uma
    // máscara raster está selecionada.
    m_paintBrushDock = new QDockWidget(tr("Pincel"), this);
    m_paintBrushDock->setObjectName(QStringLiteral("paintBrushDock"));
    m_paintBrushDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_paintBrushDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_paintBrushDock->setMinimumWidth(250);
    m_paintBrushDock->setMaximumWidth(420);

    auto* brushScroll = new QScrollArea(m_paintBrushDock);
    brushScroll->setWidgetResizable(true);
    brushScroll->setFrameShape(QFrame::NoFrame);
    auto* brushBody = new QWidget(brushScroll);
    auto* brushRoot = new QVBoxLayout(brushBody);
    brushRoot->setContentsMargins(8, 8, 8, 8);
    brushRoot->setSpacing(8);

    auto* basicBox = new QGroupBox(tr("Pincel"), brushBody);
    auto* basicForm = new QFormLayout(basicBox);
    basicForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_paintBrushCombo = new QComboBox(basicBox);
    m_paintBrushCombo->setMinimumWidth(150);
    m_paintBrushCombo->setIconSize(QSize(32, 32));
    m_paintBrushCombo->setToolTip(tr("Escolha um pincel das pastas Pinceis/Brushes/Brushs. Imagens adicionadas à pasta aparecem aqui automaticamente."));
    basicForm->addRow(tr("Pincel:"), m_paintBrushCombo);

    m_paintBrushSizeSpin = new QSpinBox(basicBox);
    m_paintBrushSizeSpin->setRange(1, 2048);
    m_paintBrushSizeSpin->setSuffix(tr(" px"));
    m_paintBrushSizeSpin->setToolTip(tr("Define o tamanho do pincel em pixels."));
    basicForm->addRow(tr("Tamanho:"), m_paintBrushSizeSpin);

    m_paintBrushOpacitySpin = new QSpinBox(basicBox);
    m_paintBrushOpacitySpin->setRange(1, 100);
    m_paintBrushOpacitySpin->setSuffix(QStringLiteral("%"));
    m_paintBrushOpacitySpin->setToolTip(tr("Limite máximo de transparência da pintura."));
    basicForm->addRow(tr("Opacidade:"), m_paintBrushOpacitySpin);

    m_paintBrushFlowSpin = new QSpinBox(basicBox);
    m_paintBrushFlowSpin->setRange(1, 100);
    m_paintBrushFlowSpin->setSuffix(QStringLiteral("%"));
    m_paintBrushFlowSpin->setToolTip(tr("Controla quanto da textura é aplicado em cada passada. Valores baixos permitem acumular sujeira, sangue ou textura aos poucos."));
    basicForm->addRow(tr("Quantidade por passada:"), m_paintBrushFlowSpin);

    m_paintBrushUseCombo = new QComboBox(basicBox);
    m_paintBrushUseCombo->addItem(tr("Usar como formato"), QStringLiteral("alpha"));
    m_paintBrushUseCombo->addItem(tr("Usar cores da imagem"), QStringLiteral("color"));
    m_paintBrushUseCombo->setToolTip(tr("Formato usa apenas a transparência da imagem e pinta com a cor escolhida. Cores da imagem mantém a textura original."));
    basicForm->addRow(tr("Uso da imagem:"), m_paintBrushUseCombo);

    m_paintBrushColorButton = new QToolButton(basicBox);
    m_paintBrushColorButton->setText(tr("Cor"));
    m_paintBrushColorButton->setToolTip(tr("Cor usada quando o pincel funciona como formato."));
    basicForm->addRow(tr("Cor:"), m_paintBrushColorButton);
    brushRoot->addWidget(basicBox);

    auto* edgeBox = new QGroupBox(tr("Mesclagem da textura"), brushBody);
    auto* edgeForm = new QFormLayout(edgeBox);
    m_paintBrushEdgeAction = new QAction(tr("Mesclar bordas"), this);
    m_paintBrushEdgeAction->setCheckable(true);
    m_paintBrushEdgeAction->setToolTip(tr("Mistura somente o contorno real da transparência com a superfície abaixo. O centro da textura não é desfocado."));
    auto* edgeEnabled = new QCheckBox(tr("Mesclar bordas"), edgeBox);
    edgeEnabled->setToolTip(m_paintBrushEdgeAction->toolTip());
    edgeForm->addRow(edgeEnabled);

    m_paintBrushEdgeSpin = new QSpinBox(edgeBox);
    m_paintBrushEdgeSpin->setRange(1, 50);
    m_paintBrushEdgeSpin->setSuffix(QStringLiteral("%"));
    m_paintBrushEdgeSpin->setToolTip(tr("Define quanto do contorno entra na transição. Entre 12% e 20% costuma funcionar bem para manchas e sujeira."));
    edgeForm->addRow(tr("Área da borda:"), m_paintBrushEdgeSpin);

    m_paintBrushEdgeStrengthSpin = new QSpinBox(edgeBox);
    m_paintBrushEdgeStrengthSpin->setRange(0, 100);
    m_paintBrushEdgeStrengthSpin->setSuffix(QStringLiteral("%"));
    m_paintBrushEdgeStrengthSpin->setToolTip(tr("Define quanto o contorno perde opacidade para se misturar com a superfície abaixo."));
    edgeForm->addRow(tr("Força:"), m_paintBrushEdgeStrengthSpin);

    m_paintBrushEdgeIrregularitySpin = new QSpinBox(edgeBox);
    m_paintBrushEdgeIrregularitySpin->setRange(0, 100);
    m_paintBrushEdgeIrregularitySpin->setSuffix(QStringLiteral("%"));
    m_paintBrushEdgeIrregularitySpin->setToolTip(tr("Quebra a transição uniforme para a borda parecer mais orgânica. Útil para sujeira, sangue, musgo e fuligem."));
    edgeForm->addRow(tr("Irregularidade:"), m_paintBrushEdgeIrregularitySpin);

    m_paintBrushPreserveCenterAction = new QAction(tr("Preservar centro"), this);
    m_paintBrushPreserveCenterAction->setCheckable(true);
    m_paintBrushPreserveCenterAction->setToolTip(tr("Mantém o miolo e os detalhes internos da textura intactos. Só a região próxima ao contorno é mesclada."));
    auto* preserveCenter = new QCheckBox(tr("Preservar centro da textura"), edgeBox);
    preserveCenter->setToolTip(m_paintBrushPreserveCenterAction->toolTip());
    edgeForm->addRow(preserveCenter);
    brushRoot->addWidget(edgeBox);

    auto* libraryBox = new QGroupBox(tr("Biblioteca de pincéis"), brushBody);
    auto* libraryLayout = new QHBoxLayout(libraryBox);
    m_paintBrushOpenFolderAction = new QAction(icons::get(QStringLiteral("folder")), tr("Abrir pasta"), this);
    m_paintBrushOpenFolderAction->setToolTip(tr("Abre a pasta Pinceis. Coloque PNG, WEBP, BMP ou JPG nela para adicionar novos pincéis."));
    auto* openFolderButton = new QToolButton(libraryBox);
    openFolderButton->setDefaultAction(m_paintBrushOpenFolderAction);
    openFolderButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    libraryLayout->addWidget(openFolderButton);

    m_paintBrushRefreshAction = new QAction(icons::get(QStringLiteral("refresh")), tr("Atualizar"), this);
    m_paintBrushRefreshAction->setToolTip(tr("Procura novamente por imagens nas pastas Pinceis, Brushes e Brushs."));
    auto* refreshBrushButton = new QToolButton(libraryBox);
    refreshBrushButton->setDefaultAction(m_paintBrushRefreshAction);
    refreshBrushButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    libraryLayout->addWidget(refreshBrushButton);
    libraryLayout->addStretch(1);
    brushRoot->addWidget(libraryBox);

    auto* moreBrushSettings = new QPushButton(icons::get(QStringLiteral("paint-brush-settings")), tr("Mais opções do pincel…"), brushBody);
    moreBrushSettings->setToolTip(tr("Abre espaçamento, rotação, dispersão, variações e outros ajustes avançados."));
    brushRoot->addWidget(moreBrushSettings);
    auto* brushHint = new QLabel(tr("Este painel aparece somente quando uma Camada de pintura ou uma máscara está selecionada."), brushBody);
    brushHint->setWordWrap(true);
    brushHint->setStyleSheet(QStringLiteral("color:#9aa3ad;"));
    brushRoot->addWidget(brushHint);
    brushRoot->addStretch(1);
    brushScroll->setWidget(brushBody);
    m_paintBrushDock->setWidget(brushScroll);
    addDockWidget(Qt::RightDockWidgetArea, m_paintBrushDock);

    auto saveBrushSession = [this] {
        EditorSessionStore::save(ed);
        if (m_view) m_view->update();
    };
    connect(m_paintBrushCombo, qOverload<int>(&QComboBox::activated), this, [this, saveBrushSession](int index) {
        const QString path = m_paintBrushCombo->itemData(index).toString();
        auto& brush = ed.session.rasterBrush;
        if (path.isEmpty()) {
            brush.tipMode = QStringLiteral("round");
            brush.tipImagePath.clear();
            brush.tipImage = QImage();
        } else {
            const QImage image = paint::loadRasterBrushTip(path);
            if (image.isNull()) {
                statusBar()->showMessage(tr("Não foi possível carregar este pincel."), 3500);
                refreshPaintBrushLibrary();
                return;
            }
            brush.tipImagePath = path;
            brush.tipImage = image;
            brush.tipMode = m_paintBrushUseCombo && m_paintBrushUseCombo->currentData().toString() == QLatin1String("color")
                ? QStringLiteral("color") : QStringLiteral("alpha");
        }
        saveBrushSession();
        syncPaintBrushToolbar();
    });
    connect(m_paintBrushSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.sizePx = value; saveBrushSession();
    });
    connect(m_paintBrushOpacitySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.opacity = value; saveBrushSession();
    });
    connect(m_paintBrushFlowSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.flow = value; saveBrushSession();
    });
    connect(m_paintBrushUseCombo, qOverload<int>(&QComboBox::activated), this, [this, saveBrushSession](int) {
        auto& brush = ed.session.rasterBrush;
        if (brush.tipImage.isNull()) return;
        brush.tipMode = m_paintBrushUseCombo->currentData().toString();
        saveBrushSession(); syncPaintBrushToolbar();
    });
    connect(m_paintBrushColorButton, &QToolButton::clicked, this, [this, saveBrushSession] {
        QColor color = QColorDialog::getColor(ed.session.rasterBrush.color, this, tr("Cor do pincel"), QColorDialog::ShowAlphaChannel);
        if (!color.isValid()) return;
        ed.session.rasterBrush.color = color;
        saveBrushSession(); syncPaintBrushToolbar();
    });
    connect(edgeEnabled, &QCheckBox::toggled, m_paintBrushEdgeAction, &QAction::setChecked);
    connect(m_paintBrushEdgeAction, &QAction::toggled, edgeEnabled, &QCheckBox::setChecked);
    connect(m_paintBrushEdgeAction, &QAction::enabledChanged, edgeEnabled, &QCheckBox::setEnabled);
    connect(m_paintBrushEdgeAction, &QAction::toggled, this, [this, saveBrushSession](bool enabled) {
        ed.session.rasterBrush.softenImageEdges = enabled;
        saveBrushSession(); syncPaintBrushToolbar();
    });
    connect(m_paintBrushEdgeSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.edgeSoftnessPercent = value;
        saveBrushSession();
    });
    connect(m_paintBrushEdgeStrengthSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.edgeSoftnessStrength = value;
        saveBrushSession();
    });
    connect(m_paintBrushEdgeIrregularitySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this, saveBrushSession](int value) {
        ed.session.rasterBrush.edgeIrregularityPercent = value;
        saveBrushSession();
    });
    connect(preserveCenter, &QCheckBox::toggled, m_paintBrushPreserveCenterAction, &QAction::setChecked);
    connect(m_paintBrushPreserveCenterAction, &QAction::toggled, preserveCenter, &QCheckBox::setChecked);
    connect(m_paintBrushPreserveCenterAction, &QAction::enabledChanged, preserveCenter, &QCheckBox::setEnabled);
    connect(m_paintBrushPreserveCenterAction, &QAction::toggled, this, [this, saveBrushSession](bool enabled) {
        ed.session.rasterBrush.preserveEdgeCenter = enabled;
        saveBrushSession();
    });
    connect(m_paintBrushOpenFolderAction, &QAction::triggered, this, [this] {
        const QString folder = primaryPaintBrushFolder();
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(m_paintBrushRefreshAction, &QAction::triggered, this, [this] { refreshPaintBrushLibrary(); });
    connect(moreBrushSettings, &QPushButton::clicked, this, [this] { openPaintBrushSettings(); });

    m_paintBrushWatcher = new QFileSystemWatcher(this);
    auto queueBrushRefresh = [this](const QString&) {
        QTimer::singleShot(120, this, [this] { refreshPaintBrushLibrary(); });
    };
    connect(m_paintBrushWatcher, &QFileSystemWatcher::directoryChanged, this, queueBrushRefresh);
    connect(m_paintBrushWatcher, &QFileSystemWatcher::fileChanged, this, queueBrushRefresh);
    refreshPaintBrushLibrary();
    m_paintBrushDock->hide();
}



} // namespace ui
