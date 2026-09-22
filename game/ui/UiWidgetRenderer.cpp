#include "UiWidgetRenderer.h"
#include "UiStyleResolver.h"

#include "UiDataBinding.h"
#include "core/Editor.h"
#include "game/GameState.h"

#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QSet>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace game::ui {
namespace {

qreal ease(qreal progress, const QString& easing)
{
    const qreal p = qBound<qreal>(0.0, progress, 1.0);
    if (easing == QLatin1String("linear")) return p;
    if (easing == QLatin1String("ease-in")) return p*p*p;
    if (easing == QLatin1String("ease-in-out")) return p < 0.5 ? 4*p*p*p : 1-std::pow(-2*p+2,3)/2;
    if (easing == QLatin1String("back")) { constexpr qreal c1=1.70158,c3=c1+1; const qreal t=p-1; return 1+c3*t*t*t+c1*t*t; }
    if (easing == QLatin1String("bounce")) {
        constexpr qreal n1=7.5625,d1=2.75; qreal t=p;
        if(t<1/d1)return n1*t*t; if(t<2/d1){t-=1.5/d1;return n1*t*t+.75;}
        if(t<2.5/d1){t-=2.25/d1;return n1*t*t+.9375;} t-=2.625/d1;return n1*t*t+.984375;
    }
    if (easing == QLatin1String("elastic")) { if(p<=0||p>=1)return p; constexpr qreal c4=(2*3.14159265358979323846)/3; return std::pow(2,-10*p)*std::sin((p*10-.75)*c4)+1; }
    const qreal inv=1-p; return 1-inv*inv*inv;
}

core::UiVisualStateSettings normalized(core::UiVisualStateSettings st)
{
    st.offset.setX(qBound(-2.0, st.offset.x(), 2.0)); st.offset.setY(qBound(-2.0, st.offset.y(), 2.0));
    st.scale.setWidth(qBound(0.05, st.scale.width(), 5.0)); st.scale.setHeight(qBound(0.05, st.scale.height(), 5.0));
    st.opacity=qBound(0.0,st.opacity,1.0); if(!st.tint.isValid())st.tint=Qt::white; return st;
}

core::UiVisualStateSettings stateValue(const core::UiLayoutElementSettings& meta, const QString& id)
{
    const auto normal=normalized(meta.visualStates.value(QStringLiteral("normal"),{}));
    return id==QLatin1String("normal") ? normal : normalized(meta.visualStates.value(id,normal));
}

core::UiVisualStateSettings clipValue(const core::UiAnimationClipSettings& clip, int timeMs)
{
    core::UiVisualStateSettings out; if(clip.keyframes.isEmpty())return out;
    if(timeMs<clip.keyframes.first().timeMs)return out;
    if(clip.keyframes.size()==1||timeMs==clip.keyframes.first().timeMs){const auto&k=clip.keyframes.first();out.offset=k.offset;out.scale=k.scale;out.opacity=k.opacity;return normalized(out);}
    if(timeMs>=clip.keyframes.last().timeMs){const auto&k=clip.keyframes.last();out.offset=k.offset;out.scale=k.scale;out.opacity=k.opacity;return normalized(out);}
    for(int i=0;i+1<clip.keyframes.size();++i){const auto&a=clip.keyframes[i];const auto&b=clip.keyframes[i+1];if(timeMs<a.timeMs||timeMs>b.timeMs)continue;const qreal p=ease((timeMs-a.timeMs)/qreal(qMax(1,b.timeMs-a.timeMs)),b.easing);out.offset=a.offset+(b.offset-a.offset)*p;out.scale=QSizeF(a.scale.width()+(b.scale.width()-a.scale.width())*p,a.scale.height()+(b.scale.height()-a.scale.height())*p);out.opacity=a.opacity+(b.opacity-a.opacity)*p;return normalized(out);} return out;
}

QColor multiply(const QColor& a,const QColor& b){return QColor(a.red()*b.red()/255,a.green()*b.green()/255,a.blue()*b.blue()/255,a.alpha()*b.alpha()/255);}
QColor alpha(QColor c,qreal a){c.setAlpha(qBound(0,qRound(c.alpha()*qBound<qreal>(0,a,1)),255));return c;}

struct Visual {
    QRectF rect;
    qreal opacity=1;
    QColor tint=Qt::white;
    bool visible=true;
    QString stateId=QStringLiteral("normal");
};

Visual visualFor(const core::UiWidgetSettings& widget,const core::UiLayoutElementSettings& meta,
                 const UiResolvedDataBindings& data,const QSize& size,const QString& trigger,qreal progress,
                 const QString& runtimeState,const QString& runtimeClip,int runtimeClipTime)
{
    Visual v; v.rect=QRectF(widget.rect.x()*size.width(),widget.rect.y()*size.height(),widget.rect.width()*size.width(),widget.rect.height()*size.height());
    v.visible=data.hasVisible?data.visible:meta.visible;
    QString effective=data.hasEnabled&&!data.enabled?QStringLiteral("disabled"):runtimeState;
    if(effective.isEmpty())effective=QStringLiteral("normal");
    v.stateId=effective;
    auto st=stateValue(meta,effective);
    for(const auto&clip:meta.animationClips){if(clip.trigger!=trigger||clip.keyframes.isEmpty())continue;const qreal chrono=trigger==QLatin1String("close")?1-progress:progress;const auto a=clipValue(clip,qRound(qBound<qreal>(0,chrono,1)*qMax(1,clip.durationMs)));st.offset+=a.offset;st.scale=QSizeF(st.scale.width()*a.scale.width(),st.scale.height()*a.scale.height());st.opacity*=a.opacity;break;}
    if(!runtimeClip.isEmpty())for(const auto&clip:meta.animationClips)if(clip.name==runtimeClip&&!clip.keyframes.isEmpty()){const auto a=clipValue(clip,qBound(0,runtimeClipTime,clip.durationMs));st.offset+=a.offset;st.scale=QSizeF(st.scale.width()*a.scale.width(),st.scale.height()*a.scale.height());st.opacity*=a.opacity;break;}
    v.rect.translate(st.offset.x()*size.width(),st.offset.y()*size.height());const QPointF pivot(v.rect.left()+meta.pivot.x()*v.rect.width(),v.rect.top()+meta.pivot.y()*v.rect.height());const QSizeF scaled(v.rect.width()*st.scale.width(),v.rect.height()*st.scale.height());v.rect=QRectF(pivot.x()-scaled.width()*meta.pivot.x(),pivot.y()-scaled.height()*meta.pivot.y(),scaled.width(),scaled.height());v.opacity=st.opacity;v.tint=st.tint;if(data.hasOpacity)v.opacity*=data.opacity;if(data.hasColor)v.tint=multiply(v.tint,data.color);if(data.hasEnabled&&!data.enabled)v.opacity*=.62;v.opacity=qBound<qreal>(0,v.opacity,1);return v;
}

QImage loadProjectImage(const core::Editor& editor,const QString& path,const QImage& embedded=QImage())
{
    if(path.trimmed().isEmpty()) return embedded;
    const QImage preloaded = editor.preloadedRuntimeImage(path);
    if (!preloaded.isNull()) return preloaded;
    QString resolved=path; QFileInfo fi(resolved);
    if(!fi.isAbsolute()&&!editor.projectRoot().isEmpty())resolved=QDir(editor.projectRoot()).filePath(resolved);
    fi.setFile(resolved);

    // LUDO 3.19 — cache de imagens do UI renderer. Antes, cada widget podia
    // recarregar o mesmo arquivo do disco a cada frame. A chave usa caminho +
    // mtime + tamanho, portanto uma imagem editada externamente é invalidada.
    struct ImageCacheEntry { QImage image; qint64 modifiedMs=0; qint64 fileSize=-1; quint64 stamp=0; qint64 nextProbeMs=0; };
    static thread_local QHash<QString,ImageCacheEntry> cache;
    static thread_local quint64 stamp=0;
    static thread_local QElapsedTimer probeClock;
    if(!probeClock.isValid()) probeClock.start();
    const QString key=QDir::cleanPath(fi.absoluteFilePath());
    const qint64 nowMs=probeClock.elapsed();
    auto it=cache.find(key);
    if(it!=cache.end() && nowMs < it->nextProbeMs) {
        it->stamp=++stamp;
        return it->image.isNull()?embedded:it->image;
    }

    // QFileInfo::exists/lastModified/size podem virar chamadas ao filesystem.
    // Fazê-las para cada imagem, em cada frame, cria pequenos picos de I/O —
    // principalmente no Windows/antivírus. O hot-reload continua existindo,
    // mas as sondagens são espaçadas e escalonadas pelo hash do caminho.
    const bool exists=fi.exists();
    const qint64 modified=exists?fi.lastModified().toMSecsSinceEpoch():0;
    const qint64 fileSize=exists?fi.size():-1;
    const qint64 nextProbe=nowMs + 900 + qint64(qHash(key)%700u);
    if(it!=cache.end()&&it->modifiedMs==modified&&it->fileSize==fileSize){
        it->stamp=++stamp;it->nextProbeMs=nextProbe;
        return it->image.isNull()?embedded:it->image;
    }
    QImage image; image.load(resolved);
    cache.insert(key,ImageCacheEntry{image,modified,fileSize,++stamp,nextProbe});
    if(cache.size()>96){QString oldest;quint64 oldestStamp=std::numeric_limits<quint64>::max();for(auto ci=cache.cbegin();ci!=cache.cend();++ci)if(ci->stamp<oldestStamp){oldestStamp=ci->stamp;oldest=ci.key();}if(!oldest.isEmpty())cache.remove(oldest);}
    return image.isNull()?embedded:image;
}

QImage resolveImage(const core::Editor& editor,const core::UiWidgetSettings&w,const QString& boundPath)
{
    // Data Binding de imagem continua podendo fornecer um caminho dinâmico.
    if(!boundPath.isEmpty())return loadProjectImage(editor,boundPath,w.image);
    // Widgets configurados pelo Designer agora referenciam a Biblioteca de
    // Pictures da LUDO, em vez de duplicar um arquivo/imagem dentro do Widget.
    if(!w.pictureAssetId.isEmpty()) {
        if(const core::PictureAsset* picture=editor.pictureById(w.pictureAssetId))return picture->image;
    }
    // Compatibilidade com projetos <= 3.20.5.
    if(!w.image.isNull())return w.image;
    return loadProjectImage(editor,w.imagePath,w.image);
}

bool skinEligibleType(const QString& type)
{
    static const QSet<QString> eligible={
        QStringLiteral("panel"),QStringLiteral("window"),QStringLiteral("card"),QStringLiteral("button"),
        QStringLiteral("checkbox"),QStringLiteral("radio"),QStringLiteral("toggle"),QStringLiteral("stepper"),
        QStringLiteral("dropdown"),QStringLiteral("text-input"),QStringLiteral("list"),QStringLiteral("sidebar"),
        QStringLiteral("context-menu"),QStringLiteral("data-table"),QStringLiteral("grid"),QStringLiteral("card-view"),
        QStringLiteral("inventory-grid"),QStringLiteral("inventory-slot"),QStringLiteral("multi-slot-item"),
        QStringLiteral("item-slot"),QStringLiteral("equipment-slot"),QStringLiteral("skill-slot"),
        QStringLiteral("tooltip"),QStringLiteral("dialogue-bubble"),QStringLiteral("hint-bar"),
        QStringLiteral("modal"),QStringLiteral("overlay"),QStringLiteral("split-container"),QStringLiteral("scroll-area")
    };
    return eligible.contains(type);
}

using ResolvedStyle = UiResolvedStyle;

ResolvedStyle resolveStyle(const UiTheme& theme,const core::Editor& editor,const core::UiWidgetSettings&w,
                           const QString& stateId,const QFont& fallbackFont)
{
    const QString mode=w.styleMode.trimmed().toLower();
    QString classId;
    if(mode==QLatin1String("class"))classId=w.styleClassId;
    else if(mode!=QLatin1String("custom"))classId=theme.widgetTypeStyles.value(w.type);
    const auto classIt=theme.styleClasses.constFind(classId);
    if(!classId.isEmpty()&&classIt!=theme.styleClasses.cend())return resolveStyleClass(theme,editor,classIt.value(),stateId,fallbackFont);

    ResolvedStyle out;out.font=fallbackFont;
    if(mode==QLatin1String("custom")){
        out.panel.fill=w.fillColor;out.panel.border=w.accentColor;out.panel.innerBorder=Qt::transparent;out.panel.innerBorderWidth=0;out.panel.radius=7;
        out.text=w.textColor;out.accent=w.accentColor;out.backgroundMode=w.customBackgroundMode;
        if(out.backgroundMode==QLatin1String("window-skin"))out.backgroundImage=theme.windowSkin;
        else if(out.backgroundMode==QLatin1String("nine-slice"))out.backgroundImage=loadProjectImage(editor,w.customSkinPath,w.customSkinImage);
        out.slices=w.customSkinSlices;return out;
    }

    // Herança de Theme: usa tipografia/cores globais e, quando habilitado, a
    // mesma Windowskin das janelas tradicionais. É o comportamento esperado
    // por usuários de RPGs sem exigir configurar cada Button manualmente.
    out.panel=theme.choiceWindow;out.text=theme.text;out.accent=theme.accent;out.paddingX=theme.paddingX;out.paddingY=theme.paddingY;
    out.font=fallbackFont;if(!theme.fontFamily.trimmed().isEmpty())out.font.setFamily(theme.fontFamily.trimmed());out.font.setPixelSize(theme.fontSize);
    if(theme.widgetsInheritWindowSkin&&theme.hasWindowSkin()&&skinEligibleType(w.type)){out.backgroundMode=QStringLiteral("window-skin");out.backgroundImage=theme.windowSkin;out.slices=theme.windowSkinSlices;}
    if(stateId==QLatin1String("hover")||stateId==QLatin1String("focused")||stateId==QLatin1String("selected"))out.stateOverlay=theme.selection.fill;
    else if(stateId==QLatin1String("pressed")){out.stateOverlay=theme.selection.fill.darker(125);}
    else if(stateId==QLatin1String("disabled")){out.opacity*=.58;}
    return out;
}

void drawBackground(UiDrawList& list,const QRectF&r,const UiTheme&theme,const ResolvedStyle&s,qreal opacity)
{
    appendResolvedBackground(list,r,theme,s,opacity);
}

QString displayText(const core::Editor& editor,const core::UiWidgetSettings&w,const UiResolvedDataBindings&data)
{
    if(data.hasText)return data.text;
    return core::resolvePlayerText(editor.localization,w.textKey,w.text);
}
qreal progressValue(const core::UiWidgetSettings&w,const UiResolvedDataBindings&d){if(d.hasProgress)return qBound(0.0,d.progress,1.0);const double value=d.hasValue?d.value:w.value;const double maximum=d.hasMaximum?d.maximum:w.maximum;return maximum>w.minimum?qBound(0.0,(value-w.minimum)/(maximum-w.minimum),1.0):0.0;}

void drawPlaceholder(UiDrawList& list,const QRectF&r,const UiTheme&theme,const ResolvedStyle&style,qreal opacity,const QString&label)
{drawBackground(list,r,theme,style,opacity*.65);list.addText(r.adjusted(5,3,-5,-3),label,style.font,style.text,Qt::AlignCenter,opacity*.9);}

const core::DatabaseRecord* inventoryRecord(const core::Editor& editor,const QString& id)
{
    for(const QString& category:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})
        for(const core::DatabaseRecord& record:editor.database.value(category))
            if(record.id==id)return &record;
    return nullptr;
}

void drawWidget(UiDrawList& list,const UiTheme&theme,const core::Editor&editor,const GameState&state,
                const core::UiWidgetSettings&w,const UiResolvedDataBindings&data,const Visual&v,const QFont&font)
{
    if(!v.visible||v.opacity<=.001)return;const QRectF r=v.rect;const QString type=w.type;const QString text=displayText(editor,w,data);
    const QString placeholder=core::resolvePlayerText(editor.localization,w.placeholderTextKey,w.placeholder);
    QStringList localizedItems=w.items;
    for(int i=0;i<localizedItems.size()&&i<w.itemTextKeys.size();++i)
        localizedItems[i]=core::resolvePlayerText(editor.localization,w.itemTextKeys.at(i),localizedItems.at(i));
    ResolvedStyle rs=resolveStyle(theme,editor,w,v.stateId,font);rs.text=multiply(rs.text,v.tint);rs.accent=multiply(rs.accent,v.tint);rs.panel.fill=multiply(rs.panel.fill,v.tint);rs.panel.border=multiply(rs.panel.border,v.tint);rs.panel.innerBorder=multiply(rs.panel.innerBorder,v.tint);if(rs.stateOverlay.isValid())rs.stateOverlay=multiply(rs.stateOverlay,v.tint);
    const QColor textColor=rs.text,accent=rs.accent;const UiPanelStyle style=rs.panel;const QFont resolvedFont=rs.font;
    if(type==QLatin1String("spacer")||type==QLatin1String("world-ui-anchor")||type==QLatin1String("particle-emitter"))return;
    if(type==QLatin1String("text")){list.addText(r,text,resolvedFont,textColor,int(Qt::AlignLeft|Qt::AlignVCenter)|int(Qt::TextWordWrap),v.opacity*rs.opacity);return;}
    if(type==QLatin1String("action-link")){QFont linkFont=resolvedFont;linkFont.setUnderline(true);list.addText(r,text,linkFont,accent,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("placeholder")){drawPlaceholder(list,r,theme,rs,v.opacity,text.isEmpty()?QStringLiteral("Empty"):text);return;}
    if(type==QLatin1String("button")){drawBackground(list,r,theme,rs,v.opacity);QFont buttonFont=resolvedFont;buttonFont.setBold(true);list.addText(r.adjusted(rs.paddingX,rs.paddingY,-rs.paddingX,-rs.paddingY),text,buttonFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("image")||type==QLatin1String("icon")||type==QLatin1String("portrait")||type==QLatin1String("animated-image")||type==QLatin1String("animated-portrait")||type==QLatin1String("character-sprite-preview")||type==QLatin1String("paper-doll")||type==QLatin1String("render-target-2d")||type==QLatin1String("viewport-2d")){
        const QImage image=resolveImage(editor,w,data.hasImage?data.imagePath:QString());if(!image.isNull())list.addImage(r,image,QRectF(),v.opacity*rs.opacity,true);else drawPlaceholder(list,r,theme,rs,v.opacity,type==QLatin1String("viewport-2d")?QStringLiteral("Viewport 2D"):text);return;
    }
    if(type==QLatin1String("radial-progress")||type==QLatin1String("hold-action")){
        const qreal thickness=qBound<qreal>(3.0,qMin(r.width(),r.height())*.10,12.0);
        list.addRadialProgress(r,progressValue(w,data),QColor(15,23,42,190),accent,thickness,v.opacity*rs.opacity);
        if(!text.isEmpty())list.addText(r.adjusted(thickness,thickness,-thickness,-thickness),text,resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);return;
    }
    if(type==QLatin1String("progress-bar")||type==QLatin1String("world-health-bar")){
        list.addGauge(r,progressValue(w,data),alpha(QColor(0,0,0,150),v.opacity*rs.opacity),alpha(accent,v.opacity*rs.opacity),alpha(textColor,v.opacity*rs.opacity),qMin<qreal>(8,r.height()/2));if(!text.isEmpty())list.addText(r,text,resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);return;
    }
    if(type==QLatin1String("checkbox")||type==QLatin1String("radio")||type==QLatin1String("toggle")){
        drawBackground(list,r,theme,rs,v.opacity);const qreal box=qMin(r.height()-8.0,24.0);QRectF mark(r.left()+6,r.center().y()-box/2,box,box);UiPanelStyle ms=style;ms.fill=w.checked?accent:QColor(0,0,0,100);list.addPanel(mark,ms,v.opacity*rs.opacity);list.addText(r.adjusted(box+14,0,-6,0),text,resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);return;
    }
    if(type==QLatin1String("slider")||type==QLatin1String("scrollbar")){drawBackground(list,r,theme,rs,v.opacity*.45);const qreal p=progressValue(w,data);QRectF knob=r;if(w.orientation==QLatin1String("vertical")){knob.setHeight(qMax(10.0,r.height()*.14));knob.moveTop(r.top()+(r.height()-knob.height())*p);}else{knob.setWidth(qMax(10.0,r.width()*.14));knob.moveLeft(r.left()+(r.width()-knob.width())*p);}UiPanelStyle ks=style;ks.fill=accent;list.addPanel(knob,ks,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("stepper")){drawBackground(list,r,theme,rs,v.opacity);list.addText(r,QStringLiteral("‹   %1   ›").arg(data.hasValue?data.value:w.value),resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("page-indicator")||type==QLatin1String("progress-dots")){const int n=qMax(1,localizedItems.isEmpty()?qMax(1,w.columns):localizedItems.size());const qreal d=qMin<qreal>(12,r.height()*.5);const qreal gap=d*.7;const qreal total=n*d+(n-1)*gap;qreal x=r.center().x()-total/2;const int active=qBound(0,w.selectedIndex,n-1);for(int i=0;i<n;++i){UiPanelStyle ds=style;ds.radius=d/2;ds.fill=(type==QLatin1String("progress-dots")?i<=active:i==active)?accent:QColor(100,116,139,180);list.addPanel(QRectF(x,r.center().y()-d/2,d,d),ds,v.opacity*rs.opacity);x+=d+gap;}return;}
    if(type==QLatin1String("tab-bar")||type==QLatin1String("icon-tabs")||type==QLatin1String("segmented-control")){const QStringList items=localizedItems.isEmpty()?QStringList{QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("C")}:localizedItems;const int n=qMax(1,items.size());for(int i=0;i<n;++i){QRectF cell=r;if(w.orientation==QLatin1String("vertical")){cell.setTop(r.top()+r.height()*i/n);cell.setHeight(r.height()/n);}else{cell.setLeft(r.left()+r.width()*i/n);cell.setWidth(r.width()/n);}UiPanelStyle cs=style;if(i==qBound(0,w.selectedIndex,n-1))cs.fill=accent;list.addPanel(cell,cs,v.opacity*rs.opacity);list.addText(cell,items.value(i),resolvedFont,i==w.selectedIndex?QColor(Qt::black):textColor,Qt::AlignCenter,v.opacity*rs.opacity);}return;}
    if(type==QLatin1String("dropdown")){
        drawBackground(list,r,theme,rs,v.opacity);const QStringList items=localizedItems.isEmpty()?QStringList{text}:localizedItems;const int selected=qBound(0,w.selectedIndex,qMax(0,items.size()-1));
        list.addText(r.adjusted(rs.paddingX,0,-28,0),items.value(selected,text),resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);
        list.addText(QRectF(r.right()-26,r.top(),22,r.height()),v.stateId==QLatin1String("expanded")?QStringLiteral("▲"):QStringLiteral("▼"),resolvedFont,accent,Qt::AlignCenter,v.opacity*rs.opacity);
        if(v.stateId==QLatin1String("expanded")&&!items.isEmpty()){const int n=qMin(items.size(),8);const qreal rowH=qMax<qreal>(24,r.height());QRectF popup(r.left(),r.bottom()+2,r.width(),rowH*n);drawBackground(list,popup,theme,rs,v.opacity);for(int i=0;i<n;++i){QRectF row(popup.left()+4,popup.top()+i*rowH,popup.width()-8,rowH);if(i==selected){UiPanelStyle ss=theme.selection;ss.fill=alpha(accent,.35);list.addPanel(row,ss,v.opacity*rs.opacity);}list.addText(row.adjusted(6,0,-6,0),items.value(i),resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);}}return;
    }
    if(type==QLatin1String("list")||type==QLatin1String("sidebar")||type==QLatin1String("context-menu")){drawBackground(list,r,theme,rs,v.opacity);const QStringList items=localizedItems.isEmpty()?QStringList{text}:localizedItems;const int n=qMax(1,qMin(items.size(),8));const qreal rowH=r.height()/n;for(int i=0;i<n;++i){QRectF row(r.left()+4,r.top()+i*rowH,r.width()-8,rowH);if(i==qBound(0,w.selectedIndex,n-1)){UiPanelStyle ss=theme.selection;ss.fill=alpha(accent,.35);list.addPanel(row,ss,v.opacity*rs.opacity);}list.addText(row.adjusted(6,0,-6,0),items.value(i),resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);}return;}
    if(type==QLatin1String("data-table")){drawBackground(list,r,theme,rs,v.opacity);const int cols=qBound(1,w.columns,8);const QStringList cells=localizedItems.isEmpty()?QStringList{QStringLiteral("Nome"),QStringLiteral("Valor"),QStringLiteral("Item A"),QStringLiteral("10"),QStringLiteral("Item B"),QStringLiteral("20")}:localizedItems;const int rows=qMax(1,int(std::ceil(cells.size()/double(cols))));const qreal cw=r.width()/cols,rh=r.height()/rows;for(int i=0;i<cells.size();++i){const int rr=i/cols,cc=i%cols;QRectF cell(r.left()+cc*cw,r.top()+rr*rh,cw,rh);list.addText(cell.adjusted(4,1,-4,-1),cells[i],resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);}return;}
    if(type==QLatin1String("inventory-grid")){
        drawBackground(list,r,theme,rs,v.opacity*.55);
        QStringList ids=state.inventoryIds();
        ids.erase(std::remove_if(ids.begin(),ids.end(),[&](const QString& id){const core::DatabaseRecord* record=inventoryRecord(editor,id);return !record||!record->data.value(QStringLiteral("showInInventory"),true).toBool();}),ids.end());
        const int cols=qBound(1,w.columns,16);
        const int capacity=cols*qMax(1,w.rows>0?w.rows:2),count=qMax(capacity,ids.size());
        const int rows=int(std::ceil(count/double(cols)));const qreal gap=4,cw=(r.width()-gap*(cols+1))/cols,ch=(r.height()-gap*(rows+1))/rows;
        for(int i=0;i<count;++i){const int rr=i/cols,cc=i%cols;QRectF cell(r.left()+gap+cc*(cw+gap),r.top()+gap+rr*(ch+gap),cw,ch);UiPanelStyle cs=style;cs.fill=i==qBound(0,w.selectedIndex,count-1)?alpha(accent,.45):QColor(15,23,42,180);list.addPanel(cell,cs,v.opacity*rs.opacity);
            if(i>=ids.size())continue;const QString id=ids.at(i);const core::DatabaseRecord* record=inventoryRecord(editor,id);if(!record)continue;
            const QRect iconRect=editor.iconSet.iconRect(record->icon);QRectF iconTarget=cell.adjusted(6,5,-6,-18);const qreal side=qMin(iconTarget.width(),iconTarget.height());iconTarget=QRectF(iconTarget.center().x()-side/2,iconTarget.center().y()-side/2,side,side);
            if(iconRect.isValid())list.addImage(iconTarget,editor.iconSet.image,iconRect,v.opacity*rs.opacity,false);
            else list.addText(iconTarget,record->name,resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);
            list.addText(QRectF(cell.left()+3,cell.bottom()-20,cell.width()-6,17),QStringLiteral("×%1").arg(state.itemCount(id)),resolvedFont,textColor,Qt::AlignRight|Qt::AlignVCenter,v.opacity*rs.opacity);
        }return;
    }
    if(type==QLatin1String("grid")||type==QLatin1String("card-view")){drawBackground(list,r,theme,rs,v.opacity*.55);const int cols=qBound(1,w.columns,16);const int count=qMax(cols,localizedItems.isEmpty()?cols*qMax(1,w.rows>0?w.rows:2):localizedItems.size());const int rows=int(std::ceil(count/double(cols)));const qreal gap=4,cw=(r.width()-gap*(cols+1))/cols,ch=(r.height()-gap*(rows+1))/rows;for(int i=0;i<count;++i){const int rr=i/cols,cc=i%cols;QRectF cell(r.left()+gap+cc*(cw+gap),r.top()+gap+rr*(ch+gap),cw,ch);UiPanelStyle cs=style;cs.fill=i==qBound(0,w.selectedIndex,count-1)?alpha(accent,.45):QColor(15,23,42,180);list.addPanel(cell,cs,v.opacity*rs.opacity);if(i<localizedItems.size())list.addText(cell.adjusted(3,2,-3,-2),localizedItems[i],resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);}return;}
    if(type==QLatin1String("inventory-slot")||type==QLatin1String("multi-slot-item")||type==QLatin1String("item-slot")||type==QLatin1String("equipment-slot")||type==QLatin1String("skill-slot")){drawBackground(list,r,theme,rs,v.opacity);list.addText(r.adjusted(4,3,-4,-3),text,resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);if(type==QLatin1String("multi-slot-item"))list.addText(QRectF(r.left()+4,r.top()+3,52,18),QStringLiteral("%1×%2").arg(w.slotWidth).arg(w.slotHeight),resolvedFont,accent,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);if(w.quantity>1)list.addText(QRectF(r.right()-42,r.bottom()-22,36,18),QString::number(w.quantity),resolvedFont,textColor,Qt::AlignRight|Qt::AlignVCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("badge")){UiPanelStyle bs=style;bs.fill=accent;bs.radius=qMin(r.width(),r.height())/2;list.addPanel(r,bs,v.opacity*rs.opacity);list.addText(r,text.isEmpty()?QString::number(w.quantity):text,resolvedFont,QColor(Qt::black),Qt::AlignCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("breadcrumb")){list.addText(r,localizedItems.isEmpty()?text:localizedItems.join(QStringLiteral("  /  ")),resolvedFont,textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("text-input")){drawBackground(list,r,theme,rs,v.opacity);const bool editing=v.stateId==QLatin1String("selected");const QString shown=text.isEmpty()?(editing?QStringLiteral("|"):placeholder):(editing?text+QStringLiteral("|"):text);list.addText(r.adjusted(rs.paddingX,0,-rs.paddingX,0),shown,resolvedFont,text.isEmpty()&&!editing?alpha(textColor,.55):textColor,Qt::AlignLeft|Qt::AlignVCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("hint-bar")){drawBackground(list,r,theme,rs,v.opacity*.8);list.addText(r.adjusted(rs.paddingX,0,-rs.paddingX,0),text,resolvedFont,textColor,Qt::AlignCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("tooltip")||type==QLatin1String("dialogue-bubble")){drawBackground(list,r,theme,rs,v.opacity);list.addText(r.adjusted(rs.paddingX,rs.paddingY,-rs.paddingX,-rs.paddingY),text,resolvedFont,textColor,int(Qt::AlignLeft|Qt::AlignTop)|int(Qt::TextWordWrap),v.opacity*rs.opacity);return;}
    if(type==QLatin1String("damage-number")){QFont big=resolvedFont;big.setBold(true);big.setPixelSize(qMax(resolvedFont.pixelSize(),22));list.addText(r,text,big,accent,Qt::AlignCenter,v.opacity*rs.opacity);return;}
    if(type==QLatin1String("quest-marker")||type==QLatin1String("interaction-prompt")){list.addText(r,text,resolvedFont,accent,Qt::AlignCenter,v.opacity*rs.opacity);return;}
    // Panel, Window, Card, Split, Scroll, Modal e Overlay são containers reais.
    drawBackground(list,r,theme,rs,v.opacity);if(!text.isEmpty())list.addText(r.adjusted(rs.paddingX,rs.paddingY,-rs.paddingX,-rs.paddingY),text,resolvedFont,textColor,int(Qt::AlignLeft|Qt::AlignTop)|int(Qt::TextWordWrap),v.opacity*rs.opacity);
}

quint32 stableHash(const QString& text)
{
    quint32 h=2166136261u;
    for(const QChar ch:text){h^=quint32(ch.unicode());h*=16777619u;}
    return h;
}
quint32 mixHash(quint32 x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
qreal random01(quint32 seed,quint32 index,quint32 channel){return (mixHash(seed^mixHash(index*0x9e3779b9u+channel*0x85ebca6bu))&0xffffu)/65535.0;}

QColor lerpColor(const QColor&a,const QColor&b,qreal t)
{
    const qreal p=qBound<qreal>(0,t,1);
    return QColor(qRound(a.red()+(b.red()-a.red())*p),qRound(a.green()+(b.green()-a.green())*p),
                  qRound(a.blue()+(b.blue()-a.blue())*p),qRound(a.alpha()+(b.alpha()-a.alpha())*p));
}

QVector<UiEffectSpec> effectSpecs(const core::UiWidgetSettings&w,qint64 elapsedMs)
{
    QVector<UiEffectSpec> out;out.reserve(qMin(8,int(w.materialEffects.size())));
    for(const auto&fx:w.materialEffects){
        if(!fx.enabled||fx.type==QLatin1String("none")||out.size()>=8)continue;
        UiEffectSpec spec;spec.type=fx.type;spec.intensity=qBound(0.0,fx.intensity,1.0);spec.amount=qBound(0.0,fx.amount,1.0);
        spec.speed=qBound(0.0,fx.speed,20.0);spec.color=fx.color;spec.seed=fx.seed;spec.timeMs=elapsedMs;out.push_back(spec);
    }
    return out;
}

void appendParticleEmitter(UiDrawList&list,const core::UiWidgetSettings&w,const Visual&v,qint64 elapsedMs,const QImage& texture)
{
    if(!v.visible||!w.particleEnabled||v.opacity<=.001)return;
    const int life=qBound(16,w.particleLifetimeMs,60000);const quint32 seed=stableHash(w.id);
    const int maxCount=qBound(1,w.particleMaxCount,2048);const qreal now=qMax<qint64>(0,elapsedMs);
    const auto emitParticle=[&](quint32 birthIndex,qreal ageMs){
        if(ageMs<0||ageMs>life)return;const qreal t=qBound<qreal>(0,ageMs/life,1);
        const qreal dir=w.particleDirectionDegrees+(random01(seed,birthIndex,1)-.5)*w.particleSpreadDegrees;
        const qreal speed=w.particleSpeedMin+(w.particleSpeedMax-w.particleSpeedMin)*random01(seed,birthIndex,2);
        const qreal radians=qDegreesToRadians(dir);const qreal sec=ageMs/1000.0;
        QPointF start=v.rect.center();
        // Área de emissão configurável: 100% = toda a largura/altura do widget; >100% permite espalhar além dele.
        start.rx()+=(random01(seed,birthIndex,3)-.5)*v.rect.width()*qBound<qreal>(0,w.particleEmissionWidth,4);
        start.ry()+=(random01(seed,birthIndex,4)-.5)*v.rect.height()*qBound<qreal>(0,w.particleEmissionHeight,4);
        QPointF pos=start+QPointF(std::cos(radians)*speed*sec+.5*w.particleGravityX*sec*sec,
                                 std::sin(radians)*speed*sec+.5*w.particleGravityY*sec*sec);
        const qreal size=qMax<qreal>(.1,w.particleStartSize+(w.particleEndSize-w.particleStartSize)*t);
        qreal fadeEnvelope=1.0;
        if(w.particleFadeInMs>0) fadeEnvelope=qMin(fadeEnvelope,qBound<qreal>(0,ageMs/qreal(w.particleFadeInMs),1));
        if(w.particleFadeOutMs>0) fadeEnvelope=qMin(fadeEnvelope,qBound<qreal>(0,(life-ageMs)/qreal(w.particleFadeOutMs),1));
        const qreal opacity=qBound<qreal>(0,(w.particleStartOpacity+(w.particleEndOpacity-w.particleStartOpacity)*t)*fadeEnvelope*v.opacity,1);
        QColor color=lerpColor(w.particleStartColor,w.particleEndColor,t);color=multiply(color,v.tint);
        const qreal spin=w.particleSpinMin+(w.particleSpinMax-w.particleSpinMin)*random01(seed,birthIndex,5);
        const qreal initialRot=random01(seed,birthIndex,6)*360.0;
        list.addParticle(pos,QSizeF(size,size),color,opacity,initialRot+spin*sec,w.particleShape,texture);
    };
    if(w.particleBurst){
        const int count=qMin(maxCount,qBound(1,w.particleBurstCount,2048));
        qreal age=now;if(w.particleLoop)age=std::fmod(now,qreal(life));if(!w.particleLoop&&age>life)return;
        for(int i=0;i<count;++i)emitParticle(quint32(i),age);
        return;
    }
    const qreal rate=qBound<qreal>(0,w.particleSpawnRate,500);if(rate<=.001)return;const qreal interval=1000.0/rate;
    const qint64 latest=qFloor(now/interval);
    int emitted=0;
    for(qint64 birth=latest;birth>=0&&emitted<maxCount;--birth){
        if(!w.particleLoop&&birth>=maxCount)continue;
        const qreal age=now-birth*interval;if(age>life)break;emitParticle(quint32(birth),age);++emitted;
    }
}

QImage maskImageFor(const core::Editor& editor,const core::UiWidgetSettings& w)
{
    return loadProjectImage(editor,w.maskImagePath,w.maskImage);
}

} // namespace

void UiWidgetRenderer::appendScreen(UiDrawList& list,const UiTheme&theme,const core::Editor&editor,const GameState&state,
                                    const QString&screen,const QSize&size,const QFont&font,const QString&transitionTrigger,
                                    qreal transitionProgress,const StateFn&stateFn,const ClipFn&clipFn,const ClipTimeFn&clipTimeFn,
                                    const VisibilityFn&visibilityFn,qint64 elapsedMs,const WidgetFn&widgetFn)
{
    // LUDO 3.19 — cache da árvore/layout ordenado. A assinatura é barata e
    // invalida automaticamente quando screen, parentId ou zOrder mudam.
    quint64 layoutSignature=quint64(qHash(screen))^quint64(theme.widgets.size()*0x9e3779b9u);
    for(auto it=theme.widgets.cbegin();it!=theme.widgets.cend();++it){
        if(it.value().screen!=screen)continue;const auto meta=theme.layoutElements.value(it.key());
        quint64 part=quint64(qHash(it.key()));part^=quint64(qHash(meta.parentId))<<1;part^=quint64(qHash(meta.zOrder))<<2;
        layoutSignature^=part+0x9e3779b97f4a7c15ULL+(layoutSignature<<6)+(layoutSignature>>2);
    }
    struct LayoutCacheEntry { quint64 signature=0; QStringList ids; QHash<QString,QStringList> children; QStringList roots; };
    static thread_local QHash<QString,LayoutCacheEntry> layoutCache;
    LayoutCacheEntry& cached=layoutCache[screen];
    if(cached.signature!=layoutSignature){
        cached=LayoutCacheEntry{};cached.signature=layoutSignature;
        for(auto it=theme.widgets.cbegin();it!=theme.widgets.cend();++it)if(it.value().screen==screen)cached.ids.push_back(it.key());
        auto byZ=[&](const QString&a,const QString&b){const int za=theme.layoutElements.value(a).zOrder,zb=theme.layoutElements.value(b).zOrder;return za==zb?a<b:za<zb;};
        std::stable_sort(cached.ids.begin(),cached.ids.end(),byZ);
        for(const QString&id:cached.ids){const QString parent=theme.layoutElements.value(id).parentId;if(theme.widgets.contains(parent)&&theme.widgets.value(parent).screen==screen)cached.children[parent].push_back(id);else cached.roots.push_back(id);}
        for(auto it=cached.children.begin();it!=cached.children.end();++it)std::stable_sort(it.value().begin(),it.value().end(),byZ);
        std::stable_sort(cached.roots.begin(),cached.roots.end(),byZ);
    }
    const QStringList& ids=cached.ids;const QHash<QString,QStringList>& children=cached.children;const QStringList& roots=cached.roots;

    QSet<QString> visiting,drawn;
    std::function<void(const QString&,bool,const QPointF&)> renderNode;
    renderNode=[&](const QString&id,bool parentVisible,const QPointF& inheritedOffset){
        if(drawn.contains(id)||visiting.contains(id)||!theme.widgets.contains(id))return;
        visiting.insert(id);
        const core::UiWidgetSettings baseWidget=theme.widgets.value(id);
        const core::UiWidgetSettings w=widgetFn?widgetFn(id,baseWidget):baseWidget;
        const auto meta=theme.layoutElements.value(id);const auto data=UiDataBindingResolver::resolveRuntime(meta,editor,state);
        const QString st=stateFn?stateFn(id):QStringLiteral("normal");const QString clip=clipFn?clipFn(id):QString();const int clipTime=clipTimeFn?clipTimeFn(id):0;
        Visual v=visualFor(w,meta,data,size,transitionTrigger,transitionProgress,st,clip,clipTime);v.rect.translate(inheritedOffset);if(visibilityFn)v.visible=visibilityFn(id,v.visible);v.visible=v.visible&&parentVisible;

        const QPointF transformPivot(v.rect.left()+meta.pivot.x()*v.rect.width(),v.rect.top()+meta.pivot.y()*v.rect.height());
        const bool hasTransform=!qFuzzyIsNull(w.rotationDegrees)||!qFuzzyCompare(w.transformScaleX,1.0)||!qFuzzyCompare(w.transformScaleY,1.0)||!qFuzzyIsNull(w.skewXDegrees)||!qFuzzyIsNull(w.skewYDegrees);
        if(hasTransform)list.pushTransform(transformPivot,w.rotationDegrees,w.transformScaleX,w.transformScaleY,w.skewXDegrees,w.skewYDegrees);

        const QString maskShape=w.maskShape.trimmed().toLower();const bool selfMask=v.visible&&maskShape!=QLatin1String("none")&&!maskShape.isEmpty();
        if(selfMask)list.pushClip(v.rect,maskShape,w.maskRadius,maskImageFor(editor,w));

        const QVector<UiEffectSpec> effects=effectSpecs(w,elapsedMs);const bool hasEffect=!effects.isEmpty()||w.blendMode!=QLatin1String("normal");
        const QRectF effectBounds=(w.materialAffectsChildren||w.type==QLatin1String("particle-emitter"))?QRectF(0,0,size.width(),size.height()):v.rect;
        if(hasEffect)list.pushEffect(effectBounds,effects,w.blendMode);
        if(w.type==QLatin1String("particle-emitter"))appendParticleEmitter(list,w,v,elapsedMs,resolveImage(editor,w,data.hasImage?data.imagePath:QString()));else drawWidget(list,theme,editor,state,w,data,v,font);
        if(hasEffect&&!w.materialAffectsChildren)list.popEffect();

        const bool scrollContainer=w.type==QLatin1String("scroll-area");
        const bool childClip=v.visible&&(w.clipChildren||scrollContainer)&&!selfMask;
        if(childClip)list.pushClip(v.rect,QStringLiteral("rect"));
        QPointF childOffset=inheritedOffset;
        if(scrollContainer&&!children.value(id).isEmpty()){
            qreal contentBottom=v.rect.bottom();
            qreal contentRight=v.rect.right();
            for(const QString& child:children.value(id)){
                const core::UiWidgetSettings childBase=theme.widgets.value(child);
                const core::UiWidgetSettings childWidget=widgetFn?widgetFn(child,childBase):childBase;
                const QRectF cr(childWidget.rect.x()*size.width()+inheritedOffset.x(),childWidget.rect.y()*size.height()+inheritedOffset.y(),
                                childWidget.rect.width()*size.width(),childWidget.rect.height()*size.height());
                contentBottom=qMax(contentBottom,cr.bottom());contentRight=qMax(contentRight,cr.right());
            }
            const qreal p=progressValue(w,data);
            if(w.orientation==QLatin1String("horizontal"))childOffset.rx()-=qMax<qreal>(0,contentRight-v.rect.right())*p;
            else childOffset.ry()-=qMax<qreal>(0,contentBottom-v.rect.bottom())*p;
        }
        for(const QString&child:children.value(id))renderNode(child,v.visible,childOffset);
        if(childClip)list.popClip();
        if(hasEffect&&w.materialAffectsChildren)list.popEffect();
        if(selfMask)list.popClip();
        if(hasTransform)list.popTransform();
        visiting.remove(id);drawn.insert(id);
    };
    for(const QString&id:roots)renderNode(id,true,QPointF());
    // Ciclos de parentId corrompidos não devem derrubar o frame: renderiza cada
    // nó ainda não visitado como raiz e o conjunto visiting impede recursão infinita.
    for(const QString&id:ids)if(!drawn.contains(id))renderNode(id,true,QPointF());
}

} // namespace game::ui
