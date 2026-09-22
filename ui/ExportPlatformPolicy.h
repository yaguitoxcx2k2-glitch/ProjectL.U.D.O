#pragma once
#include <QString>
#include <QStringList>
namespace ui {
enum class ExportPlatform { Windows, Linux, MacOS, Unknown };
struct ExportPlatformProfile { ExportPlatform platform=ExportPlatform::Unknown; QString id; QString displayName; QString executableSuffix; QStringList preferredBackends; bool portablePackaging=false; bool deploymentToolExpected=false; QString deploymentTool; };
class ExportPlatformPolicy {
public:
    static ExportPlatform hostPlatform();
    static ExportPlatformProfile profile(ExportPlatform platform);
    static ExportPlatformProfile hostProfile();
    static QString platformId(ExportPlatform platform);
};
} // namespace ui
