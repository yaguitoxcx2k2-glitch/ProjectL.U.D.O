#include "core/AssetDatabase.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <iostream>
#define CHECK(condition) do { if(!(condition)){std::cerr<<"Failed line "<<__LINE__<<": "<<#condition<<'\n';return 1;} }while(false)
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temp;CHECK(temp.isValid());
    QDir root(temp.path());CHECK(root.mkpath("Assets"));
    auto write=[&](const QString& name,const QByteArray& bytes){QFile file(root.filePath(name));return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();};
    CHECK(write("Assets/voice.ogg","audio"));CHECK(write("Assets/font.ttf","font"));CHECK(write("Assets/movie.webm","video"));CHECK(write("Assets/data.bin","other"));
    core::AssetDatabase db;QString error;CHECK(db.synchronize(temp.path(),&error));CHECK(db.records().size()==4);
    CHECK(db.recordByPath("Assets/voice.ogg")->type=="audio");CHECK(db.recordByPath("Assets/font.ttf")->type=="font");
    CHECK(db.recordByPath("Assets/movie.webm")->type=="video");
    const auto id=db.idForPath("Assets/voice.ogg");CHECK(QFile::rename(root.filePath("Assets/voice.ogg"),root.filePath("Assets/moved.ogg")));
    CHECK(db.synchronize(temp.path(),&error));CHECK(db.idForPath("Assets/moved.ogg")==id);CHECK(db.takePathChanges().size()==1);
    int notifications=0;db.metadataChanged=[&](const QString&){++notifications;};
    CHECK(db.setMetadataValueForPath("Assets/moved.ogg","favorite",true));CHECK(notifications==1);
    auto copy=db;CHECK(!copy.metadataChanged);db=copy;
    CHECK(db.setMetadataValueForPath("Assets/moved.ogg","favorite",false));CHECK(notifications==2);
    CHECK(QFile::remove(root.filePath("Assets/moved.ogg")));CHECK(write("Assets/copy1.ogg","audio"));CHECK(write("Assets/copy2.ogg","audio"));
    CHECK(db.synchronize(temp.path(),&error));CHECK(db.recordById(id)->missing);
    CHECK(db.idForPath("Assets/copy1.ogg")!=id);CHECK(db.idForPath("Assets/copy2.ogg")!=id);
    CHECK(root.mkpath("Assets/LUDO/Team"));CHECK(write("Assets/LUDO/Team/internal.bin","internal"));CHECK(db.synchronize(temp.path(),&error));
    CHECK(db.recordByPath("Assets/LUDO/Team/internal.bin")->category=="internal");
    CHECK(QDir(root.filePath("Assets")).removeRecursively());CHECK(db.synchronize(temp.path(),&error));CHECK(db.missingRecords().size()==db.records().size());
    return 0;
}
