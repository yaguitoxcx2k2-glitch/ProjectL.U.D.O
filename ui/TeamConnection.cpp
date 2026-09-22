#include "TeamConnection.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
namespace ui {
void TeamConnection::post(const QUrl& server,const QString& token,const QString& path,const QJsonObject& body,int timeoutMs,quint64 epoch,std::function<bool(quint64)> current,JsonDone done){
    QUrl url=server;url.setPath("/v1/"+path);QNetworkRequest req(url);req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");if(!token.isEmpty())req.setRawHeader("Authorization","Bearer "+token.toUtf8());
    auto* reply=network.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));
    QTimer::singleShot(timeoutMs+1500,reply,[reply]{if(reply->isRunning())reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[reply,epoch,current=std::move(current),done=std::move(done)]() mutable {
        if(!current(epoch)){reply->deleteLater();return;}
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto document=QJsonDocument::fromJson(reply->readAll());
        const bool ok=reply->error()==QNetworkReply::NoError&&status==200&&document.isObject();const QString reason=document.object().value("error").toString(reply->errorString());
        const auto object=document.object();reply->deleteLater();done(ok,status,object,reason);
    });
}
}
