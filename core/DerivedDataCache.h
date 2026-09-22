#pragma once
#include <QByteArray>
#include <QString>
namespace core {
class DerivedDataCache {
public:
    explicit DerivedDataCache(QString projectRoot={});
    void setProjectRoot(QString projectRoot);
    QString cacheRoot() const;
    QString keyFor(const QString& assetId,const QString& importerId,int importerVersion,const QByteArray& sourceHash) const;
    QString pathForKey(const QString& key,const QString& extension={}) const;
    bool write(const QString& key,const QByteArray& data,const QString& extension={},QString* error=nullptr) const;
    QByteArray read(const QString& key,const QString& extension={},bool* ok=nullptr) const;
    bool remove(const QString& key,const QString& extension={}) const;
private: QString m_projectRoot;
};
} // namespace core
