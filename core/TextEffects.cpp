#include "TextEffects.h"

#include <QObject>
#include <QStringList>
#include <QtGlobal>

namespace core {

namespace {
QString normTarget(QString s)
{
    s = s.trimmed().toLower();
    if (s != QLatin1String("word") && s != QLatin1String("block")) s = QStringLiteral("character");
    return s;
}
QString normLoop(QString s)
{
    s = s.trimmed().toLower();
    if (s != QLatin1String("while-visible") && s != QLatin1String("count") && s != QLatin1String("ping-pong"))
        s = QStringLiteral("once");
    return s;
}
QString normMotion(QString s)
{
    s = s.trimmed().toLower();
    static const QStringList valid = {
        QStringLiteral("tween"), QStringLiteral("wave"), QStringLiteral("shake"),
        QStringLiteral("float"), QStringLiteral("jitter"), QStringLiteral("pulse"),
        QStringLiteral("rainbow"), QStringLiteral("glow"), QStringLiteral("sweep"),
        QStringLiteral("typewriter")
    };
    return valid.contains(s) ? s : QStringLiteral("tween");
}
QString normEasing(QString s)
{
    s = s.trimmed().toLower();
    static const QStringList valid = {
        QStringLiteral("linear"), QStringLiteral("quad-in"), QStringLiteral("quad-out"),
        QStringLiteral("quad-in-out"), QStringLiteral("expo-out"), QStringLiteral("back-out"),
        QStringLiteral("elastic-out")
    };
    return valid.contains(s) ? s : QStringLiteral("linear");
}
TextEffectPreset preset(const char* id, const char* name, const TextEffectPhaseSpec& phase)
{
    TextEffectPreset p; p.id = QLatin1String(id); p.name = QObject::tr(name); p.phase = phase; return p;
}
TextEffectPhaseSpec phase()
{
    TextEffectPhaseSpec p; p.enabled = true; return p;
}
}

QVariantMap TextGradientSpec::toVariantMap() const
{
    QVariantMap out;
    if (!enabled()) return out;
    out[QStringLiteral("direction")] = direction;
    QVariantList list;
    for (const QColor& color : colors) if (color.isValid()) list.push_back(color.name(QColor::HexArgb));
    out[QStringLiteral("colors")] = list;
    return out;
}

TextGradientSpec TextGradientSpec::fromVariantMap(const QVariantMap& map)
{
    TextGradientSpec out;
    QString direction = map.value(QStringLiteral("direction"), QStringLiteral("vertical")).toString().trimmed().toLower();
    if (direction != QLatin1String("horizontal") && direction != QLatin1String("diagonal")) direction = QStringLiteral("vertical");
    out.direction = direction;
    for (const QVariant& value : map.value(QStringLiteral("colors")).toList()) {
        const QColor c(value.toString());
        if (c.isValid()) out.colors.push_back(c);
        if (out.colors.size() >= 8) break;
    }
    return out;
}

QVariantMap TextEffectPhaseSpec::toVariantMap() const
{
    QVariantMap out;
    if (!enabled) return out;
    out[QStringLiteral("enabled")] = true;
    out[QStringLiteral("target")] = target;
    out[QStringLiteral("motion")] = motion;
    out[QStringLiteral("easing")] = easing;
    out[QStringLiteral("durationMs")] = durationMs;
    out[QStringLiteral("delayMs")] = delayMs;
    out[QStringLiteral("staggerMs")] = staggerMs;
    out[QStringLiteral("fadeOnReveal")] = fadeOnReveal;
    out[QStringLiteral("fadeRevealMs")] = fadeRevealMs;
    out[QStringLiteral("opacityFrom")] = opacityFrom;
    out[QStringLiteral("opacityTo")] = opacityTo;
    out[QStringLiteral("translateXFrom")] = translateXFrom;
    out[QStringLiteral("translateXTo")] = translateXTo;
    out[QStringLiteral("translateYFrom")] = translateYFrom;
    out[QStringLiteral("translateYTo")] = translateYTo;
    out[QStringLiteral("scaleXFrom")] = scaleXFrom;
    out[QStringLiteral("scaleXTo")] = scaleXTo;
    out[QStringLiteral("scaleYFrom")] = scaleYFrom;
    out[QStringLiteral("scaleYTo")] = scaleYTo;
    out[QStringLiteral("rotationFrom")] = rotationFrom;
    out[QStringLiteral("rotationTo")] = rotationTo;
    out[QStringLiteral("amount")] = amount;
    out[QStringLiteral("frequency")] = frequency;
    out[QStringLiteral("loopMode")] = loopMode;
    out[QStringLiteral("loopCount")] = loopCount;
    return out;
}

TextEffectPhaseSpec TextEffectPhaseSpec::fromVariantMap(const QVariantMap& map)
{
    TextEffectPhaseSpec out;
    if (map.isEmpty()) return out;
    out.enabled = map.value(QStringLiteral("enabled"), true).toBool();
    out.target = normTarget(map.value(QStringLiteral("target"), QStringLiteral("character")).toString());
    out.motion = normMotion(map.value(QStringLiteral("motion"), QStringLiteral("tween")).toString());
    out.easing = normEasing(map.value(QStringLiteral("easing"), QStringLiteral("linear")).toString());
    out.durationMs = qBound(1, map.value(QStringLiteral("durationMs"), 600).toInt(), 60000);
    out.delayMs = qBound(0, map.value(QStringLiteral("delayMs"), 0).toInt(), 60000);
    out.staggerMs = qBound(0, map.value(QStringLiteral("staggerMs"), 0).toInt(), 10000);
    out.fadeOnReveal = map.value(QStringLiteral("fadeOnReveal"), false).toBool();
    out.fadeRevealMs = qBound(1, map.value(QStringLiteral("fadeRevealMs"), 120).toInt(), 5000);
    out.opacityFrom = qBound(0.0, map.value(QStringLiteral("opacityFrom"), 1.0).toDouble(), 1.0);
    out.opacityTo = qBound(0.0, map.value(QStringLiteral("opacityTo"), 1.0).toDouble(), 1.0);
    out.translateXFrom = qBound(-10000.0, map.value(QStringLiteral("translateXFrom"), 0.0).toDouble(), 10000.0);
    out.translateXTo = qBound(-10000.0, map.value(QStringLiteral("translateXTo"), 0.0).toDouble(), 10000.0);
    out.translateYFrom = qBound(-10000.0, map.value(QStringLiteral("translateYFrom"), 0.0).toDouble(), 10000.0);
    out.translateYTo = qBound(-10000.0, map.value(QStringLiteral("translateYTo"), 0.0).toDouble(), 10000.0);
    out.scaleXFrom = qBound(0.01, map.value(QStringLiteral("scaleXFrom"), 1.0).toDouble(), 20.0);
    out.scaleXTo = qBound(0.01, map.value(QStringLiteral("scaleXTo"), 1.0).toDouble(), 20.0);
    out.scaleYFrom = qBound(0.01, map.value(QStringLiteral("scaleYFrom"), 1.0).toDouble(), 20.0);
    out.scaleYTo = qBound(0.01, map.value(QStringLiteral("scaleYTo"), 1.0).toDouble(), 20.0);
    out.rotationFrom = qBound(-3600.0, map.value(QStringLiteral("rotationFrom"), 0.0).toDouble(), 3600.0);
    out.rotationTo = qBound(-3600.0, map.value(QStringLiteral("rotationTo"), 0.0).toDouble(), 3600.0);
    out.amount = qBound(0.0, map.value(QStringLiteral("amount"), 0.0).toDouble(), 10000.0);
    out.frequency = qBound(0.01, map.value(QStringLiteral("frequency"), 1.0).toDouble(), 1000.0);
    out.loopMode = normLoop(map.value(QStringLiteral("loopMode"), QStringLiteral("once")).toString());
    out.loopCount = qBound(1, map.value(QStringLiteral("loopCount"), 1).toInt(), 9999);
    return out;
}

QVariantMap TextEffectStack::toVariantMap() const
{
    QVariantMap out;
    if (entrance.enabled) out[QStringLiteral("entrance")] = entrance.toVariantMap();
    if (loop.enabled) out[QStringLiteral("loop")] = loop.toVariantMap();
    if (exit.enabled) out[QStringLiteral("exit")] = exit.toVariantMap();
    return out;
}

TextEffectStack TextEffectStack::fromVariantMap(const QVariantMap& map)
{
    TextEffectStack out;
    out.entrance = TextEffectPhaseSpec::fromVariantMap(map.value(QStringLiteral("entrance")).toMap());
    out.loop = TextEffectPhaseSpec::fromVariantMap(map.value(QStringLiteral("loop")).toMap());
    out.exit = TextEffectPhaseSpec::fromVariantMap(map.value(QStringLiteral("exit")).toMap());
    return out;
}

QVariantMap TextEffectPreset::toVariantMap() const
{
    QVariantMap out;
    out[QStringLiteral("id")] = id;
    out[QStringLiteral("name")] = name;
    out[QStringLiteral("phase")] = phase.toVariantMap();
    return out;
}

TextEffectPreset TextEffectPreset::fromVariantMap(const QVariantMap& map, const QString& fallbackId)
{
    TextEffectPreset out;
    out.id = map.value(QStringLiteral("id"), fallbackId).toString().trimmed().left(128);
    out.name = map.value(QStringLiteral("name"), out.id).toString().trimmed().left(128);
    out.phase = TextEffectPhaseSpec::fromVariantMap(map.value(QStringLiteral("phase")).toMap());
    return out;
}

QVector<TextEffectPreset> builtInTextEffectPresets()
{
    QVector<TextEffectPreset> out;
    {
        auto p=phase(); p.scaleXFrom=p.scaleYFrom=.3; p.opacityFrom=0; p.durationMs=600;p.staggerMs=70;p.easing=QStringLiteral("expo-out");
        out.push_back(preset("impact","Impacto",p));
    }
    {
        auto p=phase(); p.scaleXFrom=p.scaleYFrom=4.0;p.opacityFrom=0;p.durationMs=950;p.staggerMs=70;p.easing=QStringLiteral("expo-out");
        out.push_back(preset("dramatic-zoom","Zoom Dramático",p));
    }
    {
        auto p=phase(); p.opacityFrom=0;p.durationMs=2250;p.staggerMs=150;p.easing=QStringLiteral("quad-in-out");
        out.push_back(preset("sequential-fade","Fade Sequencial",p));
    }
    {
        auto p=phase(); p.translateYFrom=18;p.durationMs=750;p.staggerMs=50;p.easing=QStringLiteral("quad-out");
        out.push_back(preset("rise-letter","Subir por Letra",p));
    }
    {
        auto p=phase(); p.translateXFrom=40;p.opacityFrom=0;p.durationMs=1200;p.staggerMs=30;p.easing=QStringLiteral("quad-out");
        out.push_back(preset("slide","Deslizar",p));
    }
    {
        auto p=phase(); p.scaleXFrom=p.scaleYFrom=.01;p.durationMs=1500;p.staggerMs=45;p.easing=QStringLiteral("elastic-out");
        out.push_back(preset("elastic-pop","Pop Elástico",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("wave");p.amount=6;p.frequency=6;p.durationMs=1000;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("wave","Wave",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("shake");p.amount=3;p.frequency=14;p.durationMs=700;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("shake","Shake",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("float");p.amount=5;p.frequency=2;p.durationMs=1000;p.loopMode=QStringLiteral("ping-pong");
        out.push_back(preset("float","Float",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("pulse");p.amount=.06;p.frequency=1.5;p.durationMs=1000;p.loopMode=QStringLiteral("ping-pong");
        out.push_back(preset("breathing","Breathing",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("pulse");p.amount=.12;p.frequency=3;p.durationMs=600;p.loopMode=QStringLiteral("ping-pong");
        out.push_back(preset("pulse","Pulse",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("glow");p.amount=.55;p.frequency=2;p.durationMs=900;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("glow","Glow",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("rainbow");p.frequency=.25;p.durationMs=1200;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("rainbow","Rainbow",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("sweep");p.amount=.8;p.frequency=.7;p.durationMs=1300;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("gradient-sweep","Gradient Sweep",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("jitter");p.amount=3;p.frequency=8;p.durationMs=650;p.loopMode=QStringLiteral("while-visible");
        out.push_back(preset("jitter","Jitter",p));
    }
    {
        auto p=phase(); p.rotationFrom=-180;p.opacityFrom=0;p.durationMs=800;p.staggerMs=45;p.easing=QStringLiteral("back-out");
        out.push_back(preset("rotate-in","Rotate In",p));
    }
    {
        auto p=phase(); p.motion=QStringLiteral("typewriter");p.amount=45;p.durationMs=1000;p.staggerMs=22;
        out.push_back(preset("typewriter","Typewriter",p));
    }
    return out;
}

TextEffectPhaseSpec builtInTextEffectPreset(const QString& id, bool* found)
{
    const QString wanted = id.trimmed().toLower();
    for (const TextEffectPreset& p : builtInTextEffectPresets()) {
        if (p.id == wanted) { if (found) *found = true; return p.phase; }
    }
    if (found) *found = false;
    return {};
}

QString textEffectTargetLabel(const QString& id)
{
    const QString n = normTarget(id);
    if (n == QLatin1String("word")) return QObject::tr("Palavra");
    if (n == QLatin1String("block")) return QObject::tr("Bloco");
    return QObject::tr("Caractere");
}
QString textEffectLoopLabel(const QString& id)
{
    const QString n = normLoop(id);
    if (n == QLatin1String("while-visible")) return QObject::tr("Enquanto visível");
    if (n == QLatin1String("count")) return QObject::tr("N vezes");
    if (n == QLatin1String("ping-pong")) return QObject::tr("Ping-pong");
    return QObject::tr("Uma vez");
}
QString textEffectEasingLabel(const QString& id)
{
    const QString n = normEasing(id);
    if (n == QLatin1String("quad-in")) return QObject::tr("Quad In");
    if (n == QLatin1String("quad-out")) return QObject::tr("Quad Out");
    if (n == QLatin1String("quad-in-out")) return QObject::tr("Quad In-Out");
    if (n == QLatin1String("expo-out")) return QObject::tr("Expo Out");
    if (n == QLatin1String("back-out")) return QObject::tr("Back Out");
    if (n == QLatin1String("elastic-out")) return QObject::tr("Elástico");
    return QObject::tr("Linear");
}

} // namespace core
