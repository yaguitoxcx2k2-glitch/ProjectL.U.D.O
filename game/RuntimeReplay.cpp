#include "RuntimeReplay.h"
#include <QJsonArray>
namespace game {
void RuntimeReplay::record(quint64 frame, QString action, bool pressed){m_frames.push_back({frame,std::move(action),pressed});}
QJsonObject RuntimeReplay::toJson() const {
    QJsonArray a; for(const auto& f:m_frames)a.append(QJsonObject{{"frame",double(f.frame)},{"action",f.action},{"pressed",f.pressed}});
    return QJsonObject{{"version",1},{"seed",QString::number(m_seed)},{"frames",a}};
}
bool RuntimeReplay::fromJson(const QJsonObject& o, QString* error){
    if(error)error->clear(); if(o.value("version").toInt()!=1){if(error)*error="Unsupported replay version";return false;}
    bool ok=false; const quint64 seed=o.value("seed").toString().toULongLong(&ok); if(!ok){if(error)*error="Invalid replay seed";return false;}
    QVector<RuntimeReplayFrame> parsed; for(const auto& v:o.value("frames").toArray()){const auto x=v.toObject();const QString action=x.value("action").toString();if(action.isEmpty()){if(error)*error="Replay action is empty";return false;}parsed.push_back({quint64(x.value("frame").toDouble()),action,x.value("pressed").toBool()});}
    m_seed=seed;m_frames=std::move(parsed);return true;
}
}
