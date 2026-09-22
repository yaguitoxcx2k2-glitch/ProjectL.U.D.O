// ============================================================================
//  Picture.cpp — Nomes, curvas de animação e serialização das pictures.
//
//  As curvas são o PORTE LITERAL de `Game_Picture.prototype.applyTweenEasing`
//  do InteractivePictureCore.js. Foram copiadas fórmula por fórmula de
//  propósito: uma cutscene montada no formato de eventos compatível tem que cair aqui com o
//  mesmo ritmo, não com um "parecido".
// ============================================================================
#include "Picture.h"

#include <QtGlobal>
#include <QJsonDocument>
#include <QtMath>

namespace core {

// ============================================================================
//  Âncora
// ============================================================================
QPointF pictureAnchorFactor(PictureAnchor a, double customX, double customY)
{
    switch (a) {
    case PictureAnchor::TopLeft:      return QPointF(0.0, 0.0);
    case PictureAnchor::TopCenter:    return QPointF(0.5, 0.0);
    case PictureAnchor::TopRight:     return QPointF(1.0, 0.0);
    case PictureAnchor::MiddleLeft:   return QPointF(0.0, 0.5);
    case PictureAnchor::Center:       return QPointF(0.5, 0.5);
    case PictureAnchor::MiddleRight:  return QPointF(1.0, 0.5);
    case PictureAnchor::BottomLeft:   return QPointF(0.0, 1.0);
    case PictureAnchor::BottomCenter: return QPointF(0.5, 1.0);
    case PictureAnchor::BottomRight:  return QPointF(1.0, 1.0);
    case PictureAnchor::Custom:       return QPointF(customX, customY);
    }
    return QPointF(0.0, 0.0);
}

QString pictureAnchorId(PictureAnchor a)
{
    switch (a) {
    case PictureAnchor::TopLeft:      return QStringLiteral("top-left");
    case PictureAnchor::TopCenter:    return QStringLiteral("top-center");
    case PictureAnchor::TopRight:     return QStringLiteral("top-right");
    case PictureAnchor::MiddleLeft:   return QStringLiteral("middle-left");
    case PictureAnchor::Center:       return QStringLiteral("center");
    case PictureAnchor::MiddleRight:  return QStringLiteral("middle-right");
    case PictureAnchor::BottomLeft:   return QStringLiteral("bottom-left");
    case PictureAnchor::BottomCenter: return QStringLiteral("bottom-center");
    case PictureAnchor::BottomRight:  return QStringLiteral("bottom-right");
    case PictureAnchor::Custom:       return QStringLiteral("custom");
    }
    return QStringLiteral("top-left");
}

PictureAnchor pictureAnchorFromId(const QString& id)
{
    if (id == QLatin1String("top-center"))    return PictureAnchor::TopCenter;
    if (id == QLatin1String("top-right"))     return PictureAnchor::TopRight;
    if (id == QLatin1String("middle-left"))   return PictureAnchor::MiddleLeft;
    if (id == QLatin1String("center"))        return PictureAnchor::Center;
    if (id == QLatin1String("middle-right"))  return PictureAnchor::MiddleRight;
    if (id == QLatin1String("bottom-left"))   return PictureAnchor::BottomLeft;
    if (id == QLatin1String("bottom-center")) return PictureAnchor::BottomCenter;
    if (id == QLatin1String("bottom-right"))  return PictureAnchor::BottomRight;
    if (id == QLatin1String("custom"))        return PictureAnchor::Custom;
    return PictureAnchor::TopLeft;
}

QString pictureAnchorLabel(PictureAnchor a)
{
    switch (a) {
    case PictureAnchor::TopLeft:      return QObject::tr("Canto superior esquerdo");
    case PictureAnchor::TopCenter:    return QObject::tr("Topo centralizado");
    case PictureAnchor::TopRight:     return QObject::tr("Canto superior direito");
    case PictureAnchor::MiddleLeft:   return QObject::tr("Meio esquerda");
    case PictureAnchor::Center:       return QObject::tr("Centro");
    case PictureAnchor::MiddleRight:  return QObject::tr("Meio direita");
    case PictureAnchor::BottomLeft:   return QObject::tr("Canto inferior esquerdo");
    case PictureAnchor::BottomCenter: return QObject::tr("Base centralizada");
    case PictureAnchor::BottomRight:  return QObject::tr("Canto inferior direito");
    case PictureAnchor::Custom:       return QObject::tr("Personalizada");
    }
    return QString();
}

// ============================================================================
//  Mistura, espaço e camada
// ============================================================================
QString pictureBlendId(PictureBlend b)
{
    switch (b) {
    case PictureBlend::Add:      return QStringLiteral("add");
    case PictureBlend::Multiply: return QStringLiteral("multiply");
    case PictureBlend::Screen:   return QStringLiteral("screen");
    case PictureBlend::Normal:   break;
    }
    return QStringLiteral("normal");
}

PictureBlend pictureBlendFromId(const QString& id)
{
    if (id == QLatin1String("add"))      return PictureBlend::Add;
    if (id == QLatin1String("multiply")) return PictureBlend::Multiply;
    if (id == QLatin1String("screen"))   return PictureBlend::Screen;
    return PictureBlend::Normal;
}

QString pictureBlendLabel(PictureBlend b)
{
    switch (b) {
    case PictureBlend::Normal:   return QObject::tr("Normal");
    case PictureBlend::Add:      return QObject::tr("Somar (brilho)");
    case PictureBlend::Multiply: return QObject::tr("Multiplicar (sombra)");
    case PictureBlend::Screen:   return QObject::tr("Clarear (screen)");
    }
    return QString();
}

QString pictureSpaceId(PictureSpace s)
{
    return s == PictureSpace::Map ? QStringLiteral("map") : QStringLiteral("screen");
}

PictureSpace pictureSpaceFromId(const QString& id)
{
    return id == QLatin1String("map") ? PictureSpace::Map : PictureSpace::Screen;
}

QString pictureSpaceLabel(PictureSpace s)
{
    return s == PictureSpace::Map ? QObject::tr("Presa ao mapa (rola com a câmera)")
                                  : QObject::tr("Fixa na tela");
}


QString pictureNineSliceModeId(PictureNineSliceMode m)
{
    return m == PictureNineSliceMode::Tile ? QStringLiteral("tile") : QStringLiteral("stretch");
}

PictureNineSliceMode pictureNineSliceModeFromId(const QString& id)
{
    return id.compare(QStringLiteral("tile"), Qt::CaseInsensitive) == 0
        ? PictureNineSliceMode::Tile : PictureNineSliceMode::Stretch;
}

QString pictureNineSliceModeLabel(PictureNineSliceMode m)
{
    return m == PictureNineSliceMode::Tile ? QObject::tr("Repetir (tile)")
                                           : QObject::tr("Esticar");
}

QVariantMap PictureNineSlice::toParams() const
{
    QVariantMap p;
    if (!enabled) return p;
    p[QStringLiteral("width")] = width;
    p[QStringLiteral("height")] = height;
    p[QStringLiteral("left")] = left;
    p[QStringLiteral("top")] = top;
    p[QStringLiteral("right")] = right;
    p[QStringLiteral("bottom")] = bottom;
    p[QStringLiteral("edgeMode")] = pictureNineSliceModeId(edgeMode);
    p[QStringLiteral("centerMode")] = pictureNineSliceModeId(centerMode);
    return p;
}

PictureNineSlice PictureNineSlice::fromParams(const QVariantMap& p)
{
    PictureNineSlice n;
    if (p.isEmpty()) return n;
    n.enabled = true;
    n.width = qMax(0, p.value(QStringLiteral("width"), 0).toInt());
    n.height = qMax(0, p.value(QStringLiteral("height"), 0).toInt());
    n.left = qMax(0, p.value(QStringLiteral("left"), 8).toInt());
    n.top = qMax(0, p.value(QStringLiteral("top"), 8).toInt());
    n.right = qMax(0, p.value(QStringLiteral("right"), 8).toInt());
    n.bottom = qMax(0, p.value(QStringLiteral("bottom"), 8).toInt());
    n.edgeMode = pictureNineSliceModeFromId(p.value(QStringLiteral("edgeMode")).toString());
    n.centerMode = pictureNineSliceModeFromId(p.value(QStringLiteral("centerMode")).toString());
    return n;
}

bool PictureNineSlice::validFor(const QSize& source) const
{
    if (!enabled) return true;
    if (source.isEmpty()) return false;
    if (left + right >= source.width() || top + bottom >= source.height()) return false;
    const int targetW = width > 0 ? width : source.width();
    const int targetH = height > 0 ? height : source.height();
    return targetW >= left + right && targetH >= top + bottom;
}

QString pictureLayerId(PictureLayer l)
{
    switch (l) {
    case PictureLayer::AboveParallax:   return QStringLiteral("above-parallax");
    case PictureLayer::BelowTiles:      return QStringLiteral("below-tiles");
    case PictureLayer::BelowEvents:     return QStringLiteral("below-events");
    case PictureLayer::SameAsPlayer:    return QStringLiteral("same-as-player");
    case PictureLayer::AboveTiles:      return QStringLiteral("above-tiles");
    case PictureLayer::AboveEvents:     return QStringLiteral("above-events");
    case PictureLayer::AboveWeather:    return QStringLiteral("above-weather");
    case PictureLayer::AboveAnimations: return QStringLiteral("above-animations");
    case PictureLayer::AboveMessage:    return QStringLiteral("above-message");
    case PictureLayer::AboveTimers:     return QStringLiteral("above-timers");
    }
    return QStringLiteral("above-weather");
}

PictureLayer pictureLayerFromId(const QString& id)
{
    if (id == QLatin1String("below-message"))  return PictureLayer::AboveWeather; // compat v3.0
    if (id == QLatin1String("above-all"))      return PictureLayer::AboveTimers;  // compat v3.0
    if (id == QLatin1String("above-parallax")) return PictureLayer::AboveParallax;
    if (id == QLatin1String("below-tiles"))    return PictureLayer::BelowTiles;
    if (id == QLatin1String("below-events"))   return PictureLayer::BelowEvents;
    if (id == QLatin1String("same-as-player")) return PictureLayer::SameAsPlayer;
    if (id == QLatin1String("above-tiles"))    return PictureLayer::AboveTiles;
    if (id == QLatin1String("above-events"))   return PictureLayer::AboveEvents;
    if (id == QLatin1String("above-weather"))  return PictureLayer::AboveWeather;
    if (id == QLatin1String("above-animations")) return PictureLayer::AboveAnimations;
    if (id == QLatin1String("above-message"))  return PictureLayer::AboveMessage;
    if (id == QLatin1String("above-timers"))   return PictureLayer::AboveTimers;
    bool ok = false;
    const int n = id.toInt(&ok);
    if (ok) return PictureLayer(qBound(0, n, 9));
    return PictureLayer::AboveWeather;
}

QString pictureLayerLabel(PictureLayer l)
{
    switch (l) {
    case PictureLayer::AboveParallax:   return QObject::tr("0: Above parallax background");
    case PictureLayer::BelowTiles:      return QObject::tr("1: Above tileset below player");
    case PictureLayer::BelowEvents:     return QObject::tr("2: Above events below player");
    case PictureLayer::SameAsPlayer:    return QObject::tr("3: Above events same level as player");
    case PictureLayer::AboveTiles:      return QObject::tr("4: Above tileset above player");
    case PictureLayer::AboveEvents:     return QObject::tr("5: Above events above player");
    case PictureLayer::AboveWeather:    return QObject::tr("6: Above weather effects");
    case PictureLayer::AboveAnimations: return QObject::tr("7: Above animations");
    case PictureLayer::AboveMessage:    return QObject::tr("8: Above message window");
    case PictureLayer::AboveTimers:     return QObject::tr("9: Above timers (highest priority)");
    }
    return QString();
}

// ============================================================================
//  Curvas de animação (porte de applyTweenEasing)
// ============================================================================
QString pictureEaseId(PictureEase e)
{
    switch (e) {
    case PictureEase::EaseIn:    return QStringLiteral("easeIn");
    case PictureEase::EaseOut:   return QStringLiteral("easeOut");
    case PictureEase::EaseInOut: return QStringLiteral("easeInOut");
    case PictureEase::Bounce:    return QStringLiteral("bounce");
    case PictureEase::Elastic:   return QStringLiteral("elastic");
    case PictureEase::Linear:    break;
    }
    return QStringLiteral("linear");
}

PictureEase pictureEaseFromId(const QString& id)
{
    const QString k = id.toLower();
    if (k == QLatin1String("easein"))    return PictureEase::EaseIn;
    if (k == QLatin1String("easeout"))   return PictureEase::EaseOut;
    if (k == QLatin1String("easeinout")) return PictureEase::EaseInOut;
    if (k == QLatin1String("bounce"))    return PictureEase::Bounce;
    if (k == QLatin1String("elastic"))   return PictureEase::Elastic;
    return PictureEase::Linear;
}

QString pictureEaseLabel(PictureEase e)
{
    switch (e) {
    case PictureEase::Linear:    return QObject::tr("Constante (linear)");
    case PictureEase::EaseIn:    return QObject::tr("Acelera (começa devagar)");
    case PictureEase::EaseOut:   return QObject::tr("Desacelera (termina devagar)");
    case PictureEase::EaseInOut: return QObject::tr("Suave nos dois lados");
    case PictureEase::Bounce:    return QObject::tr("Quica no final");
    case PictureEase::Elastic:   return QObject::tr("Elástico (represinha)");
    }
    return QString();
}

double applyEase(PictureEase e, double t)
{
    t = qBound(0.0, t, 1.0);
    switch (e) {
    case PictureEase::Linear:
        return t;
    case PictureEase::EaseIn:
        return t * t;
    case PictureEase::EaseOut:
        return t * (2.0 - t);
    case PictureEase::EaseInOut:
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    case PictureEase::Bounce: {
        if (t < 1.0 / 2.75)      return 7.5625 * t * t;
        if (t < 2.0 / 2.75)    { t -= 1.5 / 2.75;   return 7.5625 * t * t + 0.75; }
        if (t < 2.5 / 2.75)    { t -= 2.25 / 2.75;  return 7.5625 * t * t + 0.9375; }
        t -= 2.625 / 2.75;       return 7.5625 * t * t + 0.984375;
    }
    case PictureEase::Elastic: {
        // Nos extremos a fórmula devolve exatamente 0 e 1; deixar o seno agir
        // ali faria a picture "estourar" um pixel no primeiro quadro.
        if (t <= 0.0 || t >= 1.0) return t;
        const double p = 0.3;
        return std::pow(2.0, -10.0 * t) * std::sin((t - p / 4.0) * (2.0 * M_PI) / p) + 1.0;
    }
    }
    return t;
}

QString picturePropId(PictureProp p)
{
    switch (p) {
    case PictureProp::Y:       return QStringLiteral("y");
    case PictureProp::ScaleX:  return QStringLiteral("scaleX");
    case PictureProp::ScaleY:  return QStringLiteral("scaleY");
    case PictureProp::Opacity: return QStringLiteral("opacity");
    case PictureProp::Angle:   return QStringLiteral("angle");
    case PictureProp::X:       break;
    }
    return QStringLiteral("x");
}

PictureProp picturePropFromId(const QString& id)
{
    if (id == QLatin1String("y"))       return PictureProp::Y;
    if (id == QLatin1String("scaleX"))  return PictureProp::ScaleX;
    if (id == QLatin1String("scaleY"))  return PictureProp::ScaleY;
    if (id == QLatin1String("opacity")) return PictureProp::Opacity;
    if (id == QLatin1String("angle"))   return PictureProp::Angle;
    return PictureProp::X;
}

QString picturePropLabel(PictureProp p)
{
    switch (p) {
    case PictureProp::X:       return QObject::tr("Posição horizontal (X)");
    case PictureProp::Y:       return QObject::tr("Posição vertical (Y)");
    case PictureProp::ScaleX:  return QObject::tr("Tamanho horizontal (%)");
    case PictureProp::ScaleY:  return QObject::tr("Tamanho vertical (%)");
    case PictureProp::Opacity: return QObject::tr("Transparência (0..255)");
    case PictureProp::Angle:   return QObject::tr("Rotação (graus)");
    }
    return QString();
}

// ============================================================================
//  Texto como imagem
// ============================================================================
QString pictureTextBgId(PictureTextBg b)
{
    switch (b) {
    case PictureTextBg::Solid:    return QStringLiteral("solid");
    case PictureTextBg::Gradient: return QStringLiteral("gradient");
    case PictureTextBg::Frosted:  return QStringLiteral("frosted");
    case PictureTextBg::Blur:     return QStringLiteral("blur");
    case PictureTextBg::TextBlur: return QStringLiteral("textBlur");
    case PictureTextBg::Window:   return QStringLiteral("window");
    case PictureTextBg::None:     break;
    }
    return QStringLiteral("none");
}

PictureTextBg pictureTextBgFromId(const QString& id)
{
    if (id == QLatin1String("solid"))    return PictureTextBg::Solid;
    if (id == QLatin1String("gradient")) return PictureTextBg::Gradient;
    if (id == QLatin1String("frosted") || id == QLatin1String("glass"))
        return PictureTextBg::Frosted;
    if (id == QLatin1String("blur"))     return PictureTextBg::Blur;
    if (id == QLatin1String("textBlur")) return PictureTextBg::TextBlur;
    if (id == QLatin1String("window"))   return PictureTextBg::Window;
    return PictureTextBg::None;
}

QString pictureTextBgLabel(PictureTextBg b)
{
    switch (b) {
    case PictureTextBg::None:     return QObject::tr("Nenhum (transparente)");
    case PictureTextBg::Solid:    return QObject::tr("Sólido");
    case PictureTextBg::Gradient: return QObject::tr("Gradiente");
    case PictureTextBg::Frosted:  return QObject::tr("Vidro fosco");
    case PictureTextBg::Blur:     return QObject::tr("Painel difuso");
    case PictureTextBg::TextBlur: return QObject::tr("Borrão atrás do texto");
    case PictureTextBg::Window:   return QObject::tr("Janela (moldura 9-slice)");
    }
    return QString();
}

QString pictureTextAlignId(PictureTextAlign a)
{
    switch (a) {
    case PictureTextAlign::Center: return QStringLiteral("center");
    case PictureTextAlign::Right:  return QStringLiteral("right");
    case PictureTextAlign::Left:   break;
    }
    return QStringLiteral("left");
}

PictureTextAlign pictureTextAlignFromId(const QString& id)
{
    if (id == QLatin1String("center")) return PictureTextAlign::Center;
    if (id == QLatin1String("right"))  return PictureTextAlign::Right;
    return PictureTextAlign::Left;
}

QString pictureTextAlignLabel(PictureTextAlign a)
{
    switch (a) {
    case PictureTextAlign::Left:   return QObject::tr("À esquerda");
    case PictureTextAlign::Center: return QObject::tr("Centralizado");
    case PictureTextAlign::Right:  return QObject::tr("À direita");
    }
    return QString();
}

QVariantMap PictureRichText::toParams() const
{
    QVariantMap m;
    if (!enabled) return m;
    m[QStringLiteral("text")]        = text;
    m[QStringLiteral("fontFamily")]  = fontFamily;
    m[QStringLiteral("fontSize")]    = fontSize;
    m[QStringLiteral("bold")]        = bold;
    m[QStringLiteral("italic")]      = italic;
    m[QStringLiteral("color")]       = color.name(QColor::HexArgb);
    if (gradient2.isValid()) m[QStringLiteral("gradient2")] = gradient2.name(QColor::HexArgb);
    if (gradient.enabled()) m[QStringLiteral("gradient")] = gradient.toVariantMap();
    if (effects.enabled()) m[QStringLiteral("textEffects")] = effects.toVariantMap();
    m[QStringLiteral("outlineColor")] = outlineColor.name(QColor::HexArgb);
    m[QStringLiteral("outlineWidth")] = outlineWidth;
    m[QStringLiteral("shadow")]       = shadow;
    m[QStringLiteral("shadowColor")]  = shadowColor.name(QColor::HexArgb);
    m[QStringLiteral("shadowX")]      = shadowOffsetX;
    m[QStringLiteral("shadowY")]      = shadowOffsetY;
    m[QStringLiteral("wordWrap")]     = wordWrap;
    m[QStringLiteral("autoSize")]     = autoSize;
    m[QStringLiteral("width")]        = width;
    m[QStringLiteral("height")]       = height;
    m[QStringLiteral("padding")]      = padding;
    m[QStringLiteral("align")]        = pictureTextAlignId(align);
    m[QStringLiteral("bg")]           = pictureTextBgId(bg);
    m[QStringLiteral("bgColor")]      = bgColor.name(QColor::HexArgb);
    m[QStringLiteral("bgGradient2")]  = bgGradient2.name(QColor::HexArgb);
    m[QStringLiteral("bgRadius")]     = bgRadius;
    m[QStringLiteral("bgAlpha")]      = bgAlpha;
    m[QStringLiteral("bgBorderImage")] = bgBorderImageId;
    m[QStringLiteral("bgBorderSlice")] = bgBorderSlice;
    m[QStringLiteral("bgBorderScale")] = bgBorderScale;
    return m;
}

PictureRichText PictureRichText::fromParams(const QVariantMap& p)
{
    PictureRichText r;
    if (p.isEmpty()) return r;
    r.enabled      = true;
    r.text         = p.value(QStringLiteral("text")).toString();
    r.fontFamily   = p.value(QStringLiteral("fontFamily")).toString();
    r.fontSize     = p.value(QStringLiteral("fontSize"), 24).toInt();
    r.bold         = p.value(QStringLiteral("bold"), false).toBool();
    r.italic       = p.value(QStringLiteral("italic"), false).toBool();
    r.color        = QColor(p.value(QStringLiteral("color"), QStringLiteral("#ffffffff")).toString());
    const QString g2 = p.value(QStringLiteral("gradient2")).toString();
    r.gradient2    = g2.isEmpty() ? QColor() : QColor(g2);
    r.gradient     = TextGradientSpec::fromVariantMap(p.value(QStringLiteral("gradient")).toMap());
    r.effects      = TextEffectStack::fromVariantMap(p.value(QStringLiteral("textEffects")).toMap());
    r.outlineColor = QColor(p.value(QStringLiteral("outlineColor"), QStringLiteral("#ff000000")).toString());
    r.outlineWidth = p.value(QStringLiteral("outlineWidth"), 4).toInt();
    r.shadow       = p.value(QStringLiteral("shadow"), false).toBool();
    r.shadowColor  = QColor(p.value(QStringLiteral("shadowColor"), QStringLiteral("#80000000")).toString());
    r.shadowOffsetX = p.value(QStringLiteral("shadowX"), 2).toInt();
    r.shadowOffsetY = p.value(QStringLiteral("shadowY"), 2).toInt();
    r.wordWrap     = p.value(QStringLiteral("wordWrap"), true).toBool();
    r.autoSize     = p.value(QStringLiteral("autoSize"), true).toBool();
    r.width        = p.value(QStringLiteral("width"), 400).toInt();
    r.height       = p.value(QStringLiteral("height"), 120).toInt();
    r.padding      = p.value(QStringLiteral("padding"), 12).toInt();
    r.align        = pictureTextAlignFromId(p.value(QStringLiteral("align")).toString());
    r.bg           = pictureTextBgFromId(p.value(QStringLiteral("bg")).toString());
    r.bgColor      = QColor(p.value(QStringLiteral("bgColor"), QStringLiteral("#80000000")).toString());
    r.bgGradient2  = QColor(p.value(QStringLiteral("bgGradient2"), QStringLiteral("#ff928dab")).toString());
    r.bgRadius     = p.value(QStringLiteral("bgRadius"), 10).toInt();
    r.bgAlpha      = p.value(QStringLiteral("bgAlpha"), 1.0).toDouble();
    r.bgBorderImageId = p.value(QStringLiteral("bgBorderImage")).toString();
    r.bgBorderSlice   = p.value(QStringLiteral("bgBorderSlice"), 8).toInt();
    r.bgBorderScale   = p.value(QStringLiteral("bgBorderScale"), 1.0).toDouble();
    return r;
}

QString PictureRichText::cacheKey() const
{
    // Tudo que muda os pixels — menos o tempo, que é a animação.
    return QStringLiteral("%1|%2|%3|%4%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18")
        .arg(text, fontFamily)
        .arg(fontSize)
        .arg(int(bold)).arg(int(italic))
        .arg(color.name(QColor::HexArgb),
             gradient2.isValid() ? gradient2.name(QColor::HexArgb) : QString(),
             outlineColor.name(QColor::HexArgb))
        .arg(outlineWidth)
        .arg(shadow ? QStringLiteral("%1,%2,%3").arg(shadowColor.name(QColor::HexArgb))
                          .arg(shadowOffsetX).arg(shadowOffsetY)
                    : QString())
        .arg(int(wordWrap)).arg(int(autoSize)).arg(width).arg(height).arg(padding)
        .arg(pictureTextAlignId(align), pictureTextBgId(bg))
        .arg(QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                 .arg(bgColor.name(QColor::HexArgb), bgGradient2.name(QColor::HexArgb))
                 .arg(bgRadius).arg(bgAlpha).arg(bgBorderImageId)
                 .arg(bgBorderSlice).arg(bgBorderScale))
        + QLatin1Char('|') + QString::fromUtf8(QJsonDocument::fromVariant(gradient.toVariantMap()).toJson(QJsonDocument::Compact))
        + QLatin1Char('|') + QString::fromUtf8(QJsonDocument::fromVariant(effects.toVariantMap()).toJson(QJsonDocument::Compact));
}

// ============================================================================
//  Serialização
// ============================================================================
FrameSequence PictureDef::frameSequence() const
{
    FrameSequence seq;
    seq.enabled = animated;
    seq.count = qMax(1, frameCount);
    seq.columns = qMax(1, frameColumns);
    seq.rows = qMax(1, frameRows);
    seq.firstFrame = qBound(0, frameIndex, seq.count - 1);
    seq.fps = qMax(0.0, frameFps);
    seq.loop = frameLoop;
    seq.playing = framePlaying;
    return seq;
}

void PictureDef::setFrameSequence(const FrameSequence& seq)
{
    animated = seq.enabled;
    frameCount = qMax(1, seq.count);
    frameColumns = qMax(1, seq.columns);
    frameRows = qMax(1, seq.rows);
    frameIndex = qBound(0, seq.firstFrame, frameCount - 1);
    frameFps = qMax(0.0, seq.fps);
    frameLoop = seq.loop;
    framePlaying = seq.playing;
}

QVariantMap PictureDef::toParams() const
{
    QVariantMap p;
    p[QStringLiteral("number")]    = number;
    p[QStringLiteral("assetId")]   = assetId;
    p[QStringLiteral("assetName")] = assetName;
    p[QStringLiteral("x")]         = x;
    p[QStringLiteral("y")]         = y;
    p[QStringLiteral("scaleX")]    = scaleX;
    p[QStringLiteral("scaleY")]    = scaleY;
    p[QStringLiteral("opacity")]   = opacity;
    p[QStringLiteral("angle")]     = angle;
    p[QStringLiteral("anchor")]    = pictureAnchorId(anchor);
    p[QStringLiteral("anchorX")]   = anchorX;
    p[QStringLiteral("anchorY")]   = anchorY;
    p[QStringLiteral("blend")]     = pictureBlendId(blend);
    p[QStringLiteral("space")]     = pictureSpaceId(space);
    p[QStringLiteral("layer")]     = pictureLayerId(layer);
    p[QStringLiteral("smooth")]    = smooth;
    p[QStringLiteral("flipH")]     = flipH;
    p[QStringLiteral("flipV")]     = flipV;
    p[QStringLiteral("duringBattle")] = duringBattle;
    p[QStringLiteral("eraseOnMapChange")] = eraseOnMapChange;
    p[QStringLiteral("affectedByTone")] = affectedByTone;
    if (animated || frameCount > 1) {
        p[QStringLiteral("animated")] = animated;
        p[QStringLiteral("frameCount")] = frameCount;
        p[QStringLiteral("frameColumns")] = frameColumns;
        p[QStringLiteral("frameRows")] = frameRows;
        p[QStringLiteral("frameIndex")] = frameIndex;
        p[QStringLiteral("frameFps")] = frameFps;
        p[QStringLiteral("frameLoop")] = frameLoop;
        p[QStringLiteral("framePlaying")] = framePlaying;
    }
    // A física só entra no arquivo quando existe: um comando "mostrar imagem"
    // simples continua legível quando alguém abre o .json na mão.
    if (hasPhysics()) {
        p[QStringLiteral("floatSpeed")] = floatSpeed;
        p[QStringLiteral("floatRange")] = floatRange;
        p[QStringLiteral("swaySpeed")]  = swaySpeed;
        p[QStringLiteral("swayRange")]  = swayRange;
        p[QStringLiteral("spinSpeed")]  = spinSpeed;
        p[QStringLiteral("pulseSpeed")] = pulseSpeed;
        p[QStringLiteral("pulseRange")] = pulseRange;
    }
    const QVariantMap layout9 = nineSlice.toParams();
    if (!layout9.isEmpty()) p[QStringLiteral("nineSlice")] = layout9;
    const QVariantMap efeitos = fx.toParams();
    if (!efeitos.isEmpty()) p[QStringLiteral("fx")] = efeitos;
    const QVariantMap texto = rich.toParams();
    if (!texto.isEmpty()) p[QStringLiteral("rich")] = texto;
    return p;
}

PictureDef PictureDef::fromParams(const QVariantMap& p)
{
    PictureDef d;
    d.number    = p.value(QStringLiteral("number"), 1).toInt();
    d.assetId   = p.value(QStringLiteral("assetId")).toString();
    d.assetName = p.value(QStringLiteral("assetName")).toString();
    d.x         = p.value(QStringLiteral("x"), 0.0).toDouble();
    d.y         = p.value(QStringLiteral("y"), 0.0).toDouble();
    d.scaleX    = p.value(QStringLiteral("scaleX"), 100.0).toDouble();
    d.scaleY    = p.value(QStringLiteral("scaleY"), 100.0).toDouble();
    d.opacity   = p.value(QStringLiteral("opacity"), 255.0).toDouble();
    d.angle     = p.value(QStringLiteral("angle"), 0.0).toDouble();
    d.anchor    = pictureAnchorFromId(p.value(QStringLiteral("anchor")).toString());
    d.anchorX   = p.value(QStringLiteral("anchorX"), 0.0).toDouble();
    d.anchorY   = p.value(QStringLiteral("anchorY"), 0.0).toDouble();
    d.blend     = pictureBlendFromId(p.value(QStringLiteral("blend")).toString());
    d.space     = pictureSpaceFromId(p.value(QStringLiteral("space")).toString());
    d.layer     = pictureLayerFromId(p.value(QStringLiteral("layer")).toString());
    d.smooth    = p.value(QStringLiteral("smooth"), false).toBool();
    d.flipH     = p.value(QStringLiteral("flipH"), false).toBool();
    d.flipV     = p.value(QStringLiteral("flipV"), false).toBool();
    d.duringBattle = p.value(QStringLiteral("duringBattle"), true).toBool();
    d.eraseOnMapChange = p.value(QStringLiteral("eraseOnMapChange"), false).toBool();
    d.affectedByTone = p.value(QStringLiteral("affectedByTone"), false).toBool();
    d.animated = p.value(QStringLiteral("animated"), false).toBool();
    d.frameCount = qMax(1, p.value(QStringLiteral("frameCount"), 1).toInt());
    d.frameColumns = qMax(1, p.value(QStringLiteral("frameColumns"), 1).toInt());
    d.frameRows = qMax(1, p.value(QStringLiteral("frameRows"), 1).toInt());
    d.frameIndex = qBound(0, p.value(QStringLiteral("frameIndex"), 0).toInt(), d.frameCount - 1);
    d.frameFps = qMax(0.0, p.value(QStringLiteral("frameFps"), 12.0).toDouble());
    d.frameLoop = p.value(QStringLiteral("frameLoop"), true).toBool();
    d.framePlaying = p.value(QStringLiteral("framePlaying"), true).toBool();
    d.floatSpeed = p.value(QStringLiteral("floatSpeed"), 0.0).toDouble();
    d.floatRange = p.value(QStringLiteral("floatRange"), 12.0).toDouble();
    d.swaySpeed  = p.value(QStringLiteral("swaySpeed"), 0.0).toDouble();
    d.swayRange  = p.value(QStringLiteral("swayRange"), 10.0).toDouble();
    d.spinSpeed  = p.value(QStringLiteral("spinSpeed"), 0.0).toDouble();
    d.pulseSpeed = p.value(QStringLiteral("pulseSpeed"), 0.0).toDouble();
    d.pulseRange = p.value(QStringLiteral("pulseRange"), 8.0).toDouble();
    if (p.contains(QStringLiteral("nineSlice")))
        d.nineSlice = PictureNineSlice::fromParams(p.value(QStringLiteral("nineSlice")).toMap());
    if (p.contains(QStringLiteral("fx")))
        d.fx = PictureEffects::fromParams(p.value(QStringLiteral("fx")).toMap());
    if (p.contains(QStringLiteral("rich")))
        d.rich = PictureRichText::fromParams(p.value(QStringLiteral("rich")).toMap());
    return d;
}

} // namespace core
