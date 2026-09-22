#include "Fog.h"
#include <QObject>

namespace core {

QString fogBlendId(FogBlend b)
{
    switch (b) {
    case FogBlend::Normal:   return QStringLiteral("normal");
    case FogBlend::Add:      return QStringLiteral("add");
    case FogBlend::Multiply: return QStringLiteral("multiply");
    case FogBlend::Screen:   return QStringLiteral("screen");
    case FogBlend::Overlay:  return QStringLiteral("overlay");
    }
    return QStringLiteral("normal");
}

FogBlend fogBlendFromId(const QString& id)
{
    if (id == QLatin1String("add") || id == QLatin1String("additive")) return FogBlend::Add;
    if (id == QLatin1String("multiply")) return FogBlend::Multiply;
    if (id == QLatin1String("screen")) return FogBlend::Screen;
    if (id == QLatin1String("overlay")) return FogBlend::Overlay;
    return FogBlend::Normal;
}

QString fogBlendLabel(FogBlend b)
{
    switch (b) {
    case FogBlend::Normal:   return QObject::tr("Normal");
    case FogBlend::Add:      return QObject::tr("Adição");
    case FogBlend::Multiply: return QObject::tr("Multiplicação");
    case FogBlend::Screen:   return QObject::tr("Screen");
    case FogBlend::Overlay:  return QObject::tr("Overlay");
    }
    return QString();
}

} // namespace core
