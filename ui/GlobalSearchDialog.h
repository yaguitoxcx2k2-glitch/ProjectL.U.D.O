#pragma once

#include "core/ProjectReferenceIndex.h"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace core { class Editor; }

namespace ui {

class GlobalSearchDialog : public QDialog
{
    Q_OBJECT
public:
    using OpenCallback = std::function<void(const core::ProjectReferenceLocation&)>;

    explicit GlobalSearchDialog(core::Editor& ed, QWidget* parent = nullptr,
                                OpenCallback openCallback = {},
                                const QString& initialSymbolKind = QString(),
                                const QString& initialSymbolId = QString());

private:
    void refresh();
    void showUses(core::ReferenceSymbolKind kind, const QString& id);
    bool currentSymbol(core::ReferenceSymbolKind* kind, QString* id,
                       core::ProjectReferenceSymbol* definition = nullptr) const;
    core::ProjectReferenceLocation currentLocation() const;
    void updateButtons();

    core::Editor& ed;
    OpenCallback m_open;
    QLineEdit* m_query = nullptr;
    QComboBox* m_scope = nullptr;
    QComboBox* m_kind = nullptr;
    QTreeWidget* m_results = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_findUses = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_renumber = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_definitionButton = nullptr;
    QString m_exactKind;
    QString m_exactId;
};

} // namespace ui
