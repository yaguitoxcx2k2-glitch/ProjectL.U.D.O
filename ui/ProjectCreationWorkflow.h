#pragma once

#include "core/ProjectBootstrap.h"

#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace core { class Editor; }

namespace ui {

/// Resultado único do fluxo visual de criação de projeto.
///
/// A criação real continua pertencendo a core::createProject(); este workflow
/// apenas centraliza o diálogo, tratamento de erro e registro em Recentes para
/// que Arquivo > Novo, Gerenciador de Projetos e futuras entradas usem
/// exatamente o mesmo comportamento.
struct ProjectCreationWorkflowResult
{
    core::ProjectCreationResult project;
};

/// Abre o diálogo de criação e cria o projeto no Editor fornecido.
/// Retorna false tanto em Cancelar quanto em falha; falhas já são apresentadas
/// ao usuário pelo próprio workflow.
bool runProjectCreationWorkflow(core::Editor& editor,
                                QWidget* parent,
                                ProjectCreationWorkflowResult* result = nullptr,
                                const QString& initialParentFolder = QString());

} // namespace ui
