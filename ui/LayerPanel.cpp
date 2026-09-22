#include "LayerPanel.h"

#include "Icons.h"
#include "DepthSettingsDialog.h"
#include "LayerFiltersDialog.h"

#include "core/Renderer.h"
#include "core/LayerRasterFilters.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

using namespace core;

namespace ui {

static bool supportsRasterMask(const LayerPtr& layer)
{
    return layer && (layer->type == LayerType::Image || layer->type == LayerType::Tile);
}

static QSize rasterMaskSizeFor(const LayerPtr& layer)
{
    if (!layer) return QSize();
    if (layer->type == LayerType::Image) return layer->image.size();
    if (layer->type == LayerType::Tile)
        return QSize(qMax(1, layer->cols * layer->tileWidth), qMax(1, layer->rows * layer->tileHeight));
    return QSize();
}

static bool isEditingMask(const Editor& ed, const LayerPtr& layer)
{
    return supportsRasterMask(layer) && ed.session.selectedMaskLayerId == layer->id &&
           !layer->imageMask.isNull();
}

static const int kRoleId = Qt::UserRole + 1;
static constexpr int kColName = 0;
static constexpr int kColAlpha = 1;
static constexpr int kColMask = 2;
static constexpr int kColLock = 3;


// Localiza o pai estrutural de um nó. parentId vazio significa raiz.
// É usado pela ação “Mover para fora da pasta” sem expor detalhes da árvore
// no restante da UI.
static bool locateLayerParent(const QVector<LayerPtr>& nodes, const QString& targetId,
                              const QString& currentParentId, QString* parentIdOut,
                              int* indexOut)
{
    for (int i = 0; i < nodes.size(); ++i) {
        const LayerPtr& node = nodes[i];
        if (!node) continue;
        if (node->id == targetId) {
            if (parentIdOut) *parentIdOut = currentParentId;
            if (indexOut) *indexOut = i;
            return true;
        }
        if (locateLayerParent(node->children, targetId, node->id, parentIdOut, indexOut))
            return true;
    }
    return false;
}

LayerPanel::LayerPanel(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(4);
    m_tree->header()->setSectionResizeMode(kColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(kColAlpha, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kColMask, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kColLock, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(true);
    m_tree->setIconSize(QSize(28, 28));
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setToolTip(tr("Ctrl seleciona camadas separadas; Shift seleciona um intervalo. Alterações compatíveis do Inspector serão aplicadas a todas."));
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDragDropOverwriteMode(false);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    // A raiz também precisa aceitar drop explicitamente. Sem isso, dependendo
    // de onde o usuário solta uma camada que está dentro de uma pasta, o Qt
    // só oferece outros itens como destino e parece impossível “tirar” a camada.
    m_tree->invisibleRootItem()->setFlags(m_tree->invisibleRootItem()->flags() | Qt::ItemIsDropEnabled);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setUniformRowHeights(false);
    root->addWidget(m_tree, 1);

    // Barra de acoes rapidas do rodape (equivalente aos botoes do painel web).
    auto* bar = new QWidget(this);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(2, 0, 2, 2);
    h->setSpacing(3);
    auto addBtn = [&](const QString& iconName, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton(bar);
        b->setIcon(icons::get(iconName));
        b->setIconSize(QSize(24, 24));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [fn] { fn(); });
        h->addWidget(b);
        return b;
    };
    auto* addLayer = new QToolButton(bar);
    addLayer->setIcon(icons::get(QStringLiteral("layer-add")));
    addLayer->setIconSize(QSize(24, 24));
    addLayer->setToolTip(tr("Adicionar camada"));
    addLayer->setAccessibleName(tr("Adicionar camada"));
    addLayer->setAutoRaise(true);
    addLayer->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addLayer);
    addMenu->addAction(icons::get(QStringLiteral("layer-tile")), tr("Camada de tiles"),
                       this, [this] { emit requestAddLayer(0); });
    addMenu->addAction(icons::get(QStringLiteral("objectlayer")), tr("Camada de objetos"),
                       this, [this] { emit requestAddLayer(-1); });
    addMenu->addAction(icons::get(QStringLiteral("group")), tr("Grupo"),
                       this, [this] { emit requestAddLayer(-2); });
    addMenu->addAction(icons::get(QStringLiteral("layer-image")), tr("Camada de imagem"),
                       this, [this] { emit requestAddLayer(-3); });
    addMenu->addAction(icons::get(QStringLiteral("layer-paint")), tr("Camada de pintura"),
                       this, [this] { emit requestAddLayer(-5); });
    if(ed.rpgMakerEngine==core::RpgMakerEngine::MZ)
        addMenu->addAction(icons::get(QStringLiteral("mask")),tr("Camada de Reflexo"),
                           this,[this]{emit requestAddLayer(-6);});
    addMenu->addAction(icons::get(QStringLiteral("layer-reference")), tr("Imagem de referência"),
                       this, [this] { emit requestAddLayer(-4); });
    addLayer->setMenu(addMenu);
    h->addWidget(addLayer);
    addBtn(QStringLiteral("duplicate"),  tr("Duplicar camada"), [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.duplicateLayer(l->id);
            ed.pushDocHistory(before, tr("Duplicar camada"));
        }
    });
    addBtn(QStringLiteral("up"), tr("Mover para cima"), [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.moveLayer(l->id, 1);
            ed.pushDocHistory(before, tr("Reordenar camada"));
        }
    });
    addBtn(QStringLiteral("down"), tr("Mover para baixo"), [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.moveLayer(l->id, -1);
            ed.pushDocHistory(before, tr("Reordenar camada"));
        }
    });
    addBtn(QStringLiteral("layer-remove"), tr("Excluir camada"), [this] {
        if (LayerPtr l = ed.selectedLayer()) {
            const DocSnapshot before = ed.snapshotDoc();
            ed.removeLayer(l->id);
            ed.pushDocHistory(before, tr("Excluir camada"));
        }
    });
    h->addStretch(1);
    root->addWidget(bar);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &LayerPanel::onItemSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayerPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (m_updating) return;
        LayerPtr layer = layerOf(item);
        if (!layer) return;
        ed.session.authoringContext = AuthoringContext::Layer;
        QSet<QString> ids;
        for (QTreeWidgetItem* selectedItem : m_tree->selectedItems())
            ids.insert(selectedItem->data(kColName, kRoleId).toString());
        ed.setSelectedLayerIds(ids, layer->id);

        if (column == kColLock) {
            const DocSnapshot before = ed.snapshotDoc();
            layer->locked = !layer->locked;
            ed.pushDocHistory(before, layer->locked ? tr("Bloquear camada") : tr("Desbloquear camada"));
            emit ed.layersChanged();
            emit statusMessage(layer->locked ? tr("Camada bloqueada.") : tr("Camada desbloqueada."));
            return;
        }
        if (column == kColAlpha && supportsRasterMask(layer)) {
            const DocSnapshot before = ed.snapshotDoc();
            layer->alphaLock = !layer->alphaLock;
            ed.pushDocHistory(before, layer->alphaLock ? tr("Proteger áreas vazias") : tr("Permitir pintar nas áreas vazias"));
            emit ed.layersChanged(); emit ed.selectionChanged(); emit ed.mapChanged();
            emit statusMessage(layer->alphaLock ? tr("Áreas vazias protegidas. O pincel só altera onde já existe conteúdo.") : tr("Áreas vazias liberadas. O pincel pode criar conteúdo em qualquer parte da camada."));
            return;
        }
        if (column == kColMask && !layer->imageMask.isNull() && supportsRasterMask(layer)) {
            const bool entering = !isEditingMask(ed, layer);
            ed.session.selectedMaskLayerId = entering ? layer->id : QString();
            if (entering) ed.session.tool = Tool::Paint;
            else if (layer->type == LayerType::Image && !layer->imagePaintLayer) ed.session.tool = Tool::Select;
            else if (layer->type == LayerType::Tile) ed.session.tool = Tool::Stamp;
            emit ed.layersChanged(); emit ed.selectionChanged(); emit ed.mapChanged();
            emit statusMessage(entering ? tr("Editando máscara — branco revela; preto ou Borracha escondem.")
                                        : tr("Edição da máscara encerrada."));
            return;
        }
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &LayerPanel::onContextMenu);
    connect(m_tree->model(), &QAbstractItemModel::rowsMoved, this, &LayerPanel::onRowsMoved);

    // Nunca reconstruimos a arvore de forma sincrona a partir de um signal do
    // proprio QTreeWidget. Um toggle de visibilidade/lock pode emitir
    // layersChanged enquanto Qt ainda processa o item; limpar a arvore nesse
    // momento invalida o item em uso e pode travar/derrubar o editor.
    connect(&ed, &Editor::layersChanged, this, &LayerPanel::scheduleRefresh);
    connect(&ed, &Editor::docsChanged,   this, &LayerPanel::scheduleRefresh);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        setCollapsedState(item, false);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        setCollapsedState(item, true);
    });
    refresh();
}

void LayerPanel::scheduleRefresh()
{
    // Nunca descarte um refresh estrutural. A versão anterior retornava quando
    // m_updating=true; se uma exclusão/undo chegasse nesse intervalo, a árvore
    // podia ficar com itens órfãos até outra operação. O refresh é coalescido e
    // re-agendado até o QTreeWidget sair da própria pilha de atualização.
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshQueued = false;
        if (m_updating) { scheduleRefresh(); return; }
        refresh();
    });
}

void LayerPanel::setCollapsedState(QTreeWidgetItem* item, bool collapsed)
{
    if (m_updating || !item) return;
    const LayerPtr layer = layerOf(item);
    if (!layer || layer->collapsed == collapsed) return;
    // E estado de apresentacao da arvore, nao uma operacao no conteudo do mapa:
    // atualizamos o modelo sem criar historico/dirty a cada abre-fecha.
    layer->collapsed = collapsed;
}

QIcon LayerPanel::thumbnailFor(const LayerPtr& l) const
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QColor(30, 30, 30, 160));
    if (l->type == LayerType::Tile) {
        // Miniatura: amostra ate 6x6 celulas da camada.
        const int step = qMax(1, qMax(l->cols, l->rows) / 6);
        int drawn = 0;
        for (int y = 0; y < l->rows && drawn < 36; y += step)
            for (int x = 0; x < l->cols && drawn < 36; x += step) {
                const TileRef t = l->topAt(x, y);
                if (!t.isValid()) continue;
                const Tileset* ts = ed.tilesetAt(t.tilesetIdx);
                if (!ts) continue;
                const int cx = (x / step) * 5, cy = (y / step) * 5;
                p.drawPixmap(QRect(cx % 28, cy % 28, 5, 5),
                             pixmapCache().pixmap(ed, t.tilesetIdx), ts->tileRect(t.tx, t.ty));
                ++drawn;
            }
        if (!drawn) {
            p.setPen(QColor("#666"));
            p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("▦"));
        }
    } else if (l->type == LayerType::Object) {
        p.setPen(QColor("#4a90d7"));
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("🔷"));
    } else if (l->type == LayerType::Image) {
        if (!l->image.isNull()) p.drawImage(pm.rect(), l->image);
        else { p.setPen(QColor("#aaa")); p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("🖼")); }
    } else {
        p.setPen(QColor("#ddd"));
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("📁"));
    }
    if (l->uiColor.isValid()) {
        p.fillRect(QRect(0, 24, 28, 4), l->uiColor);
    }
    return QIcon(pm);
}

void LayerPanel::buildItems(QTreeWidgetItem* parent, const QVector<LayerPtr>& nodes)
{
    // A lista e exibida de cima para baixo = ordem inversa de renderizacao
    // (a ultima camada do vetor e a que fica por cima), igual ao original.
    for (int i = nodes.size() - 1; i >= 0; --i) {
        const LayerPtr& l = nodes[i];
        if (!l) continue;
        auto* item = new QTreeWidgetItem;
        // O texto editavel contem somente o nome real. Antes os badges/tamanho
        // eram concatenados ao nome e depois removidos por parsing; nomes com
        // dois espacos ou simbolos podiam ser truncados ao renomear.
        item->setText(kColName, l->name);
        item->setData(kColName, kRoleId, l->id);
        // Badges interativos: [AL] alterna Alpha Lock; [M] entra/sai da edição
        // de máscara. Os ícones podem ser substituídos no Editor de Ícones.
        item->setCheckState(kColName, l->visible ? Qt::Checked : Qt::Unchecked);
        if (supportsRasterMask(l) && l->alphaLock) {
            item->setText(kColAlpha, QStringLiteral("[AL]"));
            item->setIcon(kColAlpha, icons::get(QStringLiteral("alpha-lock")));
            item->setToolTip(kColAlpha, tr("Áreas vazias protegidas — clique para permitir pintura fora do conteúdo"));
        } else if (supportsRasterMask(l)) {
            item->setToolTip(kColAlpha, tr("Pintura livre — clique para proteger as áreas vazias"));
        }
        if (supportsRasterMask(l) && !l->imageMask.isNull()) {
            const bool editing = isEditingMask(ed, l);
            item->setText(kColMask, editing ? QStringLiteral("[M*]") : QStringLiteral("[M]"));
            item->setIcon(kColMask, icons::get(editing ? QStringLiteral("mask-edit") : QStringLiteral("mask")));
            item->setToolTip(kColMask, editing ? tr("Editando máscara — clique para voltar ao conteúdo")
                                                : tr("Máscara disponível — clique para escolher onde a camada aparece"));
            if (!l->imageMaskEnabled) item->setForeground(kColMask, QColor("#777"));
        } else if(parent){
            const LayerPtr parentLayer=layerOf(parent);
            if(parentLayer&&parentLayer->isMask){
                item->setText(kColMask,QStringLiteral("[C]"));
                item->setIcon(kColMask,icons::get(QStringLiteral("mask")));
                item->setToolTip(kColMask,tr("Esta camada está dentro da Clipping Mask “%1”.").arg(parentLayer->name));
            }
        }
        item->setIcon(kColLock, icons::get(l->locked ? QStringLiteral("lock") : QStringLiteral("unlock")));
        item->setToolTip(kColLock, l->locked ? tr("Clique para desbloquear a camada")
                                             : tr("Clique para bloquear a camada"));
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
                       Qt::ItemIsDragEnabled);
        if (l->isContainer()) item->setFlags(item->flags() | Qt::ItemIsDropEnabled);
        else item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        QString tip = tr("%1 — opacidade %2%").arg(l->name).arg(int(l->opacity * 100));
        if (l->type == LayerType::Tile) {
            tip += tr(" · tiles %1×%2").arg(l->tileWidth).arg(l->tileHeight);
            if (!l->imageMask.isNull()) tip += l->imageMaskEnabled ? tr(" · máscara ativa") : tr(" · máscara desligada");
        }
        if (l->type == LayerType::Image) {
            if (l->reflectionLayer) tip += tr(" · superfície de reflexo MZ");
            else if (l->imagePaintLayer) tip += tr(" · pintura");
            else tip += l->imageReferenceOnly ? tr(" · referência (não exporta)") : tr(" · imagem exportável");
            if(l->parallaxLayer)tip+=tr(" · fundo com profundidade");
            if (!l->imageMask.isNull()) tip += l->imageMaskEnabled ? tr(" · máscara ativa") : tr(" · máscara desativada");
            if (isEditingMask(ed, l)) tip += tr(" · editando máscara");
        }
        if (l->type == LayerType::Group) {
            tip += tr(" · %1 camada(s) dentro").arg(l->children.size());
            if (!l->imageFilters.isEmpty()) tip += tr(" · sombra de contato no grupo");
        }
        if (l->isMask) tip += tr(" · usada como recorte");
        if (l->alphaLock) tip += tr(" · áreas vazias protegidas");
        if (!l->imageFilters.isEmpty()) tip += tr(" · %1 filtro(s) na camada").arg(l->imageFilters.size());
        if (!l->maskFilters.isEmpty()) tip += tr(" · %1 efeito(s) na máscara").arg(l->maskFilters.size());
        tip += tr(" · nível %1").arg(l->depthLevel);
        if (l->zMode == QLatin1String("above")) tip += tr(" · acima do jogador");
        if (l->locked) tip += tr(" · bloqueada");
        item->setToolTip(0, tip);
        if (l->locked) item->setForeground(0, QColor("#888"));
        item->setTextAlignment(kColAlpha, Qt::AlignCenter);
        item->setTextAlignment(kColMask, Qt::AlignCenter);
        item->setTextAlignment(kColLock, Qt::AlignCenter);
        if (parent) parent->addChild(item); else m_tree->addTopLevelItem(item);
        if (!l->children.isEmpty()) {
            buildItems(item, l->children);
            item->setExpanded(!l->collapsed);
        }
    }
}

void LayerPanel::refresh()
{
    if (m_updating) return;
    m_refreshQueued = false;
    m_updating = true;
    const QSignalBlocker blocker(m_tree);
    const LayerPtr selected = ed.selectedLayer();
    QSet<QString> selectedIds = ed.session.selectedLayerIds;
    if (selected) selectedIds.insert(selected->id);
    m_tree->clear();
    buildItems(nullptr, ed.layers());
    if (!selectedIds.isEmpty()) {
        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            const QString id = (*it)->data(0, kRoleId).toString();
            if (selectedIds.contains(id)) {
                (*it)->setSelected(true);
                if (selected && id == selected->id) m_tree->setCurrentItem(*it, kColName,
                    QItemSelectionModel::NoUpdate);
            }
            ++it;
        }
    }
    m_updating = false;
}

LayerPtr LayerPanel::layerOf(QTreeWidgetItem* item) const
{
    if (!item) return LayerPtr();
    return static_cast<const Editor&>(ed).findNode(item->data(0, kRoleId).toString());
}

void LayerPanel::onItemSelectionChanged()
{
    if (m_updating) return;
    if (QTreeWidgetItem* item = m_tree->currentItem()) {
        const QString id = item->data(0, kRoleId).toString();
        QSet<QString> ids;
        for (QTreeWidgetItem* selectedItem : m_tree->selectedItems())
            ids.insert(selectedItem->data(kColName, kRoleId).toString());
        if (ids.isEmpty()) ids.insert(id);
        m_updating = true;
        ed.session.authoringContext = AuthoringContext::Layer;
        ed.setSelectedLayerIds(ids, id);
        m_updating = false;
    }
}

void LayerPanel::onItemChanged(QTreeWidgetItem* item, int)
{
    if (m_updating || !item) return;
    LayerPtr l = layerOf(item);
    if (!l) return;
    const DocSnapshot before = ed.snapshotDoc();
    QStringList changes;

    const bool vis = item->checkState(0) == Qt::Checked;
    if (vis != l->visible) {
        l->visible = vis;
        changes.push_back(vis ? tr("Mostrar camada") : tr("Ocultar camada"));
    }

    const QString text = item->text(0).trimmed();
    if (!text.isEmpty() && text != l->name) {
        const QString oldName = l->name;
        l->name = text;
        changes.push_back(tr("Renomear “%1” para “%2”").arg(oldName, text));
    }

    if (!changes.isEmpty()) {
        ed.session.authoringContext = AuthoringContext::Layer;
        ed.pushDocHistory(before, changes.join(QStringLiteral(" · ")));
        emit ed.mapChanged();
        emit ed.layersChanged();
    }
}

void LayerPanel::collectPlacements(QTreeWidgetItem* item, const QString& parentId,
                                   QVector<LayerTreePlacement>& out) const
{
    const int n = item ? item->childCount() : m_tree->topLevelItemCount();
    for (int visualIndex = 0; visualIndex < n; ++visualIndex) {
        QTreeWidgetItem* child = item ? item->child(visualIndex) : m_tree->topLevelItem(visualIndex);
        if (!child) continue;
        const QString id = child->data(0, kRoleId).toString();
        // A arvore mostra topo -> fundo; o modelo/render usa fundo -> topo.
        out.push_back(LayerTreePlacement{id, parentId, n - 1 - visualIndex});
        collectPlacements(child, id, out);
    }
}

void LayerPanel::rebuildModelFromTree()
{
    if (!ed.doc()) return;
    const DocSnapshot before = ed.snapshotDoc();
    QVector<LayerTreePlacement> placements;
    collectPlacements(nullptr, QString(), placements);
    QString error;
    if (!ed.applyLayerTreeOrder(placements, &error)) {
        emit statusMessage(tr("A reorganização foi cancelada: %1").arg(error));
        return;
    }
    ed.pushDocHistory(before, tr("Reorganizar camadas"));
}

void LayerPanel::onRowsMoved()
{
    if (m_updating || m_reorderQueued) return;
    // rowsMoved e emitido enquanto o model do QTreeWidget ainda finaliza a
    // movimentacao interna. Alterar o Core e reconstruir a arvore dentro desse
    // mesmo stack era outra fonte de travamentos. Fazemos a transacao no proximo
    // ciclo do event loop, quando a hierarquia visual ja esta estavel.
    m_reorderQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_reorderQueued = false;
        if (m_updating) { scheduleRefresh(); return; }
        m_updating = true;
        rebuildModelFromTree();
        m_updating = false;
        scheduleRefresh();
    });
}

void LayerPanel::onContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    LayerPtr l = layerOf(item);
    if (!l) return;
    m_updating = true;
    ed.session.authoringContext = AuthoringContext::Layer;
    ed.setSelectedLayerById(l->id);
    m_updating = false;

    QMenu menu(this);
    // Pasta não é Tile Layer: opções de altura pertencem a conteúdo do mapa,
    // não ao contêiner organizacional.
    if (l->type != LayerType::Group) {
        auto* levels = menu.addMenu(tr("Nível de altura"));
        for (int level = 0; level < 2; ++level) {
            auto* action = levels->addAction(level == 0 ? tr("0 — Chão inferior") : tr("1 — Ponte / elevado"));
            action->setCheckable(true); action->setChecked(l->depthLevel == level);
            connect(action, &QAction::triggered, this, [this, l, level] {
                const auto before = ed.snapshotDoc();
                std::function<void(const LayerPtr&)> apply = [&](const LayerPtr& layer) {
                    layer->depthLevel = level; for (const auto& child : layer->children) apply(child);
                };
                apply(l); ed.pushDocHistory(before, tr("Alterar nível de altura"));
                emit ed.layersChanged(); emit ed.mapChanged();
            });
        }
        menu.addAction(tr("Níveis e escadas do mapa…"), [this] { editMapDepth(ed, this); });
        menu.addSeparator();
    }
    menu.addAction(tr("Renomear"), [this, item] { m_tree->editItem(item, 0); });
    menu.addAction(l->locked ? tr("Desbloquear") : tr("Bloquear"), [this, l] {
        const DocSnapshot before = ed.snapshotDoc();
        l->locked = !l->locked;
        ed.pushDocHistory(before, l->locked ? tr("Bloquear camada") : tr("Desbloquear camada"));
        emit ed.layersChanged();
        emit statusMessage(l->locked ? tr("Camada bloqueada.") : tr("Camada desbloqueada."));
    });
    menu.addAction(tr("Cor na lista…"), [this, l] {
        const QColor c = QColorDialog::getColor(l->uiColor.isValid() ? l->uiColor : QColor("#4a90d7"),
                                                this, tr("Cor da camada"));
        if (c.isValid()) {
            const DocSnapshot before = ed.snapshotDoc();
            l->uiColor = c;
            ed.pushDocHistory(before, tr("Alterar cor da camada"));
            emit ed.layersChanged();
        }
    });
    menu.addSeparator();
    menu.addAction(tr("Duplicar"), [this, l] {
        const DocSnapshot before = ed.snapshotDoc();
        ed.duplicateLayer(l->id);
        ed.pushDocHistory(before, tr("Duplicar camada"));
    });
    auto needsBakeNode = [](const LayerPtr& node) {
        return node && (node->isMask || !node->children.isEmpty() ||
            ((node->type == LayerType::Tile || node->type == LayerType::Image) && !node->imageMask.isNull()));
    };
    QVector<LayerPtr>* mergeParent = nullptr;
    int mergeIndex = -1;
    ed.findNode(l->id, &mergeParent, &mergeIndex);
    const LayerPtr mergeBelow = (mergeParent && mergeIndex > 0) ? mergeParent->at(mergeIndex - 1) : LayerPtr();
    const bool mergeNeedsBake = needsBakeNode(l) || needsBakeNode(mergeBelow);
    if (l->type == LayerType::Tile || l->type == LayerType::Object ||
        (l->type == LayerType::Image && mergeNeedsBake)) {
        menu.addAction(mergeNeedsBake ? tr("Mesclar com a de baixo (Bake)")
                                      : tr("Mesclar com a de baixo"), [this, l, mergeNeedsBake] {
            if (mergeNeedsBake && QMessageBox::question(this, tr("Mesclar máscara com Bake"),
                    tr("A aparência atual da máscara, filtros e recortes será transformada em pixels e as duas camadas virarão uma única Camada de pintura.\n\n"
                       "Isso preserva o resultado visual, mas a máscara deixa de ser editável separadamente. Ctrl+Z restaura as camadas originais.\n\nContinuar?"))
                    != QMessageBox::Yes)
                return;
            const DocSnapshot before = ed.snapshotDoc();
            QString error;
            if (!ed.mergeDown(l->id, &error)) {
                QMessageBox::information(this, tr("Mesclar camadas"), error);
                return;
            }
            ed.pushDocHistory(before, mergeNeedsBake ? tr("Mesclar camadas com Bake") : tr("Mesclar camadas"));
            emit statusMessage(mergeNeedsBake ? tr("Bake concluído — máscara e camadas foram consolidadas em uma Camada de pintura.")
                                              : tr("Camadas mescladas."));
        });
    }

    // Bake é uma ação geral de consolidação, não apenas de Tile/Object Layer.
    // Grupos, Image/Paint Layers e camadas com máscara usam o mesmo compositor
    // e podem gerar um Tileset reutilizável ou oculto da paleta principal.
    menu.addAction(tr("Fazer Bake para Tileset…"), [this, l] {
        QMessageBox choice(this);
        choice.setWindowTitle(tr("Bake para Tileset"));
        choice.setIcon(QMessageBox::Question);
        choice.setText(tr("A aparência atual desta camada será consolidada e transformada em tiles.\n\n"
                          "Deseja disponibilizar o resultado na paleta principal para reutilizá-lo em outros lugares?"));
        choice.setInformativeText(tr("Se escolher 'Não mostrar', o Tileset continua no projeto e no Gerenciador de Tilesets, mas não ocupa espaço na paleta de pintura."));
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
        emit statusMessage(choice.clickedButton() == show
            ? tr("Bake concluído — o novo Tileset está disponível na paleta.")
            : tr("Bake concluído — o Tileset ficou organizado somente no Gerenciador."));
    });

    if ((l->type == LayerType::Image && !l->imageReferenceOnly) ||
        l->type == LayerType::Tile || l->type == LayerType::Group) {
        menu.addSeparator();
        const bool contactOnly = l->type == LayerType::Tile || l->type == LayerType::Group;
        auto* filtersAction = menu.addAction(icons::get(contactOnly ? QStringLiteral("filter-contact-shadow")
                                                                    : QStringLiteral("layer-filters")),
                                             contactOnly ? tr("Sombra de contato…") : tr("Filtros da camada…"));
        filtersAction->setToolTip(l->type == LayerType::Group
            ? tr("Cria uma única sombra para a composição das camadas dentro do grupo.")
            : (l->type == LayerType::Tile
               ? tr("Cria profundidade de contato usando a silhueta dos tiles, sem destruir a camada.")
               : tr("Desfoque, ruído e sombra de contato sem alterar os pixels originais.")));
        connect(filtersAction, &QAction::triggered, this, [this, l, contactOnly] {
            if (editLayerRasterFilters(ed, l, false, this))
                emit statusMessage(contactOnly ? tr("Sombra de contato atualizada.")
                                               : tr("Filtros da camada atualizados."));
        });
    }

    if (supportsRasterMask(l)) {
        menu.addSeparator();
        auto* alphaLockAction = menu.addAction(icons::get(QStringLiteral("alpha-lock")), tr("Proteger áreas vazias"));
        alphaLockAction->setCheckable(true);
        alphaLockAction->setChecked(l->alphaLock);
        alphaLockAction->setToolTip(l->type == LayerType::Tile
            ? tr("Ao pintar a máscara, limita o pincel à área ocupada pelos tiles.")
            : tr("Ao pintar o conteúdo, mantém a transparência atual da imagem."));
        connect(alphaLockAction, &QAction::toggled, this, [this, l](bool enabled) {
            const DocSnapshot before = ed.snapshotDoc();
            l->alphaLock = enabled;
            ed.pushDocHistory(before, enabled ? tr("Proteger áreas vazias") : tr("Permitir pintar nas áreas vazias"));
            emit ed.layersChanged(); emit ed.mapChanged();
            emit statusMessage(enabled ? tr("Áreas vazias protegidas. O pincel só altera onde já existe conteúdo.") : tr("Áreas vazias liberadas. O pincel pode criar conteúdo em qualquer parte da camada."));
        });

        if (l->imageMask.isNull()) {
            menu.addAction(icons::get(QStringLiteral("mask")), tr("Adicionar máscara da camada"), this, [this, l] {
                const QSize size = rasterMaskSizeFor(l);
                if (!size.isValid() || size.isEmpty()) return;
                const DocSnapshot before = ed.snapshotDoc();
                l->imageMask = QImage(size, QImage::Format_ARGB32_Premultiplied);
                l->imageMask.fill(Qt::white);
                l->maskFilters.clear();
                l->imageMaskEnabled = true;
                ed.session.selectedMaskLayerId = l->id;
                ed.pushDocHistory(before, tr("Adicionar máscara da camada"));
                emit ed.layersChanged(); emit ed.selectionChanged(); emit ed.mapChanged();
                emit statusMessage(tr("Máscara criada. Pinte de branco para mostrar partes da camada e de preto para esconder."));
            });
        } else {
            menu.addAction(icons::get(isEditingMask(ed, l) ? QStringLiteral("mask") : QStringLiteral("mask-edit")),
                           isEditingMask(ed, l) ? tr("Voltar a editar conteúdo")
                                                : tr("Editar máscara"), this, [this, l] {
                const bool wasEditing = isEditingMask(ed, l);
                ed.session.selectedMaskLayerId = wasEditing ? QString() : l->id;
                emit ed.layersChanged(); emit ed.selectionChanged(); emit ed.mapChanged();
                emit statusMessage(wasEditing
                    ? tr("Voltando a editar o conteúdo da camada.")
                    : tr("Editando a máscara: branco mostra, preto esconde. Use a borracha para esconder também."));
            });
            auto* maskFiltersAction = menu.addAction(icons::get(QStringLiteral("mask-filters")), tr("Efeitos da máscara…"));
            maskFiltersAction->setToolTip(tr("Suavize, desgaste ou dê textura às partes mostradas/escondidas sem alterar o conteúdo original da camada."));
            connect(maskFiltersAction, &QAction::triggered, this, [this, l] {
                if (editLayerRasterFilters(ed, l, true, this))
                    emit statusMessage(tr("Efeitos da máscara atualizados."));
            });
            menu.addAction(l->imageMaskEnabled ? tr("Desligar máscara") : tr("Ligar máscara"), [this, l] {
                const DocSnapshot before = ed.snapshotDoc();
                l->imageMaskEnabled = !l->imageMaskEnabled;
                ed.pushDocHistory(before, l->imageMaskEnabled ? tr("Ligar máscara")
                                                              : tr("Desligar máscara"));
                emit ed.layersChanged(); emit ed.mapChanged();
            });
            menu.addAction(tr("Inverter máscara"), [this, l] {
                const DocSnapshot before = ed.snapshotDoc();
                QImage mask = l->imageMask.convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < mask.height(); ++y) {
                    QRgb* row = reinterpret_cast<QRgb*>(mask.scanLine(y));
                    for (int x = 0; x < mask.width(); ++x) {
                        const int current = (qGray(row[x]) * qAlpha(row[x]) + 127) / 255;
                        const int inv = 255 - current;
                        row[x] = qRgba(inv, inv, inv, 255);
                    }
                }
                l->imageMask = mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                l->imageMaskEnabled = true;
                ed.pushDocHistory(before, tr("Inverter máscara"));
                emit ed.mapChanged(); emit ed.layersChanged();
            });
            menu.addAction(tr("Preencher máscara com branco"), [this, l] {
                const DocSnapshot before = ed.snapshotDoc();
                l->imageMask.fill(Qt::white); l->imageMaskEnabled = true;
                ed.pushDocHistory(before, tr("Preencher máscara com branco"));
                emit ed.mapChanged(); emit ed.layersChanged();
            });
            menu.addAction(tr("Preencher máscara com preto"), [this, l] {
                const DocSnapshot before = ed.snapshotDoc();
                l->imageMask.fill(QColor(0, 0, 0, 255)); l->imageMaskEnabled = true;
                ed.pushDocHistory(before, tr("Preencher máscara com preto"));
                emit ed.mapChanged(); emit ed.layersChanged();
            });
            if (l->type == LayerType::Image) {
                menu.addAction(tr("Aplicar máscara permanentemente…"), [this, l] {
                    if (QMessageBox::question(this, tr("Aplicar máscara"),
                        tr("Transformar o resultado da máscara em parte permanente da imagem?\n\nDepois disso, as partes escondidas serão removidas da imagem e a máscara deixará de existir. Ctrl+Z pode desfazer."))
                        != QMessageBox::Yes) return;
                    const DocSnapshot before = ed.snapshotDoc();
                    QImage image = l->image.convertToFormat(QImage::Format_ARGB32);
                    QImage mask = filteredRasterCached(l->imageMask, l->maskFilters, true).convertToFormat(QImage::Format_ARGB32);
                    if (mask.size() != image.size())
                        mask = mask.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    for (int y = 0; y < image.height(); ++y) {
                        QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
                        const QRgb* mk = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
                        for (int x = 0; x < image.width(); ++x) {
                            const int maskValue = (qGray(mk[x]) * qAlpha(mk[x]) + 127) / 255;
                            const int a = (qAlpha(dst[x]) * maskValue + 127) / 255;
                            dst[x] = qRgba(qRed(dst[x]), qGreen(dst[x]), qBlue(dst[x]), a);
                        }
                    }
                    l->image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    l->imageMask = QImage(); l->maskFilters.clear(); l->imageMaskEnabled = false;
                    if (ed.session.selectedMaskLayerId == l->id) ed.session.selectedMaskLayerId.clear();
                    ed.pushDocHistory(before, tr("Aplicar máscara permanentemente"));
                    emit ed.mapChanged(); emit ed.layersChanged();
                });
            }
            menu.addAction(tr("Remover máscara"), [this, l] {
                if (QMessageBox::question(this, tr("Remover máscara"),
                    tr("Remover a máscara? A imagem, a pintura ou os tiles originais continuarão intactos."))
                    != QMessageBox::Yes) return;
                const DocSnapshot before = ed.snapshotDoc();
                l->imageMask = QImage(); l->maskFilters.clear(); l->imageMaskEnabled = false;
                if (ed.session.selectedMaskLayerId == l->id) ed.session.selectedMaskLayerId.clear();
                ed.pushDocHistory(before, tr("Remover máscara"));
                emit ed.layersChanged(); emit ed.selectionChanged(); emit ed.mapChanged();
            });
            menu.addSeparator();
        }
    }

    if (l->type == LayerType::Image && l->imagePaintLayer) {
        menu.addAction(tr("Limpar conteúdo"), [this, l] {
            if (l->image.isNull()) return;
            if (QMessageBox::question(this, tr("Limpar camada de pintura"),
                                      tr("Apagar toda a pintura desta camada?\nVocê pode desfazer com Ctrl+Z."))
                != QMessageBox::Yes) return;
            Editor::EditSession edit = ed.beginLayerEdit(l);
            l->image.fill(Qt::transparent);
            ed.commitLayerEdit(edit, tr("Limpar camada de pintura"));
            emit statusMessage(tr("Camada de pintura limpa."));
        });
    }
    if (l->type == LayerType::Tile || l->type == LayerType::Image) {
        menu.addAction(l->isMask ? tr("Parar de usar esta camada como recorte")
                                 : tr("Usar esta camada como recorte"), [this, l] {
            const DocSnapshot before = ed.snapshotDoc();
            l->isMask = !l->isMask;
            ed.pushDocHistory(before, tr("Alternar uso como recorte"));
            emit ed.mapChanged();
            emit ed.layersChanged();
        });
    }
    if (l->type == LayerType::Tile) {
        menu.addAction(l->zMode == QLatin1String("above")
                           ? tr("Voltar para abaixo do jogador")
                           : tr("Marcar camada como acima do jogador"), [this, l] {
            const DocSnapshot before = ed.snapshotDoc();
            l->zMode = l->zMode == QLatin1String("above") ? QStringLiteral("below")
                                                          : QStringLiteral("above");
            ed.pushDocHistory(before, tr("Alterar ordem Z da camada"));
            emit ed.layersChanged();
        });
        menu.addAction(tr("Limpar conteúdo"), [this, l] {
            Editor::EditSession s = ed.beginLayerEdit(l);
            l->allocGrid();
            ed.commitLayerEdit(s, tr("Limpar camada"));
        });
    }
    menu.addSeparator();

    QString structuralParentId;
    int structuralIndex = -1;
    const bool hasStructuralParent = locateLayerParent(ed.layers(), l->id, QString(),
                                                       &structuralParentId, &structuralIndex);
    const LayerPtr structuralParent = structuralParentId.isEmpty()
        ? LayerPtr() : static_cast<const Editor&>(ed).findNode(structuralParentId);
    if (hasStructuralParent && structuralParent && structuralParent->type == LayerType::Group) {
        auto* outdent = menu.addAction(icons::get(QStringLiteral("outdent")),
                                       tr("Mover para fora desta pasta"));
        outdent->setToolTip(tr("Tira esta camada da pasta atual sem apagar ou alterar seu conteúdo."));
        connect(outdent, &QAction::triggered, this, [this, l, structuralParentId] {
            QString grandParentId;
            int parentIndex = -1;
            if (!locateLayerParent(ed.layers(), structuralParentId, QString(),
                                   &grandParentId, &parentIndex)) return;
            const DocSnapshot before = ed.snapshotDoc();
            // Insere logo acima da pasta na ordem de renderização. Em pasta
            // aninhada isso sobe somente um nível, como “outdent” em editores.
            if (!ed.reparentLayer(l->id, grandParentId, parentIndex + 1)) return;
            ed.pushDocHistory(before, tr("Mover camada para fora da pasta"));
            emit statusMessage(tr("“%1” foi movida para fora da pasta.").arg(l->name));
        });
    }

    if (l->type != LayerType::Group) {
        menu.addAction(tr("Agrupar nesta pasta nova"), [this, l] {
            const DocSnapshot before = ed.snapshotDoc();
            QVector<LayerPtr>* parent = nullptr;
            int index = -1;
            if (ed.findNode(l->id, &parent, &index) && parent) {
                LayerPtr g = makeGroupLayer(tr("Grupo"));
                parent->replace(index, g);
                g->children.push_back(l);
                ed.pushDocHistory(before, tr("Agrupar camada"));
                emit ed.layersChanged();
                emit ed.mapChanged();
            }
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Excluir"), [this, l] {
        const DocSnapshot before = ed.snapshotDoc();
        ed.removeLayer(l->id);
        ed.pushDocHistory(before, tr("Excluir camada"));
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace ui
