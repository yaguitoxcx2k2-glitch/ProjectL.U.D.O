#include "RuntimeStateHash.h"
#include "GameState.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
namespace game {
QByteArray RuntimeStateHash::calculate(const GameState& state,const QString& mapId,const QPointF& pos,quint64 rngState){
    QJsonObject root{{"map",mapId},{"x",pos.x()},{"y",pos.y()},{"rng",QString::number(rngState)},{"state",state.toJson()}};
    return QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex();
}
}
