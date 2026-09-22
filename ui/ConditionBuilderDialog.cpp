#include "ConditionBuilderDialog.h"

#include "Dialogs.h"
#include "core/ConditionTree.h"
#include "core/Editor.h"

#include <QComboBox>
#include <QColor>
#include <QBrush>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {
namespace {

QString atomText(const QVariantMap& atom)
{
    const QString kind = atom.value(QStringLiteral("kind"), QStringLiteral("switch")).toString();
    if (kind == QLatin1String("switch")) return QObject::tr("Switch %1").arg(atom.value(QStringLiteral("id"), 1).toInt());
    if (kind == QLatin1String("selfSwitch")) return QObject::tr("Self Switch %1").arg(atom.value(QStringLiteral("letter"), QStringLiteral("A")).toString());
    if (kind == QLatin1String("variable")) return QObject::tr("Variável %1 %2 …").arg(atom.value(QStringLiteral("id"), 1).toInt()).arg(atom.value(QStringLiteral("op"), QStringLiteral(">=")).toString());
    if (kind == QLatin1String("string")) return QObject::tr("String %1 %2 …").arg(atom.value(QStringLiteral("id"), 1).toInt()).arg(atom.value(QStringLiteral("op"), QStringLiteral("==")).toString());
    if (kind == QLatin1String("commonValue")) return QObject::tr("Parâmetro/local %1").arg(atom.value(QStringLiteral("id")).toString());
    if (kind == QLatin1String("random")) return QObject::tr("Probabilidade %1%").arg(atom.value(QStringLiteral("chance"), 50).toInt());
    if (kind == QLatin1String("gold")) return QObject::tr("Ouro %1 …").arg(atom.value(QStringLiteral("op"), QStringLiteral(">=")).toString());
    if (kind == QLatin1String("item")) return QObject::tr("Item %1").arg(atom.value(QStringLiteral("itemId")).toString());
    return QObject::tr("Condição %1").arg(kind);
}

} // namespace

ConditionBuilderDialog::ConditionBuilderDialog(core::Editor& editor, const QVariantMap& tree,
                                               const core::CommonEvent* commonContext,
                                               QWidget* parent)
    : QDialog(parent), m_editor(editor), m_commonContext(commonContext)
{
    setWindowTitle(tr("Construtor de Condições AND / OR"));
    resize(820, 620);
    auto* rootLayout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Monte a condição com grupos aninhados. AND exige todos os filhos; OR aceita qualquer filho. O limite seguro é 10 níveis."), this);
    hint->setWordWrap(true);
    rootLayout->addWidget(hint);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("conditionBuilderTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setAlternatingRowColors(true);
    rootLayout->addWidget(m_tree, 1);

    auto* tools = new QHBoxLayout;
    auto* addConditionButton = new QPushButton(tr("+ Condição"), this);
    auto* addAndButton = new QPushButton(tr("+ Grupo AND"), this);
    auto* addOrButton = new QPushButton(tr("+ Grupo OR"), this);
    m_editButton = new QPushButton(tr("Editar"), this);
    m_removeButton = new QPushButton(tr("Remover"), this);
    m_upButton = new QPushButton(tr("Mover acima"), this);
    m_downButton = new QPushButton(tr("Mover abaixo"), this);
    tools->addWidget(addConditionButton); tools->addWidget(addAndButton); tools->addWidget(addOrButton);
    tools->addStretch(); tools->addWidget(m_editButton); tools->addWidget(m_removeButton);
    tools->addWidget(m_upButton); tools->addWidget(m_downButton);
    rootLayout->addLayout(tools);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Lógica do grupo selecionado:"), this));
    m_groupMode = new QComboBox(this);
    m_groupMode->setObjectName(QStringLiteral("conditionBuilderGroupMode"));
    m_groupMode->addItem(tr("Todas (AND)"), QStringLiteral("all"));
    m_groupMode->addItem(tr("Qualquer (OR)"), QStringLiteral("any"));
    modeRow->addWidget(m_groupMode);
    m_stats = new QLabel(this);
    modeRow->addStretch(); modeRow->addWidget(m_stats);
    rootLayout->addLayout(modeRow);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("conditionBuilderPreview"));
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_preview->setMinimumHeight(58);
    rootLayout->addWidget(m_preview);

    QVariantMap initial = tree;
    if (!core::isConditionTreeNode(initial))
        initial = core::conditionGroupNode(QStringLiteral("all"), {core::conditionLeafNode({{QStringLiteral("kind"),QStringLiteral("switch")},{QStringLiteral("id"),1},{QStringLiteral("value"),true}})});
    m_root = appendNode(nullptr, initial);
    m_tree->expandAll();
    m_tree->setCurrentItem(m_root);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    rootLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const QString problem = core::conditionTreeStructureProblem(conditionTree(), 10);
        if (!problem.isEmpty()) { QMessageBox::warning(this, tr("Condição inválida"), problem); return; }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ConditionBuilderDialog::syncSelection);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editCurrent(); });
    connect(m_groupMode, &QComboBox::currentIndexChanged, this, [this](int) {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (!item || item->data(0, NodeTypeRole).toString() != QLatin1String("group")) return;
        item->setData(0, NodeDataRole, m_groupMode->currentData());
        refreshItem(item); refreshPreview();
    });
    connect(addConditionButton, &QPushButton::clicked, this, &ConditionBuilderDialog::addCondition);
    connect(addAndButton, &QPushButton::clicked, this, [this] { addGroup(QStringLiteral("all")); });
    connect(addOrButton, &QPushButton::clicked, this, [this] { addGroup(QStringLiteral("any")); });
    connect(m_editButton, &QPushButton::clicked, this, &ConditionBuilderDialog::editCurrent);
    connect(m_removeButton, &QPushButton::clicked, this, &ConditionBuilderDialog::removeCurrent);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveCurrent(1); });
    syncSelection(); refreshPreview();
}

QVariantMap ConditionBuilderDialog::conditionTree() const
{
    return serialize(m_root);
}

QTreeWidgetItem* ConditionBuilderDialog::appendNode(QTreeWidgetItem* parent, const QVariantMap& node)
{
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    const QString type = node.value(QStringLiteral("node")).toString();
    item->setData(0, NodeTypeRole, type);
    if (type == QLatin1String("group")) {
        item->setData(0, NodeDataRole, core::normalizedConditionGroupMode(node.value(QStringLiteral("mode")).toString()));
        for (const QVariant& child : node.value(QStringLiteral("children")).toList()) appendNode(item, child.toMap());
    } else {
        item->setData(0, NodeDataRole, node.value(QStringLiteral("condition")).toMap());
    }
    refreshItem(item);
    return item;
}

QVariantMap ConditionBuilderDialog::serialize(QTreeWidgetItem* item) const
{
    if (!item) return {};
    if (item->data(0, NodeTypeRole).toString() == QLatin1String("condition"))
        return core::conditionLeafNode(item->data(0, NodeDataRole).toMap());
    QVariantList children;
    for (int i = 0; i < item->childCount(); ++i) children.push_back(serialize(item->child(i)));
    return core::conditionGroupNode(item->data(0, NodeDataRole).toString(), children);
}

QTreeWidgetItem* ConditionBuilderDialog::selectedGroup() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item) return m_root;
    if (item->data(0, NodeTypeRole).toString() == QLatin1String("group")) return item;
    return item->parent() ? item->parent() : m_root;
}

int ConditionBuilderDialog::itemDepth(const QTreeWidgetItem* item) const
{
    int depth = 1;
    while (item && item->parent()) { ++depth; item = item->parent(); }
    return depth;
}

void ConditionBuilderDialog::refreshItem(QTreeWidgetItem* item)
{
    if (!item) return;
    if (item->data(0, NodeTypeRole).toString() == QLatin1String("group")) {
        const bool any = item->data(0, NodeDataRole).toString() == QLatin1String("any");
        item->setText(0, any ? tr("QUALQUER condição (OR)") : tr("TODAS as condições (AND)"));
        item->setForeground(0, QBrush(any ? QColor(220, 142, 52) : QColor(72, 145, 220)));
        QFont font = item->font(0); font.setBold(true); item->setFont(0, font);
        item->setExpanded(true);
    } else {
        item->setText(0, atomText(item->data(0, NodeDataRole).toMap()));
    }
}

void ConditionBuilderDialog::refreshPreview()
{
    const QVariantMap tree = serialize(m_root);
    m_preview->setText(tr("Prévia: %1").arg(core::conditionTreeToString(tree, atomText)));
    const core::ConditionTreeStats stats = core::conditionTreeStats(tree);
    m_stats->setText(tr("Condições: %1 · grupos: %2 · profundidade %3/10").arg(stats.conditions).arg(stats.groups).arg(stats.maxDepth));
}

void ConditionBuilderDialog::syncSelection()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    const bool group = item && item->data(0, NodeTypeRole).toString() == QLatin1String("group");
    const bool leaf = item && !group;
    m_groupMode->setEnabled(group);
    QSignalBlocker blocker(m_groupMode);
    if (group) { const int index = m_groupMode->findData(item->data(0, NodeDataRole)); if (index >= 0) m_groupMode->setCurrentIndex(index); }
    m_editButton->setEnabled(leaf);
    m_removeButton->setEnabled(item && item != m_root && item->parent() && item->parent()->childCount() > 1);
    const int index = item && item->parent() ? item->parent()->indexOfChild(item) : -1;
    m_upButton->setEnabled(index > 0);
    m_downButton->setEnabled(index >= 0 && index + 1 < item->parent()->childCount());
}

void ConditionBuilderDialog::addCondition()
{
    QTreeWidgetItem* parent = selectedGroup();
    core::EventCommand command{QStringLiteral("if"), {{QStringLiteral("kind"),QStringLiteral("switch")},{QStringLiteral("id"),1},{QStringLiteral("value"),true}}};
    LogicCommandDialog editor(m_editor, QStringLiteral("if"), command, this, m_commonContext, false);
    if (editor.exec() != QDialog::Accepted) return;
    command.params.remove(QStringLiteral("createElse")); command.params.remove(QStringLiteral("conditionTree")); command.params.remove(QStringLiteral("timeoutFrames"));
    QTreeWidgetItem* item = appendNode(parent, core::conditionLeafNode(command.params));
    m_tree->setCurrentItem(item); refreshPreview(); syncSelection();
}

void ConditionBuilderDialog::addGroup(const QString& mode)
{
    QTreeWidgetItem* parent = selectedGroup();
    if (itemDepth(parent) >= 9) { QMessageBox::information(this, tr("Limite de profundidade"), tr("O novo grupo ultrapassaria o limite de 10 níveis.")); return; }
    QVariantList children{core::conditionLeafNode({{QStringLiteral("kind"),QStringLiteral("switch")},{QStringLiteral("id"),1},{QStringLiteral("value"),true}})};
    QTreeWidgetItem* item = appendNode(parent, core::conditionGroupNode(mode, children));
    m_tree->setCurrentItem(item); m_tree->expandAll(); refreshPreview(); syncSelection();
}

void ConditionBuilderDialog::editCurrent()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || item->data(0, NodeTypeRole).toString() != QLatin1String("condition")) return;
    core::EventCommand command{QStringLiteral("if"), item->data(0, NodeDataRole).toMap()};
    LogicCommandDialog editor(m_editor, QStringLiteral("if"), command, this, m_commonContext, false);
    if (editor.exec() != QDialog::Accepted) return;
    command.params.remove(QStringLiteral("createElse")); command.params.remove(QStringLiteral("conditionTree")); command.params.remove(QStringLiteral("timeoutFrames"));
    item->setData(0, NodeDataRole, command.params); refreshItem(item); refreshPreview();
}

void ConditionBuilderDialog::removeCurrent()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || item == m_root || !item->parent() || item->parent()->childCount() <= 1) return;
    QTreeWidgetItem* parent = item->parent(); delete item; m_tree->setCurrentItem(parent); refreshPreview(); syncSelection();
}

void ConditionBuilderDialog::moveCurrent(int direction)
{
    QTreeWidgetItem* item = m_tree->currentItem();
    QTreeWidgetItem* parent = item ? item->parent() : nullptr;
    if (!parent) return;
    const int from = parent->indexOfChild(item), to = from + direction;
    if (to < 0 || to >= parent->childCount()) return;
    parent->takeChild(from); parent->insertChild(to, item); m_tree->setCurrentItem(item); refreshPreview(); syncSelection();
}

} // namespace ui
