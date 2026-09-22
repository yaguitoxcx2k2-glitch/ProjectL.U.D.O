#include "ExportPlatformPolicy.h"
namespace ui {
ExportPlatform ExportPlatformPolicy::hostPlatform(){
#ifdef Q_OS_WIN
 return ExportPlatform::Windows;
#elif defined(Q_OS_MACOS)
 return ExportPlatform::MacOS;
#elif defined(Q_OS_LINUX)
 return ExportPlatform::Linux;
#else
 return ExportPlatform::Unknown;
#endif
}
QString ExportPlatformPolicy::platformId(ExportPlatform p){switch(p){case ExportPlatform::Windows:return QStringLiteral("windows");case ExportPlatform::Linux:return QStringLiteral("linux");case ExportPlatform::MacOS:return QStringLiteral("macos");default:return QStringLiteral("unknown");}}
ExportPlatformProfile ExportPlatformPolicy::profile(ExportPlatform p){ExportPlatformProfile x;x.platform=p;x.id=platformId(p);switch(p){case ExportPlatform::Windows:x.displayName=QStringLiteral("Windows");x.executableSuffix=QStringLiteral(".exe");x.preferredBackends={QStringLiteral("d3d11"),QStringLiteral("vulkan")};x.portablePackaging=true;x.deploymentToolExpected=true;x.deploymentTool=QStringLiteral("windeployqt");break;case ExportPlatform::Linux:x.displayName=QStringLiteral("Linux");x.preferredBackends={QStringLiteral("vulkan"),QStringLiteral("opengl")};x.portablePackaging=true;x.deploymentToolExpected=false;break;case ExportPlatform::MacOS:x.displayName=QStringLiteral("macOS");x.preferredBackends={QStringLiteral("metal")};x.portablePackaging=true;x.deploymentToolExpected=true;x.deploymentTool=QStringLiteral("macdeployqt");break;default:x.displayName=QStringLiteral("Unknown");break;}return x;}
ExportPlatformProfile ExportPlatformPolicy::hostProfile(){return profile(hostPlatform());}
} // namespace ui
