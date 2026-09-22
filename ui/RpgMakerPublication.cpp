#include "RpgMakerPublication.h"
#include "CollaborationClient.h"
#include "RpgMakerExporter.h"
#include "RpgMakerMvReset.h"
#include "core/ProjectIO.h"
#include "core/PublicationPolicy.h"
#include <QCryptographicHash>
#include <QDialog>
#include <QDateTime>
#include <QTemporaryDir>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace ui {
namespace {
QString digest(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QJsonValue canonical(const QJsonValue& value) {
    if(value.isArray()){QJsonArray result;for(const auto& v:value.toArray())result.append(canonical(v));return result;}
    if(!value.isObject())return value;
    auto result=value.toObject();
    for(auto it=result.begin();it!=result.end();) {
        if(it.key()=="activeLayerIdx"||it.key()=="activeLayerId"||it.key()=="collapsed"||it.key()=="source"||it.key()=="sourcePath"||it.key()=="panoramaPath")it=result.erase(it);
        else{it.value()=canonical(it.value());++it;}
    }
    return result;
}
QJsonObject publicationSnapshot(QJsonObject payload) {
    for(const auto& key:{"activeMapDocIdx","rpgMakerProjectRoot","assetDatabase","assetReferences","editorVersion"})payload.remove(key);
    return canonical(payload).toObject();
}
bool readJson(const QString& path,QJsonDocument& result) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
    QJsonParseError error;result=QJsonDocument::fromJson(file.readAll(),&error);return error.error==QJsonParseError::NoError;
}
bool writeState(const QString& path,const QJsonObject& state) {
    QSaveFile file(path);const auto data=QJsonDocument(state).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
}
bool targetManifestMatches(const QString& root,int target,const QString& projectId,const QString& mapId) {
    if(target<=0)return false;
    QJsonDocument document;
    if(!readJson(QDir(root).filePath(QStringLiteral("data/ludoMaps/")+rpgMaker::mapJsonName(target)),document)||!document.isObject())return false;
    const QJsonObject source=document.object().value(QStringLiteral("editorSource")).toObject();
    if(source.value(QStringLiteral("projectId")).toString()==projectId)return true;
    for(const auto& value:source.value(QStringLiteral("maps")).toArray())
        if(value.toObject().value(QStringLiteral("id")).toString()==mapId)return true;
    return false;
}
struct Entry {QString id,name,fingerprint;int target=0,parent=0,depth=0;bool changed=false,exists=false,reassigned=false;};
}
void runRpgMakerPublication(core::Editor& ed,QWidget* parent,CollaborationClient* team) {
    const QString engineName=core::rpgMakerEngineName(ed.rpgMakerEngine);
    const QString engineShort=core::rpgMakerEngineId(ed.rpgMakerEngine).toUpper();
    const QString settingsPrefix=QStringLiteral("LudoRpgMaker/")+core::rpgMakerEngineId(ed.rpgMakerEngine);
    const bool online=team&&team->attached();
    int revision=0;
    if(online && !team->preparePublication(revision))return;
    if(ed.docs.isEmpty())return;
    const QString projectKey=digest(ed.projectId.toUtf8());
    QSettings settings;
    QString root=ed.rpgMakerProjectRoot;
    QString reason;
    if(!rpgMaker::isRpgMakerProjectRoot(root,ed.rpgMakerEngine,&reason)) {
        QMessageBox::warning(parent,QObject::tr("Projeto RPG Maker não vinculado"),
            QObject::tr("Abra o projeto RPG Maker pela Home ou use Vincular projeto antes de atualizar.\n\n%1").arg(reason));return;
    }
    root=QFileInfo(root).canonicalFilePath();
    QLockFile lock(QDir(root).filePath(".ludo-publication.lock"));
    lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){QMessageBox::warning(parent,QObject::tr("Atualização em andamento"),QObject::tr("Outra atualização está usando esta pasta. Aguarde e tente novamente."));return;}
    const QString statePath=QDir(root).filePath("data/ludoMaps/publication-"+projectKey+".json");
    QJsonObject state,records;
    if(QFileInfo::exists(statePath)) {
        QJsonDocument stored;
        if(!readJson(statePath,stored)||!stored.isObject()||stored.object().value("projectId").toString()!=ed.projectId) {
            QMessageBox::warning(parent,QObject::tr("Registro de atualização inválido"),QObject::tr("Não foi possível ler o histórico de atualização desta pasta. Restaure o registro antes de continuar."));return;
        }
        state=stored.object();records=state.value("maps").toObject();
    }
    QJsonDocument mapInfosDocument;
    if(!readJson(QDir(root).filePath("data/MapInfos.json"),mapInfosDocument)||!mapInfosDocument.isArray()) {
        QMessageBox::warning(parent,QObject::tr("Atualização RPG Maker"),QObject::tr("Não foi possível ler MapInfos.json."));return;
    }
    const auto infos=mapInfosDocument.array();
    std::set<int> occupied,assigned;
    for(int i=1;i<infos.size();++i)if(infos[i].isObject())occupied.insert(i);
    for(const auto& file:QDir(QDir(root).filePath("data")).entryList({"Map*.json"},QDir::Files)) {
        bool ok=false;const int id=file.mid(3,file.size()-8).toInt(&ok);if(ok&&id>0)occupied.insert(id);
    }
    // Avoid handing an unbound map an ID reserved by a later bound map.
    for(const auto& doc:ed.docs) {
        int id=records.value(doc.id).toObject().value("target").toInt(doc.rpgMakerMapId);
        if(id>0)occupied.insert(id);
    }
    QHash<int,QString> otherOwners;
    const QDir manifests(QDir(root).filePath("data/ludoMaps"));
    for(const auto& file:manifests.entryList({"publication-*.json"},QDir::Files)) {
        if(manifests.filePath(file)==statePath)continue;
        QJsonDocument other;
        if(!readJson(manifests.filePath(file),other)||!other.isObject()) {
            QMessageBox::warning(parent,QObject::tr("Registro inválido"),QObject::tr("Não foi possível verificar outro registro de publicação nesta pasta: %1").arg(file));return;
        }
        const auto maps=other.object().value("maps").toObject();
        for(auto it=maps.begin();it!=maps.end();++it) {
            const int id=it.value().toObject().value("target").toInt();
            if(id>0){occupied.insert(id);otherOwners[id]=file;}
        }
    }
    const auto payload=core::io::buildProjectPayload(ed);
    const auto reviewed=publicationSnapshot(payload);
    auto shared=payload;
    for(const auto& key:{"maps","activeMapDocIdx","rpgMakerProjectRoot","assetDatabase","assetReferences","editorVersion"})shared.remove(key);
    const bool fitGrid=settings.value(settingsPrefix+QStringLiteral("/referenceFitGrid"),false).toBool();
    shared["publicationReferenceFitGrid"]=fitGrid;
    shared["publicationFormat"]=1;
    const QString resourceHash=digest(QJsonDocument(canonical(shared).toObject()).toJson(QJsonDocument::Compact));
    QHash<QString,QJsonObject> mapPayloads;
    for(const auto& map:payload.value("maps").toArray()){const auto m=map.toObject();mapPayloads[m.value("id").toString()]=m;}
    QVector<Entry> entries;QHash<QString,int> targets;
    for(const auto& doc:ed.docs) {
        Entry e;e.id=doc.id;e.name=doc.name;
        const auto old=records.value(doc.id).toObject();
        const int previous=old.value("target").toInt();
        const int preferred=previous>0?previous:doc.rpgMakerMapId;
        std::set<int> blocked;
        for(auto it=otherOwners.constBegin();it!=otherOwners.constEnd();++it)
            if(!targetManifestMatches(root,it.key(),ed.projectId,doc.id))blocked.insert(it.key());
        e.target=core::reconciledPublicationMapId(previous,doc.rpgMakerMapId,occupied,assigned,blocked);

        // Um manifesto publication-*.json de outra cópia do LUDO não deve
        // bloquear para sempre o mesmo mapa lógico. Se o alvo pertence de fato
        // a outro mapa (ou um vínculo duplicado antigo), a política central
        // escolhe um Map ID livre e o vínculo é atualizado após exportar.
        e.reassigned=preferred>0&&e.target!=preferred;
        if(e.target<1) {
            QMessageBox::warning(parent,QObject::tr("Vínculo de mapas"),QObject::tr("Não há um Map ID livre para publicar %1 sem sobrescrever outro mapa.").arg(doc.name));return;
        }
        assigned.insert(e.target);targets[e.id]=e.target;entries.push_back(e);
    }
    for(auto& e:entries) {
        const auto* doc=ed.mapById(e.id);QString ancestor=doc->parentId;QSet<QString> visited{e.id};
        e.parent=targets.value(ancestor,0);
        while(!ancestor.isEmpty()) {
            if(visited.contains(ancestor)){QMessageBox::warning(parent,QObject::tr("Árvore de mapas"),QObject::tr("Há um ciclo na árvore de mapas."));return;}
            visited.insert(ancestor);const auto* d=ed.mapById(ancestor);if(!d)break;
            ++e.depth;ancestor=d->parentId;
        }
        auto value=canonical(mapPayloads.value(e.id)).toObject();
        value["publicationTarget"]=e.target;value["publicationParent"]=e.parent;value["resources"]=resourceHash;
        e.fingerprint=digest(QJsonDocument(value).toJson(QJsonDocument::Compact));
        e.exists=QFileInfo::exists(QDir(root).filePath("data/"+rpgMaker::mapJsonName(e.target)));
        const bool manifestExists=QFileInfo::exists(QDir(root).filePath("data/ludoMaps/"+rpgMaker::mapJsonName(e.target)));
        e.changed=core::publicationChanged(records.value(e.id).toObject().value("fingerprint").toString()==e.fingerprint,e.exists,manifestExists);
    }
    std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.depth<b.depth;});
    QDialog dialog(parent);dialog.setWindowTitle(QObject::tr("Atualizar %1").arg(engineName));dialog.resize(790,480);
    QVBoxLayout layout(&dialog);
    QLabel destination(QObject::tr("Destino: %1").arg(QDir::toNativeSeparators(root)),&dialog);destination.setWordWrap(true);layout.addWidget(&destination);
    QLabel hint(online?QObject::tr("Versão %1 da equipe confirmada. O RPG Maker será atualizado com esse estado.").arg(revision):QObject::tr("O RPG Maker será atualizado com o projeto aberto. Os eventos existentes serão preservados."),&dialog);
    hint.setWordWrap(true);layout.addWidget(&hint);
    QTableWidget table(entries.size(),3,&dialog);table.setHorizontalHeaderLabels({QObject::tr("Mapa"),QObject::tr("Destino %1").arg(engineShort),QObject::tr("Situação")});
    table.setEditTriggers(QAbstractItemView::NoEditTriggers);table.horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    for(int i=0;i<entries.size();++i) {
        const auto& e=entries[i];auto* name=new QTableWidgetItem(e.name);
        name->setCheckState(e.changed?Qt::Checked:Qt::Unchecked);table.setItem(i,0,name);
        const QString existingName=e.target<infos.size()?infos[e.target].toObject().value("name").toString():QString();
        auto* target=new QTableWidgetItem(QString("MAP%1").arg(e.target,3,10,QLatin1Char('0'))+(existingName.isEmpty()?QString():" · "+existingName));
        target->setToolTip(e.exists?QObject::tr("Atualizar: %1").arg(existingName):QObject::tr("Criar novo mapa"));table.setItem(i,1,target);
        QString situation=e.changed?(e.exists?QObject::tr("Alterado"):QObject::tr("Novo")):QObject::tr("Atualizado");
        if(e.reassigned)situation=QObject::tr("Novo destino");
        table.setItem(i,2,new QTableWidgetItem(situation));
    }
    table.setColumnWidth(1,220);table.setColumnWidth(2,110);
    layout.addWidget(&table);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons.button(QDialogButtonBox::Ok)->setText(QObject::tr("Atualizar selecionados"));layout.addWidget(&buttons);
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    QSet<int> selected;
    for(int i=0;i<entries.size();++i)if(table.item(i,0)->checkState()==Qt::Checked)selected.insert(entries[i].target);
    if(selected.isEmpty())return;
    for(const auto& e:entries)if(selected.contains(e.target)&&e.parent>0&&!selected.contains(e.parent)&&!QFileInfo::exists(QDir(root).filePath("data/"+rpgMaker::mapJsonName(e.parent)))) {
        QMessageBox::information(parent,QObject::tr("Selecione o mapa pai"),QObject::tr("O mapa pai de %1 ainda não existe no jogo. Selecione-o também.").arg(e.name));return;
    }
    if(online) {
        if(!team->verifyPublication(revision))return;
    }
    if(publicationSnapshot(core::io::buildProjectPayload(ed))!=reviewed) {
        QMessageBox::information(parent,QObject::tr("Projeto atualizado"),QObject::tr("O projeto mudou durante a revisão. Abra a atualização novamente para conferir as alterações."));return;
    }
    // Export a fixed snapshot: timers and local source watchers cannot change
    // tilesets underneath an in-progress export or its recorded fingerprint.
    QTemporaryDir snapshotFolder;
    if(!snapshotFolder.isValid()){QMessageBox::warning(parent,QObject::tr("Atualização RPG Maker"),QObject::tr("Não foi possível criar a cópia temporária."));return;}
    const QString snapshotPath=QDir(snapshotFolder.path()).filePath("snapshot.ludo");
    if(!writeState(snapshotPath,reviewed)){QMessageBox::warning(parent,QObject::tr("Atualização RPG Maker"),QObject::tr("Não foi possível preparar a atualização temporária."));return;}
    core::Editor frozen;QString snapshotError;
    if(!core::io::loadProject(frozen,snapshotPath,&snapshotError,core::io::ProjectLoadMode::ReadOnlyPreview)) {
        QMessageBox::warning(parent,QObject::tr("Preparar atualização"),snapshotError);return;
    }
    if(!QDir().mkpath(QFileInfo(statePath).absolutePath())){QMessageBox::warning(parent,QObject::tr("Atualização RPG Maker"),QObject::tr("Não foi possível criar a pasta do registro."));return;}
    state["projectId"]=ed.projectId;state["format"]=1;state["maps"]=records;
    // Verify tracking is writable before exporting anything.
    if(!writeState(statePath,state)){QMessageBox::warning(parent,QObject::tr("Atualização RPG Maker"),QObject::tr("Não foi possível gravar o registro da atualização."));return;}
    QDialog progress(parent);progress.setWindowTitle(QObject::tr("Atualizando mapas"));QVBoxLayout progressLayout(&progress);QLabel message(&progress);progressLayout.addWidget(&message);progress.resize(450,100);progress.show();
    int succeeded=0;bool bindingUpdated=false;QStringList failures;QSet<int> failedIds,attempted;
    for(const auto& e:entries) {
        if(!selected.contains(e.target))continue;
        attempted.insert(e.target);
        message.setText(QObject::tr("Atualizando %1…").arg(e.name));progress.repaint();
        if(failedIds.contains(e.parent)){failures<<e.name+QObject::tr(": o mapa pai falhou");failedIds.insert(e.target);continue;}
        const auto* doc=frozen.mapById(e.id);if(!doc){failures<<e.name;failedIds.insert(e.target);continue;}
        if(!rpgMaker::exportBoundMap(frozen,*doc,root,e.target,fitGrid,parent,false,e.parent)) {
            failures<<e.name;failedIds.insert(e.target);continue;
        }
        QJsonObject record{{"target",e.target},{"fingerprint",e.fingerprint},{"teamRevision",revision},{"publishedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
        records[e.id]=record;state["maps"]=records;
        if(!writeState(statePath,state)) {
            failures<<e.name+QObject::tr(": exportado, mas o registro não foi salvo; atualize novamente");break;
        }
        // Se a publicação precisou reparar um vínculo antigo/conflitante, o
        // Editor passa a usar o Map ID confirmado. Isso evita repetir a mesma
        // reconciliação na próxima publicação e mantém a árvore coerente.
        if(auto* live=ed.mapById(e.id);live&&live->rpgMakerMapId!=e.target){
            live->rpgMakerMapId=e.target;ed.markDirty();bindingUpdated=true;
        }
        ++succeeded;
    }
    progress.close();
    if(bindingUpdated)emit ed.projectChanged();
    for(const auto& e:entries)if(selected.contains(e.target)&&!attempted.contains(e.target))failures<<e.name+QObject::tr(": não iniciado");
    QString result=succeeded==1?QObject::tr("1 mapa atualizado."):QObject::tr("%1 mapas atualizados.").arg(succeeded);
    if(!failures.isEmpty())result+=QObject::tr("\n\nPendentes:\n%1").arg(failures.join("\n"));
    result += ed.rpgMakerEngine == core::RpgMakerEngine::MV
        ? QObject::tr("\n\nO LUDO pode resetar o RPG Maker MV na próxima etapa para carregar os mapas atualizados. O LudoMapSystem precisa estar instalado e ativado.")
        : QObject::tr("\n\nReabra o projeto no %1 para atualizar a árvore e execute o Playtest. O LudoMapSystem precisa estar instalado e ativado.").arg(engineName);
    QMessageBox::information(parent,failures.isEmpty()?QObject::tr("Atualização RPG Maker concluída"):QObject::tr("Atualização RPG Maker com pendências"),result);
    if(succeeded>0) rpgMakerMvReset::askAndReset(parent,ed.rpgMakerEngine,root);
}
}
