#include "ProjectCreationWorkflow.h"

#include "ProjectCreationDialog.h"
#include "ProjectManagerDialog.h"

#include "core/Editor.h"

#include <QDir>
#include <QMessageBox>
#include <QObject>

namespace ui {

bool runProjectCreationWorkflow(core::Editor& editor,
                                QWidget* parent,
                                ProjectCreationWorkflowResult* result,
                                const QString& initialParentFolder)
{
    ProjectCreationDialog dialog(parent,
        initialParentFolder.trimmed().isEmpty() ? QDir::homePath() : initialParentFolder);
    if (dialog.exec() != QDialog::Accepted) return false;

    core::ProjectCreationResult created;
    QString error;
    if (!core::createProject(editor, dialog.request(), &created, &error)) {
        QMessageBox::warning(parent, QObject::tr("Novo projeto de mapas"), error);
        return false;
    }

    // Recentes faz parte do mesmo contrato de criação independentemente da
    // entrada usada pela UI. Assim não existe mais um "Novo" que esquece de
    // registrar o projeto enquanto outro registra.
    ProjectManagerDialog::rememberProject(created.projectPath);

    if (result) result->project = created;
    return true;
}

} // namespace ui
