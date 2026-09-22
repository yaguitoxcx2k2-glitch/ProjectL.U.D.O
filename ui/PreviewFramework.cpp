#include "PreviewFramework.h"

#include "core/Editor.h"
#include "core/FilterSystem.h"

#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <cmath>

namespace ui {
namespace {
enum class VisualDomain { Camera,Fog,Cutscene,Picture,Screen,Weather,Filter };

static QRgb previewGaussianSample(const QImage& src,int x,int y,double radius,bool horizontal){static constexpr double w[3]={.375,.25,.0625};const double step=qMax(.5,radius*.5);double rr=0,gg=0,bb=0,aa=0;for(int i=-2;i<=2;++i){const int sx=qBound(0,x+(horizontal?int(std::lround(i*step)):0),src.width()-1),sy=qBound(0,y+(horizontal?0:int(std::lround(i*step))),src.height()-1);const QRgb q=reinterpret_cast<const QRgb*>(src.constScanLine(sy))[sx];const double k=w[std::abs(i)];rr+=qRed(q)*k;gg+=qGreen(q)*k;bb+=qBlue(q)*k;aa+=qAlpha(q)*k;}return qRgba(int(std::lround(rr)),int(std::lround(gg)),int(std::lround(bb)),int(std::lround(aa)));}
static QImage previewGaussianPass(const QImage& src,double radius,bool horizontal){if(radius<=.0001)return src;QImage out(src.size(),QImage::Format_ARGB32_Premultiplied);for(int y=0;y<src.height();++y){QRgb* d=reinterpret_cast<QRgb*>(out.scanLine(y));for(int x=0;x<src.width();++x)d[x]=previewGaussianSample(src,x,y,radius,horizontal);}return out;}
static QImage previewGaussianBlur(const QImage& src,double radius,core::BlurDirection dir){if(dir==core::BlurDirection::Horizontal)return previewGaussianPass(src,radius,true);if(dir==core::BlurDirection::Vertical)return previewGaussianPass(src,radius,false);return previewGaussianPass(previewGaussianPass(src,radius,true),radius,false);}
static int previewBlurHalfWidth(core::BlurQuality q){return q==core::BlurQuality::Low?1:(q==core::BlurQuality::Medium?2:(q==core::BlurQuality::High?3:4));}
static QRgb previewBlurSample(const QImage&src,int x,int y,const core::BlurFilterConfig&cfg,bool full,double dx,double dy){const int half=previewBlurHalfWidth(cfg.quality);const double step=cfg.radiusPixels/qMax(1,half);const QRgb center=src.pixel(x,y);const double centerL=(qRed(center)*.2126+qGreen(center)*.7152+qBlue(center)*.0722)/255.0;double rr=0,gg=0,bb=0,aa=0,total=0;for(int yy=-4;yy<=4;++yy){if(std::abs(yy)>half)continue;for(int xx=-4;xx<=4;++xx){if(std::abs(xx)>half||(!full&&yy!=0))continue;const double mul=full?1.0:double(xx);const int sx=qBound(0,int(std::lround(x+(full?xx*step:dx*mul*step))),src.width()-1),sy=qBound(0,int(std::lround(y+(full?yy*step:dy*mul*step))),src.height()-1);const QRgb q=src.pixel(sx,sy);const double sigma=cfg.style==core::BlurStyle::GaussianSharp?qMax(.65,half*.55):qMax(1.0,half*.9);double wx=cfg.style==core::BlurStyle::Box?1.0:std::exp(-(xx*xx)/(2.0*sigma*sigma)),wy=full?(cfg.style==core::BlurStyle::Box?1.0:std::exp(-(yy*yy)/(2.0*sigma*sigma))):1.0,w=wx*wy;if(cfg.edgePreservation>0){const double l=(qRed(q)*.2126+qGreen(q)*.7152+qBlue(q)*.0722)/255.0;w*=std::exp(-std::abs(l-centerL)*cfg.edgePreservation*12.0);}rr+=qRed(q)*w;gg+=qGreen(q)*w;bb+=qBlue(q)*w;aa+=qAlpha(q)*w;total+=w;}}if(total<=0)return center;auto mix=[&](double a,double b){return qBound(0,int(std::lround(a+(b-a)*cfg.strength)),255);};return qRgba(mix(qRed(center),rr/total),mix(qGreen(center),gg/total),mix(qBlue(center),bb/total),mix(qAlpha(center),aa/total));}
static QImage previewBlur2(const QImage&src,const core::BlurFilterConfig&cfg){if(cfg.style==core::BlurStyle::LegacyGaussian)return previewGaussianBlur(src,cfg.radiusPixels,cfg.direction);QImage out(src.size(),QImage::Format_ARGB32_Premultiplied);const double a=cfg.angleDegrees*3.14159265358979323846/180.0,dx=std::cos(a),dy=std::sin(a);for(int y=0;y<src.height();++y){QRgb*d=reinterpret_cast<QRgb*>(out.scanLine(y));for(int x=0;x<src.width();++x){if(cfg.style==core::BlurStyle::Pixel){const int b=qMax(1,int(std::lround(cfg.radiusPixels))),sx=qBound(0,(x/b)*b+b/2,src.width()-1),sy=qBound(0,(y/b)*b+b/2,src.height()-1);const QRgb c=src.pixel(x,y),q=src.pixel(sx,sy);auto mix=[&](int aa,int bb){return qBound(0,int(std::lround(aa+(bb-aa)*cfg.strength)),255);};d[x]=qRgba(mix(qRed(c),qRed(q)),mix(qGreen(c),qGreen(q)),mix(qBlue(c),qBlue(q)),mix(qAlpha(c),qAlpha(q)));}else if(cfg.style==core::BlurStyle::Directional)d[x]=previewBlurSample(src,x,y,cfg,false,dx,dy);else if(cfg.direction==core::BlurDirection::Full)d[x]=previewBlurSample(src,x,y,cfg,true,1,1);else d[x]=previewBlurSample(src,x,y,cfg,false,cfg.direction==core::BlurDirection::Horizontal?1:0,cfg.direction==core::BlurDirection::Vertical?1:0);}}return out;}

class VisualCommandPreview final:public QWidget
{
public:
    VisualCommandPreview(VisualDomain domain,core::EventCommand command,PreviewContext context,QWidget* parent)
        :QWidget(parent),m_domain(domain),m_command(std::move(command)),m_context(std::move(context))
    {setMinimumSize(280,210);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);const bool animatedFilter=m_domain==VisualDomain::Filter&&(m_command.type==QLatin1String("ludo.filter.noise")||m_command.type==QLatin1String("ludo.filter.scanlines"));const bool animatedDomain=m_domain==VisualDomain::Fog||m_domain==VisualDomain::Weather||animatedFilter||(m_domain==VisualDomain::Screen&&m_command.type.contains(QLatin1String("shake")));if(m_context.animated&&animatedDomain){auto* timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this]{m_phase+=0.035;update();});timer->start(33);}}
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing,true);p.fillRect(rect(),QColor("#141b27"));const QSize logical=m_context.viewportSize.isValid()?m_context.viewportSize:QSize(800,600);const double scale=qMin((width()-20.0)/logical.width(),(height()-20.0)/logical.height());QRectF stage((width()-logical.width()*scale)/2.0,(height()-logical.height()*scale)/2.0,logical.width()*scale,logical.height()*scale);p.save();p.setClipRect(stage);p.translate(stage.topLeft());p.scale(scale,scale);drawScene(p,QRectF(QPointF(0,0),QSizeF(logical)));p.restore();p.setPen(QPen(QColor("#6f8fb8"),1));p.drawRect(stage);p.setPen(QColor("#d7e5f7"));p.drawText(QRectF(10,4,width()-20,20),Qt::AlignRight|Qt::AlignVCenter,m_command.type);
    }
private:
    void baseWorld(QPainter& p,const QRectF& r)
    {QLinearGradient sky(0,0,0,r.height());sky.setColorAt(0,QColor("#243b68"));sky.setColorAt(1,QColor("#6f9c94"));p.fillRect(r,sky);p.fillRect(QRectF(0,r.height()*.68,r.width(),r.height()*.32),QColor("#527447"));p.setPen(QPen(QColor(255,255,255,25),1));for(int x=0;x<r.width();x+=32)p.drawLine(x,0,x,r.height());for(int y=0;y<r.height();y+=32)p.drawLine(0,y,r.width(),y);p.setPen(Qt::NoPen);p.setBrush(QColor("#f2ca68"));p.drawEllipse(QRectF(r.width()*.48-18,r.height()*.58-36,36,52));}
    void drawScene(QPainter& p,const QRectF& r)
    {
        const QVariantMap& v=m_command.params;
        if(m_domain==VisualDomain::Camera){p.save();const double zoom=qBound(.1,v.value(QStringLiteral("zoom"),v.value(QStringLiteral("scale"),1.25)).toDouble(),8.0);const double x=v.value(QStringLiteral("x"),v.value(QStringLiteral("offsetX"),0)).toDouble(),y=v.value(QStringLiteral("y"),v.value(QStringLiteral("offsetY"),0)).toDouble();p.translate(r.center());p.scale(zoom,zoom);p.translate(-r.center()-QPointF(x,y));baseWorld(p,r);p.restore();p.setPen(QPen(QColor("#7bd5ff"),3));p.drawRect(r.adjusted(12,12,-12,-12));return;}
        baseWorld(p,r);
        if(m_domain==VisualDomain::Fog){const int opacity=qBound(0,v.value(QStringLiteral("opacity"),150).toInt(),255);p.setPen(Qt::NoPen);for(int i=-2;i<8;++i){const double x=std::fmod(i*173.0+m_phase*70.0+r.width()*2,r.width()+260)-130;p.setBrush(QColor(220,230,240,opacity/3));p.drawEllipse(QRectF(x,r.height()*.12+(i%3)*75,280,130));}return;}
        if(m_domain==VisualDomain::Cutscene){const int bar=qMax(28,int(r.height()*.12));p.fillRect(QRectF(0,0,r.width(),bar),QColor(0,0,0,235));p.fillRect(QRectF(0,r.height()-bar,r.width(),bar),QColor(0,0,0,235));p.setPen(Qt::white);p.drawText(QRectF(0,r.height()-bar,r.width(),bar),Qt::AlignCenter,QObject::tr("Região de cutscene · pular e avançar rápido continuam disponíveis"));return;}
        if(m_domain==VisualDomain::Picture){const double x=v.value(QStringLiteral("x"),r.width()/2).toDouble(),y=v.value(QStringLiteral("y"),r.height()/2).toDouble(),sx=v.value(QStringLiteral("scaleX"),100).toDouble()/100.0,sy=v.value(QStringLiteral("scaleY"),100).toDouble()/100.0;const int opacity=qBound(0,v.value(QStringLiteral("opacity"),255).toInt(),255);QImage image;QString source=v.value(QStringLiteral("source"),v.value(QStringLiteral("image"))).toString();if(m_context.editor&&!source.isEmpty()){QString path=source;if(QFileInfo(path).isRelative())path=QFileInfo(m_context.editor->projectPath).dir().filePath(path);image.load(path);}QRectF target(x-90*sx,y-65*sy,180*sx,130*sy);p.setOpacity(opacity/255.0);if(!image.isNull())p.drawImage(target,image);else{p.setBrush(QColor("#d7a5d8"));p.setPen(QPen(QColor("#2b2433"),3));p.drawRoundedRect(target,12,12);p.drawLine(target.topLeft(),target.bottomRight());p.drawLine(target.topRight(),target.bottomLeft());}p.setOpacity(1);return;}
        if(m_domain==VisualDomain::Screen){QColor color(v.value(QStringLiteral("color"),QStringLiteral("#000000")).toString());if(!color.isValid())color=QColor(v.value(QStringLiteral("red"),0).toInt(),v.value(QStringLiteral("green"),0).toInt(),v.value(QStringLiteral("blue"),0).toInt());color.setAlpha(qBound(0,v.value(QStringLiteral("alpha"),v.value(QStringLiteral("opacity"),120)).toInt(),255));p.fillRect(r,color);if(m_command.type.contains(QLatin1String("shake"))){p.setPen(QPen(QColor("#ffcc65"),4));p.drawRect(r.adjusted(10+std::sin(m_phase*9)*8,10,-10,-10));}return;}
        if(m_domain==VisualDomain::Weather){const QString type=v.value(QStringLiteral("type"),QStringLiteral("rain")).toString();const int count=qBound(5,v.value(QStringLiteral("intensity"),50).toInt()/2,80);p.setPen(QPen(type==QLatin1String("snow")?Qt::white:QColor("#9ed8ff"),type==QLatin1String("snow")?3:2));for(int i=0;i<count;++i){const double x=std::fmod(i*83.0+m_phase*(type==QLatin1String("snow")?18:90),r.width());const double y=std::fmod(i*137.0+m_phase*(type==QLatin1String("snow")?42:210),r.height());if(type==QLatin1String("snow"))p.drawEllipse(QPointF(x,y),3,3);else p.drawLine(QPointF(x,y),QPointF(x-7,y+18));}return;}
        if(m_domain==VisualDomain::Filter){
            if(m_command.type==QLatin1String("ludo.filter.clear")){
                p.setPen(QColor("#cbd8e8"));p.drawText(r.adjusted(24,24,-24,-24),Qt::AlignCenter,QObject::tr("Remover filtro · %1").arg(v.value(QStringLiteral("filter"),QStringLiteral("all")).toString()));return;
            }
            // Cena simples para visualizar o filtro no mesmo espaço da tela.
            p.fillRect(r,QColor("#17202b"));
            p.setPen(QPen(QColor("#314257"),1));
            for(int x=20;x<int(r.width());x+=32)p.drawLine(x,0,x,int(r.height()));
            for(int y=20;y<int(r.height());y+=32)p.drawLine(0,y,int(r.width()),y);
            const QRectF actor(r.width()*.5-24,r.height()*.50-38,48,70);
            p.setBrush(QColor("#e9c46a"));p.setPen(QPen(QColor("#f8f3e7"),2));p.drawRoundedRect(actor,8,8);
            p.setBrush(QColor("#2a9d8f"));p.drawEllipse(QRectF(actor.center().x()-10,actor.top()+12,20,20));

            if(m_command.type==QLatin1String("ludo.filter.chromaticAberration")){
                const core::ChromaticAberrationConfig cfg=core::ChromaticAberrationConfig::fromVariantMap(v);
                const double edge=qBound(0.0,cfg.edgeStart,.95);const double shift=cfg.intensityPixels*1.8;
                p.setBrush(Qt::NoBrush);
                if(cfg.mode==core::ChromaticAberrationMode::Normal){
                    p.setPen(QPen(QColor(255,55,85,int(210*cfg.mix)),qMax(2.0,shift*.45)));p.drawRoundedRect(actor.translated(shift,0),8,8);
                    p.setPen(QPen(QColor(65,135,255,int(210*cfg.mix)),qMax(2.0,shift*.45)));p.drawRoundedRect(actor.translated(-shift,0),8,8);
                }else{
                    p.setPen(QPen(QColor(255,55,85,int(210*cfg.mix)),qMax(2.0,shift*.45)));p.drawRect(r.adjusted(10+shift,10+shift,-10,-10));
                    p.setPen(QPen(QColor(65,135,255,int(210*cfg.mix)),qMax(2.0,shift*.45)));p.drawRect(r.adjusted(10,10,-10-shift,-10-shift));
                }
                p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,
                    QObject::tr("Aberração %1 · %2 px · slot %3").arg(core::chromaticAberrationModeLabel(cfg.mode)).arg(cfg.intensityPixels,0,'f',1).arg(v.value(QStringLiteral("slot"),1).toInt()));return;
            }
            if(m_command.type==QLatin1String("ludo.filter.noise")){
                const core::NoiseFilterConfig cfg=core::NoiseFilterConfig::fromVariantMap(v);
                const int grain=qMax(1,int(std::lround(cfg.grainSizePixels)));const int phase=cfg.temporalMode==core::NoiseTemporalMode::Static?0:int(m_phase*cfg.speed*50.0);
                p.setPen(Qt::NoPen);
                const int step=cfg.style==core::NoiseStyle::FilmGrain?qMax(1,grain/2):grain;
                for(int y=0;y<int(r.height());y+=step)for(int x=0;x<int(r.width());x+=step){
                    const quint32 h=quint32((x/step)*73856093u)^quint32((y/step)*19349663u)^quint32((phase+cfg.seed)*83492791u);const double signedN=(double(h%1000u)/999.0)*2.0-1.0;const int a=qBound(0,int(std::abs(signedN)*cfg.intensity*cfg.contrast*120.0),180);if(a<2)continue;
                    const int chroma=int(cfg.colorAmount*70.0);const QColor c=signedN>=0?QColor(255,255-chroma/2,255-chroma,a):QColor(45+chroma,35,55+chroma/2,a);p.setBrush(c);if(cfg.style==core::NoiseStyle::FilmGrain)p.drawEllipse(QRect(x,y,qMax(1,step+1),qMax(1,step+1)));else p.drawRect(QRect(x,y,grain,grain));}
                p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,
                    QObject::tr("%1 · %2% · %3 · RGB %4%").arg(core::noiseStyleLabel(cfg.style)).arg(int(cfg.intensity*100)).arg(core::noiseTemporalModeLabel(cfg.temporalMode)).arg(int(cfg.colorAmount*100)));return;
            }
            if(m_command.type==QLatin1String("ludo.filter.scanlines")){
                const core::ScanlineFilterConfig cfg=core::ScanlineFilterConfig::fromVariantMap(v);const double time=m_phase*3.0;
                for(int yi=0;yi<int(r.height());++yi){double wave=0;const double py=yi+.5;if(cfg.style==core::ScanlineStyle::Legacy)wave=.5+.5*std::cos(6.28318530718*py/cfg.spacingPixels);else{const double sp=(py+cfg.phasePixels+cfg.scrollSpeed*time)/cfg.spacingPixels,local=sp-std::floor(sp),dist=std::abs(local-.5)*2.0;const auto smooth=[](double a,double b,double x){if(b<=a)return x>=b?1.0:0.0;double t=qBound(0.0,(x-a)/(b-a),1.0);return t*t*(3-2*t);};wave=1.0-smooth(cfg.thickness,qMin(1.0,cfg.thickness+cfg.softness),dist);if(cfg.style==core::ScanlineStyle::SharpCrt)wave=wave*wave*wave;else if(cfg.style==core::ScanlineStyle::SoftCrt)wave=std::sqrt(qMax(0.0,wave));else if(cfg.style==core::ScanlineStyle::Interlaced&&((int(std::floor(sp))+int(std::floor(time*cfg.interlaceSpeed)))&1))wave*=1.0-cfg.interlaceAmount;}if(wave>.001){p.setPen(QColor(0,0,0,qBound(0,int(wave*cfg.intensity*220),220)));p.drawLine(QPointF(0,yi),QPointF(r.width(),yi));}}
                if(cfg.whiteSweep&&cfg.sweepSpeed>0){const double half=qMax(.005,cfg.sweepWidth),path=1.0+half*2.0,travel=path/cfg.sweepSpeed,cycle=travel+cfg.sweepDelaySeconds,phase=std::fmod(time,qMax(.001,cycle));if(phase<travel){const double pos=(-half+phase*cfg.sweepSpeed)*r.height(),inner=half*(1.0-cfg.sweepSoftness);QLinearGradient g(0,pos-r.height()*half,0,pos+r.height()*half);const QColor c(cfg.sweepRed,cfg.sweepGreen,cfg.sweepBlue,int(cfg.sweepIntensity*220));const double edge=qBound(0.0,(half-inner)/qMax(.0001,half*2.0),.5);g.setColorAt(0,QColor(c.red(),c.green(),c.blue(),0));g.setColorAt(edge,c);g.setColorAt(1.0-edge,c);g.setColorAt(1,QColor(c.red(),c.green(),c.blue(),0));p.fillRect(QRectF(0,pos-r.height()*half,r.width(),r.height()*half*2),g);}}
                p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,QObject::tr("%1 · %2% · %3 px · scroll %4").arg(core::scanlineStyleLabel(cfg.style)).arg(int(cfg.intensity*100)).arg(cfg.spacingPixels,0,'f',1).arg(cfg.scrollSpeed,0,'f',1));return;
            }
            if(m_command.type==QLatin1String("ludo.filter.vignette")){
                const core::VignetteFilterConfig cfg=core::VignetteFilterConfig::fromVariantMap(v);QColor edge(0,0,0,int(cfg.intensity*230));QRadialGradient g(r.center(),qMax(r.width(),r.height())*.72);g.setColorAt(qBound(0.0,cfg.radius,1.0),QColor(0,0,0,0));g.setColorAt(qBound(0.0,cfg.radius+cfg.softness,1.0),edge);g.setColorAt(1.0,edge);p.fillRect(r,g);p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,QObject::tr("Vignette · %1% · raio %2%").arg(int(cfg.intensity*100)).arg(int(cfg.radius*100)));return;
            }
            if(m_command.type==QLatin1String("ludo.filter.blur")){
                const core::BlurFilterConfig cfg=core::BlurFilterConfig::fromVariantMap(v);QImage base(qMax(1,int(r.width())),qMax(1,int(r.height())),QImage::Format_ARGB32_Premultiplied);base.fill(QColor("#17202b"));{QPainter bp(&base);bp.setPen(QPen(QColor("#314257"),1));for(int x=20;x<base.width();x+=32)bp.drawLine(x,0,x,base.height());for(int y=20;y<base.height();y+=32)bp.drawLine(0,y,base.width(),y);QRectF a(base.width()*.5-24,base.height()*.50-38,48,70);bp.setBrush(QColor("#e9c46a"));bp.setPen(QPen(QColor("#f8f3e7"),2));bp.drawRoundedRect(a,8,8);bp.setBrush(QColor("#2a9d8f"));bp.drawEllipse(QRectF(a.center().x()-10,a.top()+12,20,20));}p.drawImage(r,previewBlur2(base,cfg));p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,QObject::tr("%1 · %2 px · %3% ").arg(cfg.style==core::BlurStyle::LegacyGaussian?QObject::tr("Blur Gaussiano Legacy"):core::blurStyleLabel(cfg.style)).arg(cfg.radiusPixels,0,'f',1).arg(int(cfg.strength*100)));return;
            }
            if(m_command.type==QLatin1String("ludo.filter.tiltShift")){
                const core::TiltShiftFilterConfig cfg=core::TiltShiftFilterConfig::fromVariantMap(v);QImage base(qMax(1,int(r.width())),qMax(1,int(r.height())),QImage::Format_ARGB32_Premultiplied);base.fill(QColor("#17202b"));{QPainter bp(&base);bp.setPen(QPen(QColor("#314257"),1));for(int x=20;x<base.width();x+=32)bp.drawLine(x,0,x,base.height());for(int y=20;y<base.height();y+=32)bp.drawLine(0,y,base.width(),y);QRectF a(base.width()*.5-24,base.height()*.50-38,48,70);bp.setBrush(QColor("#e9c46a"));bp.setPen(QPen(QColor("#f8f3e7"),2));bp.drawRoundedRect(a,8,8);bp.setBrush(QColor("#2a9d8f"));bp.drawEllipse(QRectF(a.center().x()-10,a.top()+12,20,20));}
                QImage blurred;if(cfg.style==core::TiltShiftStyle::LegacyGaussian)blurred=previewGaussianBlur(base,cfg.blurPixels,core::BlurDirection::Full);else{core::BlurFilterConfig bc;bc.radiusPixels=cfg.blurPixels;bc.direction=core::BlurDirection::Full;bc.style=cfg.style==core::TiltShiftStyle::GaussianSharp?core::BlurStyle::GaussianSharp:(cfg.style==core::TiltShiftStyle::Box?core::BlurStyle::Box:core::BlurStyle::GaussianSoft);bc.quality=cfg.quality;bc.strength=cfg.strength;bc.edgePreservation=cfg.edgePreservation;blurred=previewBlur2(base,bc);}
                QImage result=base;const double rad=cfg.angleDegrees*3.14159265358979323846/180.0,nx=-std::sin(rad),ny=std::cos(rad);for(int y=0;y<result.height();++y){QRgb*dst=reinterpret_cast<QRgb*>(result.scanLine(y));const QRgb*src=reinterpret_cast<const QRgb*>(blurred.constScanLine(y));for(int x=0;x<result.width();++x){const double ux=(x+.5)/qMax(1.0,double(result.width())),uy=(y+.5)/qMax(1.0,double(result.height())),sd=(ux-.5)*nx+(uy-cfg.centerY)*ny,d=qMax(0.0,std::abs(sd)-cfg.focusWidth*.5),u=qBound(0.0,d/qMax(.01,cfg.falloff),1.0),amount=u*u*(3.0-2.0*u),side=sd<0?cfg.upperBlurAmount:cfg.lowerBlurAmount,w=qBound(0.0,amount*side,1.0);const QRgb a=dst[x],b=src[x];dst[x]=qRgba(int(std::lround(qRed(a)+(qRed(b)-qRed(a))*w)),int(std::lround(qGreen(a)+(qGreen(b)-qGreen(a))*w)),int(std::lround(qBlue(a)+(qBlue(b)-qBlue(a))*w)),qAlpha(a));}}p.drawImage(r,result);p.save();p.translate(r.width()*.5,r.height()*cfg.centerY);p.rotate(cfg.angleDegrees);const double half=r.height()*cfg.focusWidth*.5;p.setPen(QPen(QColor("#80d4ff"),2,Qt::DashLine));p.drawLine(QPointF(-r.width(),-half),QPointF(r.width(),-half));p.drawLine(QPointF(-r.width(),half),QPointF(r.width(),half));p.restore();p.setPen(QColor("#f3f7ff"));p.drawText(QRectF(16,r.height()-58,r.width()-32,42),Qt::AlignCenter,QObject::tr("%1 · %2 px · foco %3% · %4°").arg(core::tiltShiftStyleLabel(cfg.style)).arg(cfg.blurPixels,0,'f',1).arg(int(cfg.focusWidth*100)).arg(cfg.angleDegrees,0,'f',0));return;
            }        }
    }
    VisualDomain m_domain;core::EventCommand m_command;PreviewContext m_context;double m_phase=0;
};

class DomainProvider final:public PreviewFramework
{
public:
    DomainProvider(QString name,VisualDomain domain):m_name(std::move(name)),m_domain(domain){}
    QString id() const override{return m_name;}
    bool supports(const core::EventCommand& command) const override
    {const QString& t=command.type;if(m_domain==VisualDomain::Camera)return t.startsWith(QLatin1String("ludo.camera."));if(m_domain==VisualDomain::Fog)return t.startsWith(QLatin1String("fog."));if(m_domain==VisualDomain::Cutscene)return t.startsWith(QLatin1String("ludo.cutscene."));if(m_domain==VisualDomain::Picture)return t.startsWith(QLatin1String("picture."));if(m_domain==VisualDomain::Screen)return t.startsWith(QLatin1String("ludo.screen."));if(m_domain==VisualDomain::Weather)return t.startsWith(QLatin1String("weather."));return t.startsWith(QLatin1String("ludo.filter."));}
    QWidget* createPreview(const core::EventCommand& command,const PreviewContext& context,QWidget* parent) const override{return new VisualCommandPreview(m_domain,command,context,parent);}
private:QString m_name;VisualDomain m_domain;
};

const QVector<const PreviewFramework*>& providers()
{static const DomainProvider camera(QStringLiteral("camera"),VisualDomain::Camera),fog(QStringLiteral("fog"),VisualDomain::Fog),cutscene(QStringLiteral("cutscene"),VisualDomain::Cutscene),picture(QStringLiteral("picture"),VisualDomain::Picture),screen(QStringLiteral("screen"),VisualDomain::Screen),weather(QStringLiteral("weather"),VisualDomain::Weather),filter(QStringLiteral("filter"),VisualDomain::Filter);static const QVector<const PreviewFramework*> all{&camera,&fog,&cutscene,&picture,&screen,&weather,&filter};return all;}
}

const PreviewFramework* PreviewFramework::providerFor(const core::EventCommand& command){for(const PreviewFramework* provider:providers())if(provider->supports(command))return provider;return nullptr;}
QString PreviewFramework::previewDomainId(const core::EventCommand& command){if(const PreviewFramework* provider=providerFor(command))return provider->id();return QString();}
bool PreviewFramework::supportsPreview(const core::EventCommand& command){return providerFor(command)!=nullptr;}
QWidget* PreviewFramework::create(const core::EventCommand& command,const PreviewContext& context,QWidget* parent){if(const PreviewFramework* provider=providerFor(command))return provider->createPreview(command,context,parent);return nullptr;}

} // namespace ui
