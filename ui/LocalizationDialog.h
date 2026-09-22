#pragma once

#include "core/Editor.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;

namespace ui {

/// Bloco F / 3.28.1 — editor No-Code de idiomas e Text Keys.
class LocalizationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LocalizationDialog(core::Editor& editor, QWidget* parent = nullptr);

private:
    void rebuildLocaleSelectors();
    void rebuildTable();
    void syncTableToWorking();
    QString resolvedPendingKey(QString key) const;
    void updateStatus();
    void addLocale();
    void removeLocale();
    void addKey();
    void renameKey();
    void removeKey();
    void scanProjectTexts();
    void importCsv();
    void exportCsv();

    core::Editor& m_editor;
    core::LocalizationSettings m_working;
    QCheckBox* m_enabled = nullptr;
    QComboBox* m_defaultLocale = nullptr;
    QComboBox* m_fallbackLocale = nullptr;
    QComboBox* m_initialMode = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QVector<QPair<QString, QString>> m_pendingRenames;
    bool m_refreshing = false;
};

} // namespace ui
