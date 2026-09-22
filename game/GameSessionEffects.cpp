#include "GameSession.h"
#include "RuntimePanorama.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QPainter>
#include <algorithm>
#include <cmath>

using namespace core;

namespace game {


QPointF GameSession::cameraTargetPoint(const QString& requested,const QVariantMap& params) const
{
    QString target=requested;if(target==QLatin1String("self"))target=QStringLiteral("event:")+params.value(QStringLiteral("_eventId")).toString();
    const MapInfo& info=ed.mapInfo();
    if(target==QLatin1String("player"))return m_world.playerVisualPixel()+QPointF(info.tileWidth/2.0,info.tileHeight/2.0);
    if(target.startsWith(QLatin1String("event:"))){if(const MapEvent* e=ed.findEvent(target.mid(6))){World::EventActorView v;if(m_world.eventView(*e,&v))return v.pixel+QPointF(info.tileWidth/2.0,info.tileHeight/2.0);}}
    const QString id=params.value(QStringLiteral("eventId")).toString();if(!id.isEmpty())if(const MapEvent* e=ed.findEvent(id)){World::EventActorView v;if(m_world.eventView(*e,&v))return v.pixel+QPointF(info.tileWidth/2.0,info.tileHeight/2.0);}
    if(params.value(QStringLiteral("unit"),QStringLiteral("cell")).toString()==QLatin1String("pixel"))return QPointF(params.value(QStringLiteral("x")).toDouble(),params.value(QStringLiteral("y")).toDouble());
    return QPointF((params.value(QStringLiteral("x")).toDouble()+.5)*info.tileWidth,(params.value(QStringLiteral("y")).toDouble()+.5)*info.tileHeight);
}

void GameSession::startEntryFade(int frames)
{
    const int safeFrames = qBound(1, frames, 600);
    m_screenEffects.startFade(QColor(0, 0, 0), 255, 0, safeFrames / 60.0);
    m_overlayDirty = true;
}

bool GameSession::runLudoCommand(const EventCommand& command,bool start)
{
    const QString& type=command.type;const QVariantMap& p=command.params;
    if(!start){
        if(type.startsWith(QLatin1String("ludo.camera.")))return m_camera.moving;
        if(type==QLatin1String("ludo.screen.tone")||type==QLatin1String("ludo.screen.clearTone"))return m_screenTone.isTransitioning();
        if(type.startsWith(QLatin1String("ludo.filter.")))return m_filters.isTransitioning();
        if(type==QLatin1String("ludo.screen.flash"))return m_screenEffects.flashActive();
        if(type==QLatin1String("ludo.screen.fade"))return m_screenEffects.fadeActive();
        if(type==QLatin1String("ludo.screen.shake"))return m_screenEffects.shakeActive();
        if(type.startsWith(QLatin1String("ludo.sprite."))){QString target=p.value("target","self").toString();if(target=="self")target="event:"+p.value("_eventId").toString();const SpriteFx fx=m_spriteFx.value(target);if(type==QLatin1String("ludo.sprite.fade"))return fx.active;if(type==QLatin1String("ludo.sprite.shake"))return fx.shakeRemaining>0;return fx.transformActive;}
        return false;
    }
    const bool cameraCombined=type==QLatin1String("ludo.camera.move");
    const bool cameraZoomOnly=type==QLatin1String("ludo.camera.zoomOnly");
    const bool cameraMoveOnly=type==QLatin1String("ludo.camera.moveOnly");
    if(cameraCombined||cameraZoomOnly||cameraMoveOnly||type==QLatin1String("ludo.camera.reset")||type==QLatin1String("ludo.camera.restore")){
        if(type==QLatin1String("ludo.camera.restore")&&!m_camera.saved)return false;
        const MapInfo& info=ed.mapInfo();const QPointF player=m_world.playerVisualPixel()+QPointF(info.tileWidth/2.0,info.tileHeight/2.0);
        const QPointF current=m_camera.active?m_camera.center:player;if(!cameraZoomOnly)m_camera.active=true;m_camera.moving=true;m_camera.center=current;m_camera.startCenter=current;m_camera.tweenCenter=!cameraZoomOnly;m_camera.tweenZoom=!cameraMoveOnly;
        if(type==QLatin1String("ludo.camera.restore")){m_camera.endCenter=m_camera.savedCenter;m_camera.endZoom=m_camera.savedZoom;m_camera.follow=false;m_camera.target.clear();}
        else if(cameraZoomOnly){m_camera.endCenter=current;m_camera.endZoom=qBound(.25,p.value(QStringLiteral("zoom"),m_zoom).toDouble(),8.0);}
        else{const QString target=type==QLatin1String("ludo.camera.reset")?QStringLiteral("player"):p.value(QStringLiteral("target"),QStringLiteral("player")).toString();m_camera.params=p;m_camera.target=target;m_camera.endCenter=cameraTargetPoint(target,p);m_camera.endZoom=type==QLatin1String("ludo.camera.reset")?2.0:(cameraMoveOnly?m_zoom:qBound(.25,p.value(QStringLiteral("zoom"),m_zoom).toDouble(),8.0));m_camera.follow=p.value(QStringLiteral("follow"),target==QLatin1String("player")).toBool();m_camera.followSpeed=qBound(.1,p.value(QStringLiteral("followSpeed"),6.0).toDouble(),30.0);m_camera.deadzone=qMax(0.0,p.value(QStringLiteral("deadzone"),0.0).toDouble());}
        m_camera.startZoom=m_zoom;m_camera.elapsed=0;m_camera.duration=p.value(QStringLiteral("disableSmoothing"),false).toBool()?0.0:qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;m_camera.ease=p.value(QStringLiteral("ease"),QStringLiteral("smooth")).toString();if(m_camera.duration<=0){if(m_camera.tweenCenter)m_camera.center=m_camera.endCenter;if(m_camera.tweenZoom)m_zoom=m_camera.endZoom;m_camera.moving=false;}return m_camera.moving;
    }
    if(type==QLatin1String("ludo.screen.tone")||type==QLatin1String("ludo.screen.clearTone")){
        const bool clear=type==QLatin1String("ludo.screen.clearTone");
        const double duration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;
        if(clear) m_screenTone.clear(duration);
        else m_screenTone.start(p.value(QStringLiteral("red"),0).toInt(),
                                p.value(QStringLiteral("green"),0).toInt(),
                                p.value(QStringLiteral("blue"),0).toInt(),
                                p.value(QStringLiteral("gray"),0).toInt(), duration);
        m_overlayDirty=true;
        return p.value(QStringLiteral("wait"),false).toBool()&&m_screenTone.isTransitioning();
    }
    if(type.startsWith(QLatin1String("ludo.filter."))){
        const double duration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;
        const int slot=core::normalizedLudoFilterSlot(p);
        if(type==QLatin1String("ludo.filter.chromaticAberration"))m_filters.setChromaticAberration(core::ChromaticAberrationConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.noise"))m_filters.setNoise(core::NoiseFilterConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.scanlines"))m_filters.setScanlines(core::ScanlineFilterConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.vignette"))m_filters.setVignette(core::VignetteFilterConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.blur"))m_filters.setBlur(core::BlurFilterConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.tiltShift"))m_filters.setTiltShift(core::TiltShiftFilterConfig::fromVariantMap(p),duration,slot);
        else if(type==QLatin1String("ludo.filter.clear")){
            const QString filter=p.value(QStringLiteral("filter"),QStringLiteral("all")).toString();
            const int clearSlot=p.value(QStringLiteral("allSlots"),false).toBool()?0:slot;
            if(filter==QLatin1String("all"))m_filters.clearAll(duration,clearSlot);else m_filters.clearFilter(filter,duration,clearSlot);
        } else return false;
        m_overlayDirty=true;
        return p.value(QStringLiteral("wait"),false).toBool()&&m_filters.isTransitioning();
    }
    if(type==QLatin1String("ludo.screen.flash")){
        const QColor color(qBound(0,p.value(QStringLiteral("red"),255).toInt(),255),
                           qBound(0,p.value(QStringLiteral("green"),255).toInt(),255),
                           qBound(0,p.value(QStringLiteral("blue"),255).toInt(),255));
        const int alpha=qBound(0,p.value(QStringLiteral("alpha"),180).toInt(),255);
        const double duration=qMax(0,p.value(QStringLiteral("duration"),20).toInt())/60.0;
        m_screenEffects.startFlash(color,alpha,duration);
        m_overlayDirty=true;
        return p.value(QStringLiteral("wait"),false).toBool()&&m_screenEffects.flashActive();
    }
    if(type==QLatin1String("ludo.screen.fade")){
        const QColor color(qBound(0,p.value(QStringLiteral("red"),0).toInt(),255),
                           qBound(0,p.value(QStringLiteral("green"),0).toInt(),255),
                           qBound(0,p.value(QStringLiteral("blue"),0).toInt(),255));
        const QString direction=p.value(QStringLiteral("direction"),QStringLiteral("out")).toString();
        const int target=qBound(0,p.value(QStringLiteral("alpha"),255).toInt(),255);
        const int from=direction==QLatin1String("in")?target:0;
        const int to=direction==QLatin1String("in")?0:target;
        const double duration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;
        m_screenEffects.startFade(color,from,to,duration);
        m_overlayDirty=true;
        return p.value(QStringLiteral("wait"),false).toBool()&&m_screenEffects.fadeActive();
    }
    if(type==QLatin1String("ludo.screen.shake")){
        const double duration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;
        m_screenEffects.startShake(qMax(0.0,p.value(QStringLiteral("x"),6.0).toDouble()),
                                   qMax(0.0,p.value(QStringLiteral("y"),3.0).toDouble()),
                                   duration,
                                   qBound(0.5,p.value(QStringLiteral("frequency"),12.0).toDouble(),60.0));
        m_overlayDirty=true;
        return p.value(QStringLiteral("wait"),false).toBool()&&m_screenEffects.shakeActive();
    }
    if(type==QLatin1String("ludo.screen.clearEffects")){
        m_screenEffects.clear();
        m_overlayDirty=true;
        return false;
    }
    if(type==QLatin1String("ludo.camera.save")){m_camera.saved=true;m_camera.savedCenter=m_camera.active?m_camera.center:cameraTargetPoint(QStringLiteral("player"),{});m_camera.savedZoom=m_zoom;return false;}
    if(type==QLatin1String("ludo.camera.release")){m_camera.active=false;m_camera.moving=false;m_camera.follow=false;m_zoom=2.0;return false;}
    if(type==QLatin1String("ludo.sprite.fade")){
        QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target==QLatin1String("self"))target=QStringLiteral("event:")+p.value(QStringLiteral("_eventId")).toString();
        SpriteFx& fx=m_spriteFx[target];const double current=fx.fadeEnabled?fx.opacity:1.0;fx.type=QStringLiteral("fade");fx.fadeEnabled=true;fx.opacity=fx.from=current;fx.to=qBound(0.0,p.value(QStringLiteral("opacity"),255).toDouble()/255.0,1.0);fx.elapsed=0;fx.duration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;fx.active=fx.duration>0;if(!fx.active)fx.opacity=fx.to;return p.value(QStringLiteral("wait"),false).toBool()&&fx.active;
    }
    if(type==QLatin1String("ludo.sprite.phantom")){
        QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target==QLatin1String("self"))target=QStringLiteral("event:")+p.value(QStringLiteral("_eventId")).toString();SpriteFx& fx=m_spriteFx[target];fx.type=QStringLiteral("phantom");fx.phantomEnabled=true;fx.mode=p.value(QStringLiteral("mode"),QStringLiteral("visibleNear")).toString();fx.nearDistance=qMax(0.0,p.value(QStringLiteral("near"),1.0).toDouble());fx.distance=qMax(fx.nearDistance+.01,p.value(QStringLiteral("distance"),6.0).toDouble());fx.minimum=qBound(0.0,p.value(QStringLiteral("minimum"),32).toDouble()/255.0,1.0);fx.maximum=qBound(fx.minimum,p.value(QStringLiteral("maximum"),255).toDouble()/255.0,1.0);double sm=p.value(QStringLiteral("smoothness"),100).toDouble();if(sm>1.0)sm/=100.0;fx.smoothness=qBound(0.0,sm,1.0);return false;
    }
    if(type==QLatin1String("ludo.sprite.offset")||type==QLatin1String("ludo.sprite.clearOffset")||
       type==QLatin1String("ludo.sprite.zoom")||type==QLatin1String("ludo.sprite.resetZoom")){
        QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target==QLatin1String("self"))target=QStringLiteral("event:")+p.value(QStringLiteral("_eventId")).toString();SpriteFx& fx=m_spriteFx[target];fx.offsetFrom=fx.offset;fx.scaleFrom=fx.scale;
        fx.offsetTo=type==QLatin1String("ludo.sprite.clearOffset")?QPointF():type==QLatin1String("ludo.sprite.offset")?QPointF(p.value(QStringLiteral("x"),0).toDouble(),p.value(QStringLiteral("y"),0).toDouble()):fx.offset;
        fx.scaleTo=type==QLatin1String("ludo.sprite.resetZoom")?QPointF(1,1):type==QLatin1String("ludo.sprite.zoom")?QPointF(qBound(.01,p.value(QStringLiteral("scaleX"),1.0).toDouble(),16.0),qBound(.01,p.value(QStringLiteral("scaleY"),1.0).toDouble(),16.0)):fx.scale;
        fx.transformElapsed=0;fx.transformDuration=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;fx.transformActive=fx.transformDuration>0;if(!fx.transformActive){fx.offset=fx.offsetTo;fx.scale=fx.scaleTo;}return p.value(QStringLiteral("wait"),false).toBool()&&fx.transformActive;
    }
    if(type==QLatin1String("ludo.sprite.shake")){
        QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target==QLatin1String("self"))target=QStringLiteral("event:")+p.value(QStringLiteral("_eventId")).toString();SpriteFx& fx=m_spriteFx[target];fx.shakeX=qMax(0.0,p.value(QStringLiteral("x"),4.0).toDouble());fx.shakeY=qMax(0.0,p.value(QStringLiteral("y"),0.0).toDouble());fx.shakeFrequency=qBound(.5,p.value(QStringLiteral("frequency"),12.0).toDouble(),60.0);fx.shakeRemaining=qMax(0,p.value(QStringLiteral("duration"),30).toInt())/60.0;fx.shakePhase=0;return p.value(QStringLiteral("wait"),false).toBool()&&fx.shakeRemaining>0;
    }
    if(type==QLatin1String("ludo.sprite.clear")){QString target=p.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target=="self")target="event:"+p.value("_eventId").toString();m_spriteFx.remove(target);return false;}
    // Compatibilidade: projetos antigos continuam abrindo, mas estes comandos
    // não criam mais filtros nem passes extras de renderização.
    if(type==QLatin1String("ludo.screen.set")||type==QLatin1String("ludo.screen.clear"))return false;
    if(type==QLatin1String("ludo.cutscene.enable")){m_cutsceneOverride=true;return false;}
    if(type==QLatin1String("ludo.cutscene.disable")){m_cutsceneOverride=false;return false;}
    return false;
}

void GameSession::updateOverlayDirty(double dt)
{
    const Interpreter* visible = visibleInterpreter();
    bool messageDynamic=false;
    bool choiceDynamic=false;
    if(visible&&visible->message().visible){
        const MessageView&message=visible->message();
        messageDynamic=message.drawableRevealed<message.page.drawableCount() || message.effects.enabled() || message.exiting;
        if(!messageDynamic)for(const TextLine&line:message.page.lines)for(const TypedChar&c:line.chars)if(c.fx.kind!=TextFx::None){messageDynamic=true;break;}
    }
    if(visible&&visible->choice().visible){
        const ChoiceView& choice=visible->choice();
        choiceDynamic=choice.effects.enabled() || choice.exiting;
        if(!choiceDynamic)for(const TextPage&page:choice.richPages)for(const TextLine&line:page.lines)for(const TypedChar&c:line.chars)if(c.fx.kind!=TextFx::None){choiceDynamic=true;break;}
    }
    const bool dynamic = messageDynamic || choiceDynamic ||
                         m_uiModal.active() || m_uiMenu.active() || m_uiBattle.active() || m_uiShop.active() ||
                         (!m_gpuOverlayCompositing&&m_subs.busy()) || m_skipHeld ||
                         (!m_gpuOverlayCompositing&&m_skipFade > 0.0);
    if (!dynamic && !m_showHud && m_hint.isEmpty()) return;
    m_overlayRefreshAcc += dt;
    const double interval = dynamic ? 1.0 / 60.0 : .20;
    if (m_overlayRefreshAcc >= interval) {
        m_overlayRefreshAcc = 0;
        m_overlayDirty = true;
    }
}

void GameSession::syncWeatherFromCurrentMap()
{
    if (m_weather.runtimeOverride) return;
    const MapDoc* d = ed.doc();
    if (!d) return;

    const WeatherState& wanted = d->environment.weather;
    if (m_weather.sameConfig(wanted)) return;

    m_weather.applyConfig(wanted, false, false);
    m_lastThunderCycle = -1;
    m_overlayDirty = true;
}

void GameSession::updateLudo(double dt)
{
    syncWeatherFromCurrentMap();
    if(const MapDoc* d=ed.doc()){
        const MapEnvironment& env=d->environment;
        const int count=!env.panoramas.isEmpty()?env.panoramas.size():(env.panorama.isNull()?0:1);
        if(m_panoramaOffsets.size()!=count)m_panoramaOffsets.resize(count);
        for(int i=0;i<count;++i){
            // "Fixar na tela" define o espaço de coordenadas, não congela o
            // auto-scroll. Uma camada fixa ainda pode rolar horizontal/vertical
            // por Loop + Velocidade; a diferença é que esse deslocamento passa
            // a ser medido em pixels lógicos de Screen Space.
            const double sx=!env.panoramas.isEmpty()?(env.panoramas[i].loopX?env.panoramas[i].speedX:0.0):(env.panoramaLoopX?env.panoramaSpeedX:0.0);
            const double sy=!env.panoramas.isEmpty()?(env.panoramas[i].loopY?env.panoramas[i].speedY:0.0):(env.panoramaLoopY?env.panoramaSpeedY:0.0);
            m_panoramaOffsets[i]+=QPointF(sx*dt,sy*dt);
        }
        m_panoramaAge+=qMax(0.0,dt);
    }
    if (m_weather.active()) {
        m_weather.elapsed += qMax(0.0, dt);
        updateStormThunder();
    } else {
        m_lastThunderCycle = -1;
    }
    if(m_screenTone.update(dt)&&!m_gpuOverlayCompositing)m_overlayDirty=true;
    m_filters.update(dt);
    const bool hadScreenOverlay=m_screenEffects.flashActive()||m_screenEffects.fadeActive();
    const bool screenEffectsChanged=m_screenEffects.update(dt,m_reduceFlash,m_reduceShake);
    // Shake só muda a projeção World; não force re-rasterizar a textura de UI
    // do QRhi a cada frame. Flash/Fade, sim, vivem no overlay.
    if(!m_gpuOverlayCompositing&&screenEffectsChanged&&(hadScreenOverlay||m_screenEffects.flashActive()||m_screenEffects.fadeActive()))
        m_overlayDirty=true;
    if(m_camera.moving){m_camera.elapsed+=dt;double t=m_camera.duration<=0?1:qBound(0.0,m_camera.elapsed/m_camera.duration,1.0);if(m_camera.ease=="smooth")t=t*t*(3-2*t);else if(m_camera.ease=="easeIn")t=t*t;else if(m_camera.ease=="easeOut")t=1-(1-t)*(1-t);else if(m_camera.ease=="slide")t=t<.5?2*t*t:1-std::pow(-2*t+2,2)/2;if(m_camera.tweenCenter)m_camera.center=m_camera.startCenter+(m_camera.endCenter-m_camera.startCenter)*t;if(m_camera.tweenZoom)m_zoom=m_camera.startZoom+(m_camera.endZoom-m_camera.startZoom)*t;if(t>=1)m_camera.moving=false;}
    // Apenas Zoom não congela uma câmera que já estava seguindo um alvo.
    if(m_camera.active&&m_camera.follow&&(!m_camera.moving||!m_camera.tweenCenter)){const QPointF desired=cameraTargetPoint(m_camera.target,m_camera.params);QPointF delta=desired-m_camera.center;const double len=std::hypot(delta.x(),delta.y());if(len>m_camera.deadzone){if(m_camera.deadzone>0)delta*=((len-m_camera.deadzone)/len);if(m_camera.params.value(QStringLiteral("disableSmoothing"),false).toBool())m_camera.center+=delta;else{const double k=1.0-std::exp(-m_camera.followSpeed*dt);m_camera.center+=delta*k;}}}
    for(auto it=m_spriteFx.begin();it!=m_spriteFx.end();++it){SpriteFx& fx=it.value();if(fx.active){fx.elapsed+=dt;const double t=fx.duration<=0?1:qBound(0.0,fx.elapsed/fx.duration,1.0);fx.opacity=fx.from+(fx.to-fx.from)*t;if(t>=1)fx.active=false;}if(fx.transformActive){fx.transformElapsed+=dt;double t=fx.transformDuration<=0?1:qBound(0.0,fx.transformElapsed/fx.transformDuration,1.0);t=t*t*(3.0-2.0*t);fx.offset=fx.offsetFrom+(fx.offsetTo-fx.offsetFrom)*t;fx.scale=fx.scaleFrom+(fx.scaleTo-fx.scaleFrom)*t;if(t>=1)fx.transformActive=false;}if(fx.shakeRemaining>0){fx.shakeRemaining=qMax(0.0,fx.shakeRemaining-dt);fx.shakePhase+=dt*fx.shakeFrequency*6.283185307179586;}}
    const bool canSkip=canSkipCutscene();
    if(canSkip&&m_skipHeld){m_skipHeldSec+=dt;if(ed.cutsceneSkip.holdToSkip&&m_skipHeldSec>=ed.cutsceneSkip.holdMs/1000.0)m_skipTriggered=true;}
    if(m_skipTriggered&&canSkip){
        if(Interpreter* target=activeCutsceneInterpreter()){
            finishCutsceneTransientState();
            if(target->skipCutscene())m_skipFade=1.0;
        }
        m_skipTriggered=false;m_skipHeld=false;m_skipHeldSec=0;
    }
    if(!canSkip){m_skipHeldSec=0;m_skipTriggered=false;}
    if(m_skipFade>0){const double sec=qMax(1,ed.cutsceneSkip.fadeFrames)/60.0;m_skipFade=qMax(0.0,m_skipFade-dt/sec);}
}

void GameSession::updateStormThunder()
{
    if (m_weather.kind != RuntimeWeatherKind::Storm || !m_weather.active() ||
        m_weather.thunderSePath.isEmpty()) {
        m_lastThunderCycle = -1;
        return;
    }
    // Clarão e SE usam a mesma fase do RuntimeWeatherState.
    const qint64 cycle = qint64(std::floor(m_weather.elapsed / 6.5));
    if (cycle == m_lastThunderCycle) return;
    m_lastThunderCycle = cycle;
    const QString path = QFileInfo(m_weather.thunderSePath).isAbsolute()
        ? m_weather.thunderSePath : QDir(ed.projectRoot()).filePath(m_weather.thunderSePath);
    if (m_channelAudio)
        m_channelAudio(QStringLiteral("se"), path, m_weather.thunderVolume, false, 0, 0,100,0);
    else if (m_audio)
        m_audio(path, m_weather.thunderVolume);
}

void GameSession::updateMapTransition(double dt)
{
    if(!m_mapTransition.active)return;
    m_mapTransition.elapsed+=qMax(0.0,dt);
    const double half=qMax(0.001,m_mapTransition.halfDuration);
    if(!m_mapTransition.transferred&&m_mapTransition.elapsed>=half){
        const QVariantMap& p=m_mapTransition.command.params;QString error;
        const int rawDirection=qBound(0,p.value(QStringLiteral("direction"),0).toInt(),int(Dir::UpRight));
        const bool ok=transferToMap(p.value(QStringLiteral("mapId")).toString(),QPoint(p.value(QStringLiteral("x")).toInt(),p.value(QStringLiteral("y")).toInt()),p.value(QStringLiteral("useSpawn"),false).toBool(),static_cast<Dir>(rawDirection),&error);
        m_mapTransition.transferred=true;
        if(!ok){m_hint=error;m_hintUntil=m_clock.elapsed()+3500;m_mapTransition.active=false;m_mapTransition.opacity=0;}
    }
    if(!m_mapTransition.active)return;
    m_mapTransition.opacity=m_mapTransition.elapsed<=half?m_mapTransition.elapsed/half:2.0-m_mapTransition.elapsed/half;
    m_mapTransition.opacity=qBound(0.0,m_mapTransition.opacity,1.0);
    if(m_mapTransition.elapsed>=half*2.0){m_mapTransition.active=false;m_mapTransition.opacity=0;}
    if(!m_gpuOverlayCompositing)m_overlayDirty=true;
}

void GameSession::drawMapTransition(QPainter& p) const
{
    if(m_mapTransition.opacity<=0)return;
    p.save();p.setCompositionMode(QPainter::CompositionMode_SourceOver);p.fillRect(QRect(0,0,m_viewW,m_viewH),QColor(0,0,0,qRound(255*m_mapTransition.opacity)));p.restore();
}

void GameSession::drawScreenEffects(QPainter& p) const
{
    const RuntimeScreenOverlay overlay=m_screenEffects.overlay(m_reduceFlash);
    if(!overlay.active())return;
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const QRect rect(0,0,m_viewW,m_viewH);
    // Flash primeiro, Fade depois: um fade opaco continua soberano mesmo se
    // um clarão for disparado no mesmo frame. UI é desenhada depois.
    if(overlay.flashColor.alpha()>0)p.fillRect(rect,overlay.flashColor);
    if(overlay.fadeColor.alpha()>0)p.fillRect(rect,overlay.fadeColor);
    p.restore();
}

double GameSession::spriteOpacity(const QString& target,const QPointF& pixel) const
{
    const auto it=m_spriteFx.constFind(target);if(it==m_spriteFx.constEnd())return 1.0;const SpriteFx& fx=it.value();double result=fx.fadeEnabled?fx.opacity:1.0;if(fx.phantomEnabled){const QPointF p=m_world.playerVisualPixel();const double tile=qMax(1.0,double(qMax(ed.mapInfo().tileWidth,ed.mapInfo().tileHeight)));const double dist=std::hypot(pixel.x()-p.x(),pixel.y()-p.y())/tile;double t=qBound(0.0,(dist-fx.nearDistance)/qMax(.01,fx.distance-fx.nearDistance),1.0);const double smooth=t*t*(3.0-2.0*t);t=t+(smooth-t)*fx.smoothness;const double visible=fx.mode==QLatin1String("visibleFar")?t:1-t;result*=fx.minimum+(fx.maximum-fx.minimum)*visible;}return result;
}

QPointF GameSession::spriteOffset(const QString& target) const
{
    const auto it=m_spriteFx.constFind(target);if(it==m_spriteFx.constEnd())return {};
    QPointF value=it->offset;if(it->shakeRemaining>0&&!m_reduceShake)value+=QPointF(std::sin(it->shakePhase)*it->shakeX,std::sin(it->shakePhase*1.173)*it->shakeY);return value;
}

QPointF GameSession::eventPageVisualOffset(const core::MapEvent& event) const
{
    const int pageIndex = m_state.choosePage(event);
    if (pageIndex < 0 || pageIndex >= event.pages.size()) return {};
    const int halfTiles = qBound(-1, event.pages.at(pageIndex).halfTileVerticalOffset, 1);
    return QPointF(0.0, halfTiles * ed.mapInfo().tileHeight * 0.5);
}

QPointF GameSession::spriteScale(const QString& target) const
{
    const auto it=m_spriteFx.constFind(target);return it==m_spriteFx.constEnd()?QPointF(1,1):it->scale;
}

QRectF GameSession::transformedSpriteRect(const QString& target,const QRectF& rect) const
{
    const QPointF scale=spriteScale(target),offset=spriteOffset(target);const QPointF pivot(rect.center().x(),rect.bottom());
    const QSizeF size(rect.width()*scale.x(),rect.height()*scale.y());return QRectF(pivot.x()-size.width()/2.0+offset.x(),pivot.y()-size.height()+offset.y(),size.width(),size.height());
}

void GameSession::drawCutsceneSkip(QPainter& p)
{
    if(!m_gpuOverlayCompositing&&m_skipFade>0){p.fillRect(QRect(0,0,m_viewW,m_viewH),QColor(0,0,0,int(255*m_skipFade)));}
    if(!m_cutsceneSkipPromptVisible||!canSkipCutscene())return;
    const QString key=ed.inputMap.describe(cutsceneSkipAction());QString text=core::resolvePlayerText(ed.localization,ed.cutsceneSkip.labelTextKey,ed.cutsceneSkip.label);text.replace(QStringLiteral("{key}"),key);
    QFont f=m_font;f.setPixelSize(14);f.setBold(true);p.setFont(f);const int w=p.fontMetrics().horizontalAdvance(text)+34,h=42,m=18;int x=m,y=m;if(ed.cutsceneSkip.corner==1||ed.cutsceneSkip.corner==3)x=m_viewW-w-m;if(ed.cutsceneSkip.corner>=2)y=m_viewH-h-m;const QRect r(x,y,w,h);p.setPen(Qt::NoPen);p.setBrush(ed.cutsceneSkip.panelColor);p.drawRoundedRect(r,10,10);p.setPen(Qt::white);p.drawText(r.adjusted(14,0,-10,-8),Qt::AlignVCenter,text);const double needed=ed.cutsceneSkip.holdToSkip?qMax(.1,ed.cutsceneSkip.holdMs/1000.0):.1;const double progress=ed.cutsceneSkip.holdToSkip?qBound(0.0,m_skipHeldSec/needed,1.0):(m_skipHeld?1:0);p.setBrush(QColor(255,255,255,35));p.drawRoundedRect(QRectF(r.left()+12,r.bottom()-10,r.width()-24,4),2,2);p.setBrush(ed.cutsceneSkip.accentColor);p.drawRoundedRect(QRectF(r.left()+12,r.bottom()-10,(r.width()-24)*progress,4),2,2);
}

namespace {
QVector<PanoramaDef> effectivePanoramas(const MapEnvironment& env)
{
    if (!env.panoramas.isEmpty()) return env.panoramas;
    QVector<PanoramaDef> out;
    if (!env.panorama.isNull()) {
        PanoramaDef d;
        d.name=QObject::tr("Panorama 1");d.sourcePath=env.panoramaPath;d.image=env.panorama;
        d.loopX=env.panoramaLoopX;d.loopY=env.panoramaLoopY;d.speedX=env.panoramaSpeedX;d.speedY=env.panoramaSpeedY;
        d.fixed=env.panoramaFixed;d.showInEditor=env.panoramaInEditor;
        out.push_back(d);
    }
    return out;
}

QPainter::CompositionMode panoramaComposition(PictureBlend b)
{
    switch(b){
    case PictureBlend::Add:return QPainter::CompositionMode_Plus;
    case PictureBlend::Multiply:return QPainter::CompositionMode_Multiply;
    case PictureBlend::Screen:return QPainter::CompositionMode_Screen;
    default:return QPainter::CompositionMode_SourceOver;
    }
}

LivePicture panoramaLive(const PanoramaDef& pano,int index,double age)
{
    LivePicture lp;lp.age=age;lp.def.number=10000+index;lp.def.opacity=pano.opacity;lp.def.blend=pano.blend;
    lp.def.flipH=pano.flipH;lp.def.flipV=pano.flipV;
    lp.def.setFrameSequence(pano.frameSequence());lp.def.fx=pano.fx;
    return lp;
}
}

void GameSession::drawPanorama(QPainter& p,const QPointF&,bool fixedPass, const RuntimeRenderState& state)
{
    const MapDoc*d=ed.doc();if(!d)return;
    const QVector<PanoramaDef> layers=effectivePanoramas(d->environment);
    for(int i=0;i<layers.size();++i){
        const PanoramaDef& layer=layers[i];
        if(!layer.enabled||layer.image.isNull()||layer.fixed!=fixedPass||layer.opacity<=0)continue;
        LivePicture lp=panoramaLive(layer,i,m_panoramaAge);
        const PictureFrame frame=m_picFx.buildImage(lp,ed,layer.image,QStringLiteral("panorama:%1:%2").arg(i).arg(layer.sourcePath));
        if(!frame.valid||frame.image.isNull()||frame.opacity<=0)continue;

        const QPointF off=i<m_panoramaOffsets.size()?m_panoramaOffsets[i]:QPointF();
        RuntimePanoramaInput input;
        input.imageSize=frame.image.size();input.scrollOffset=off;input.fixed=layer.fixed;
        input.parallaxX=layer.parallaxX;input.parallaxY=layer.parallaxY;
        const RuntimePanoramaLayout layout=buildRuntimePanoramaLayout(state,input);
        if(!layout.valid)continue;

        p.save();
        p.setTransform(layout.space==RuntimeCoordinateSpace::World?state.worldToScreenTransform():QTransform(),false);
        p.setClipRect(layout.visibleRect,Qt::ReplaceClip);
        p.setOpacity(qBound(0.0,frame.opacity/255.0,1.0));
        p.setCompositionMode(panoramaComposition(layer.blend));
        for(const QRectF& tile:layout.tiles)p.drawImage(tile,frame.image);
        p.restore();
    }
}

void GameSession::appendPanoramaQuads(SpriteBatcher& out,ImageProvider& prov)
{
    appendPanoramaQuads(out, prov, renderState());
}

void GameSession::appendPanoramaQuads(SpriteBatcher& out,ImageProvider& prov, const RuntimeRenderState& state)
{
    const MapInfo&info=ed.mapInfo();
    out.addSolid(QRectF(0,0,info.pixelWidth(),info.pixelHeight()),info.background);
    const MapDoc*d=ed.doc();if(!d)return;
    const QVector<PanoramaDef> layers=effectivePanoramas(d->environment);
    for(int i=0;i<layers.size();++i){
        const PanoramaDef& layer=layers[i];
        if(!layer.enabled||layer.image.isNull()||layer.opacity<=0)continue;
        LivePicture lp=panoramaLive(layer,i,m_panoramaAge);
        const PictureFrame frame=m_picFx.buildImage(lp,ed,layer.image,QStringLiteral("panorama:%1:%2").arg(i).arg(layer.sourcePath));
        if(!frame.valid||frame.image.isNull()||frame.opacity<=0)continue;

        const QImage&img=frame.image;
        const QString key=TextureKey::image(QStringLiteral("panorama:%1").arg(i));
        prov.insert(key,img);
        const QPointF off=i<m_panoramaOffsets.size()?m_panoramaOffsets[i]:QPointF();
        RuntimePanoramaInput input;
        input.imageSize=img.size();input.scrollOffset=off;input.fixed=layer.fixed;
        input.parallaxX=layer.parallaxX;input.parallaxY=layer.parallaxY;
        const RuntimePanoramaLayout layout=buildRuntimePanoramaLayout(state,input);
        if(!layout.valid)continue;
        const double opacity=qBound(0.0,frame.opacity/255.0,1.0);
        for(const QRectF& tile:layout.tiles)
            out.add(key,tile,QRectF(img.rect()),img.size(),opacity,QColor(),layer.blend,true,layout.space);
    }
}


} // namespace game
