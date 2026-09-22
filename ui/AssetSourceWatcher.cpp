#include "AssetSourceWatcher.h"
#include "core/Editor.h"
#include "core/ResourceManager.h"
#include "core/ProjectIO.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>

namespace ui {
AssetSourceWatcher::AssetSourceWatcher(core::Editor& editor,QObject* parent):QObject(parent),ed(editor) {
    debounce.setSingleShot(true);debounce.setInterval(350);
    connect(&watcher,&QFileSystemWatcher::fileChanged,this,[this](const QString& path){notifiedFiles.insert(path);debounce.start();});
    connect(&watcher,&QFileSystemWatcher::directoryChanged,this,[this]{debounce.start();});
    connect(&debounce,&QTimer::timeout,this,[this]{scan();});
    // Covers platform watch limits and creation of Assets after project open.
    fallback.setInterval(5000);connect(&fallback,&QTimer::timeout,this,[this]{scan();});fallback.start();
    connect(&ed,&core::Editor::projectChanged,this,[this]{if(root!=ed.projectRoot())rescan();});
    rescan();
}
void AssetSourceWatcher::rescan(){
    if(root!=ed.projectRoot()){
        root=ed.projectRoot();stamps.clear();notifiedFiles.clear();
        if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());
        if(!watcher.directories().isEmpty())watcher.removePaths(watcher.directories());
    }
    debounce.start();
}
void AssetSourceWatcher::scan(){
    if(root!=ed.projectRoot()){rescan();return;}
    if(root.isEmpty())return;
    if(running){again=true;return;}
    QHash<QString,QString> next;QStringList directories,files,changed;
    const QDir project(root);const QString assets=project.filePath("Assets");
    directories<<root;
    if(QFileInfo(assets).isDir())directories<<assets;
    QDirIterator it(assets,QDir::AllEntries|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()){
        const QString path=it.next();const QFileInfo info(path);
        if(info.isSymLink())continue;
        if(info.isDir()){directories<<path;continue;}
        if(!info.isFile())continue;
        const QString stamp=QString::number(info.size())+":"+QString::number(info.lastModified().toMSecsSinceEpoch());
        next.insert(path,stamp);files<<path;
        if(stamps.value(path)!=stamp||notifiedFiles.contains(path))changed<<project.relativeFilePath(path);
    }
    bool structural=next.size()!=stamps.size();
    for(auto i=stamps.begin();i!=stamps.end();++i)if(!next.contains(i.key())){changed<<project.relativeFilePath(i.key());structural=true;}
    for(auto i=next.begin();i!=next.end();++i)if(!stamps.contains(i.key()))structural=true;
    const auto watchedFiles=watcher.files(),watchedDirs=watcher.directories();
    for(const auto& p:watchedFiles)if(!next.contains(p))watcher.removePath(p);
    for(const auto& p:watchedDirs)if(!directories.contains(p))watcher.removePath(p);
    for(const auto& p:files)if(!watchedFiles.contains(p))watcher.addPath(p);
    for(const auto& p:directories)if(!watchedDirs.contains(p))watcher.addPath(p);
    if(changed.isEmpty())return;
    running=true;const QString scanRoot=root;
    auto database=ed.assetDatabase;database.metadataChanged={};
    const auto original=ed.assetDatabase.toJson();
    using Result=QPair<core::AssetDatabase,QString>;
    auto* task=new QFutureWatcher<Result>(this);
    connect(task,&QFutureWatcher<Result>::finished,this,[this,task,scanRoot,original,next,changed,structural]{
        auto result=task->result();task->deleteLater();running=false;
        if(scanRoot==ed.projectRoot()&&result.second.isEmpty()){
            // Network/import/metadata may have changed the catalog while hashing.
            // Retry against that newer state instead of replacing it.
            if(ed.assetDatabase.toJson()!=original){again=true;}
            else {
                stamps=next;notifiedFiles.clear();
                const auto callback=ed.assetDatabase.metadataChanged;
                ed.assetDatabase=std::move(result.first);ed.assetDatabase.metadataChanged=callback;
                for(const auto& move:ed.assetDatabase.takePathChanges())core::io::rewriteEditorAssetPath(ed,move.oldPath,move.newPath);
                bool visual=false;
                std::function<void(const QVector<core::LayerPtr>&)> reload=[&](const QVector<core::LayerPtr>& layers){
                    for(const auto& layer:layers){
                        if(!layer)continue;
                        if(changed.contains(layer->imagePath)&&!layer->imagePaintLayer){
                            QImage image(QDir(scanRoot).filePath(layer->imagePath));
                            if(!image.isNull()&&image!=layer->image){layer->image=image;layer->imagewidth=image.width();layer->imageheight=image.height();visual=true;}
                        }
                        reload(layer->children);
                    }
                };
                for(auto& doc:ed.docs){reload(doc.layers);if(changed.contains(doc.map.panoramaPath)){
                    QImage image(QDir(scanRoot).filePath(doc.map.panoramaPath));
                    if(!image.isNull()&&image!=doc.map.panorama){doc.map.panorama=image;visual=true;}
                }}
                if(ed.assetDatabase.toJson()!=original)ed.markDirty();
                if(visual){ed.markDirty();emit ed.docsChanged();emit ed.mapChanged();}
                ed.resources().notifyAssetsChanged(structural?QStringList():changed);
            }
        }else if(scanRoot==ed.projectRoot())again=true;
        if(again){again=false;debounce.start();}
    });
    task->setFuture(QtConcurrent::run([database,scanRoot,changed,structural]() mutable {
        QString error;
        if(structural)database.synchronize(scanRoot,&error);else database.synchronizePaths(scanRoot,changed,&error);
        return Result(std::move(database),error);
    }));
}
}
