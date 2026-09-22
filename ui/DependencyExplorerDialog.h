#pragma once

#include "core/ProjectDependencyIndex.h"

#include <QDialog>
#include <functional>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

class DependencyExplorerDialog final : public QDialog
{
public:
    using OpenCallback = std::function<void(const core::ProjectReferenceLocation&)>;

    explicit DependencyExplorerDialog(core::Editor& editor,
                                      QWidget* parent = nullptr,
                                      OpenCallback open = {});

private:
    void rebuildNodes();
    void rebuildRelations();
    void openSelectedNode();
    void openSelectedRelation();
    void toggleFavorite();
    QString currentNodeKey() const;

    core::Editor& ed;
    core::ProjectDependencySnapshot m_snapshot;
    OpenCallback m_open;
    QLineEdit* m_search = nullptr;
    QComboBox* m_filter = nullptr;
    QTreeWidget* m_nodes = nullptr;
    QTreeWidget* m_relations = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_favoriteButton = nullptr;
};

} // namespace ui
