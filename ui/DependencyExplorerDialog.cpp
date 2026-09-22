#include "DependencyExplorerDialog.h"

#include "EditorUiPrimitives.h"
#include "NavigationHistory.h"
#include "core/Editor.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <utility>

namespace ui {
namespace {
constexpr int RoleKey = Qt::UserRole;
constexpr int RoleOwnerType = Qt::UserRole + 1;
constexpr int RoleOwnerId = Qt::UserRole + 2;
constexpr int RoleOwnerName = Qt::UserRole + 3;
constexpr int RoleMapId = Qt::UserRole + 4;
constexpr int RolePage = Qt::UserRole + 5;
constexpr int RoleCommand = Qt::UserRole + 6;
constexpr int RoleDetail = Qt::UserRole + 7;

void storeLocation(QTreeWidgetItem* item, const core::ProjectReferenceLocation& location)
{
    item->setData(0, RoleOwnerType, location.ownerType);
    item->setData(0, RoleOwnerId, location.ownerId);
    item->setData(0, RoleOwnerName, location.ownerName);
    item->setData(0, RoleMapId, location.mapId);
    item->setData(0, RolePage, location.pageIndex);
    item->setData(0, RoleCommand, location.commandIndex);
    item->setData(0, RoleDetail, location.detail);
}

core::ProjectReferenceLocation readLocation(const QTreeWidgetItem* item)
{
    core::ProjectReferenceLocation location;
    if (!item) return location;
    location.ownerType = item->data(0, RoleOwnerType).toString();
    location.ownerId = item->data(0, RoleOwnerId).toString();
    location.ownerName = item->data(0, RoleOwnerName).toString();
    location.mapId = item->data(0, RoleMapId).toString();
    location.pageIndex = item->data(0, RolePage).toInt();
    location.commandIndex = item->data(0, RoleCommand).toInt();
    location.detail = item->data(0, RoleDetail).toString();
    return location;
}

QString normalized(const QString& value) { return value.trimmed().toCaseFolded(); }

bool matches(const core::ProjectDependencyNode& node, const QString& query)
{
    const QString q = normalized(query);
    if (q.isEmpty()) return true;
    const QString haystack = normalized(node.label + QLatin1Char(' ') + node.context +
                                        QLatin1Char(' ') + node.typeLabel + QLatin1Char(' ') +
                                        node.assetPath + QLatin1Char(' ') + node.symbolId);
    const QStringList tokens = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& token : tokens) if (!haystack.contains(token)) return false;
    return true;
}

bool isNavigable(const core::ProjectReferenceLocation& location)
{
    return !location.ownerType.trimmed().isEmpty() && location.ownerType != QLatin1String("project");
}

QString statusText(const core::ProjectDependencySnapshot& snapshot,
                   const core::ProjectDependencyNode& node)
{
    if (node.missing) return QObject::tr("Ausente");
    const int incoming = snapshot.incomingCount(node.key);
    const int outgoing = snapshot.outgoingCount(node.key);
    if (node.kind == core::ProjectDependencyNodeKind::Asset && incoming == 0)
        return QObject::tr("Não usado");
    if (incoming == 0 && outgoing == 0) return QObject::tr("Sem relações");
    return QObject::tr("Entradas: %1 · saídas: %2").arg(incoming).arg(outgoing);
}
}

DependencyExplorerDialog::DependencyExplorerDialog(core::Editor& editor, QWidget* parent,
                                                   OpenCallback open)
    : QDialog(parent), ed(editor), m_snapshot(core::ProjectDependencyIndex::build(editor)),
      m_open(std::move(open))
{
    setWindowTitle(tr("Dependências do projeto"));
    resize(1080, 680);
    setAccessibleName(tr("Explorador de dependências do projeto"));

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(primitives::infoBanner(
        tr("Veja onde cada mapa, evento, dado ou arquivo é usado no projeto."), this));

    auto* top = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar mapa, evento, dado, asset…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Buscar dependência"));
    m_filter = new QComboBox(this);
    m_filter->addItem(tr("Tudo"), QStringLiteral("all"));
    m_filter->addItem(tr("Referências do projeto"), QStringLiteral("references"));
    m_filter->addItem(tr("Assets"), QStringLiteral("assets"));
    m_filter->addItem(tr("Não usados / sem relações"), QStringLiteral("orphans"));
    m_filter->addItem(tr("Ausentes"), QStringLiteral("missing"));
    top->addWidget(m_search, 1);
    top->addWidget(m_filter);
    outer->addLayout(top);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_nodes = new QTreeWidget(split);
    m_nodes->setHeaderLabels({tr("Item"), tr("Tipo"), tr("Estado")});
    m_nodes->setRootIsDecorated(false);
    m_nodes->setAlternatingRowColors(true);
    m_nodes->header()->setStretchLastSection(false);
    m_nodes->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_nodes->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_nodes->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_nodes->setAccessibleName(tr("Itens do grafo de dependências"));

    auto* right = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    rightLayout->addWidget(primitives::sectionTitle(tr("Relações"), right));
    m_relations = new QTreeWidget(right);
    m_relations->setHeaderLabels({tr("Direção"), tr("Item"), tr("Detalhe")});
    m_relations->setRootIsDecorated(false);
    m_relations->setAlternatingRowColors(true);
    m_relations->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_relations->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_relations->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_relations->setAccessibleName(tr("Relações do item selecionado"));
    rightLayout->addWidget(m_relations, 1);
    split->addWidget(m_nodes);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    auto* footer = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_summary->setProperty("uiRole", QStringLiteral("hint"));
    m_favoriteButton = new QPushButton(tr("Favoritar"), this);
    m_openButton = new QPushButton(tr("Abrir"), this);
    auto* close = new QPushButton(tr("Fechar"), this);
    footer->addWidget(m_summary, 1);
    footer->addWidget(m_favoriteButton);
    footer->addWidget(m_openButton);
    footer->addWidget(close);
    outer->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildNodes(); });
    connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { rebuildNodes(); });
    connect(m_nodes, &QTreeWidget::itemSelectionChanged, this, [this] { rebuildRelations(); });
    connect(m_nodes, &QTreeWidget::itemDoubleClicked, this, [this] { openSelectedNode(); });
    connect(m_relations, &QTreeWidget::itemDoubleClicked, this, [this] { openSelectedRelation(); });
    connect(m_openButton, &QPushButton::clicked, this, [this] { openSelectedNode(); });
    connect(m_favoriteButton, &QPushButton::clicked, this, [this] { toggleFavorite(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    rebuildNodes();
}

QString DependencyExplorerDialog::currentNodeKey() const
{
    QTreeWidgetItem* item = m_nodes->currentItem();
    return item ? item->data(0, RoleKey).toString() : QString();
}

void DependencyExplorerDialog::rebuildNodes()
{
    const QString oldKey = currentNodeKey();
    m_nodes->clear();
    const QString query = m_search->text();
    const QString filter = m_filter->currentData().toString();
    int visible = 0;
    QTreeWidgetItem* restore = nullptr;
    for (const core::ProjectDependencyNode& node : m_snapshot.nodes) {
        if (node.kind == core::ProjectDependencyNodeKind::Owner) continue;
        if (!matches(node, query)) continue;
        const int incoming = m_snapshot.incomingCount(node.key);
        const int outgoing = m_snapshot.outgoingCount(node.key);
        if (filter == QLatin1String("references") && node.kind != core::ProjectDependencyNodeKind::Reference) continue;
        if (filter == QLatin1String("assets") && node.kind != core::ProjectDependencyNodeKind::Asset) continue;
        if (filter == QLatin1String("orphans") && (incoming > 0 || outgoing > 0)) continue;
        if (filter == QLatin1String("missing") && !node.missing) continue;

        QString label = node.label;
        if (NavigationHistory::isFavorite(NavigationHistory::Domain::Targets, node.key))
            label.prepend(QStringLiteral("★ "));
        auto* item = new QTreeWidgetItem(m_nodes, {label, node.typeLabel, statusText(m_snapshot, node)});
        item->setData(0, RoleKey, node.key);
        item->setToolTip(0, node.context);
        if (node.missing) item->setToolTip(2, tr("O Asset Database conhece este item, mas o arquivo não existe no caminho atual."));
        if (node.key == oldKey) restore = item;
        ++visible;
    }
    if (restore) m_nodes->setCurrentItem(restore);
    else if (m_nodes->topLevelItemCount() > 0) m_nodes->setCurrentItem(m_nodes->topLevelItem(0));
    m_summary->setText(tr("Itens visíveis: %1 • Relações: %2 • Arquivos sem uso: %3")
                           .arg(visible).arg(m_snapshot.edges.size()).arg(m_snapshot.unreferencedAssetCount()));
    rebuildRelations();
}

void DependencyExplorerDialog::rebuildRelations()
{
    m_relations->clear();
    const QString key = currentNodeKey();
    const core::ProjectDependencyNode* node = m_snapshot.node(key);
    const bool navigable = node && isNavigable(node->location);
    m_openButton->setEnabled(bool(m_open) && navigable);
    m_favoriteButton->setEnabled(node != nullptr);
    if (node) {
        m_favoriteButton->setText(NavigationHistory::isFavorite(NavigationHistory::Domain::Targets, node->key)
                                      ? tr("Desfavoritar") : tr("Favoritar"));
    }
    if (!node) return;

    for (const core::ProjectDependencyEdge& edge : m_snapshot.incoming(key)) {
        const core::ProjectDependencyNode* source = m_snapshot.node(edge.sourceKey);
        const QString label = source ? source->label : edge.sourceKey;
        auto* item = new QTreeWidgetItem(m_relations, {tr("Usado por"), label, edge.detail});
        storeLocation(item, edge.sourceLocation);
    }
    for (const core::ProjectDependencyEdge& edge : m_snapshot.outgoing(key)) {
        const core::ProjectDependencyNode* target = m_snapshot.node(edge.targetKey);
        const QString label = target ? target->label : edge.targetKey;
        auto* item = new QTreeWidgetItem(m_relations, {tr("Depende de"), label, edge.detail});
        if (target) storeLocation(item, target->location);
    }
}

void DependencyExplorerDialog::openSelectedNode()
{
    if (!m_open) return;
    const core::ProjectDependencyNode* node = m_snapshot.node(currentNodeKey());
    if (node && isNavigable(node->location)) {
        const core::ProjectReferenceLocation location = node->location;
        accept();
        m_open(location);
    }
}

void DependencyExplorerDialog::openSelectedRelation()
{
    if (!m_open) return;
    QTreeWidgetItem* item = m_relations->currentItem();
    if (!item) return;
    const core::ProjectReferenceLocation location = readLocation(item);
    if (isNavigable(location)) {
        accept();
        m_open(location);
    }
}

void DependencyExplorerDialog::toggleFavorite()
{
    const QString key = currentNodeKey();
    if (key.isEmpty()) return;
    NavigationHistory::toggleFavorite(NavigationHistory::Domain::Targets, key);
    rebuildNodes();
}

} // namespace ui
