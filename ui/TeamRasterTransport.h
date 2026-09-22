#pragma once
#include <QObject>
#include <QJsonValue>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QHash>
#include <functional>
namespace ui {
class TeamRasterTransport final : public QObject {
public:
    using Done=std::function<void(bool,const QJsonValue&)>;
    explicit TeamRasterTransport(QObject* parent=nullptr):QObject(parent){}
    void upload(const QJsonValue& value,const QUrl& base,const QString& token,Done done);
    void download(const QJsonValue& value,const QUrl& base,const QString& token,Done done);
    static bool hasReferences(const QJsonValue& value);
private:
    QNetworkAccessManager network;
};
}
