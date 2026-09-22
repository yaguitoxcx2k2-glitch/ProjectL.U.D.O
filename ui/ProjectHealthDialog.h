#pragma once

#include <QDialog>

namespace core { class Editor; struct ProjectHealthSnapshot; }

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace ui {

class ProjectRecoveryManager;

class ProjectHealthDialog : public QDialog
{
    Q_OBJECT
public:
    ProjectHealthDialog(core::Editor& editor, ProjectRecoveryManager* recovery,
                        QWidget* parent = nullptr);

private:
    void refresh();
    void refreshRecovery();
    bool acceptsCurrentFilter(int severity, int area) const;

    core::Editor& m_editor;
    ProjectRecoveryManager* m_recovery = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_onboardingSummary = nullptr;
    QLabel* m_readinessSummary = nullptr;
    QLabel* m_recoverySummary = nullptr;
    QComboBox* m_severityFilter = nullptr;
    QComboBox* m_areaFilter = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_repair = nullptr;
    QPushButton* m_writeRecovery = nullptr;
    QPushButton* m_discardRecovery = nullptr;
};

} // namespace ui
