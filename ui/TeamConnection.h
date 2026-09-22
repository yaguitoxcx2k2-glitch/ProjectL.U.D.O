#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QUrl>
#include <functional>
namespace ui {
class TeamConnection final : public QObject {
public:
    using JsonDone=std::function<void(bool,int,const QJsonObject&,const QString&)>;
    explicit TeamConnection(QObject* parent=nullptr):QObject(parent){}
    QNetworkAccessManager& manager(){return network;}
    void post(const QUrl& server,const QString& token,const QString& path,const QJsonObject& body,int timeoutMs,quint64 epoch,std::function<bool(quint64)> current,JsonDone done);
private: QNetworkAccessManager network;
};
}
