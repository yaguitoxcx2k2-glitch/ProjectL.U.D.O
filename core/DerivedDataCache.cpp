#include "DerivedDataCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
namespace core {
DerivedDataCache::DerivedDataCache(QString root):m_projectRoot(QDir::cleanPath(root)){}
void DerivedDataCache::setProjectRoot(QString root){m_projectRoot=QDir::cleanPath(root);}
QString DerivedDataCache::cacheRoot() const{return QDir(m_projectRoot).filePath(QStringLiteral(".ludo/cache/derived"));}
QString DerivedDataCache::keyFor(const QString& assetId,const QString& importerId,int importerVersion,const QByteArray& sourceHash) const {QByteArray b=assetId.toUtf8();b+='\0';b+=importerId.toUtf8();b+='\0';b+=QByteArray::number(importerVersion);b+='\0';b+=sourceHash;return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
QString DerivedDataCache::pathForKey(const QString& key,const QString& ext) const {const QString cleanExt=ext.startsWith('.')?ext:(ext.isEmpty()?QString():QStringLiteral(".")+ext);return QDir(cacheRoot()).filePath(key.left(2)+QLatin1Char('/')+key+cleanExt);}
bool DerivedDataCache::write(const QString& key,const QByteArray& data,const QString& ext,QString* error) const {const QString path=pathForKey(key,ext);if(!QDir().mkpath(QFileInfo(path).absolutePath())){if(error)*error=QStringLiteral("Não foi possível criar o cache derivado.");return false;}QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit()){if(error)*error=f.errorString();return false;}return true;}
QByteArray DerivedDataCache::read(const QString& key,const QString& ext,bool* ok) const {QFile f(pathForKey(key,ext));const bool opened=f.open(QIODevice::ReadOnly);if(ok)*ok=opened;return opened?f.readAll():QByteArray();}
bool DerivedDataCache::remove(const QString& key,const QString& ext) const{return QFile::remove(pathForKey(key,ext));}
} // namespace core
