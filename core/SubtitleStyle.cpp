#include "SubtitleStyle.h"

#include <QJsonArray>

namespace core {

namespace {
QString colorToStr(const QColor& c) { return c.name(QColor::HexArgb); }
QColor  strToColor(const QString& s, const QColor& fb)
{
    const QColor c(s);
    return c.isValid() ? c : fb;
}
} // namespace

QString subtitleTrackId(SubtitleTrack track)
{
    switch(track){case SubtitleTrack::Dialogue:return QStringLiteral("dialogue");case SubtitleTrack::Notification:return QStringLiteral("notification");case SubtitleTrack::System:return QStringLiteral("system");case SubtitleTrack::Custom1:return QStringLiteral("custom1");case SubtitleTrack::Custom2:return QStringLiteral("custom2");}return QStringLiteral("dialogue");
}
SubtitleTrack subtitleTrackFromId(const QString& id)
{
    const QString value=id.trimmed().toLower();if(value=="notification")return SubtitleTrack::Notification;if(value=="system")return SubtitleTrack::System;if(value=="custom1")return SubtitleTrack::Custom1;if(value=="custom2")return SubtitleTrack::Custom2;return SubtitleTrack::Dialogue;
}
QJsonObject SubtitleTrackSettings::toJson() const{return {{"maxSimultaneous",maxSimultaneous},{"minDurationFrames",minDurationFrames},{"charsPerSecond",charsPerSecond},{"style",styleOverrides}};}
SubtitleTrackSettings SubtitleTrackSettings::fromJson(const QJsonObject& o){SubtitleTrackSettings s;s.maxSimultaneous=qBound(1,o.value("maxSimultaneous").toInt(s.maxSimultaneous),20);s.minDurationFrames=qBound(1,o.value("minDurationFrames").toInt(s.minDurationFrames),36000);s.charsPerSecond=qBound(1.0,o.value("charsPerSecond").toDouble(s.charsPerSecond),1000.0);s.styleOverrides=o.value("style").toObject();return s;}

QString subtitlePositionId(SubtitlePosition p)
{
    switch (p) {
    case SubtitlePosition::Top:    return QStringLiteral("top");
    case SubtitlePosition::Middle: return QStringLiteral("middle");
    case SubtitlePosition::Bottom: return QStringLiteral("bottom");
    }
    return QStringLiteral("bottom");
}

SubtitlePosition subtitlePositionFromId(const QString& id)
{
    if (id == QLatin1String("top"))    return SubtitlePosition::Top;
    if (id == QLatin1String("middle") || id == QLatin1String("center"))
        return SubtitlePosition::Middle;
    return SubtitlePosition::Bottom;
}

QString subtitleAlignId(SubtitleAlign a)
{
    switch (a) {
    case SubtitleAlign::Left:   return QStringLiteral("left");
    case SubtitleAlign::Center: return QStringLiteral("center");
    case SubtitleAlign::Right:  return QStringLiteral("right");
    }
    return QStringLiteral("center");
}

SubtitleAlign subtitleAlignFromId(const QString& id)
{
    if (id == QLatin1String("left"))  return SubtitleAlign::Left;
    if (id == QLatin1String("right")) return SubtitleAlign::Right;
    return SubtitleAlign::Center;
}

QString subtitleBgId(SubtitleBg b)
{
    switch (b) {
    case SubtitleBg::None:     return QStringLiteral("none");
    case SubtitleBg::Solid:    return QStringLiteral("solid");
    case SubtitleBg::Gradient: return QStringLiteral("gradient");
    case SubtitleBg::Glass:    return QStringLiteral("glass");
    }
    return QStringLiteral("solid");
}

SubtitleBg subtitleBgFromId(const QString& id)
{
    if (id == QLatin1String("none"))     return SubtitleBg::None;
    if (id == QLatin1String("gradient")) return SubtitleBg::Gradient;
    if (id == QLatin1String("glass"))    return SubtitleBg::Glass;
    return SubtitleBg::Solid;
}

QString subtitleTransitionId(SubtitleTransition t)
{
    switch (t) {
    case SubtitleTransition::None:       return QStringLiteral("none");
    case SubtitleTransition::SlideUp:    return QStringLiteral("slideUp");
    case SubtitleTransition::SlideDown:  return QStringLiteral("slideDown");
    case SubtitleTransition::SlideLeft:  return QStringLiteral("slideLeft");
    case SubtitleTransition::SlideRight: return QStringLiteral("slideRight");
    case SubtitleTransition::ZoomIn:     return QStringLiteral("zoomIn");
    case SubtitleTransition::ZoomOut:    return QStringLiteral("zoomOut");
    case SubtitleTransition::Bounce:     return QStringLiteral("bounce");
    case SubtitleTransition::FlipX:      return QStringLiteral("flipX");
    case SubtitleTransition::FlipY:      return QStringLiteral("flipY");
    }
    return QStringLiteral("none");
}

SubtitleTransition subtitleTransitionFromId(const QString& id)
{
    const QString s = id.trimmed();
    if (s.compare(QLatin1String("slideUp"), Qt::CaseInsensitive) == 0)    return SubtitleTransition::SlideUp;
    if (s.compare(QLatin1String("slideDown"), Qt::CaseInsensitive) == 0)  return SubtitleTransition::SlideDown;
    if (s.compare(QLatin1String("slideLeft"), Qt::CaseInsensitive) == 0)  return SubtitleTransition::SlideLeft;
    if (s.compare(QLatin1String("slideRight"), Qt::CaseInsensitive) == 0) return SubtitleTransition::SlideRight;
    if (s.compare(QLatin1String("zoomIn"), Qt::CaseInsensitive) == 0)     return SubtitleTransition::ZoomIn;
    if (s.compare(QLatin1String("zoomOut"), Qt::CaseInsensitive) == 0)    return SubtitleTransition::ZoomOut;
    if (s.compare(QLatin1String("bounce"), Qt::CaseInsensitive) == 0)     return SubtitleTransition::Bounce;
    if (s.compare(QLatin1String("flipX"), Qt::CaseInsensitive) == 0)      return SubtitleTransition::FlipX;
    if (s.compare(QLatin1String("flipY"), Qt::CaseInsensitive) == 0)      return SubtitleTransition::FlipY;
    return SubtitleTransition::None;
}

QJsonObject SubtitleStyle::toJson() const
{
    QJsonObject o;
    o["defaultDuration"] = defaultDuration;
    o["fadeInFrames"]    = fadeInFrames;
    o["fadeOutFrames"]   = fadeOutFrames;
    o["fontFamily"]      = fontFamily;
    o["fontSize"]        = fontSize;
    o["fontColor"]       = colorToStr(fontColor);
    o["fontBold"]        = fontBold;
    o["fontItalic"]      = fontItalic;
    o["outlineColor"]    = colorToStr(outlineColor);
    o["outlineWidth"]    = outlineWidth;
    o["shadow"]          = shadow;
    o["shadowColor"]     = colorToStr(shadowColor);
    o["shadowOffsetX"]   = shadowOffsetX;
    o["shadowOffsetY"]   = shadowOffsetY;
    o["nameFontSize"]    = nameFontSize;
    o["nameColor"]       = colorToStr(nameColor);
    o["nameAlign"]       = subtitleAlignId(nameAlign);
    o["bgStyle"]         = subtitleBgId(bgStyle);
    o["bgColor"]         = colorToStr(bgColor);
    o["bgRadius"]        = bgRadius;
    o["paddingH"]        = paddingH;
    o["paddingV"]        = paddingV;
    o["maxWidth"]        = maxWidth;
    o["marginBottom"]    = marginBottom;
    o["marginHorizontal"] = marginHorizontal;
    o["position"]        = subtitlePositionId(position);
    o["textAlign"]       = subtitleAlignId(textAlign);
    o["boxAlign"]        = subtitleAlignId(boxAlign);
    o["maxSimultaneous"] = maxSimultaneous;
    o["waitForInput"]    = waitForInput;
    o["freezeMap"]       = freezeMap;
    o["typewriter"]      = typewriter;
    o["typewriterSpeed"] = typewriterSpeed;
    o["inputIndicator"]  = inputIndicator;
    o["transitionIn"]    = subtitleTransitionId(transitionIn);
    o["transitionOut"]   = subtitleTransitionId(transitionOut);
    o["offsetX"]         = offsetX;
    o["offsetY"]         = offsetY;
    QJsonObject trackObject;for(auto it=tracks.cbegin();it!=tracks.cend();++it)trackObject.insert(it.key(),it.value().toJson());o["tracks"]=trackObject;
    return o;
}

SubtitleTrackSettings SubtitleStyle::trackSettings(SubtitleTrack track) const
{
    SubtitleTrackSettings settings;settings.maxSimultaneous=maxSimultaneous;const auto it=tracks.constFind(subtitleTrackId(track));return it==tracks.cend()?settings:it.value();
}
SubtitleStyle SubtitleStyle::resolvedForTrack(SubtitleTrack track) const
{
    QJsonObject merged=toJson();merged.remove("tracks");const QJsonObject override=trackSettings(track).styleOverrides;for(auto it=override.begin();it!=override.end();++it)merged.insert(it.key(),it.value());SubtitleStyle result=fromJson(merged);result.tracks=tracks;return result;
}

SubtitleStyle SubtitleStyle::fromJson(const QJsonObject& o)
{
    SubtitleStyle s;
    s.defaultDuration = o.value("defaultDuration").toInt(s.defaultDuration);
    s.fadeInFrames    = o.value("fadeInFrames").toInt(s.fadeInFrames);
    s.fadeOutFrames   = o.value("fadeOutFrames").toInt(s.fadeOutFrames);
    s.fontFamily      = o.value("fontFamily").toString();
    s.fontSize        = o.value("fontSize").toInt(s.fontSize);
    s.fontColor       = strToColor(o.value("fontColor").toString(), s.fontColor);
    s.fontBold        = o.value("fontBold").toBool(s.fontBold);
    s.fontItalic      = o.value("fontItalic").toBool(s.fontItalic);
    s.outlineColor    = strToColor(o.value("outlineColor").toString(), s.outlineColor);
    s.outlineWidth    = o.value("outlineWidth").toInt(s.outlineWidth);
    s.shadow          = o.value("shadow").toBool(s.shadow);
    s.shadowColor     = strToColor(o.value("shadowColor").toString(), s.shadowColor);
    s.shadowOffsetX   = o.value("shadowOffsetX").toInt(s.shadowOffsetX);
    s.shadowOffsetY   = o.value("shadowOffsetY").toInt(s.shadowOffsetY);
    s.nameFontSize    = o.value("nameFontSize").toInt(s.nameFontSize);
    s.nameColor       = strToColor(o.value("nameColor").toString(), s.nameColor);
    s.nameAlign       = subtitleAlignFromId(o.value("nameAlign").toString(subtitleAlignId(s.nameAlign)));
    s.bgStyle         = subtitleBgFromId(o.value("bgStyle").toString(subtitleBgId(s.bgStyle)));
    s.bgColor         = strToColor(o.value("bgColor").toString(), s.bgColor);
    s.bgRadius        = o.value("bgRadius").toInt(s.bgRadius);
    s.paddingH        = o.value("paddingH").toInt(s.paddingH);
    s.paddingV        = o.value("paddingV").toInt(s.paddingV);
    s.maxWidth        = o.value("maxWidth").toInt(s.maxWidth);
    s.marginBottom    = o.value("marginBottom").toInt(s.marginBottom);
    s.marginHorizontal = o.value("marginHorizontal").toInt(s.marginHorizontal);
    s.position        = subtitlePositionFromId(o.value("position").toString(subtitlePositionId(s.position)));
    s.textAlign       = subtitleAlignFromId(o.value("textAlign").toString(subtitleAlignId(s.textAlign)));
    s.boxAlign        = subtitleAlignFromId(o.value("boxAlign").toString(subtitleAlignId(s.boxAlign)));
    s.maxSimultaneous = qMax(1, o.value("maxSimultaneous").toInt(s.maxSimultaneous));
    s.waitForInput    = o.value("waitForInput").toBool(s.waitForInput);
    s.freezeMap       = o.value("freezeMap").toBool(s.freezeMap);
    s.typewriter      = o.value("typewriter").toBool(s.typewriter);
    s.typewriterSpeed = o.value("typewriterSpeed").toDouble(s.typewriterSpeed);
    s.inputIndicator  = o.value("inputIndicator").toBool(s.inputIndicator);
    s.transitionIn    = subtitleTransitionFromId(o.value("transitionIn").toString());
    s.transitionOut   = subtitleTransitionFromId(o.value("transitionOut").toString());
    s.offsetX         = o.value("offsetX").toInt(s.offsetX);
    s.offsetY         = o.value("offsetY").toInt(s.offsetY);
    const QJsonObject trackObject=o.value("tracks").toObject();for(auto it=trackObject.begin();it!=trackObject.end();++it)if(it.value().isObject())s.tracks.insert(it.key(),SubtitleTrackSettings::fromJson(it.value().toObject()));
    return s;
}

} // namespace core
