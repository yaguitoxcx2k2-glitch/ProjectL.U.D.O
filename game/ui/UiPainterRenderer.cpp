#include "UiPainterRenderer.h"

#include <QBitmap>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegion>
#include <QTransform>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace game::ui {

namespace {

void drawPanel(QPainter& p, const UiPanelCommand& cmd)
{
    p.save();
    p.setOpacity(cmd.opacity);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(cmd.style.fill);
    if (cmd.style.borderWidth > 0.0 && cmd.style.border.alpha() > 0)
        p.setPen(QPen(cmd.style.border, cmd.style.borderWidth));
    else
        p.setPen(Qt::NoPen);
    p.drawRoundedRect(cmd.rect, cmd.style.radius, cmd.style.radius);

    if (cmd.style.innerBorderWidth > 0.0 && cmd.style.innerBorder.alpha() > 0
        && cmd.style.innerInset > 0.0) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(cmd.style.innerBorder, cmd.style.innerBorderWidth));
        const QRectF inner = cmd.rect.adjusted(cmd.style.innerInset, cmd.style.innerInset,
                                                -cmd.style.innerInset, -cmd.style.innerInset);
        p.drawRoundedRect(inner, qMax<qreal>(0.0, cmd.style.radius - 2.0),
                          qMax<qreal>(0.0, cmd.style.radius - 2.0));
    }
    p.restore();
}

void drawText(QPainter& p, const UiTextCommand& cmd)
{
    p.save();
    p.setOpacity(cmd.opacity);
    p.setFont(cmd.font);
    p.setPen(cmd.color);
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawText(cmd.rect, int(cmd.flags), cmd.text);
    p.restore();
}

void drawImage(QPainter& p, const UiImageCommand& cmd)
{
    if (cmd.image.isNull() || cmd.target.isEmpty()) return;
    p.save();
    p.setOpacity(cmd.opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform, cmd.smooth);
    QRectF src = cmd.source;
    if (src.isEmpty()) src = QRectF(QPointF(0, 0), QSizeF(cmd.image.size()));
    p.drawImage(cmd.target, cmd.image, src);
    p.restore();
}

void pushClip(QPainter& p, const UiPushClipCommand& cmd)
{
    p.save();
    const QString shape = cmd.shape.trimmed().toLower();
    if (shape == QLatin1String("circle")) {
        QPainterPath path;
        const qreal d = qMin(cmd.rect.width(), cmd.rect.height());
        const QRectF circle(cmd.rect.center().x() - d * 0.5, cmd.rect.center().y() - d * 0.5, d, d);
        path.addEllipse(circle);
        p.setClipPath(path, Qt::IntersectClip);
        return;
    }
    if (shape == QLatin1String("rounded")) {
        QPainterPath path;
        path.addRoundedRect(cmd.rect, qMax<qreal>(0.0, cmd.radius), qMax<qreal>(0.0, cmd.radius));
        p.setClipPath(path, Qt::IntersectClip);
        return;
    }
    if (shape == QLatin1String("image-alpha") && !cmd.maskImage.isNull()) {
        const QSize targetSize(qMax(1, qRound(cmd.rect.width())), qMax(1, qRound(cmd.rect.height())));
        const QImage alpha = cmd.maskImage.convertToFormat(QImage::Format_Alpha8)
            .scaled(targetSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const QBitmap bitmap = QBitmap::fromImage(alpha.createAlphaMask());
        QRegion region(bitmap);
        region.translate(qRound(cmd.rect.left()), qRound(cmd.rect.top()));
        p.setClipRegion(region, Qt::IntersectClip);
        return;
    }
    p.setClipRect(cmd.rect, Qt::IntersectClip);
}

void pushTransform(QPainter& p, const UiPushTransformCommand& cmd)
{
    p.save();
    p.translate(cmd.pivot);
    if (!qFuzzyIsNull(cmd.rotationDegrees)) p.rotate(cmd.rotationDegrees);
    const qreal sh = std::tan(qDegreesToRadians(qBound<qreal>(-80.0, cmd.skewXDegrees, 80.0)));
    const qreal sv = std::tan(qDegreesToRadians(qBound<qreal>(-80.0, cmd.skewYDegrees, 80.0)));
    if (!qFuzzyIsNull(sh) || !qFuzzyIsNull(sv)) p.shear(sh, sv);
    p.scale(qBound<qreal>(0.05, cmd.scaleX, 10.0), qBound<qreal>(0.05, cmd.scaleY, 10.0));
    p.translate(-cmd.pivot);
}

void drawGauge(QPainter& p, const UiGaugeCommand& cmd)
{
    if (cmd.rect.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(cmd.border, 1.0));
    p.setBrush(cmd.background);
    p.drawRoundedRect(cmd.rect, cmd.radius, cmd.radius);

    QRectF fillRect = cmd.rect.adjusted(2, 2, -2, -2);
    fillRect.setWidth(fillRect.width() * qBound<qreal>(0.0, cmd.value, 1.0));
    if (fillRect.width() > 0.0) {
        p.setPen(Qt::NoPen);
        p.setBrush(cmd.fill);
        p.drawRoundedRect(fillRect, qMax<qreal>(0.0, cmd.radius - 1.0),
                          qMax<qreal>(0.0, cmd.radius - 1.0));
    }
    p.restore();
}

void drawRadialProgress(QPainter& p, const UiRadialProgressCommand& cmd)
{
    if (cmd.rect.isEmpty()) return;
    p.save();
    p.setOpacity(cmd.opacity);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF arc = cmd.rect.adjusted(cmd.thickness * 0.5, cmd.thickness * 0.5,
                                   -cmd.thickness * 0.5, -cmd.thickness * 0.5);
    QPen trackPen(cmd.track, cmd.thickness, Qt::SolidLine, Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arc, 90 * 16, -360 * 16);
    QPen fillPen(cmd.fill, cmd.thickness, Qt::SolidLine, Qt::RoundCap);
    p.setPen(fillPen);
    p.drawArc(arc, 90 * 16, qRound(-360.0 * 16.0 * qBound<qreal>(0.0, cmd.value, 1.0)));
    p.restore();
}

void drawTriangle(QPainter& p, const UiTriangleCommand& cmd)
{
    p.save();
    p.setOpacity(cmd.opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(cmd.color);
    const qreal hw = cmd.size.width() * 0.5;
    const qreal hh = cmd.size.height() * 0.5;
    QPolygonF poly;
    if (cmd.pointsDown) {
        poly << cmd.center + QPointF(-hw, -hh)
             << cmd.center + QPointF(hw, -hh)
             << cmd.center + QPointF(0, hh);
    } else {
        poly << cmd.center + QPointF(-hw, hh)
             << cmd.center + QPointF(hw, hh)
             << cmd.center + QPointF(0, -hh);
    }
    p.drawPolygon(poly);
    p.restore();
}

void drawParticle(QPainter& p, const UiParticleCommand& cmd)
{
    if (cmd.opacity <= 0.0 || cmd.size.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(qBound<qreal>(0.0, cmd.opacity, 1.0));
    p.translate(cmd.center);
    if (!qFuzzyIsNull(cmd.rotationDegrees)) p.rotate(cmd.rotationDegrees);
    const QRectF r(-cmd.size.width() * 0.5, -cmd.size.height() * 0.5,
                   cmd.size.width(), cmd.size.height());
    if (cmd.shape == QLatin1String("image") && !cmd.image.isNull()) {
        p.drawImage(r, cmd.image);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(cmd.color);
        if (cmd.shape == QLatin1String("square")) p.drawRect(r);
        else p.drawEllipse(r);
    }
    p.restore();
}

quint32 fastHash(quint32 x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}

QImage blendImages(const QImage& baseIn, const QImage& effectIn, qreal mix)
{
    const qreal t = qBound<qreal>(0.0, mix, 1.0);
    if (t <= 0.0) return baseIn;
    if (t >= 1.0) return effectIn;
    QImage base = baseIn.convertToFormat(QImage::Format_ARGB32);
    const QImage effect = effectIn.convertToFormat(QImage::Format_ARGB32);
    if (base.size() != effect.size()) return baseIn;
    for (int y = 0; y < base.height(); ++y) {
        QRgb* d = reinterpret_cast<QRgb*>(base.scanLine(y));
        const QRgb* e = reinterpret_cast<const QRgb*>(effect.constScanLine(y));
        for (int x = 0; x < base.width(); ++x) {
            const int a = qRound(qAlpha(d[x]) + (qAlpha(e[x]) - qAlpha(d[x])) * t);
            const int r = qRound(qRed(d[x]) + (qRed(e[x]) - qRed(d[x])) * t);
            const int g = qRound(qGreen(d[x]) + (qGreen(e[x]) - qGreen(d[x])) * t);
            const int b = qRound(qBlue(d[x]) + (qBlue(e[x]) - qBlue(d[x])) * t);
            d[x] = qRgba(qBound(0,r,255), qBound(0,g,255), qBound(0,b,255), qBound(0,a,255));
        }
    }
    return base;
}

QImage colorMatrix(const QImage& source, const QString& type, qreal intensity)
{
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb px = row[x]; const int a=qAlpha(px), r=qRed(px), g=qGreen(px), b=qBlue(px);
            int tr=r,tg=g,tb=b;
            if (type == QLatin1String("grayscale")) { const int l=qBound(0,qRound(r*.299+g*.587+b*.114),255);tr=tg=tb=l; }
            else if (type == QLatin1String("sepia")) { tr=qBound(0,qRound(r*.393+g*.769+b*.189),255);tg=qBound(0,qRound(r*.349+g*.686+b*.168),255);tb=qBound(0,qRound(r*.272+g*.534+b*.131),255); }
            else if (type == QLatin1String("invert")) { tr=255-r;tg=255-g;tb=255-b; }
            row[x]=qRgba(qRound(r+(tr-r)*intensity),qRound(g+(tg-g)*intensity),qRound(b+(tb-b)*intensity),a);
        }
    }
    return out;
}

QImage blurApprox(const QImage& source, qreal amount, qreal intensity)
{
    const int factor = qBound(1, 1 + qRound(qBound<qreal>(0,amount,1) * qBound<qreal>(0,intensity,1) * 12.0), 16);
    if (factor <= 1) return source;
    const QSize small(qMax(1, source.width()/factor), qMax(1, source.height()/factor));
    QImage blurred = source.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return blendImages(source, blurred, qBound<qreal>(0.0, intensity, 1.0));
}

QImage pixelate(const QImage& source, qreal amount, qreal intensity)
{
    const int factor = qBound(1, 1 + qRound(qBound<qreal>(0,amount,1) * 24.0), 32);
    if (factor <= 1) return source;
    const QSize small(qMax(1, source.width()/factor), qMax(1, source.height()/factor));
    QImage pix = source.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                       .scaled(source.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return blendImages(source, pix, intensity);
}

QImage chromatic(const QImage& source, qreal amount, qreal intensity)
{
    QImage src=source.convertToFormat(QImage::Format_ARGB32), out=src;
    const int shift=qBound(1,qRound(qBound<qreal>(0,amount,1)*10.0),12);
    for(int y=0;y<src.height();++y){QRgb*d=reinterpret_cast<QRgb*>(out.scanLine(y));const QRgb*s=reinterpret_cast<const QRgb*>(src.constScanLine(y));for(int x=0;x<src.width();++x){const QRgb pr=s[qBound(0,x+shift,src.width()-1)],pg=s[x],pb=s[qBound(0,x-shift,src.width()-1)];d[x]=qRgba(qRed(pr),qGreen(pg),qBlue(pb),qAlpha(pg));}}
    return blendImages(src,out,intensity);
}

QImage wave(const QImage& source,const UiEffectSpec& fx)
{
    QImage src=source.convertToFormat(QImage::Format_ARGB32),out(src.size(),QImage::Format_ARGB32);out.fill(Qt::transparent);
    const qreal maxShift=qBound<qreal>(0,fx.amount,1)*16.0;const qreal phase=fx.timeMs*.001*fx.speed*5.0;
    for(int y=0;y<src.height();++y){const int shift=qRound(std::sin(y*.09+phase)*maxShift);QRgb*d=reinterpret_cast<QRgb*>(out.scanLine(y));const QRgb*s=reinterpret_cast<const QRgb*>(src.constScanLine(y));for(int x=0;x<src.width();++x)d[x]=s[qBound(0,x-shift,src.width()-1)];}
    return blendImages(src,out,fx.intensity);
}

QImage glitch(const QImage& source,const UiEffectSpec& fx)
{
    QImage src=source.convertToFormat(QImage::Format_ARGB32),out=src;
    const quint32 tick=quint32(std::floor(fx.timeMs*.001*qMax<qreal>(.01,fx.speed)*18.0));const int maxShift=qRound(qBound<qreal>(0,fx.amount,1)*28.0);
    int y=0;while(y<src.height()){const quint32 h=fastHash(quint32(fx.seed)^quint32(y*131)^tick);const int band=2+int(h%9);if((h>>8)%100<quint32(18+fx.amount*55)){const int shift=int((h>>16)%(2*maxShift+1))-maxShift;for(int yy=y;yy<qMin(y+band,src.height());++yy){QRgb*d=reinterpret_cast<QRgb*>(out.scanLine(yy));const QRgb*s=reinterpret_cast<const QRgb*>(src.constScanLine(yy));for(int x=0;x<src.width();++x)d[x]=s[qBound(0,x-shift,src.width()-1)];}}y+=band;}
    return blendImages(src,out,fx.intensity);
}

QImage dissolve(const QImage& source,const UiEffectSpec& fx)
{
    QImage out=source.convertToFormat(QImage::Format_ARGB32);const qreal threshold=qBound<qreal>(0,fx.amount,1)*qBound<qreal>(0,fx.intensity,1);const quint32 tick=quint32(std::floor(fx.timeMs*.001*qMax<qreal>(0,fx.speed)*8.0));
    for(int y=0;y<out.height();++y){QRgb*row=reinterpret_cast<QRgb*>(out.scanLine(y));for(int x=0;x<out.width();++x){const quint32 h=fastHash(quint32(fx.seed)^quint32(x*73856093)^quint32(y*19349663)^tick);const qreal n=(h&0xffff)/65535.0;if(n<threshold){const QRgb px=row[x];row[x]=qRgba(qRed(px),qGreen(px),qBlue(px),0);}}}return out;
}

QImage scanlines(const QImage& source,qreal amount,qreal intensity)
{
    QImage out=source.convertToFormat(QImage::Format_ARGB32);const qreal dark=qBound<qreal>(0,amount,1)*qBound<qreal>(0,intensity,1)*.72;const int period=3;
    for(int y=0;y<out.height();++y){if(y%period!=period-1)continue;QRgb*row=reinterpret_cast<QRgb*>(out.scanLine(y));for(int x=0;x<out.width();++x){const QRgb p=row[x];row[x]=qRgba(qRound(qRed(p)*(1-dark)),qRound(qGreen(p)*(1-dark)),qRound(qBlue(p)*(1-dark)),qAlpha(p));}}return out;
}

QImage glow(const QImage& source,const UiEffectSpec& fx)
{
    QImage src=source.convertToFormat(QImage::Format_ARGB32);QImage layer(src.size(),QImage::Format_ARGB32);layer.fill(Qt::transparent);const QColor c=fx.color.isValid()?fx.color:QColor("#67d6ff");
    for(int y=0;y<src.height();++y){const QRgb*s=reinterpret_cast<const QRgb*>(src.constScanLine(y));QRgb*d=reinterpret_cast<QRgb*>(layer.scanLine(y));for(int x=0;x<src.width();++x){const int a=qRound(qAlpha(s[x])*qBound<qreal>(0,fx.intensity,1)*(.25+.75*qBound<qreal>(0,fx.amount,1)));d[x]=qRgba(c.red(),c.green(),c.blue(),qBound(0,a,255));}}
    layer=blurApprox(layer,.35+.65*fx.amount,1.0);QImage out(src.size(),QImage::Format_ARGB32_Premultiplied);out.fill(Qt::transparent);QPainter p(&out);p.drawImage(0,0,layer);p.drawImage(0,0,src);p.end();return out;
}

QImage crt(const QImage& source,const UiEffectSpec& fx)
{
    QImage out=chromatic(source,.25+.45*fx.amount,.55*fx.intensity);out=scanlines(out,.55+.4*fx.amount,fx.intensity);
    QImage img=out.convertToFormat(QImage::Format_ARGB32);const qreal cx=(img.width()-1)*.5,cy=(img.height()-1)*.5;const qreal rx=qMax<qreal>(1,cx),ry=qMax<qreal>(1,cy);
    for(int y=0;y<img.height();++y){QRgb*row=reinterpret_cast<QRgb*>(img.scanLine(y));for(int x=0;x<img.width();++x){const qreal dx=(x-cx)/rx,dy=(y-cy)/ry;const qreal d=qMin<qreal>(1.0,std::sqrt(dx*dx+dy*dy));const qreal f=1.0-d*d*.32*fx.intensity;const QRgb p=row[x];row[x]=qRgba(qRound(qRed(p)*f),qRound(qGreen(p)*f),qRound(qBlue(p)*f),qAlpha(p));}}
    return img;
}

QImage applyEffect(QImage image,const UiEffectSpec& fx)
{
    if(image.isNull()||!qIsFinite(fx.intensity)||fx.intensity<=0.0)return image;const QString type=fx.type.trimmed().toLower();
    if(type==QLatin1String("grayscale")||type==QLatin1String("sepia")||type==QLatin1String("invert"))return colorMatrix(image,type,qBound<qreal>(0,fx.intensity,1));
    if(type==QLatin1String("blur"))return blurApprox(image,fx.amount,fx.intensity);
    if(type==QLatin1String("glow"))return glow(image,fx);
    if(type==QLatin1String("pixelate"))return pixelate(image,fx.amount,fx.intensity);
    if(type==QLatin1String("chromatic"))return chromatic(image,fx.amount,fx.intensity);
    if(type==QLatin1String("glitch"))return glitch(image,fx);
    if(type==QLatin1String("wave"))return wave(image,fx);
    if(type==QLatin1String("dissolve"))return dissolve(image,fx);
    if(type==QLatin1String("scanlines"))return scanlines(image,fx.amount,fx.intensity);
    if(type==QLatin1String("crt"))return crt(image,fx);
    if(type==QLatin1String("vhs")){UiEffectSpec g=fx;g.amount=qMax<qreal>(.25,fx.amount);image=glitch(image,g);image=chromatic(image,.45+.4*fx.amount,.7*fx.intensity);return scanlines(image,.7,fx.intensity);}return image;
}

QPainter::CompositionMode compositionMode(const QString& mode)
{
    if(mode==QLatin1String("add"))return QPainter::CompositionMode_Plus;
    if(mode==QLatin1String("multiply"))return QPainter::CompositionMode_Multiply;
    if(mode==QLatin1String("screen"))return QPainter::CompositionMode_Screen;
    if(mode==QLatin1String("darken"))return QPainter::CompositionMode_Darken;
    if(mode==QLatin1String("lighten"))return QPainter::CompositionMode_Lighten;
    return QPainter::CompositionMode_SourceOver;
}

qreal effectPadding(const QVector<UiEffectSpec>& effects)
{
    qreal pad=2.0;for(const auto&fx:effects){if(!fx.type.compare(QStringLiteral("blur"),Qt::CaseInsensitive)||!fx.type.compare(QStringLiteral("glow"),Qt::CaseInsensitive))pad=qMax(pad,8.0+fx.amount*48.0);else if(fx.type==QLatin1String("wave")||fx.type==QLatin1String("glitch")||fx.type==QLatin1String("chromatic")||fx.type==QLatin1String("vhs"))pad=qMax(pad,4.0+fx.amount*32.0);}return pad;
}

using Commands=std::vector<UiCommand>;
void renderRange(QPainter& painter,const Commands& commands,size_t begin,size_t end);

size_t matchingEffectEnd(const Commands& commands,size_t begin,size_t end)
{
    int depth=0;for(size_t i=begin;i<end;++i){if(std::holds_alternative<UiPushEffectCommand>(commands[i]))++depth;else if(std::holds_alternative<UiPopEffectCommand>(commands[i])){if(depth==0)return i;--depth;}}return end;
}

void renderEffectGroup(QPainter& painter,const UiPushEffectCommand& cmd,const Commands& commands,size_t begin,size_t end)
{
    QRectF logical=cmd.bounds;if(logical.isEmpty()){renderRange(painter,commands,begin,end);return;}const qreal pad=effectPadding(cmd.effects);logical=logical.adjusted(-pad,-pad,pad,pad);
    QRectF deviceBounds=painter.worldTransform().mapRect(logical).normalized();const QRectF deviceRect(0,0,painter.device()->width(),painter.device()->height());deviceBounds=deviceBounds.intersected(deviceRect.adjusted(-1,-1,1,1));if(deviceBounds.isEmpty())return;
    const QPoint origin(qFloor(deviceBounds.left()),qFloor(deviceBounds.top()));const QSize size(qMax(1,qCeil(deviceBounds.right())-origin.x()+1),qMax(1,qCeil(deviceBounds.bottom())-origin.y()+1));
    // Evita explosões de memória em projetos corrompidos ou transforms absurdos.
    if(size.width()>8192||size.height()>8192||qint64(size.width())*size.height()>32ll*1024ll*1024ll){renderRange(painter,commands,begin,end);return;}
    QImage layer(size,QImage::Format_ARGB32_Premultiplied);layer.fill(Qt::transparent);QPainter lp(&layer);lp.setRenderHints(painter.renderHints());QTransform offset;offset.translate(-origin.x(),-origin.y());lp.setWorldTransform(offset*painter.worldTransform());
    if(painter.hasClipping()){const QPainterPath path=painter.clipPath();if(!path.isEmpty())lp.setClipPath(path,Qt::ReplaceClip);else lp.setClipRegion(painter.clipRegion(),Qt::ReplaceClip);}renderRange(lp,commands,begin,end);lp.end();
    QImage effected=layer;for(const auto&fx:cmd.effects)effected=applyEffect(effected,fx);
    painter.save();painter.setWorldTransform(QTransform());painter.setCompositionMode(compositionMode(cmd.blendMode));painter.drawImage(QPointF(origin),effected);painter.restore();
}

void renderRange(QPainter& painter,const Commands& commands,size_t begin,size_t end)
{
    int saveDepth=0;for(size_t i=begin;i<end;++i){const UiCommand&command=commands[i];if(const auto*effect=std::get_if<UiPushEffectCommand>(&command)){const size_t close=matchingEffectEnd(commands,i+1,end);renderEffectGroup(painter,*effect,commands,i+1,close);i=close;continue;}if(std::holds_alternative<UiPopEffectCommand>(command))continue;
        std::visit([&](const auto&cmd){using T=std::decay_t<decltype(cmd)>;if constexpr(std::is_same_v<T,UiPanelCommand>)drawPanel(painter,cmd);else if constexpr(std::is_same_v<T,UiTextCommand>)drawText(painter,cmd);else if constexpr(std::is_same_v<T,UiRichTextCommand>){painter.save();drawTextPage(painter,cmd.page,cmd.font,cmd.rect,cmd.options);painter.restore();}else if constexpr(std::is_same_v<T,UiImageCommand>)drawImage(painter,cmd);else if constexpr(std::is_same_v<T,UiNineSliceCommand>)UiPainterRenderer::drawNineSlice(painter,cmd);else if constexpr(std::is_same_v<T,UiPushClipCommand>){pushClip(painter,cmd);++saveDepth;}else if constexpr(std::is_same_v<T,UiPopClipCommand>){if(saveDepth>0){painter.restore();--saveDepth;}}else if constexpr(std::is_same_v<T,UiPushTransformCommand>){pushTransform(painter,cmd);++saveDepth;}else if constexpr(std::is_same_v<T,UiPopTransformCommand>){if(saveDepth>0){painter.restore();--saveDepth;}}else if constexpr(std::is_same_v<T,UiParticleCommand>)drawParticle(painter,cmd);else if constexpr(std::is_same_v<T,UiGaugeCommand>)drawGauge(painter,cmd);else if constexpr(std::is_same_v<T,UiRadialProgressCommand>)drawRadialProgress(painter,cmd);else if constexpr(std::is_same_v<T,UiTriangleCommand>)drawTriangle(painter,cmd);},command);
    }while(saveDepth-->0)painter.restore();
}

} // namespace

void UiPainterRenderer::render(QPainter& painter, const UiDrawList& list)
{
    renderRange(painter,list.commands(),0,list.commands().size());
}

void UiPainterRenderer::drawNineSlice(QPainter& p, const UiNineSliceCommand& cmd)
{
    if (cmd.image.isNull() || cmd.target.isEmpty()) return;
    const int sw = cmd.image.width();
    const int sh = cmd.image.height();
    if (sw <= 0 || sh <= 0) return;

    const int l = qBound(0, cmd.slices.left(), sw);
    const int r = qBound(0, cmd.slices.right(), sw - l);
    const int t = qBound(0, cmd.slices.top(), sh);
    const int b = qBound(0, cmd.slices.bottom(), sh - t);

    const qreal dl = qMin<qreal>(l, cmd.target.width() * 0.5);
    const qreal dr = qMin<qreal>(r, cmd.target.width() - dl);
    const qreal dt = qMin<qreal>(t, cmd.target.height() * 0.5);
    const qreal db = qMin<qreal>(b, cmd.target.height() - dt);

    const qreal sx[4] = {0.0, qreal(l), qreal(sw - r), qreal(sw)};
    const qreal sy[4] = {0.0, qreal(t), qreal(sh - b), qreal(sh)};
    const qreal dx[4] = {cmd.target.x(), cmd.target.x() + dl,
                         cmd.target.x() + cmd.target.width() - dr,
                         cmd.target.x() + cmd.target.width()};
    const qreal dy[4] = {cmd.target.y(), cmd.target.y() + dt,
                         cmd.target.y() + cmd.target.height() - db,
                         cmd.target.y() + cmd.target.height()};

    p.save();
    p.setOpacity(cmd.opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform, cmd.smooth);
    for (int yi = 0; yi < 3; ++yi) {
        for (int xi = 0; xi < 3; ++xi) {
            const QRectF src(sx[xi], sy[yi], sx[xi + 1] - sx[xi], sy[yi + 1] - sy[yi]);
            const QRectF dst(dx[xi], dy[yi], dx[xi + 1] - dx[xi], dy[yi + 1] - dy[yi]);
            if (src.width() <= 0 || src.height() <= 0 || dst.width() <= 0 || dst.height() <= 0)
                continue;
            p.drawImage(dst, cmd.image, src);
        }
    }
    p.restore();
}

} // namespace game::ui
