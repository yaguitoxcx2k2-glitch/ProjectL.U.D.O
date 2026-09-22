#pragma once

#include "RpgMakerTarget.h"

#include <QString>
#include <QVector>
#include <QStringList>

namespace core {

class Editor;

struct ProjectTemplateDescriptor
{
    QString id;
    QString name;
    QString description;
    QString category;
    QStringList features;
};

struct ProjectCreationRequest
{
    QString projectName;
    QString parentFolder;
    RpgMakerEngine engine = RpgMakerEngine::MZ;
    QString templateId;
};

struct ProjectCreationResult
{
    QString projectRoot;
    QString projectPath;
};

/// Catálogo único dos modelos de projeto exibidos por qualquer entrada da UI.
QVector<ProjectTemplateDescriptor> projectTemplateCatalog();

/// Aplica somente o preset inicial ao Editor já inicializado por newProject().
bool applyProjectTemplate(Editor& editor, const QString& templateId, QString* error = nullptr);

/// Criação transacional do projeto. O destino deve estar vazio; em caso de
/// falha, uma pasta criada pelo próprio workflow é removida.
bool createProject(Editor& target, const ProjectCreationRequest& request,
                   ProjectCreationResult* result = nullptr, QString* error = nullptr);

/// Abre um projeto RPG Maker como projeto LUDO único. O contêiner fica em
/// <raiz>/LUDO/projeto.ludo e guarda a própria raiz/engine como autoridade.
bool openRpgMakerProject(Editor& target, const QString& rpgMakerRoot,
                         ProjectCreationResult* result = nullptr, QString* error = nullptr);

} // namespace core
