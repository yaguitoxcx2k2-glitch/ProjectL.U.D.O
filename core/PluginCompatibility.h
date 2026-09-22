#pragma once
#include <QString>
#include <QStringList>
namespace core {
struct NoCodePlugin;
inline constexpr int LudoPluginApiVersion = 2;
struct PluginCompatibilityReport { bool compatible=true; int requestedApi=1; int supportedApi=LudoPluginApiVersion; QStringList requiredCapabilities; QStringList unsupportedCapabilities; QString message; };
class PluginCompatibility {
public:
    static QStringList supportedCapabilities();
    static PluginCompatibilityReport evaluate(int requestedApi,const QStringList& capabilities);
    static PluginCompatibilityReport evaluate(const NoCodePlugin& plugin);
};
} // namespace core
