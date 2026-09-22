#include "RuntimeGpuPolicy.h"
#include "game/pure/RuntimeGpuPolicy.h"

#include <QSettings>

namespace game {
namespace {
pure::Platform currentPlatform()
{
#if defined(Q_OS_WIN)
    return pure::Platform::Windows;
#elif defined(Q_OS_MACOS)
    return pure::Platform::MacOS;
#else
    return pure::Platform::Other;
#endif
}
QString fromUtf8(const std::string& value) { return QString::fromUtf8(value.data(), int(value.size())); }
std::string toUtf8(const QString& value) { const QByteArray bytes=value.toUtf8(); return std::string(bytes.constData(), size_t(bytes.size())); }
}

QString normalizeRuntimeGpuBackend(const QString& backend)
{
    return fromUtf8(pure::normalizeRuntimeGpuBackend(toUtf8(backend)));
}

QString runtimeGpuBackendDisplayName(const QString& backend)
{
    return fromUtf8(pure::runtimeGpuBackendDisplayName(toUtf8(backend)));
}

QStringList runtimeGpuBackendFallbackChain(const QString& requested)
{
    QStringList result;
    for (const std::string& backend : pure::runtimeGpuBackendFallbackChain(toUtf8(requested), currentPlatform()))
        result.push_back(fromUtf8(backend));
    return result;
}

void migrateLegacyRuntimeRendererSettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("game/renderer"));
    const QString backend = normalizeRuntimeGpuBackend(
        settings.value(QStringLiteral("game/gpuBackend"), QStringLiteral("auto")).toString());
    settings.setValue(QStringLiteral("game/gpuBackend"), backend);
}

QString configuredRuntimeGpuBackend()
{
    migrateLegacyRuntimeRendererSettings();
    return normalizeRuntimeGpuBackend(
        QSettings().value(QStringLiteral("game/gpuBackend"), QStringLiteral("auto")).toString());
}

} // namespace game
