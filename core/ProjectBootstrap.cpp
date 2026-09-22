#include "ProjectBootstrap.h"

#include "AssetWorkflow.h"
#include "Editor.h"
#include "ProjectIO.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <functional>

namespace core {
namespace {

QString safeFolderName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty()) name = QStringLiteral("MeuMapa");
    name.replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|]+")), QStringLiteral("_"));
    if (name == QLatin1String(".") || name == QLatin1String("..")) name = QStringLiteral("MeuMapa");
    return name;
}

bool directoryIsEmpty(const QString& path)
{
    QDir dir(path);
    return !dir.exists() || dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
}

void applyEngineDefaultsToBlankProject(Editor& editor, RpgMakerEngine engine)
{
    const int tileSize = rpgMakerDefaultAuthoringTileSize(engine);
    for (MapDoc& doc : editor.docs) {
        doc.map.tileWidth = tileSize;
        doc.map.tileHeight = tileSize;

        std::function<void(const QVector<LayerPtr>&)> applyLayers;
        applyLayers = [&](const QVector<LayerPtr>& layers) {
            for (const LayerPtr& layer : layers) {
                if (!layer) continue;
                if (layer->type == LayerType::Tile) {
                    layer->tileWidth = tileSize;
                    layer->tileHeight = tileSize;
                }
                applyLayers(layer->children);
            }
        };
        applyLayers(doc.layers);
    }
}

} // namespace

QVector<ProjectTemplateDescriptor> projectTemplateCatalog()
{
    return {
        {QStringLiteral("rpg_maker_mz_map"), QObject::tr("Mapa para RPG Maker MZ"),
         QObject::tr("Projeto de mapas para RPG Maker MZ."), QObject::tr("Mapa"),
         {QObject::tr("Tilesets"), QObject::tr("Colisão"), QObject::tr("Prioridades"), QObject::tr("Panorama 1:1")}},
        {QStringLiteral("rpg_maker_mv_map"), QObject::tr("Mapa para RPG Maker MV"),
         QObject::tr("Projeto de mapas para RPG Maker MV com suporte ao LudoMapSystem."), QObject::tr("Mapa"),
         {QObject::tr("Tilesets"), QObject::tr("Colisão"), QObject::tr("Prioridades"), QObject::tr("Panorama 1:1")}}
    };
}

bool applyProjectTemplate(Editor& editor, const QString& templateId, QString* error)
{
    Q_UNUSED(editor);
    if (error) error->clear();

    const QString id = templateId.trimmed().toLower();
    if (id.isEmpty() || id == QLatin1String("rpg_maker_mz_map") ||
        id == QLatin1String("rpg_maker_mv_map") || id == QLatin1String("map") ||
        id == QLatin1String("blank")) {
        // Projetos novos não recebem mais presets de gameplay. O estado base do
        // Editor é suficiente para a autoria do mapa e a integração RPG Maker.
        return true;
    }

    if (error)
        *error = QObject::tr("O template '%1' pertence à antiga LUDO Engine e não é usado pelo LUDO Map Editor.")
                     .arg(templateId);
    return false;
}

bool createProject(Editor& target, const ProjectCreationRequest& request,
                   ProjectCreationResult* result, QString* error)
{
    if (error) error->clear();
    if (result) *result = ProjectCreationResult();

    const QString projectName = request.projectName.trimmed().isEmpty()
                                    ? QObject::tr("MeuMapa") : request.projectName.trimmed();
    const QString parent = QDir::cleanPath(request.parentFolder.trimmed());
    if (parent.isEmpty()) {
        if (error) *error = QObject::tr("Escolha uma pasta para criar o projeto.");
        return false;
    }

    const QString root = QDir(parent).filePath(safeFolderName(projectName));
    const bool rootExisted = QFileInfo::exists(root);
    if (rootExisted) {
        if (error) *error = directoryIsEmpty(root)
                                ? QObject::tr("A pasta de destino já existe. Escolha outro nome para que a criação seja transacional: %1").arg(root)
                                : QObject::tr("A pasta de destino já existe e não está vazia: %1").arg(root);
        return false;
    }
    if (!QDir().mkpath(root)) {
        if (error) *error = QObject::tr("Não foi possível criar a pasta do projeto: %1").arg(root);
        return false;
    }

    auto rollback = [&] { QDir(root).removeRecursively(); };

    QString workflowError;
    if (!AssetWorkflow::ensureProjectFolders(root, &workflowError)) {
        rollback();
        if (error) *error = workflowError;
        return false;
    }

    Editor staging;
    staging.projectName = projectName;
    staging.rpgMakerEngine = request.engine;
    applyEngineDefaultsToBlankProject(staging, request.engine);
    if (!applyProjectTemplate(staging, request.templateId, &workflowError)) {
        rollback();
        if (error) *error = workflowError;
        return false;
    }

    const QString projectPath = QDir(root).filePath(QStringLiteral("projeto.ludo"));
    staging.projectPath = projectPath;
    staging.projectDirty = true;
    if (!io::saveProject(staging, projectPath, &workflowError)) {
        rollback();
        if (error) *error = workflowError;
        return false;
    }

    if (!io::loadProject(target, projectPath, &workflowError)) {
        rollback();
        if (error) *error = QObject::tr("O projeto foi criado, mas não pôde ser carregado: %1").arg(workflowError);
        return false;
    }

    if (result) {
        result->projectRoot = root;
        result->projectPath = projectPath;
    }
    return true;
}

bool openRpgMakerProject(Editor& target, const QString& rpgMakerRoot,
                         ProjectCreationResult* result, QString* error)
{
    if(error)error->clear();if(result)*result={};
    const QDir root(QDir::cleanPath(rpgMakerRoot));
    if(!root.exists()||!QFileInfo::exists(root.filePath(QStringLiteral("data/MapInfos.json")))){
        if(error)*error=QObject::tr("A pasta escolhida não contém data/MapInfos.json.");return false;
    }
    RpgMakerEngine engine=RpgMakerEngine::MZ;bool marker=false;
    for(const QString& file:root.entryList(QDir::Files|QDir::NoDotAndDotDot)){
        if(file.endsWith(QStringLiteral(".rmmzproject"),Qt::CaseInsensitive)){engine=RpgMakerEngine::MZ;marker=true;break;}
        if(file.endsWith(QStringLiteral(".rpgproject"),Qt::CaseInsensitive)){engine=RpgMakerEngine::MV;marker=true;}
    }
    if(!marker){if(error)*error=QObject::tr("A pasta não contém um projeto RPG Maker MV ou MZ.");return false;}
    const QString ludoRoot=root.filePath(QStringLiteral("LUDO"));
    const QString projectPath=QDir(ludoRoot).filePath(QStringLiteral("projeto.ludo"));
    QString workflowError;
    if(QFileInfo::exists(projectPath)){
        if(!io::loadProject(target,projectPath,&workflowError)){if(error)*error=workflowError;return false;}
    }else{
        if(!QDir().mkpath(ludoRoot)||!AssetWorkflow::ensureProjectFolders(ludoRoot,&workflowError)){
            if(error)*error=workflowError.isEmpty()?QObject::tr("Não foi possível criar a pasta LUDO no projeto."):workflowError;return false;
        }
        Editor staging;staging.projectName=root.dirName();staging.rpgMakerEngine=engine;
        staging.rpgMakerProjectRoot=root.absolutePath();staging.projectPath=projectPath;staging.projectDirty=true;
        applyEngineDefaultsToBlankProject(staging,engine);
        if(!io::saveProject(staging,projectPath,&workflowError)||!io::loadProject(target,projectPath,&workflowError)){
            if(error)*error=workflowError;return false;
        }
    }
    bool changed=target.rpgMakerProjectRoot!=root.absolutePath()||target.rpgMakerEngine!=engine;
    target.rpgMakerProjectRoot=root.absolutePath();target.rpgMakerEngine=engine;
    if(changed&&!io::saveProject(target,projectPath,&workflowError)){if(error)*error=workflowError;return false;}
    if(result){result->projectRoot=ludoRoot;result->projectPath=projectPath;}
    return true;
}

} // namespace core
