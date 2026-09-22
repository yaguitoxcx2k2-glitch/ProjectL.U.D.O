#pragma once

#include "core/EventModel.h"
#include "core/ProjectReferenceIndex.h"
#include "core/ProjectValidator.h"

#include <QWidget>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QLabel;
class QTextBrowser;
class QTreeWidget;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

struct CommandInspectionData {
    QString type,domain,lifecycle,description,paramsJson;
    QStringList runtimeMetadata;
    QVector<core::ValidationIssue> diagnostics;
    QVector<core::ProjectReferenceUsage> references;
};

CommandInspectionData inspectEventCommand(const core::Editor& editor,
                                          const core::EventCommand& command,
                                          const core::ProjectReferenceLocation& location = {});

/// Painel semântico do Event Editor. Não mantém uma tabela paralela: Registry,
/// Validator e Reference Index são consultados novamente a cada seleção.
class CommandInspector final : public QWidget
{
    Q_OBJECT
public:
    explicit CommandInspector(core::Editor& editor,QWidget* parent=nullptr);
    void setCommand(const core::EventCommand* command,
                    const core::ProjectReferenceLocation& location = {});
    bool goToCurrentDefinition();

signals:
    void navigateRequested(const core::ProjectReferenceLocation& location);

private:
    core::Editor& m_editor;
    QLabel* m_summary=nullptr;
    QTextBrowser *m_type=nullptr,*m_params=nullptr,*m_diagnostics=nullptr,*m_runtime=nullptr;
    QTreeWidget* m_references=nullptr;
};

} // namespace ui
