#pragma once

#include "ProjectRecoveryManager.h"

#include <QDialog>

namespace ui {

enum class RecoveryChoice {
    RestoreRecovery,
    OpenSavedProject,
    DiscardRecovery
};

class RecoveryDecisionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RecoveryDecisionDialog(const ProjectRecoveryStatus& status, QWidget* parent = nullptr);
    RecoveryChoice choice() const { return m_choice; }

private:
    RecoveryChoice m_choice = RecoveryChoice::OpenSavedProject;
};

} // namespace ui
