// ============================================================================
// VisualEffects.cpp — IDs, serialização e cache-key dos efeitos compartilhados.
// ============================================================================
#include "VisualEffects.h"

#include <QObject>
#include <QtGlobal>

namespace core {

QString pictureBorderStyleId(PictureBorderStyle s)
{
    return s == PictureBorderStyle::NineSlice ? QStringLiteral("image") : QStringLiteral("simple");
}

PictureBorderStyle pictureBorderStyleFromId(const QString& id)
{
    return id == QLatin1String("image") ? PictureBorderStyle::NineSlice : PictureBorderStyle::Simple;
}

QString pictureBorderStyleLabel(PictureBorderStyle s)
{
    return s == PictureBorderStyle::NineSlice ? QObject::tr("Imagem 9-slice")
                                              : QObject::tr("Linha simples");
}


QString pictureTintModeId(PictureTintMode m)
{
    switch (m) {
    case PictureTintMode::Multiply: return QStringLiteral("multiply");
    case PictureTintMode::Screen:   return QStringLiteral("screen");
    case PictureTintMode::Overlay:  return QStringLiteral("overlay");
    case PictureTintMode::SoftLight:return QStringLiteral("softLight");
    case PictureTintMode::HardLight:return QStringLiteral("hardLight");
    case PictureTintMode::Normal:   break;
    }
    return QStringLiteral("normal");
}

PictureTintMode pictureTintModeFromId(const QString& id)
{
    const QString k = id.toLower();
    if (k == QLatin1String("multiply")) return PictureTintMode::Multiply;
    if (k == QLatin1String("screen"))   return PictureTintMode::Screen;
    if (k == QLatin1String("overlay"))  return PictureTintMode::Overlay;
    if (k == QLatin1String("softlight"))return PictureTintMode::SoftLight;
    if (k == QLatin1String("hardlight"))return PictureTintMode::HardLight;
    return PictureTintMode::Normal;
}

QString pictureTintModeLabel(PictureTintMode m)
{
    switch (m) {
    case PictureTintMode::Normal:    return QObject::tr("Normal");
    case PictureTintMode::Multiply:  return QObject::tr("Multiplicar");
    case PictureTintMode::Screen:    return QObject::tr("Clarear (screen)");
    case PictureTintMode::Overlay:   return QObject::tr("Overlay");
    case PictureTintMode::SoftLight: return QObject::tr("Soft Light");
    case PictureTintMode::HardLight: return QObject::tr("Hard Light");
    }
    return QString();
}

QString pictureTransitionId(PictureTransition t)
{
    switch (t) {
    case PictureTransition::Fade:       return QStringLiteral("fade");
    case PictureTransition::SlideDown:  return QStringLiteral("slideDown");
    case PictureTransition::SlideUp:    return QStringLiteral("slideUp");
    case PictureTransition::SlideLeft:  return QStringLiteral("slideLeft");
    case PictureTransition::SlideRight: return QStringLiteral("slideRight");
    case PictureTransition::Zoom:       return QStringLiteral("zoom");
    case PictureTransition::ZoomIn:     return QStringLiteral("zoomIn");
    case PictureTransition::ZoomOut:    return QStringLiteral("zoomOut");
    case PictureTransition::Rotate:     return QStringLiteral("rotate");
    case PictureTransition::Flip:       return QStringLiteral("flip");
    case PictureTransition::Elastic:    return QStringLiteral("elastic");
    case PictureTransition::Shake:      return QStringLiteral("shake");
    case PictureTransition::Dissolve:   return QStringLiteral("dissolve");
    case PictureTransition::Bounce:     return QStringLiteral("bounce");
    case PictureTransition::None:       break;
    }
    return QStringLiteral("none");
}

PictureTransition pictureTransitionFromId(const QString& id)
{
    const QString k = id.toLower();
    if (k == QLatin1String("fade") || k == QLatin1String("fadeout") || k == QLatin1String("fadein"))
        return PictureTransition::Fade;
    if (k == QLatin1String("slidedown"))  return PictureTransition::SlideDown;
    if (k == QLatin1String("slideup"))    return PictureTransition::SlideUp;
    if (k == QLatin1String("slideleft"))  return PictureTransition::SlideLeft;
    if (k == QLatin1String("slideright")) return PictureTransition::SlideRight;
    if (k == QLatin1String("zoom")) return PictureTransition::Zoom;
    if (k == QLatin1String("zoomin")) return PictureTransition::ZoomIn;
    if (k == QLatin1String("zoomout")) return PictureTransition::ZoomOut;
    if (k == QLatin1String("rotate") || k == QLatin1String("rotateout") || k == QLatin1String("rotatein"))
        return PictureTransition::Rotate;
    if (k == QLatin1String("flip") || k == QLatin1String("flipout") || k == QLatin1String("flipin"))
        return PictureTransition::Flip;
    if (k == QLatin1String("elastic") || k == QLatin1String("elasticout") || k == QLatin1String("elasticin"))
        return PictureTransition::Elastic;
    if (k == QLatin1String("shake") || k == QLatin1String("shakeout") || k == QLatin1String("shakein"))
        return PictureTransition::Shake;
    if (k == QLatin1String("dissolve")) return PictureTransition::Dissolve;
    if (k == QLatin1String("bounce"))   return PictureTransition::Bounce;
    return PictureTransition::None;
}

QString pictureTransitionLabel(PictureTransition t)
{
    switch (t) {
    case PictureTransition::None:       return QObject::tr("Nenhuma (aparece/some na hora)");
    case PictureTransition::Fade:       return QObject::tr("Esmaecer");
    case PictureTransition::SlideDown:  return QObject::tr("Deslizar para baixo");
    case PictureTransition::SlideUp:    return QObject::tr("Deslizar para cima");
    case PictureTransition::SlideLeft:  return QObject::tr("Deslizar para a esquerda");
    case PictureTransition::SlideRight: return QObject::tr("Deslizar para a direita");
    case PictureTransition::Zoom:       return QObject::tr("Zoom clássico");
    case PictureTransition::ZoomIn:     return QObject::tr("Zoom In");
    case PictureTransition::ZoomOut:    return QObject::tr("Zoom Out");
    case PictureTransition::Rotate:     return QObject::tr("Girar");
    case PictureTransition::Flip:       return QObject::tr("Virar carta");
    case PictureTransition::Elastic:    return QObject::tr("Elástico");
    case PictureTransition::Shake:      return QObject::tr("Tremer");
    case PictureTransition::Dissolve:   return QObject::tr("Dissolver");
    case PictureTransition::Bounce:     return QObject::tr("Quicar (só na entrada)");
    }
    return QString();
}

namespace {
QString corParaTexto(const QColor& c) { return c.name(QColor::HexRgb); }
QColor  corDeTexto(const QVariant& v, const QColor& padrao)
{
    if (!v.isValid()) return padrao;
    const QColor c(v.toString());
    return c.isValid() ? c : padrao;
}
} // namespace

QVariantMap VisualEffects::toParams() const
{
    QVariantMap m;
    if (border.enabled) {
        QVariantMap b;
        b[QStringLiteral("width")]    = border.width;
        b[QStringLiteral("color")]    = corParaTexto(border.color);
        b[QStringLiteral("alpha")]    = border.alpha;
        b[QStringLiteral("style")]    = pictureBorderStyleId(border.style);
        b[QStringLiteral("image")]    = border.imageAssetId;
        b[QStringLiteral("slice")]    = border.slice;
        b[QStringLiteral("scale")]    = border.scale;
        b[QStringLiteral("paddingX")] = border.paddingX;
        b[QStringLiteral("paddingY")] = border.paddingY;
        b[QStringLiteral("tint")]     = corParaTexto(border.tint);
        b[QStringLiteral("tileH")]    = border.tileH;
        b[QStringLiteral("tileV")]    = border.tileV;
        m[QStringLiteral("border")] = b;
    }
    if (glow.enabled) {
        QVariantMap g;
        g[QStringLiteral("color")]    = corParaTexto(glow.color);
        g[QStringLiteral("strength")] = glow.strength;
        g[QStringLiteral("blink")]    = glow.blink;
        g[QStringLiteral("blinkSpeed")] = glow.blinkSpeed;
        g[QStringLiteral("borderOnly")] = glow.borderOnly;
        m[QStringLiteral("glow")] = g;
    }
    if (blink.enabled) {
        QVariantMap b;
        b[QStringLiteral("hard")]     = blink.hard;
        b[QStringLiteral("speed")]    = blink.speed;
        b[QStringLiteral("minAlpha")] = blink.minAlpha;
        b[QStringLiteral("delay")]    = blink.delay;
        m[QStringLiteral("blink")] = b;
    }
    if (distort.enabled) {
        QVariantMap d;
        d[QStringLiteral("amplitude")]  = distort.amplitude;
        d[QStringLiteral("wavelength")] = distort.wavelength;
        d[QStringLiteral("speed")]      = distort.speed;
        m[QStringLiteral("distort")] = d;
    }
    if (shine.enabled) {
        QVariantMap s;
        s[QStringLiteral("color")] = corParaTexto(shine.color);
        s[QStringLiteral("speed")] = shine.speed;
        s[QStringLiteral("width")] = shine.width;
        s[QStringLiteral("delay")] = shine.delay;
        m[QStringLiteral("shine")] = s;
    }
    if (mask.enabled) {
        QVariantMap k;
        k[QStringLiteral("image")]  = mask.assetId;
        k[QStringLiteral("invert")] = mask.invert;
        k[QStringLiteral("offsetX")] = mask.offsetX;
        k[QStringLiteral("offsetY")] = mask.offsetY;
        k[QStringLiteral("scaleX")] = mask.scaleX;
        k[QStringLiteral("scaleY")] = mask.scaleY;
        k[QStringLiteral("angle")] = mask.angle;
        m[QStringLiteral("mask")] = k;
    }
    if (tone.enabled) {
        QVariantMap t;
        t[QStringLiteral("red")] = qBound(-255, tone.red, 255);
        t[QStringLiteral("green")] = qBound(-255, tone.green, 255);
        t[QStringLiteral("blue")] = qBound(-255, tone.blue, 255);
        t[QStringLiteral("gray")] = qBound(0, tone.gray, 255);
        m[QStringLiteral("tone")] = t;
    }
    if (tint.enabled) {
        QVariantMap t;
        t[QStringLiteral("color")] = tint.color.name(QColor::HexArgb);
        t[QStringLiteral("strength")] = tint.strength;
        t[QStringLiteral("mode")] = pictureTintModeId(tint.mode);
        m[QStringLiteral("tint")] = t;
    }
    if (negative.enabled) {
        QVariantMap n;
        n[QStringLiteral("strength")] = qBound(0.0, negative.strength, 1.0);
        n[QStringLiteral("transitionFrames")] = qMax(0, negative.transitionFrames);
        m[QStringLiteral("negative")] = n;
    }
    if (transitionIn != PictureTransition::None) {
        m[QStringLiteral("transitionIn")] = pictureTransitionId(transitionIn);
        m[QStringLiteral("transitionInFrames")] = transitionInFrames;
    }
    if (transitionOut != PictureTransition::None) {
        m[QStringLiteral("transitionOut")] = pictureTransitionId(transitionOut);
        m[QStringLiteral("transitionOutFrames")] = transitionOutFrames;
    }
    return m;
}

VisualEffects VisualEffects::fromParams(const QVariantMap& p)
{
    VisualEffects fx;
    if (p.contains(QStringLiteral("border"))) {
        const QVariantMap b = p.value(QStringLiteral("border")).toMap();
        fx.border.enabled  = true;
        fx.border.width    = b.value(QStringLiteral("width"), 3.0).toDouble();
        fx.border.color    = corDeTexto(b.value(QStringLiteral("color")), QColor(255, 255, 255));
        fx.border.alpha    = b.value(QStringLiteral("alpha"), 1.0).toDouble();
        fx.border.style    = pictureBorderStyleFromId(b.value(QStringLiteral("style")).toString());
        fx.border.imageAssetId = b.value(QStringLiteral("image")).toString();
        fx.border.slice    = b.value(QStringLiteral("slice"), 5).toInt();
        fx.border.scale    = b.value(QStringLiteral("scale"), 4.0).toDouble();
        fx.border.paddingX = b.value(QStringLiteral("paddingX"), 0).toInt();
        fx.border.paddingY = b.value(QStringLiteral("paddingY"), 0).toInt();
        fx.border.tint     = corDeTexto(b.value(QStringLiteral("tint")), QColor(255, 255, 255));
        fx.border.tileH    = b.value(QStringLiteral("tileH"), false).toBool();
        fx.border.tileV    = b.value(QStringLiteral("tileV"), false).toBool();
    }
    if (p.contains(QStringLiteral("glow"))) {
        const QVariantMap g = p.value(QStringLiteral("glow")).toMap();
        fx.glow.enabled  = true;
        fx.glow.color    = corDeTexto(g.value(QStringLiteral("color")), QColor(255, 255, 0));
        fx.glow.strength = g.value(QStringLiteral("strength"), 8.0).toDouble();
        fx.glow.blink    = g.value(QStringLiteral("blink"), false).toBool();
        fx.glow.blinkSpeed = g.value(QStringLiteral("blinkSpeed"), 2.0).toDouble();
        fx.glow.borderOnly = g.value(QStringLiteral("borderOnly"), false).toBool();
    }
    if (p.contains(QStringLiteral("blink"))) {
        const QVariantMap b = p.value(QStringLiteral("blink")).toMap();
        fx.blink.enabled  = true;
        fx.blink.hard     = b.value(QStringLiteral("hard"), false).toBool();
        fx.blink.speed    = b.value(QStringLiteral("speed"), 3.0).toDouble();
        fx.blink.minAlpha = b.value(QStringLiteral("minAlpha"), 0.0).toDouble();
        fx.blink.delay    = b.value(QStringLiteral("delay"), 0.0).toDouble();
    }
    if (p.contains(QStringLiteral("distort"))) {
        const QVariantMap d = p.value(QStringLiteral("distort")).toMap();
        fx.distort.enabled    = true;
        fx.distort.amplitude  = d.value(QStringLiteral("amplitude"), 8.0).toDouble();
        fx.distort.wavelength = d.value(QStringLiteral("wavelength"), 20.0).toDouble();
        fx.distort.speed      = d.value(QStringLiteral("speed"), 1.0).toDouble();
    }
    if (p.contains(QStringLiteral("shine"))) {
        const QVariantMap s = p.value(QStringLiteral("shine")).toMap();
        fx.shine.enabled = true;
        fx.shine.color   = corDeTexto(s.value(QStringLiteral("color")), QColor(255, 255, 255));
        fx.shine.speed   = s.value(QStringLiteral("speed"), 2.0).toDouble();
        fx.shine.width   = s.value(QStringLiteral("width"), 40.0).toDouble();
        fx.shine.delay   = qMax(0.0, s.value(QStringLiteral("delay"), 0.0).toDouble());
    }
    if (p.contains(QStringLiteral("mask"))) {
        const QVariantMap k = p.value(QStringLiteral("mask")).toMap();
        fx.mask.enabled = true;
        fx.mask.assetId = k.value(QStringLiteral("image")).toString();
        fx.mask.invert  = k.value(QStringLiteral("invert"), false).toBool();
        fx.mask.offsetX = k.value(QStringLiteral("offsetX"), 0.0).toDouble();
        fx.mask.offsetY = k.value(QStringLiteral("offsetY"), 0.0).toDouble();
        fx.mask.scaleX = k.value(QStringLiteral("scaleX"), 100.0).toDouble();
        fx.mask.scaleY = k.value(QStringLiteral("scaleY"), 100.0).toDouble();
        fx.mask.angle = k.value(QStringLiteral("angle"), 0.0).toDouble();
    }
    if (p.contains(QStringLiteral("tone"))) {
        const QVariantMap t = p.value(QStringLiteral("tone")).toMap();
        fx.tone.enabled = true;
        fx.tone.red = qBound(-255, t.value(QStringLiteral("red"), 0).toInt(), 255);
        fx.tone.green = qBound(-255, t.value(QStringLiteral("green"), 0).toInt(), 255);
        fx.tone.blue = qBound(-255, t.value(QStringLiteral("blue"), 0).toInt(), 255);
        fx.tone.gray = qBound(0, t.value(QStringLiteral("gray"), 0).toInt(), 255);
    }
    if (p.contains(QStringLiteral("tint"))) {
        const QVariantMap t = p.value(QStringLiteral("tint")).toMap();
        fx.tint.enabled = true;
        fx.tint.color = corDeTexto(t.value(QStringLiteral("color")), QColor(255, 255, 255));
        fx.tint.strength = qBound(0.0, t.value(QStringLiteral("strength"), 1.0).toDouble(), 1.0);
        fx.tint.mode = pictureTintModeFromId(t.value(QStringLiteral("mode"), QStringLiteral("normal")).toString());
    }
    if (p.contains(QStringLiteral("negative"))) {
        const QVariantMap n = p.value(QStringLiteral("negative")).toMap();
        fx.negative.enabled = true;
        fx.negative.strength = qBound(0.0, n.value(QStringLiteral("strength"), 1.0).toDouble(), 1.0);
        fx.negative.transitionFrames = qMax(0, n.value(QStringLiteral("transitionFrames"), 0).toInt());
    }
    fx.transitionIn = pictureTransitionFromId(p.value(QStringLiteral("transitionIn")).toString());
    fx.transitionInFrames = p.value(QStringLiteral("transitionInFrames"), 30).toInt();
    fx.transitionOut = pictureTransitionFromId(p.value(QStringLiteral("transitionOut")).toString());
    fx.transitionOutFrames = p.value(QStringLiteral("transitionOutFrames"), 30).toInt();
    return fx;
}

QString VisualEffects::staticKey() const
{
    // Só o que altera os PIXELS de forma estável entra aqui: onda e brilho
    // deslizante mudam a cada quadro e não podem participar do cache.
    QString k;
    if (border.enabled)
        k += QStringLiteral("b%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11;")
                 .arg(border.width).arg(border.color.name()).arg(border.alpha)
                 .arg(pictureBorderStyleId(border.style), border.imageAssetId)
                 .arg(border.slice).arg(border.scale).arg(border.paddingX).arg(border.paddingY)
                 .arg(border.tint.name())
                 .arg(int(border.tileH) * 2 + int(border.tileV));
    if (glow.enabled)
        k += QStringLiteral("g%1,%2,%3,%4,%5;").arg(glow.color.name()).arg(glow.strength)
                 .arg(int(glow.blink)).arg(glow.blinkSpeed).arg(int(glow.borderOnly));
    if (tone.enabled)
        k += QStringLiteral("tone%1,%2,%3,%4;").arg(tone.red).arg(tone.green).arg(tone.blue).arg(tone.gray);
    if (tint.enabled)
        k += QStringLiteral("t%1,%2,%3;").arg(tint.color.name(QColor::HexArgb))
                 .arg(tint.strength).arg(pictureTintModeId(tint.mode));
    if (negative.enabled)
        k += QStringLiteral("n%1;").arg(qBound(0.0, negative.strength, 1.0));
    if (mask.enabled)
        k += QStringLiteral("m%1,%2,%3,%4,%5,%6,%7;").arg(mask.assetId).arg(int(mask.invert))
                 .arg(mask.offsetX).arg(mask.offsetY).arg(mask.scaleX).arg(mask.scaleY).arg(mask.angle);
    return k;
}


} // namespace core
