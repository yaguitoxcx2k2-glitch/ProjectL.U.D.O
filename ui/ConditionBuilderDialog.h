#pragma once

#include <QDialog>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace core { class Editor; struct CommonEvent; }

namespace ui {

/// Construtor visual reutilizável da árvore canônica ConditionTree. A edição
/// de cada folha delega ao LogicCommandDialog já usado pelos comandos If e
/// Wait Until, evitando uma segunda definição de tipos/operadores.
class ConditionBuilderDialog final : public QDialog
{
    Q_OBJECT
public:
    ConditionBuilderDialog(core::Editor& editor, const QVariantMap& tree,
                           const core::CommonEvent* commonContext = nullptr,
                           QWidget* parent = nullptr);

    QVariantMap conditionTree() const;

private:
    enum { NodeTypeRole = Qt::UserRole + 40, NodeDataRole = Qt::UserRole + 41 };
    QTreeWidgetItem* appendNode(QTreeWidgetItem* parent, const QVariantMap& node);
    QVariantMap serialize(QTreeWidgetItem* item) const;
    QTreeWidgetItem* selectedGroup() const;
    int itemDepth(const QTreeWidgetItem* item) const;
    void refreshItem(QTreeWidgetItem* item);
    void refreshPreview();
    void syncSelection();
    void addCondition();
    void addGroup(const QString& mode);
    void editCurrent();
    void removeCurrent();
    void moveCurrent(int direction);

    core::Editor& m_editor;
    const core::CommonEvent* m_commonContext = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTreeWidgetItem* m_root = nullptr;
    QComboBox* m_groupMode = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_stats = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;
};

} // namespace ui
