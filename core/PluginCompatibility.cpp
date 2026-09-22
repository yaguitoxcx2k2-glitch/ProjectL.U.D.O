#include "PluginCompatibility.h"
#include "NoCodePlugin.h"
#include <QObject>
#include <QSet>
namespace core {
QStringList PluginCompatibility::supportedCapabilities(){return {QStringLiteral("events.generate"),QStringLiteral("events.commands.safe"),QStringLiteral("assets.read"),QStringLiteral("project.metadata.read")};}
PluginCompatibilityReport PluginCompatibility::evaluate(int api,const QStringList& caps){PluginCompatibilityReport r;r.requestedApi=api;r.requiredCapabilities=caps;const QStringList supportedList=supportedCapabilities();const QSet<QString> supported(supportedList.cbegin(),supportedList.cend());for(const QString& c:caps)if(!supported.contains(c))r.unsupportedCapabilities.push_back(c);r.compatible=api>=1&&api<=LudoPluginApiVersion&&r.unsupportedCapabilities.isEmpty();if(api>LudoPluginApiVersion)r.message=QObject::tr("O plugin exige API %1, mas esta LUDO suporta até %2.").arg(api).arg(LudoPluginApiVersion);else if(api<1)r.message=QObject::tr("A versão da API do plugin é inválida.");else if(!r.unsupportedCapabilities.isEmpty())r.message=QObject::tr("Capabilities não suportadas: %1").arg(r.unsupportedCapabilities.join(QStringLiteral(", ")));return r;}
PluginCompatibilityReport PluginCompatibility::evaluate(const NoCodePlugin& p){return evaluate(p.apiVersion,p.capabilities);}
} // namespace core
