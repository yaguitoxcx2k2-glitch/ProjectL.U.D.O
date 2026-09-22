#include "RuntimeResourceBudget.h"
#include <QtGlobal>
namespace game {
RuntimeResourceBudget::RuntimeResourceBudget(RuntimeResourceBudgetLimits limits):m_limits(limits){
    for (RuntimeResourceClass c : {RuntimeResourceClass::Textures,RuntimeResourceClass::Audio,RuntimeResourceClass::MapChunks,RuntimeResourceClass::Pictures,RuntimeResourceClass::DerivedData}) {
        RuntimeResourceUsage u; u.resourceClass=c; u.budgetBytes=limitFor(c); m_usage.push_back(u);
    }
}
void RuntimeResourceBudget::setLimits(RuntimeResourceBudgetLimits limits){m_limits=limits;for(auto& u:m_usage)u.budgetBytes=limitFor(u.resourceClass);}
qint64 RuntimeResourceBudget::limitFor(RuntimeResourceClass c) const {switch(c){case RuntimeResourceClass::Textures:return m_limits.textures;case RuntimeResourceClass::Audio:return m_limits.audio;case RuntimeResourceClass::MapChunks:return m_limits.mapChunks;case RuntimeResourceClass::Pictures:return m_limits.pictures;case RuntimeResourceClass::DerivedData:return m_limits.derivedData;}return 0;}
RuntimeResourceUsage& RuntimeResourceBudget::mutableUsage(RuntimeResourceClass c){for(auto& u:m_usage)if(u.resourceClass==c)return u;Q_UNREACHABLE();}
void RuntimeResourceBudget::setUsage(RuntimeResourceClass c,qint64 bytes){auto& u=mutableUsage(c);u.usedBytes=qMax<qint64>(0,bytes);u.peakBytes=qMax(u.peakBytes,u.usedBytes);}
void RuntimeResourceBudget::addUsage(RuntimeResourceClass c,qint64 delta){auto& u=mutableUsage(c);setUsage(c,u.usedBytes+delta);}
void RuntimeResourceBudget::recordEviction(RuntimeResourceClass c,qint64 released){auto& u=mutableUsage(c);++u.evictions;setUsage(c,u.usedBytes-qMax<qint64>(0,released));}
RuntimeResourceUsage RuntimeResourceBudget::usage(RuntimeResourceClass c) const {for(const auto& u:m_usage)if(u.resourceClass==c)return u;return {};}
QVector<RuntimeResourceUsage> RuntimeResourceBudget::snapshot() const{return m_usage;}
qint64 RuntimeResourceBudget::bytesOverBudget(RuntimeResourceClass c) const {const auto u=usage(c);return qMax<qint64>(0,u.usedBytes-u.budgetBytes);}
QString RuntimeResourceBudget::resourceClassId(RuntimeResourceClass c){switch(c){case RuntimeResourceClass::Textures:return QStringLiteral("textures");case RuntimeResourceClass::Audio:return QStringLiteral("audio");case RuntimeResourceClass::MapChunks:return QStringLiteral("mapChunks");case RuntimeResourceClass::Pictures:return QStringLiteral("pictures");case RuntimeResourceClass::DerivedData:return QStringLiteral("derivedData");}return {};}
} // namespace game
