#include "TeamRasterTransport.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <memory>

namespace ui {
namespace {
struct Packed { QJsonValue value; QHash<QString,QByteArray> blobs; };
QJsonValue pack(QJsonValue value,QHash<QString,QByteArray>& blobs){
    if(value.isArray()){QJsonArray out;for(const auto& v:value.toArray())out.append(pack(v,blobs));return out;}
    if(!value.isObject())return value;
    auto out=value.toObject();
    for(auto i=out.begin();i!=out.end();++i){
        if((i.key()=="imageSrc"||i.key()=="maskImageSrc"||i.key()=="panoramaSrc")&&i.value().isString()){
            const auto text=i.value().toString();
            if(text.startsWith("data:image/png;base64,")){
                const QByteArray data=QByteArray::fromBase64(text.mid(22).toLatin1());
                const QString hash=QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
                blobs.insert(hash,data);i.value()=QJsonObject{{"$raster",hash}};continue;
            }
        }
        i.value()=pack(i.value(),blobs);
    }
    return out;
}
void references(const QJsonValue& value,QSet<QString>& hashes){
    if(value.isObject()){
        const auto o=value.toObject();
        if(o.size()==1&&o.value("$raster").isString()){hashes.insert(o.value("$raster").toString());return;}
        for(auto i=o.begin();i!=o.end();++i)references(i.value(),hashes);
    }else if(value.isArray())for(const auto& v:value.toArray())references(v,hashes);
}
QJsonValue unpack(const QJsonValue& value,const QHash<QString,QByteArray>& blobs){
    if(value.isArray()){QJsonArray out;for(const auto& v:value.toArray())out.append(unpack(v,blobs));return out;}
    if(!value.isObject())return value;
    auto out=value.toObject();
    if(out.size()==1&&out.value("$raster").isString())return QStringLiteral("data:image/png;base64,")+QString::fromLatin1(blobs.value(out.value("$raster").toString()).toBase64());
    for(auto i=out.begin();i!=out.end();++i)i.value()=unpack(i.value(),blobs);
    return out;
}
struct Transfer {QJsonValue value;QHash<QString,QByteArray> blobs;QStringList hashes;int index=0;};
}
bool TeamRasterTransport::hasReferences(const QJsonValue& value){QSet<QString> hashes;references(value,hashes);return !hashes.isEmpty();}
void TeamRasterTransport::upload(const QJsonValue& value,const QUrl& base,const QString& token,Done done){
    auto* task=new QFutureWatcher<Packed>(this);
    connect(task,&QFutureWatcher<Packed>::finished,this,[this,task,base,token,done]{
        auto packed=task->result();task->deleteLater();
        auto state=std::make_shared<Transfer>();state->value=packed.value;state->blobs=std::move(packed.blobs);state->hashes=state->blobs.keys();
        auto step=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weak=step;
        *step=[this,state,base,token,done,weak]{
            if(state->index==state->hashes.size()){done(true,state->value);return;}
            const QString hash=state->hashes[state->index];QUrl url=base;url.setPath(base.path()+"/"+hash);
            QNetworkRequest req(url);req.setTransferTimeout(60000);req.setRawHeader("Authorization","Bearer "+token.toUtf8());
            req.setHeader(QNetworkRequest::ContentTypeHeader,"application/octet-stream");
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
            auto* reply=network.post(req,state->blobs.value(hash));
            connect(reply,&QNetworkReply::finished,this,[reply,state,done,next=weak.lock()]{
                const bool ok=reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200;reply->deleteLater();
                if(!ok){done(false,{});return;}++state->index;if(next)(*next)();
            });
        };
        if(state->hashes.isEmpty()){(*step)();return;}
        QUrl check=base;check.setPath(base.path()+"-check");
        QNetworkRequest req(check);req.setTransferTimeout(15000);req.setRawHeader("Authorization","Bearer "+token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        QJsonArray hashes;for(const auto& hash:state->hashes)hashes.append(hash);
        // Very large first maps may exceed the manifest check limit; uploading
        // directly remains correct and the server deduplicates bytes by hash.
        if(hashes.size()>1000){(*step)();return;}
        auto* reply=network.post(req,QJsonDocument(QJsonObject{{"hashes",hashes}}).toJson(QJsonDocument::Compact));
        connect(reply,&QNetworkReply::finished,this,[reply,state,done,step]{
            const auto response=QJsonDocument::fromJson(reply->readAll()).object();
            const bool ok=reply->error()==QNetworkReply::NoError&&response.value("missing").isArray();reply->deleteLater();
            if(!ok){done(false,{});return;}
            QStringList missing;for(const auto& hash:response.value("missing").toArray())missing<<hash.toString();
            state->hashes=missing;(*step)();
        });
    });
    task->setFuture(QtConcurrent::run([value]{Packed result;result.value=pack(value,result.blobs);return result;}));
}
void TeamRasterTransport::download(const QJsonValue& value,const QUrl& base,const QString& token,Done done){
    QSet<QString> hashes;references(value,hashes);
    if(hashes.isEmpty()){done(true,value);return;}
    auto state=std::make_shared<Transfer>();state->value=value;state->hashes=hashes.values();
    auto step=std::make_shared<std::function<void()>>();std::weak_ptr<std::function<void()>> weak=step;
    *step=[this,state,base,token,done,weak]{
        if(state->index==state->hashes.size()){done(true,unpack(state->value,state->blobs));return;}
        const QString hash=state->hashes[state->index];QUrl url=base;url.setPath(base.path()+"/"+hash);
        QNetworkRequest req(url);req.setTransferTimeout(60000);req.setRawHeader("Authorization","Bearer "+token.toUtf8());
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        auto* reply=network.get(req);
        connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 received,qint64 total){if(received>128LL*1024*1024||total>128LL*1024*1024)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[reply,state,hash,done,next=weak.lock()]{
            const QByteArray bytes=reply->readAll();
            const bool ok=reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==hash;
            reply->deleteLater();if(!ok){done(false,{});return;}
            state->blobs.insert(hash,bytes);++state->index;if(next)(*next)();
        });
    };(*step)();
}
}
