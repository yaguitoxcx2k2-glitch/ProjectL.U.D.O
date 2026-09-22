#pragma once

#include "ExportPreflight.h"
#include "ExportPlatformPolicy.h"
#include "ExportBuildProfile.h"

#include <QDialog>
#include <QString>

class QLabel;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QPushButton;

namespace core { class Editor; }

namespace ui {

struct ExportWizardOptions
{
    QString gameName;
    QString iconPath;
    QString parentDir;
    bool portableZip = true;
    bool secure = false;
    bool cleanupUnused = true;
    QString buildProfile = QStringLiteral("release");
    ExportPlatform targetPlatform = ExportPlatform::Unknown;
};

class ExportWizardDialog : public QDialog
{
    Q_OBJECT
public:
    ExportWizardDialog(core::Editor& editor, const ExportPreflightResult& preflight,
                       QWidget* parent = nullptr);
    ExportWizardOptions options() const;

private:
    void updateSummary();
    void updateExportEnabled();
    void applyBuildProfile();
    void restorePreferences();
    void persistPreferences() const;

    core::Editor& m_editor;
    const ExportPreflightResult& m_preflight;
    QLineEdit* m_gameName = nullptr;
    QComboBox* m_profile = nullptr;
    QComboBox* m_platform = nullptr;
    QLineEdit* m_icon = nullptr;
    QLineEdit* m_target = nullptr;
    QCheckBox* m_portable = nullptr;
    QCheckBox* m_secure = nullptr;
    QCheckBox* m_cleanup = nullptr;
    QLabel* m_assetSummary = nullptr;
    QPushButton* m_export = nullptr;
};

} // namespace ui
