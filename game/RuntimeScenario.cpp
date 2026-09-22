#include "RuntimeScenario.h"
#include "GameSession.h"
#include "core/Editor.h"
#include "core/RuntimeProject.h"
#include "core/InputMap.h"
#include <QFont>
#include <QJsonArray>

namespace game {
bool RuntimeScenarioDefinition::parse(const QJsonObject& o, RuntimeScenarioDefinition* out, QString* error){
    if(error)error->clear(); if(!out){if(error)*error="Null scenario output";return false;}
    RuntimeScenarioDefinition d; d.frames=qMax(1,o.value("frames").toInt(1));
    bool seedOk=false; if(o.value("seed").isString()) d.seed=o.value("seed").toString().toULongLong(&seedOk); else {d.seed=quint64(o.value("seed").toDouble(double(d.seed)));seedOk=true;}
    if(!seedOk){if(error)*error="Invalid seed";return false;}
    const auto start=o.value("startPixel").toObject(); d.startPixel=QPointF(start.value("x").toDouble(),start.value("y").toDouble());
    for(const auto& v:o.value("inputs").toArray()){const auto x=v.toObject();RuntimeScenarioInput in;in.frame=x.value("frame").toInt();in.action=x.value("action").toString();in.pressed=x.value("pressed").toBool(true);bool ok=false;core::gameActionFromId(in.action,&ok);if(!ok){if(error)*error=QStringLiteral("Unknown action: %1").arg(in.action);return false;}d.inputs.push_back(in);}
    const auto e=o.value("expected").toObject();d.expected.mapId=e.value("mapId").toString();d.expected.stateHash=e.value("stateHash").toString();
    const auto vars=e.value("variables").toObject();for(auto it=vars.begin();it!=vars.end();++it)d.expected.variables[it.key().toInt()]=it.value().toInt();
    const auto sw=e.value("switches").toObject();for(auto it=sw.begin();it!=sw.end();++it)d.expected.switches[it.key().toInt()]=it.value().toBool();
    *out=std::move(d);return true;
}
bool RuntimeScenarioRunner::run(core::Editor& source,const RuntimeScenarioDefinition& d,QString* error,QString* finalHash){
    if(error)error->clear();QString boundaryError;auto runtime=core::RuntimeProject::fromEditor(source,&boundaryError);if(!runtime){if(error)*error=boundaryError;return false;}
    GameSession session(*runtime,d.startPixel,QFont());session.setDeterministicMode(true,d.seed);
    int inputIndex=0;for(int frame=0;frame<d.frames;++frame){while(inputIndex<d.inputs.size()&&d.inputs[inputIndex].frame==frame){bool ok=false;const auto a=core::gameActionFromId(d.inputs[inputIndex].action,&ok);if(ok){if(d.inputs[inputIndex].pressed)session.actionPress(a);else session.actionRelease(a);}++inputIndex;}session.tickFixedStep();}
    const QString hash=QString::fromLatin1(session.deterministicStateHash());if(finalHash)*finalHash=hash;
    if(!d.expected.mapId.isEmpty()&&session.currentMapId()!=d.expected.mapId){if(error)*error=QStringLiteral("Expected map %1, got %2").arg(d.expected.mapId,session.currentMapId());return false;}
    for(auto it=d.expected.variables.cbegin();it!=d.expected.variables.cend();++it)if(session.state().variable(it.key())!=it.value()){if(error)*error=QStringLiteral("Variable %1 mismatch").arg(it.key());return false;}
    for(auto it=d.expected.switches.cbegin();it!=d.expected.switches.cend();++it)if(session.state().switchOn(it.key())!=it.value()){if(error)*error=QStringLiteral("Switch %1 mismatch").arg(it.key());return false;}
    if(!d.expected.stateHash.isEmpty()&&hash.compare(d.expected.stateHash,Qt::CaseInsensitive)!=0){if(error)*error=QStringLiteral("State hash mismatch: %1").arg(hash);return false;}
    return true;
}
}
