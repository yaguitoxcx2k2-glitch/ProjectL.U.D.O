#pragma once

#include "core/EventDiagnostics.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

/// Painel não modal de problemas do projeto. Todos os itens são derivados do
/// ProjectValidator para manter exportação, saúde do projeto e editor de
/// eventos sob o mesmo contrato de validação.
class ValidationPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit ValidationPanel(core::Editor& editor, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    void navigateRequested(const core::ProjectReferenceLocation& location);
    void projectRepaired();

private:
    void applyFilters();
    void navigateToItem(QTreeWidgetItem* item);
    void repairSafeIssues();
    void exportDiagnostics();

    core::Editor& m_editor;
    QVector<core::EventDiagnostic> m_diagnostics;
    QLabel* m_summary = nullptr;
    QComboBox* m_severityFilter = nullptr;
    QComboBox* m_codeFilter = nullptr;
    QLineEdit* m_contextFilter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_fixButton = nullptr;
    QTimer* m_refreshTimer = nullptr;
};

} // namespace ui
