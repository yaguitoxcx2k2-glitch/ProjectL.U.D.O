#include "GameSession.h"
#include "core/Version.h"
#include "core/GameValueRegistry.h"
#include "core/LudoCommandSystem.h"
#include "core/EventCommandCodec.h"
#include "StarActorDepth.h"
#include "FootstepResolver.h"
#include "core/Renderer.h"
#include "core/TilesetOps.h"
#include "game/TextDraw.h"
#include "game/TextPicture.h"
#include "game/GameSave.h"
#include "game/BattleTypes.h"
#include "game/RpgSystem.h"
#include "game/RuntimePictureTransform.h"
#include "game/ui/UiPainterRenderer.h"
#include "game/ui/UiStyleResolver.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QLinearGradient>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <limits>
#include "game/GameStateTransition.h"
using namespace core;
namespace game {
namespace {
int choiceNavigationDelta(const ChoiceView& choice, GameAction action)
{
    const QString layout = choice.layout.trimmed().toLower();
    const int count = choice.options.size();
    const int columns = layout == QLatin1String("horizontal")
        ? qMax(1, count)
        : (layout == QLatin1String("grid") ? qBound(1, choice.columns, qMax(1, count)) : 1);

    if (layout == QLatin1String("grid")) {
        if (action == GameAction::Left) return -1;
        if (action == GameAction::Right) return 1;
        if (action == GameAction::Up) return -columns;
        if (action == GameAction::Down) return columns;
        return 0;
    }
    if (layout == QLatin1String("horizontal")) {
        // Left/Right are the natural controls; keep Up/Down as aliases so old
        // projects and remapped controllers do not lose navigation.
        if (action == GameAction::Left || action == GameAction::Up) return -1;
        if (action == GameAction::Right || action == GameAction::Down) return 1;
        return 0;
    }
    if (action == GameAction::Up || action == GameAction::Left) return -1;
    if (action == GameAction::Down || action == GameAction::Right) return 1;
    return 0;
}

ui::UiTheme accessibleUiTheme(const core::Editor& ed, int scalePercent, bool strongFocus)
{
    ui::UiTheme theme = ui::UiTheme::fromSettings(ed.gameUi);
    if (!ed.accessibility.enabled) return theme;
    scalePercent = ed.accessibility.allowUiScale ? qBound(100, scalePercent, 150) : 100;
    const qreal factor = scalePercent / 100.0;
    theme.fontSize = qMax(6, qRound(theme.fontSize * factor));
    theme.paddingX = qMax(0, qRound(theme.paddingX * factor));
    theme.paddingY = qMax(0, qRound(theme.paddingY * factor));
    if (strongFocus) {
        theme.selection.borderWidth = qMax<qreal>(4.0, theme.selection.borderWidth * 1.75);
        theme.selection.innerBorderWidth = qMax<qreal>(2.0, theme.selection.innerBorderWidth * 1.5);
        theme.selection.fill.setAlpha(qMax(210, theme.selection.fill.alpha()));
        theme.selection.border = Qt::white;
        theme.selectedText = Qt::white;
    }
    for (auto it = theme.styleClasses.begin(); it != theme.styleClasses.end(); ++it) {
        if (it->fontSize > 0) it->fontSize = qMax(6, qRound(it->fontSize * factor));
        it->paddingX = qMax(0, qRound(it->paddingX * factor));
        it->paddingY = qMax(0, qRound(it->paddingY * factor));
    }
    return theme;
}


} // namespace

GameSession::GameSession(Editor& editorRef, const QPointF& startPixel, const QFont& fonteBase)
    : ed(editorRef), m_promptController(editorRef.inputSystem.forcedController),
      m_world(editorRef), m_uiMenu(*this), m_uiBattle(editorRef, m_state), m_uiShop(*this), m_font(fonteBase)
{
    carregarFontesDoProjeto(ed.projectPath);
    if (!ed.mainFontFamily.isEmpty()) m_font.setFamily(ed.mainFontFamily);
    m_gameUi.setTheme(accessibleUiTheme(ed, m_uiScalePercent, m_strongFocus));

    const MapInfo& info = ed.mapInfo();
    // O chamador escolhe o começo: F5 passa o centro da vista; F6/player passa
    // o único início global configurado no projeto.
    QPoint startCell(int(startPixel.x()) / qMax(1, info.tileWidth),
                     int(startPixel.y()) / qMax(1, info.tileHeight));
    // Toda a "Configuração de Personagem" (passo, diagonais, hitbox, animação)
    // entra no runtime aqui — o mundo não olha para o editor por conta própria.
    m_world.config.from(ed.player);
    m_world.reset(startCell);
    if (const MapDoc* map = ed.doc()) {
        const int base = qMax(0, map->encounterSteps);
        m_nextEncounterSteps = base > 0 ? qMax(1, base * (75 + randomBounded(51)) / 100) : 0;
        m_weather.applyConfig(map->environment.weather, false, true);
    }

    // Medidas da caixa de mensagem. A largura útil depende do zoom (a caixa é
    // desenhada em pixels de tela, não em pixels do mapa), por isso é refeita
    // no primeiro quadro também.
    m_clock.start();

    MessageStyle ms;
    ms.font = m_font;
    if (!ed.gameUi.fontFamily.trimmed().isEmpty()) ms.font.setFamily(ed.gameUi.fontFamily.trimmed());
    ms.font.setPixelSize(qMax(6, ed.gameUi.fontSize));
    ms.maxLines = 4;
    ms.innerWidth = m_viewW - 2 * 12 - 2 * qMax(0, ed.gameUi.paddingX);
    ms.charsPerSecond = 45.0;
    m_interp.setStyle(ms);
    refreshPlayerPreferences();

    // Legendas: mesma fonte da caixa, estilo vindo do projeto.
    QFont fs = m_font;
    if(!ed.subtitleStyle.fontFamily.isEmpty())fs.setFamily(ed.subtitleStyle.fontFamily);
    fs.setPixelSize(qMax(6, ed.subtitleStyle.fontSize));
    m_subs.setFont(fs);
    m_bubbles.setFont(m_font);
    m_subs.setStyle(ed.subtitleStyle);
    m_subsStyleCache = ed.subtitleStyle.toJson();
    m_subs.setScreenWidth(m_viewW / m_zoom);
    // Memória da partida: começa nos valores iniciais do projeto e vive só
    // enquanto o jogo estiver aberto (o projeto nunca é alterado).
    m_state.resetForNewGame(ed);
    m_fogs.resetFrom(ed);
    m_world.setEventPageResolver([this](const MapEvent& e) {
        return m_state.choosePage(e);
    });
    m_world.setEventSwitchHook([this](int id, bool on) { m_state.setSwitch(id, on); });
    m_world.setEventSoundHook([this](const QString& source, int volume) {
        if (!m_audio || source.isEmpty()) return;
        const QString file = QFileInfo(source).isAbsolute()
                                 ? source : QDir(ed.projectRoot()).filePath(source);
        m_audio(file, volume);
    });
    m_world.setFootstepHook([this](const QString& eventId, const QPointF& cell) {
        playFootstep(eventId, cell);
    });
    m_world.setRememberEventPositionHook([this](const QString& eventId,const QPoint& halfCell) {
        const QString mapId=currentMapId();
        if(mapId.isEmpty()||eventId.isEmpty())return;
        m_rememberedEventPositions.insert(mapId+QLatin1Char('\n')+eventId,{mapId,halfCell});
    });
    m_world.setRuntimeMapCellResolver([this](const QString& layerId,int x,int y) {
        return m_state.runtimeMapCell(ed,currentMapId(),layerId,x,y);
    });
    m_world.setRuntimePassageResolver([this](int x,int y,int inheritedMask) {
        return m_state.runtimeMapPassageMask(currentMapId(),x,y,inheritedMask);
    });
    m_world.setRuntimeMapRevisionResolver([this] { return m_state.runtimeMapRevision(); });
    configureInterpreter(m_interp);
    m_pics.setDynamicValueResolver([this](const QVariantMap& sourceSpec) {
        return m_interp.resolveSchedulerSource(sourceSpec, core::CommonValueType::Number);
    });
    m_pics.setTargetResolver([this](const QString& rawTarget) {
        PictureTargetState state;
        const QString target=rawTarget.trimmed();
        if(target==QLatin1String("player")){
            state.valid=true;
            state.x=(m_world.playerVisualPixel().x()-cameraTopLeft().x())*m_zoom;
            state.y=(m_world.playerVisualPixel().y()-cameraTopLeft().y())*m_zoom;
            state.opacity=m_world.playerOpacity();
            return state;
        }
        if(target.startsWith(QLatin1String("event:"))){
            const QString id=target.mid(6);
            for(const MapEvent& event:ed.events())if(event.id==id){
                World::EventActorView view;
                if(!m_world.eventView(event,&view))return state;
                state.valid=true;
                state.x=(view.pixel.x()-cameraTopLeft().x())*m_zoom;
                state.y=(view.pixel.y()-cameraTopLeft().y())*m_zoom;
                state.opacity=view.opacity;
                return state;
            }
        }
        return state;
    });
    // Texto rico dentro de uma picture usa a fonte do jogo e enxerga as
    // variáveis da partida (\v[n] no meio da frase).
    m_picFx.setFont(m_font);
    m_picFx.setVariableResolver([this](int id) { return QString::number(m_state.variable(id)); });
    m_subs.setVariableResolver([this](int id) { return QString::number(m_state.variable(id)); });

    // Voz das falas: a sessão não sabe tocar som, ela avisa por um hook que a
    // janela liga (é o que mantém isto testável sem áudio e sem tela).
    auto tocarEfeito = [this](const QString& f, int v) {
        if (!m_audio || f.isEmpty()) return;
        const QString path = QFileInfo(f).isAbsolute() ? f : QDir(ed.projectRoot()).filePath(f);
        m_audio(path, v);
    };
    m_subs.setVoiceHook([this](const QString& f,int v){
        if(!m_voiceAudio||f.isEmpty())return;
        const QString path=QFileInfo(f).isAbsolute()?f:QDir(ed.projectRoot()).filePath(f);
        m_voiceAudio(path,v);
    });
    m_subs.setSeHook(tocarEfeito);
    m_subs.setStopVoiceHook([this] { if (m_stopVoiceAudio) m_stopVoiceAudio(); });
}

void GameSession::configureInterpreter(Interpreter& interpreter)
{
    Interpreter* self=&interpreter;
    interpreter.setSubtitles(&m_subs);
    interpreter.setState(&m_state,&ed);
    interpreter.setInputController(&m_promptController);
    interpreter.setPictures(&m_pics);
    interpreter.setFogs(&m_fogs);
    interpreter.setMessageVoiceHook([this](const QString& file,int volume){if(!m_voiceAudio||file.isEmpty())return;const QString path=QFileInfo(file).isAbsolute()?file:QDir(ed.projectRoot()).filePath(file);m_voiceAudio(path,volume);});
    interpreter.setDialogueHistoryHook([this](const QString& text, const QString& speaker,
                                               const QString& speakerId, const QString& mapId,
                                               const QString& eventId) {
        core::DialogueEntry entry;
        entry.text = text; entry.speaker = speaker; entry.speakerId = speakerId;
        entry.mapId = mapId; entry.eventId = eventId;
        entry.timestamp = QDateTime::currentDateTimeUtc();
        m_dialogueHistory.addEntry(std::move(entry));
    });
    interpreter.setMoveRouteStartHook([this,self](const QString& target,const MoveRoute& route){
        return m_world.startMoveRoute(target,route,self->currentEventId());
    });
    interpreter.setMoveRouteStateHook([this](const QString& target,MoveRouteTicket ticket){
        return m_world.moveRouteTicketState(target,ticket);
    });
    interpreter.setMoveRouteControlHook([this](const QString& target,MoveRouteControlAction action){
        return m_world.controlMoveRoute(target,action);
    });
    interpreter.setRuntimeCommandHook([this](const EventCommand& c,bool start){return runRuntimeCommand(c,start);});
    interpreter.setReserveCommonEventHook([this](const QString& commonId,const QVariantMap& arguments,int priority){
        return reserveCommonEvent(commonId,arguments,priority);
    });
    interpreter.setParallelBlockStartHook([this](const QVector<QVector<EventCommand>>& tasks,
                                                 const core::EventExecutionContext& context){
        return startInlineParallelBlock(tasks, context);
    });
    interpreter.setParallelBlockStateHook([this](quint64 ticket){
        return inlineParallelBlockActive(ticket);
    });
    interpreter.setParallelBlockCancelHook([this](quint64 ticket, CancellationReason reason){
        cancelInlineParallelBlock(ticket, reason);
    });
    interpreter.setLocaleChangedHook([this](const QString&){ refreshLocalizationState(); });
    interpreter.setDebugGateHook([this,self](const QString& source, const QString& eventId, int index, int depth,
                                        const EventCommand& command) {
        DebugCommandLocation location;
        location.source = source;
        location.mapId = currentMapId();
        location.eventId = eventId;
        location.commandType = command.type;
        location.commandIndex = index;
        location.callDepth = depth;
        EventRuntimeDebugState runtime = self->runtimeDebugState();
        runtime.current = location;
        const EventRuntimeDebugState previous = m_eventDebugger.runtimeState();
        runtime.targetResolution = previous.targetResolution;
        runtime.conditionEvaluation = previous.conditionEvaluation;
        QList<quint64> tickets = m_inlineParallelBlocks.keys();
        std::sort(tickets.begin(), tickets.end());
        for (quint64 ticket : tickets) runtime.parallelTickets.push_back(ticket);
        m_eventDebugger.setRuntimeState(runtime);
        return m_eventDebugger.beforeCommand(location, [this,self](const QVariantMap& condition) {
            return evaluateDebugBreakpointCondition(condition, *self);
        });
    });
    interpreter.setTraceHook([this,self](const QString& eventId, int index, int depth,
                                    const EventCommand& command) {
        appendDebugTrace(QStringLiteral("command.execute"), self->executionContext().source,
                         eventId, index, depth, command.type,
                         {{QStringLiteral("cutsceneDepth"), self->cutsceneDepth()},
                          {QStringLiteral("awaitable"), awaitableKindId(self->awaitableKind())}});
    });
    interpreter.setDiagnosticTraceHook([this,self](const QString& phase, const QVariantMap& details) {
        if (phase == QLatin1String("target.resolve"))
            m_eventDebugger.recordTargetResolution(QStringLiteral("%1 → %2 [%3]")
                .arg(details.value(QStringLiteral("requested")).toString(),
                     details.value(QStringLiteral("canonical")).toString(),
                     details.value(QStringLiteral("error")).toString()));
        if (phase == QLatin1String("condition.evaluate"))
            m_eventDebugger.recordConditionEvaluation(QStringLiteral("%1 = %2")
                .arg(details.value(QStringLiteral("kind"), QStringLiteral("tree")).toString(),
                     details.value(QStringLiteral("result")).toBool() ? QStringLiteral("true") : QStringLiteral("false")));
        appendDebugTrace(phase, details.value(QStringLiteral("source"), self->executionContext().source).toString(),
                         details.value(QStringLiteral("eventId"), self->currentEventId()).toString(),
                         details.value(QStringLiteral("commandIndex"), self->commandIndex()).toInt(),
                         details.value(QStringLiteral("callDepth"), self->callDepth()).toInt(),
                         QString(), details);
    });
    interpreter.setConditionHook([this,self](const EventCommand& c){
        const QVariantMap&p=c.params;const QString k=p.value("kind").toString();
        if(k=="playerPosition"){const QPoint q=m_world.playerCell();return compareWithOp(q.x(),p.value("opX","==").toString(),p.value("x").toInt())&&compareWithOp(q.y(),p.value("opY","==").toString(),p.value("y").toInt());}
        if(k=="direction")return int(m_world.facing())==p.value("direction").toInt();
        if(k=="button"){bool ok=false;const GameAction a=gameActionFromId(p.value("action").toString(),&ok);return ok&&inputActionMatches(a,p.value("state",QStringLiteral("held")).toString());}
        if(k=="routeRunning"){const auto target=self->resolveEventTarget(p.value("target","player").toString());return target.valid()&&m_world.moveRouteRunning(target.canonical);}
        if(k=="map")return ed.doc()&&ed.doc()->id==p.value("mapId").toString();
        if(k=="distance"){QString id=p.value("eventId").toString();if(id.isEmpty())id=self->currentEventId();const QPoint e=m_world.eventCell(id),q=m_world.playerCell();return std::hypot(e.x()-q.x(),e.y()-q.y())<=p.value("distance",3).toDouble();}
        return false;
    });
    interpreter.setGameValueHook([this,self](const QVariantMap& query)->QVariant {
        const QString key = query.value(QStringLiteral("key")).toString();
        if (!core::isKnownGameValue(key)) return QVariant();
        const auto eventId = [&] {
            QString id = query.value(QStringLiteral("eventId")).toString();
            if (id == QLatin1String("self") || id.isEmpty()) id = self->currentEventId();
            return id;
        };
        if (key == QLatin1String("player.x")) return m_world.playerCell().x();
        if (key == QLatin1String("player.y")) return m_world.playerCell().y();
        if (key == QLatin1String("player.xPrecise")) return m_world.playerHalfCell().x();
        if (key == QLatin1String("player.yPrecise")) return m_world.playerHalfCell().y();
        if (key == QLatin1String("player.screenX")) return qRound((m_world.playerVisualPixel().x() - cameraTopLeft().x()) * m_zoom);
        if (key == QLatin1String("player.screenY")) return qRound((m_world.playerVisualPixel().y() - cameraTopLeft().y()) * m_zoom);
        if (key == QLatin1String("player.direction")) return int(m_world.facing());
        if (key == QLatin1String("player.moving")) return m_world.isMoving();
        if (key == QLatin1String("player.steps")) return m_world.stepsTaken();
        if (key == QLatin1String("player.opacity")) return m_world.playerOpacity();
        if (key == QLatin1String("player.speed")) return qRound(m_world.config.tilesPerSecond * 1000.0) / 1000.0;
        if (key == QLatin1String("player.transparent")) return m_world.playerTransparent();
        if (key == QLatin1String("player.blend")) return m_world.playerBlend();
        if (key == QLatin1String("player.animFrame")) return m_world.animFrame();
        if (key == QLatin1String("player.stepProgress")) return qRound(m_world.stepProgress() * 1000.0);
        if (key == QLatin1String("player.mapId")) return currentMapId();
        if (key == QLatin1String("player.footstepSurface")) {
            const QPoint half=m_world.playerHalfCell();
            return FootstepResolver::actorSurfaceId(ed,m_state,currentMapId(),QString(),QPointF(half.x()/2.0,half.y()/2.0));
        }

        if (key == QLatin1String("party.gold")) return m_state.gold();
        if (key == QLatin1String("party.size")) return m_state.party().size();
        if (key == QLatin1String("party.hasItem")) return m_state.itemCount(query.value(QStringLiteral("itemId")).toString()) > 0;
        if (key == QLatin1String("actor.hp") || key == QLatin1String("actor.mp")) {
            const PartyMemberState* actor=m_state.partyMember(query.value(QStringLiteral("actorId")).toString());
            if(!actor)return 0;
            return key==QLatin1String("actor.hp")?actor->hp:actor->mp;
        }
        if (key == QLatin1String("timer.value")) {
            const QString timerId=query.value(QStringLiteral("timerId")).toString();
            if(timerId.compare(QLatin1String("playtime"),Qt::CaseInsensitive)==0)return int(m_playTimeSeconds);
            bool numeric=false;const int variableId=timerId.toInt(&numeric);return numeric?m_state.variable(variableId):0;
        }

        if (key.startsWith(QLatin1String("event."))) {
            const QString id = eventId();
            const QPoint cell = m_world.eventCell(id);
            if (key == QLatin1String("event.x")) return cell.x();
            if (key == QLatin1String("event.y")) return cell.y();
            if (key == QLatin1String("event.moving")) return m_world.moveRouteRunning(QStringLiteral("event:") + id);
            for (const MapEvent& ev : ed.events()) if (ev.id == id) {
                World::EventActorView view;
                if (!m_world.eventView(ev, &view)) break;
                const int screenX = qRound((view.pixel.x() - cameraTopLeft().x()) * m_zoom);
                const int screenY = qRound((view.pixel.y() - cameraTopLeft().y()) * m_zoom);
                if (key == QLatin1String("event.page")) return view.page + 1;
                if (key == QLatin1String("event.direction")) return int(view.graphic.dir);
                if (key == QLatin1String("event.priority")) return int(view.priority);
                if (key == QLatin1String("event.trigger")) return view.page >= 0 ? int(ev.page(view.page).trigger) : -1;
                if (key == QLatin1String("event.distance")) {
                    const QPoint player = m_world.playerCell();
                    return qRound(std::hypot(double(view.cell.x() - player.x()), double(view.cell.y() - player.y())));
                }
                if (key == QLatin1String("event.opacity")) return view.opacity;
                if (key == QLatin1String("event.screenX")) return screenX;
                if (key == QLatin1String("event.screenY")) return screenY;
                if (key == QLatin1String("event.onScreen")) {
                    const int marginX = qMax(1, ed.mapInfo().tileWidth);
                    const int marginY = qMax(1, ed.mapInfo().tileHeight);
                    return screenX >= -marginX && screenY >= -marginY &&
                           screenX <= ed.gameResolution.width() + marginX &&
                           screenY <= ed.gameResolution.height() + marginY;
                }
                if (key == QLatin1String("event.transparent")) return view.transparent;
                if (key == QLatin1String("event.through")) return view.through;
                if (key == QLatin1String("event.blocksPlayer")) return view.blocksPlayer;
                if (key == QLatin1String("event.blend")) return view.blend;
                if (key == QLatin1String("event.footstepSurface"))
                    return FootstepResolver::actorSurfaceId(ed,m_state,currentMapId(),id,view.cell);
                break;
            }
            return 0;
        }

        if (key == QLatin1String("map.id")) return currentMapId();
        if (key == QLatin1String("map.name")) return ed.doc() ? ed.doc()->name : QString();
        if (key == QLatin1String("map.width")) return m_world.mapSizeInCells().width();
        if (key == QLatin1String("map.height")) return m_world.mapSizeInCells().height();
        if (key == QLatin1String("map.cameraX")) return qRound(cameraTopLeft().x());
        if (key == QLatin1String("map.cameraY")) return qRound(cameraTopLeft().y());
        if (key == QLatin1String("map.zoom")) return qRound(m_zoom * 100.0);
        if (key == QLatin1String("map.eventCount")) return ed.events().size();
        if (key == QLatin1String("map.runtimeOverrideCount")) return m_state.runtimeMapOverrideCount(currentMapId());
        if (key == QLatin1String("map.passable")) return !m_world.isBlocked(query.value(QStringLiteral("x")).toInt(), query.value(QStringLiteral("y")).toInt());
        if (key == QLatin1String("map.passageMask")) return m_world.cellMask(query.value(QStringLiteral("x")).toInt(), query.value(QStringLiteral("y")).toInt());
        if (key == QLatin1String("map.terrain")) return m_state.runtimeMapTerrain(currentMapId(),query.value(QStringLiteral("x")).toInt(),query.value(QStringLiteral("y")).toInt(),0);
        if (key == QLatin1String("map.runtimeChanged")) {
            const int gx=query.value(QStringLiteral("x")).toInt(), gy=query.value(QStringLiteral("y")).toInt();
            const QString mapId=currentMapId();
            if (m_state.runtimeMapChangedAt(mapId,gx,gy)) return true;
            const QSize mapCells=m_world.mapSizeInCells();
            if(gx<0||gy<0||gx>=mapCells.width()||gy>=mapCells.height())return false;
            // Um remapeamento de tileset é um delta do mapa inteiro. A consulta
            // por célula também deve enxergá-lo quando ele altera algum tile que
            // cobre a célula pedida, mesmo sem existir override direto em X/Y.
            const MapInfo& info=ed.mapInfo(); const int baseW=qMax(1,info.tileWidth),baseH=qMax(1,info.tileHeight);
            const int px0=gx*baseW,py0=gy*baseH;
            for(const LayerPtr& layer:ed.flatLayers()){
                if(!layer||layer->type!=LayerType::Tile)continue;
                const int lw=qMax(1,layer->tileWidth),lh=qMax(1,layer->tileHeight);
                const int lx0=px0/lw,ly0=py0/lh,lx1=(px0+baseW-1)/lw,ly1=(py0+baseH-1)/lh;
                for(int ly=ly0;ly<=ly1;++ly)for(int lx=lx0;lx<=lx1;++lx){
                    if(!layer->inBounds(lx,ly))continue;
                    const Cell original=layer->cellAt(lx,ly);
                    for(const TileRef& tile:original){
                        const Tileset* source=ed.tilesetAt(tile.tilesetIdx); if(!source)continue;
                        const QString targetId=m_state.runtimeMapTilesetRemap(mapId,source->id); if(targetId.isEmpty())continue;
                        for(int ti=0;ti<ed.tilesets.size();++ti)if(ed.tilesets.at(ti).id==targetId){
                            const Tileset* target=ed.tilesetAt(ti); if(target&&target->contains(tile.tx,tile.ty))return true; break;
                        }
                    }
                }
            }
            return false;
        }
        if (key == QLatin1String("map.tileTilesetId") || key == QLatin1String("map.tileLayerId") || key == QLatin1String("map.tileX") || key == QLatin1String("map.tileY")) {
            const int gx=query.value(QStringLiteral("x")).toInt(),gy=query.value(QStringLiteral("y")).toInt();
            const MapInfo& info=ed.mapInfo();const int baseW=qMax(1,info.tileWidth),baseH=qMax(1,info.tileHeight);const int px0=gx*baseW,py0=gy*baseH;
            const QVector<LayerPtr> layers=ed.flatLayers();
            for(int li=layers.size()-1;li>=0;--li){const LayerPtr& layer=layers.at(li);if(!layer||!layer->visible||layer->type!=LayerType::Tile)continue;const int lw=qMax(1,layer->tileWidth),lh=qMax(1,layer->tileHeight);const int lx0=px0/lw,ly0=py0/lh,lx1=(px0+baseW-1)/lw,ly1=(py0+baseH-1)/lh;for(int ly=ly1;ly>=ly0;--ly)for(int lx=lx1;lx>=lx0;--lx){if(!layer->inBounds(lx,ly))continue;const Cell cell=m_state.runtimeMapCell(ed,currentMapId(),layer->id,lx,ly);if(cell.isEmpty())continue;const TileRef tile=cell.last();if(key==QLatin1String("map.tileLayerId"))return layer->id;if(key==QLatin1String("map.tileX"))return tile.tx;if(key==QLatin1String("map.tileY"))return tile.ty;const Tileset* ts=ed.tilesetAt(tile.tilesetIdx);return ts?ts->id:QString();}}
            return key==QLatin1String("map.tileTilesetId")||key==QLatin1String("map.tileLayerId")?QVariant(QString()):QVariant(-1);
        }
        if (key == QLatin1String("map.eventAt")) {
            const QPoint target(query.value(QStringLiteral("x")).toInt(), query.value(QStringLiteral("y")).toInt());
            for (const MapEvent& ev : ed.events()) if (m_world.eventCell(ev.id) == target) return ev.id;
            return QString();
        }

        if (key.startsWith(QLatin1String("picture."))) {
            const int number = query.value(QStringLiteral("number"), 1).toInt();
            const LivePicture* picture = m_pics.at(number);
            if (key == QLatin1String("picture.exists")) return picture != nullptr;
            if (key == QLatin1String("picture.busy")) return m_pics.busy(number);
            if (!picture) return 0;
            if (key == QLatin1String("picture.x")) return qRound(picture->effectiveX());
            if (key == QLatin1String("picture.y")) return qRound(picture->effectiveY());
            if (key == QLatin1String("picture.scaleX")) return qRound(picture->effectiveScaleX() * 100.0);
            if (key == QLatin1String("picture.scaleY")) return qRound(picture->effectiveScaleY() * 100.0);
            if (key == QLatin1String("picture.angle")) return qRound(picture->effectiveAngle());
            if (key == QLatin1String("picture.opacity")) return qRound(picture->effectiveOpacity());
            if (key == QLatin1String("picture.frame")) return picture->effectiveFrameIndex();
            if (key == QLatin1String("picture.frameCount")) return qMax(1, picture->def.frameCount);
            if (key == QLatin1String("picture.framePlaying")) return picture->framePlayback.playing && !picture->framePlayback.paused;
            if (key == QLatin1String("picture.flipH")) return picture->def.flipH;
            if (key == QLatin1String("picture.flipV")) return picture->def.flipV;
            if (key == QLatin1String("picture.layer")) return core::pictureLayerId(picture->def.layer);
            if (key == QLatin1String("picture.blend")) return core::pictureBlendId(picture->def.blend);
            return 0;
        }

        if (key.startsWith(QLatin1String("audio."))) {
            const QString channel = key.section(QLatin1Char('.'), 1, 1);
            const QString property = key.section(QLatin1Char('.'), 2, 2);
            const AudioChannelState state = m_audioChannels.value(channel);
            if (property == QLatin1String("source")) return state.source;
            if (property == QLatin1String("volume")) return state.volume;
            if (property == QLatin1String("pitch")) return state.pitch;
            if (property == QLatin1String("pan")) return state.pan;
            if (property == QLatin1String("playing")) return !state.source.isEmpty();
        }

        if (key == QLatin1String("input.mouseX")) return qRound(m_mouseLogical.x());
        if (key == QLatin1String("input.mouseY")) return qRound(m_mouseLogical.y());
        if (key == QLatin1String("input.mouseDeltaX")) return qRound(m_mouseDelta.x());
        if (key == QLatin1String("input.mouseDeltaY")) return qRound(m_mouseDelta.y());
        if (key == QLatin1String("input.wheel")) return m_mouseWheelDelta;
        if (key == QLatin1String("input.device")) return m_promptController.isEmpty() ? QStringLiteral("keyboard") : QStringLiteral("gamepad");
        if (key.startsWith(QLatin1String("input.action"))) {
            bool ok=false; const GameAction action=gameActionFromId(query.value(QStringLiteral("action")).toString(),&ok);
            if(!ok)return QVariant();
            if(key==QLatin1String("input.actionHeld"))return inputHeld(action);
            if(key==QLatin1String("input.actionPressed"))return inputPressed(action);
            if(key==QLatin1String("input.actionReleased"))return inputReleased(action);
            if(key==QLatin1String("input.actionHoldFrames"))return inputHoldFrames(action);
        }
        if (key == QLatin1String("input.gamepadConnected")) return m_gamepadConnected;
        if (key == QLatin1String("input.axisX")) return qRound(m_gamepadAxisX*1000.0);
        if (key == QLatin1String("input.axisY")) return qRound(m_gamepadAxisY*1000.0);
        if (key == QLatin1String("input.axisMagnitude")) return qRound(qMin(1.0,std::hypot(m_gamepadAxisX,m_gamepadAxisY))*1000.0);
        if (key == QLatin1String("input.leftTrigger")) return qRound(m_gamepadLeftTrigger*1000.0);
        if (key == QLatin1String("input.rightTrigger")) return qRound(m_gamepadRightTrigger*1000.0);
        if (key == QLatin1String("input.mouseLeftHeld")) return m_mouseButtons.testFlag(Qt::LeftButton);
        if (key == QLatin1String("input.mouseLeftPressed")) return m_mouseLeftPressedFrame==m_visualFrameSerial;
        if (key == QLatin1String("input.mouseLeftReleased")) return m_mouseLeftReleasedFrame==m_visualFrameSerial;
        if (key == QLatin1String("input.mouseRightHeld")) return m_mouseButtons.testFlag(Qt::RightButton);
        if (key == QLatin1String("input.mouseRightPressed")) return m_mouseRightPressedFrame==m_visualFrameSerial;
        if (key == QLatin1String("input.mouseRightReleased")) return m_mouseRightReleasedFrame==m_visualFrameSerial;

        if (key == QLatin1String("system.fps")) return qRound(m_fps);
        if (key == QLatin1String("system.width")) return ed.gameResolution.width();
        if (key == QLatin1String("system.height")) return ed.gameResolution.height();
        if (key == QLatin1String("system.playTime")) return int(qBound<qint64>(qint64(0), qint64(m_playTimeSeconds), qint64(999999999)));
        if (key == QLatin1String("system.locale")) return playerLocale();
        if (key == QLatin1String("system.version")) return QString::fromLatin1(core::version::Engine);
        if (key == QLatin1String("system.platform")) return QSysInfo::productType().isEmpty() ? QSysInfo::kernelType() : QSysInfo::productType();
        return QVariant();
    });
}

quint64 GameSession::startInlineParallelBlock(const QVector<QVector<EventCommand>>& tasks,
                                               const core::EventExecutionContext& context)
{
    if (tasks.isEmpty()) return 0;
    quint64 ticket = ++m_inlineParallelSerial;
    if (ticket == 0) ticket = ++m_inlineParallelSerial;
    InlineParallelBlockRuntime block;
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks.at(i).isEmpty()) continue;
        const QString key = QStringLiteral("inline-parallel:%1:%2").arg(ticket).arg(i);
        auto ptr = std::make_shared<Interpreter>(m_interp.style());
        configureInterpreter(*ptr);
        core::EventExecutionContext taskContext = context;
        taskContext.mode = core::EventExecutionMode::Parallel;
        taskContext.source = context.source.isEmpty()
            ? key : QStringLiteral("%1:parallel:%2:%3").arg(context.source).arg(ticket).arg(i);
        ptr->setDebugSource(taskContext.source);
        ptr->start(tasks.at(i), taskContext);
        ptr->setDebugSource(taskContext.source);
        m_parallel.insert(key, ptr);
        block.interpreterKeys.push_back(key);
    }
    if (block.interpreterKeys.isEmpty()) return 0;
    m_inlineParallelBlocks.insert(ticket, block);
    return ticket;
}

bool GameSession::inlineParallelBlockActive(quint64 ticket)
{
    auto it = m_inlineParallelBlocks.find(ticket);
    if (it == m_inlineParallelBlocks.end()) return false;
    bool active = false;
    for (const QString& key : std::as_const(it->interpreterKeys)) {
        const auto ptr = m_parallel.value(key);
        if (ptr && ptr->running()) { active = true; break; }
    }
    if (active) return true;
    const QStringList keys = it->interpreterKeys;
    m_inlineParallelBlocks.erase(it);
    for (const QString& key : keys) m_parallel.remove(key);
    return false;
}

void GameSession::cancelInlineParallelBlock(quint64 ticket, CancellationReason reason)
{
    auto it = m_inlineParallelBlocks.find(ticket);
    if (it == m_inlineParallelBlocks.end()) return;
    const QStringList keys = it->interpreterKeys;
    m_inlineParallelBlocks.erase(it);
    QVector<std::shared_ptr<Interpreter>> toStop;
    for (const QString& key : keys) {
        const auto ptr = m_parallel.take(key);
        if (ptr) toStop.push_back(ptr);
    }
    for (const auto& ptr : std::as_const(toStop)) if (ptr) ptr->stop(reason);
}

void GameSession::stopParallelInterpreters(CancellationReason reason)
{
    QVector<std::shared_ptr<Interpreter>> interpreters;
    interpreters.reserve(m_parallel.size());
    for (auto it = m_parallel.cbegin(); it != m_parallel.cend(); ++it)
        if (it.value()) interpreters.push_back(it.value());
    m_parallel.clear();
    m_inlineParallelBlocks.clear();
    for (const auto& ptr : std::as_const(interpreters)) if (ptr) ptr->stop(reason);
}

void GameSession::stopAllInterpreters(CancellationReason reason)
{
    m_interp.stop(reason);
    stopParallelInterpreters(reason);
}

void GameSession::finishCutsceneTransientState()
{
    // Skip finaliza o estado visual em vez de deixar animações órfãs. O
    // Interpreter cancela seu Awaitable (inclusive ParallelBlock/MoveRoute);
    // a GameSession finaliza os efeitos que podem continuar mesmo sem wait.
    m_world.cancelAllMoveRoutes();
    m_pics.finishAnimations();
    for (auto it = m_spriteFx.begin(); it != m_spriteFx.end(); ++it) {
        if (it->active) { it->opacity = it->to; it->active = false; }
        if (it->transformActive) {
            it->offset = it->offsetTo; it->scale = it->scaleTo; it->transformActive = false;
        }
        it->shakeRemaining = 0.0;
    }
    if (m_camera.moving) {
        if (m_camera.tweenCenter) m_camera.center = m_camera.endCenter;
        if (m_camera.tweenZoom) m_zoom = m_camera.endZoom;
        m_camera.moving = false;
    }
    m_screenEffects.clear();
    m_subs.clearWithFade();
}

bool GameSession::startUiCommonEvent(int number)
{
    if (number <= 0 || m_interp.running()) return false;
    return m_interp.startCommonEvent(number);
}

QString GameSession::currentMapId() const
{
    return ed.doc() ? ed.doc()->id : QString();
}

bool GameSession::transferToMap(const QString& mapId, const QPoint& cell, bool useMapSpawn,
                                Dir facing, QString* error)
{
    GameTransitionTarget resolved;
    if (!resolveGameTransitionTarget(ed, GameStateTransitionKind::MapTransfer,
                                     mapId, cell, &resolved, error)) return false;
    const int index = ed.mapIndexById(resolved.mapId);
    if (index < 0) return false; // protegido pelo resolver; defesa contra mutacao concorrente.

    stopParallelInterpreters(CancellationReason::MapTransfer);
    const QString sourceMapId = currentMapId();
    ed.switchDoc(index);

    const MapDoc* destination = ed.doc();
    QPoint target = resolved.cell;
    // Compatibilidade defensiva com chamadas em memória criadas por versões
    // antigas. Projetos carregados já são migrados para coordenadas explícitas.
    if (useMapSpawn && destination && destination->hasSpawn) {
        GameTransitionTarget spawnTarget;
        if (!resolveGameTransitionTarget(ed, GameStateTransitionKind::MapTransfer,
                                         destination->id, destination->spawn, &spawnTarget, error)) return false;
        target = spawnTarget.cell;
    }
    m_world.config.from(ed.player);
    m_world.restorePlayer(QPoint(target.x() * 2, target.y() * 2), facing);
    applyRememberedEventPositions();
    m_sensorLatched.clear();
    m_pics.eraseOnMapChange();
    m_fogs.resetFrom(ed);
    m_subs.clearAll();
    m_camera = RuntimeCameraState();
    m_spriteFx.clear();
    m_panoramaOffsets.clear();
    m_panoramaAge = 0.0;
    // Teleporte dentro do mesmo mapa preserva o clima vivo (inclusive override
    // de evento e fase). Troca REAL de mapa limpa o override e reinicia a fase
    // usando o WeatherState do mapa de destino.
    const bool changedMap = !destination || destination->id != sourceMapId;
    m_cutsceneRegionInside.clear();
    if (changedMap) m_cutsceneRegionVisitFired.clear();
    if (changedMap) {
        WeatherState targetWeather;
        if (destination) targetWeather = destination->environment.weather;
        m_weather.applyConfig(targetWeather, false, true);
        m_lastThunderCycle = -1;
    } else {
        syncWeatherFromCurrentMap();
    }
    m_keys.clear();
    m_confirmPending = false;
    m_gameOver = false;
    m_stepsAtLastEncounter = 0;
    const int encounterBase = destination ? qMax(0, destination->encounterSteps) : 0;
    m_nextEncounterSteps = encounterBase > 0
        ? qMax(1, encounterBase * (75 + randomBounded(51)) / 100) : 0;
    m_overlayDirty = true;
    if (m_debugPresentation) {
        m_hint = QCoreApplication::translate("GameSession", "Mapa: %1")
                     .arg(destination ? destination->name : QString());
        m_hintUntil = m_clock.elapsed() + 1800;
    } else {
        m_hint.clear();
        m_hintUntil = 0;
    }
    refreshMapAudio();
    if (ed.autosaveEnabled && !m_loadingGame) {
        QString autosaveError;
        if (saveGame(ed.autosaveSlot, &autosaveError))
            m_hint = QCoreApplication::translate("GameSession", "Autosave concluído no slot %1.").arg(ed.autosaveSlot);
        else if (!autosaveError.isEmpty())
            m_hint = autosaveError;
    }
    return true;
}

void GameSession::playFootstep(const QString& eventId, const QPointF& actorCell,
                               const QString& forcedSurfaceId, int commandVolume)
{
    const QString mapId = currentMapId();
    if (mapId.isEmpty()) return;
    const QString lastKeyPrefix = eventId.isEmpty() ? QStringLiteral("player") : QStringLiteral("event:") + eventId;
    int actorVolume = 100;
    QString surfaceId = forcedSurfaceId.trimmed();
    if (surfaceId.isEmpty())
        surfaceId = FootstepResolver::actorSurfaceId(ed, m_state, mapId, eventId, actorCell, &actorVolume);
    else
        (void)FootstepResolver::actorSurfaceId(ed, m_state, mapId, eventId, actorCell, &actorVolume);
    if (surfaceId.isEmpty()) return;
    const QString variantKey = lastKeyPrefix + QLatin1Char('|') + surfaceId;
    const FootstepVariantHistory history = m_footstepVariantHistory.value(variantKey);
    const FootstepDecision decision = FootstepResolver::resolve(
        ed, m_state, mapId, eventId, actorCell, forcedSurfaceId, commandVolume, ++m_footstepSerial,
        history.last, history.previous);
    if (!decision.valid()) return;
    m_footstepVariantHistory.insert(variantKey, FootstepVariantHistory{history.last, decision.variantIndex});

    int spatialVolume = decision.volume;
    int spatialPan = decision.pan;
    if (!eventId.isEmpty()) {
        // RC2.68: passos de EVENTOS são áudio espacial relativo ao Player.
        // Atenuação contínua evita o efeito liga/desliga e o pan usa apenas o
        // eixo horizontal, como um campo estéreo 2D simples e previsível.
        const QPointF playerCell = QPointF(m_world.playerHalfCell()) / 2.0;
        const QPointF delta = actorCell - playerCell;
        const double distance = std::hypot(delta.x(), delta.y());
        constexpr double audibleRadius = 10.0;
        constexpr double fullVolumeRadius = 1.0;
        const double attenuation = distance <= fullVolumeRadius ? 1.0
            : qBound(0.0, 1.0 - (distance - fullVolumeRadius) / (audibleRadius - fullVolumeRadius), 1.0);
        spatialVolume = qBound(0, qRound(decision.volume * attenuation), 100);
        spatialPan = qBound(-100, decision.pan + qRound(qBound(-1.0, delta.x() / audibleRadius, 1.0) * 100.0), 100);
        if (spatialVolume <= 0) return;
    }

    if (m_effectAudio) m_effectAudio(decision.sourcePath, spatialVolume, decision.pitch, spatialPan);
    else if (m_audio) m_audio(decision.sourcePath, spatialVolume);
}

void GameSession::refreshMapAudio()
{
    const MapDoc* map = ed.doc();
    if (!map) return;
    auto play = [this](const QString& channel, const QString& source, int volume) {
        if (!m_channelAudio || source.isEmpty()) return;
        const QString path = QFileInfo(source).isAbsolute()
            ? source : QDir(ed.projectRoot()).filePath(source);
        const int boundedVolume = qBound(0, volume, 100);
        m_audioChannels[channel] = AudioChannelState{source, boundedVolume, true};
        m_channelAudio(channel, path, boundedVolume, true, 0, 0,100,0);
    };
    if (map->environment.autoBgm && !map->environment.bgmPath.isEmpty())
        play(QStringLiteral("bgm"), map->environment.bgmPath, map->environment.bgmVolume);
    else { m_audioChannels.remove(QStringLiteral("bgm")); if (m_stopChannelAudio) m_stopChannelAudio(QStringLiteral("bgm"), 0); }
    if (map->environment.autoBgs && !map->environment.bgsPath.isEmpty())
        play(QStringLiteral("bgs"), map->environment.bgsPath, map->environment.bgsVolume);
    else { m_audioChannels.remove(QStringLiteral("bgs")); if (m_stopChannelAudio) m_stopChannelAudio(QStringLiteral("bgs"), 0); }
}

QJsonObject GameSession::runtimeSnapshot() const
{
    QJsonArray audio;
    QStringList channels = m_audioChannels.keys();
    channels.sort();
    for (const QString& channel : channels) {
        const AudioChannelState& state = m_audioChannels.value(channel);
        // Sons pontuais não recomeçam ao carregar; somente canais contínuos.
        if (!state.loop || state.source.isEmpty()) continue;
        audio.append(QJsonObject{{QStringLiteral("channel"), channel},
                                 {QStringLiteral("source"), state.source},
                                 {QStringLiteral("volume"), state.volume},
                                 {QStringLiteral("loop"), state.loop},
                                 {QStringLiteral("pitch"),state.pitch},{QStringLiteral("pan"),state.pan}});
    }
    const QJsonObject camera = m_camera.toJson(m_zoom);
    const QJsonObject screenTone = m_screenTone.toJson();
    const QJsonObject filters = m_filters.toJson();
    const QJsonObject screenEffects = m_screenEffects.toJson();
    QJsonObject spriteEffects;
    for(auto it=m_spriteFx.cbegin();it!=m_spriteFx.cend();++it){const SpriteFx& f=it.value();spriteEffects.insert(it.key(),QJsonObject{
        {QStringLiteral("type"),f.type},{QStringLiteral("mode"),f.mode},{QStringLiteral("fadeEnabled"),f.fadeEnabled},{QStringLiteral("phantomEnabled"),f.phantomEnabled},{QStringLiteral("opacity"),f.opacity},{QStringLiteral("from"),f.from},{QStringLiteral("to"),f.to},{QStringLiteral("elapsed"),f.elapsed},{QStringLiteral("duration"),f.duration},{QStringLiteral("active"),f.active},
        {QStringLiteral("offsetX"),f.offset.x()},{QStringLiteral("offsetY"),f.offset.y()},{QStringLiteral("offsetFromX"),f.offsetFrom.x()},{QStringLiteral("offsetFromY"),f.offsetFrom.y()},{QStringLiteral("offsetToX"),f.offsetTo.x()},{QStringLiteral("offsetToY"),f.offsetTo.y()},
        {QStringLiteral("scaleX"),f.scale.x()},{QStringLiteral("scaleY"),f.scale.y()},{QStringLiteral("scaleFromX"),f.scaleFrom.x()},{QStringLiteral("scaleFromY"),f.scaleFrom.y()},{QStringLiteral("scaleToX"),f.scaleTo.x()},{QStringLiteral("scaleToY"),f.scaleTo.y()},
        {QStringLiteral("transformElapsed"),f.transformElapsed},{QStringLiteral("transformDuration"),f.transformDuration},{QStringLiteral("transformActive"),f.transformActive},
        {QStringLiteral("shakeX"),f.shakeX},{QStringLiteral("shakeY"),f.shakeY},{QStringLiteral("shakeFrequency"),f.shakeFrequency},{QStringLiteral("shakeRemaining"),f.shakeRemaining},{QStringLiteral("shakePhase"),f.shakePhase},
        {QStringLiteral("distance"),f.distance},{QStringLiteral("nearDistance"),f.nearDistance},{QStringLiteral("minimum"),f.minimum},{QStringLiteral("maximum"),f.maximum},{QStringLiteral("smoothness"),f.smoothness}});}
    QJsonArray rememberedPositions;
    QStringList rememberedKeys=m_rememberedEventPositions.keys();rememberedKeys.sort();
    for(const QString& key:rememberedKeys){const RememberedEventPosition& p=m_rememberedEventPositions.value(key);
        rememberedPositions.append(QJsonObject{{QStringLiteral("mapId"),p.mapId},{QStringLiteral("eventId"),key.section(QLatin1Char('\n'),1)},
                                                {QStringLiteral("xHalf"),p.halfCell.x()},{QStringLiteral("yHalf"),p.halfCell.y()}});}
    return QJsonObject{
        {QStringLiteral("runtimeVersion"), RuntimeSnapshotPayloadVersion},
        {QStringLiteral("pictures"), m_pics.toJson()},
        {QStringLiteral("dialogueHistory"), m_dialogueHistory.toJson()},
        {QStringLiteral("fogs"), m_fogs.toJson()},
        {QStringLiteral("world"), m_world.runtimeState()},
        {QStringLiteral("weather"), runtimeWeatherToJson(m_weather)},
        {QStringLiteral("camera"), camera},
        {QStringLiteral("screenTone"), screenTone},
        {QStringLiteral("filters"), filters},
        {QStringLiteral("screenEffects"), screenEffects},
        {QStringLiteral("spriteEffects"), spriteEffects},
        {QStringLiteral("panoramaX"), m_panoramaOffsets.isEmpty() ? 0.0 : m_panoramaOffsets.first().x()},
        {QStringLiteral("panoramaY"), m_panoramaOffsets.isEmpty() ? 0.0 : m_panoramaOffsets.first().y()},
        {QStringLiteral("panoramaAge"), m_panoramaAge},
        {QStringLiteral("panoramaOffsets"), [&] { QJsonArray a; for (const QPointF& pt : m_panoramaOffsets) a.append(QJsonObject{{QStringLiteral("x"),pt.x()},{QStringLiteral("y"),pt.y()}}); return a; }()},
        {QStringLiteral("audio"), audio},
        {QStringLiteral("rememberedEventPositions"),rememberedPositions},
        {QStringLiteral("commonScheduler"), [&]{QJsonArray a;QStringList ids=m_commonSchedule.keys();ids.sort();for(const QString& id:ids){const CommonScheduleRuntime& st=m_commonSchedule.value(id);a.append(QJsonObject{{QStringLiteral("id"),id},{QStringLiteral("initialized"),st.initialized},{QStringLiteral("previous"),st.previousCondition},{QStringLiteral("pending"),st.pending},{QStringLiteral("frames"),st.framesUntilRun}});}return a;}()},
        {QStringLiteral("reservedCommonEvents"), [&]{QJsonArray a;for(const ReservedCommonCall& call:m_reservedCommonEvents)a.append(QJsonObject{{QStringLiteral("commonId"),call.commonId},{QStringLiteral("arguments"),QJsonObject::fromVariantMap(call.arguments)},{QStringLiteral("priority"),call.priority},{QStringLiteral("serial"),QString::number(call.serial)}});return a;}()},
        {QStringLiteral("reservedCommonSerial"), QString::number(m_reservedCommonSerial)},
        {QStringLiteral("cutsceneRegionPersistentFired"), [&]{QJsonArray a;QStringList keys;for(const QString& key:m_cutsceneRegionPersistentFired)keys.push_back(key);keys.sort();for(const QString& key:keys)a.append(key);return a;}()},
        {QStringLiteral("stepsAtLastEncounter"), m_stepsAtLastEncounter},
        {QStringLiteral("nextEncounterSteps"), m_nextEncounterSteps}
    };
}

void GameSession::restoreRuntimeSnapshot(const QJsonObject& snapshot)
{
    // Carregar uma partida é uma nova linha do tempo. Campos ausentes em saves
    // v1/v2/v3 antigos usam defaults seguros e NUNCA herdam Tone/Flash/Fade/
    // Shake da sessão que estava rodando antes do Load.
    m_screenTone = ScreenToneState();
    m_filters.reset();
    m_screenEffects.clear();
    m_picFx.clear();
    m_commonSchedule.clear();m_commonScheduleTickSerial=0;
    m_reservedCommonEvents.clear();m_reservedCommonSerial=0;
    m_cutsceneRegionInside.clear();m_cutsceneRegionVisitFired.clear();m_cutsceneRegionPersistentFired.clear();
    m_overlayDirty = true;
    m_dialogueHistory.clear();
    if (snapshot.isEmpty()) return; // saves v1/v2 usam os padrões do mapa
    if (snapshot.value(QStringLiteral("dialogueHistory")).isObject())
        m_dialogueHistory.fromJson(snapshot.value(QStringLiteral("dialogueHistory")).toObject());
    if (snapshot.value(QStringLiteral("pictures")).isObject())
        m_pics.fromJson(snapshot.value(QStringLiteral("pictures")).toObject());
    if (snapshot.value(QStringLiteral("fogs")).isArray())
        m_fogs.restoreFromJson(snapshot.value(QStringLiteral("fogs")).toArray(), ed);
    if (snapshot.value(QStringLiteral("world")).isObject())
        m_world.restoreRuntimeState(snapshot.value(QStringLiteral("world")).toObject());
    for(const QJsonValue& value:snapshot.value(QStringLiteral("commonScheduler")).toArray()){
        const QJsonObject o=value.toObject();const QString id=o.value(QStringLiteral("id")).toString();if(id.isEmpty())continue;
        CommonScheduleRuntime st;st.initialized=o.value(QStringLiteral("initialized")).toBool(false);st.previousCondition=o.value(QStringLiteral("previous")).toBool(false);st.pending=o.value(QStringLiteral("pending")).toBool(false);st.framesUntilRun=qBound(0,o.value(QStringLiteral("frames")).toInt(),360000);m_commonSchedule.insert(id,st);
        if(m_commonSchedule.size()>=4096)break;
    }
    m_reservedCommonSerial=snapshot.value(QStringLiteral("reservedCommonSerial")).toString().toULongLong();
    for (const QJsonValue& value : snapshot.value(QStringLiteral("cutsceneRegionPersistentFired")).toArray())
        if (value.isString() && m_cutsceneRegionPersistentFired.size() < 16384)
            m_cutsceneRegionPersistentFired.insert(value.toString());
    for(const QJsonValue& value:snapshot.value(QStringLiteral("reservedCommonEvents")).toArray()){
        if(m_reservedCommonEvents.size()>=1024)break;
        const QJsonObject o=value.toObject();const QString id=o.value(QStringLiteral("commonId")).toString();
        if(id.isEmpty()||!ed.commonEventById(id))continue;
        ReservedCommonCall call;call.commonId=id;call.arguments=o.value(QStringLiteral("arguments")).toObject().toVariantMap();
        call.priority=qBound(-1000,o.value(QStringLiteral("priority")).toInt(),1000);call.serial=o.value(QStringLiteral("serial")).toString().toULongLong();
        if(call.serial==0)call.serial=++m_reservedCommonSerial;else m_reservedCommonSerial=qMax(m_reservedCommonSerial,call.serial);
        m_reservedCommonEvents.push_back(call);
    }
    m_rememberedEventPositions.clear();
    for(const QJsonValue& value:snapshot.value(QStringLiteral("rememberedEventPositions")).toArray()){
        const QJsonObject o=value.toObject();const QString mapId=o.value(QStringLiteral("mapId")).toString();
        const QString eventId=o.value(QStringLiteral("eventId")).toString();if(mapId.isEmpty()||eventId.isEmpty())continue;
        m_rememberedEventPositions.insert(mapId+QLatin1Char('\n')+eventId,{mapId,QPoint(o.value(QStringLiteral("xHalf")).toInt(),o.value(QStringLiteral("yHalf")).toInt())});
    }
    m_spriteFx.clear();
    const QJsonObject spriteEffects=snapshot.value(QStringLiteral("spriteEffects")).toObject();
    for(auto it=spriteEffects.begin();it!=spriteEffects.end();++it){const QJsonObject o=it.value().toObject();SpriteFx f;f.type=o.value(QStringLiteral("type")).toString();f.mode=o.value(QStringLiteral("mode")).toString(QStringLiteral("visibleNear"));f.fadeEnabled=o.contains(QStringLiteral("fadeEnabled"))?o.value(QStringLiteral("fadeEnabled")).toBool():f.type==QLatin1String("fade");f.phantomEnabled=o.contains(QStringLiteral("phantomEnabled"))?o.value(QStringLiteral("phantomEnabled")).toBool():f.type==QLatin1String("phantom");f.opacity=o.value(QStringLiteral("opacity")).toDouble(1);f.from=o.value(QStringLiteral("from")).toDouble(f.opacity);f.to=o.value(QStringLiteral("to")).toDouble(f.opacity);f.elapsed=qMax(0.0,o.value(QStringLiteral("elapsed")).toDouble());f.duration=qMax(0.0,o.value(QStringLiteral("duration")).toDouble());f.active=o.value(QStringLiteral("active")).toBool(false);f.offset=QPointF(o.value(QStringLiteral("offsetX")).toDouble(),o.value(QStringLiteral("offsetY")).toDouble());f.offsetFrom=QPointF(o.value(QStringLiteral("offsetFromX")).toDouble(f.offset.x()),o.value(QStringLiteral("offsetFromY")).toDouble(f.offset.y()));f.offsetTo=QPointF(o.value(QStringLiteral("offsetToX")).toDouble(f.offset.x()),o.value(QStringLiteral("offsetToY")).toDouble(f.offset.y()));f.scale=QPointF(o.value(QStringLiteral("scaleX")).toDouble(1),o.value(QStringLiteral("scaleY")).toDouble(1));f.scaleFrom=QPointF(o.value(QStringLiteral("scaleFromX")).toDouble(f.scale.x()),o.value(QStringLiteral("scaleFromY")).toDouble(f.scale.y()));f.scaleTo=QPointF(o.value(QStringLiteral("scaleToX")).toDouble(f.scale.x()),o.value(QStringLiteral("scaleToY")).toDouble(f.scale.y()));f.transformElapsed=qMax(0.0,o.value(QStringLiteral("transformElapsed")).toDouble());f.transformDuration=qMax(0.0,o.value(QStringLiteral("transformDuration")).toDouble());f.transformActive=o.value(QStringLiteral("transformActive")).toBool(false);f.shakeX=qMax(0.0,o.value(QStringLiteral("shakeX")).toDouble());f.shakeY=qMax(0.0,o.value(QStringLiteral("shakeY")).toDouble());f.shakeFrequency=qBound(.5,o.value(QStringLiteral("shakeFrequency")).toDouble(12),60.0);f.shakeRemaining=qMax(0.0,o.value(QStringLiteral("shakeRemaining")).toDouble());f.shakePhase=o.value(QStringLiteral("shakePhase")).toDouble();f.distance=o.value(QStringLiteral("distance")).toDouble(6);f.nearDistance=o.value(QStringLiteral("nearDistance")).toDouble(1);f.minimum=o.value(QStringLiteral("minimum")).toDouble(.15);f.maximum=o.value(QStringLiteral("maximum")).toDouble(1);f.smoothness=o.value(QStringLiteral("smoothness")).toDouble(1);m_spriteFx.insert(it.key(),f);}

    const QJsonObject weather = snapshot.value(QStringLiteral("weather")).toObject();
    if (runtimeWeatherFromJson(weather, &m_weather)) m_lastThunderCycle = -1;
    m_panoramaOffsets.clear();
    for (const QJsonValue& v : snapshot.value(QStringLiteral("panoramaOffsets")).toArray()) {
        const QJsonObject o=v.toObject();m_panoramaOffsets.push_back(QPointF(o.value(QStringLiteral("x")).toDouble(),o.value(QStringLiteral("y")).toDouble()));
    }
    if (m_panoramaOffsets.isEmpty())
        m_panoramaOffsets.push_back(QPointF(snapshot.value(QStringLiteral("panoramaX")).toDouble(),
                                            snapshot.value(QStringLiteral("panoramaY")).toDouble()));
    m_panoramaAge=qMax(0.0,snapshot.value(QStringLiteral("panoramaAge")).toDouble(0.0));
    const QJsonObject camera = snapshot.value(QStringLiteral("camera")).toObject();
    if (!camera.isEmpty()) m_camera.fromJson(camera, &m_zoom);
    const QJsonObject tone = snapshot.value(QStringLiteral("screenTone")).toObject();
    if (!tone.isEmpty()) {
        m_screenTone.fromJson(tone);
        m_overlayDirty = true;
    }
    const QJsonObject filters = snapshot.value(QStringLiteral("filters")).toObject();
    if (!filters.isEmpty()) m_filters.fromJson(filters);
    const QJsonObject screenEffects = snapshot.value(QStringLiteral("screenEffects")).toObject();
    if (!screenEffects.isEmpty()) m_screenEffects.fromJson(screenEffects);
    else m_screenEffects.clear(); // RC2.13 e anteriores não podem herdar efeito do frame atual.
    m_overlayDirty = true;
    if (snapshot.value(QStringLiteral("audio")).isArray()) {
        for (const QString& channel : m_audioChannels.keys())
            if (m_stopChannelAudio) m_stopChannelAudio(channel, 0);
        m_audioChannels.clear();
        for (const QJsonValue& value : snapshot.value(QStringLiteral("audio")).toArray()) {
            const QJsonObject saved = value.toObject();
            const QString channel = saved.value(QStringLiteral("channel")).toString();
            const QString source = saved.value(QStringLiteral("source")).toString();
            if (channel.isEmpty() || source.isEmpty()) continue;
            const int volume = qBound(0, saved.value(QStringLiteral("volume")).toInt(90), 100);
            const bool loop = saved.value(QStringLiteral("loop")).toBool(true);
            const int pitch=qBound(50,saved.value(QStringLiteral("pitch")).toInt(100),200);
            const int pan=qBound(-100,saved.value(QStringLiteral("pan")).toInt(),100);
            m_audioChannels[channel] = AudioChannelState{source, volume, loop,pitch,pan};
            if (m_channelAudio) {
                const QString path = QFileInfo(source).isAbsolute() ? source
                    : QDir(ed.projectRoot()).filePath(source);
                m_channelAudio(channel, path, volume, loop, 0, 0,pitch,pan);
            }
        }
    }
    m_stepsAtLastEncounter = qMax(0, snapshot.value(QStringLiteral("stepsAtLastEncounter")).toInt());
    m_nextEncounterSteps = qMax(0, snapshot.value(QStringLiteral("nextEncounterSteps")).toInt());
    m_overlayDirty = true;
}

bool GameSession::saveGame(int slot, QString* error) const
{
    const MapDoc* map = ed.doc();
    if (!map) {
        if (error) *error = QCoreApplication::translate("GameSession", "Não há mapa ativo para salvar.");
        return false;
    }
    GameTransitionTarget saveTarget;
    if (!resolveGameTransitionTarget(ed, GameStateTransitionKind::Save, map->id,
                                     QPoint(m_world.playerHalfCell().x() / 2,
                                            m_world.playerHalfCell().y() / 2),
                                     &saveTarget, error)) return false;
    GameSaveData data;
    data.projectId = ed.projectId;
    data.projectName = ed.projectName;
    data.mapId = map->id;
    data.playerHalfCell = m_world.playerHalfCell();
    data.playerDirection = int(m_world.facing());
    data.playTimeSeconds=qint64(m_playTimeSeconds);
    data.state = m_state;
    data.runtime = runtimeSnapshot();
    return writeGameSave(gameSavePath(ed, slot), data, error);
}

bool GameSession::loadGame(int slot, QString* error)
{
    GameSaveData data;
    if (!readGameSave(gameSavePath(ed, slot), ed, &data, error)) return false;

    // Valida ANTES de substituir qualquer estado da sessão. Load nunca usa
    // fallback de mapa: save quebrado é erro explícito e a partida atual fica intacta.
    GameTransitionTarget resolved;
    const QPoint requestedCell(data.playerHalfCell.x() / 2, data.playerHalfCell.y() / 2);
    if (!resolveGameTransitionTarget(ed, GameStateTransitionKind::Load,
                                     data.mapId, requestedCell, &resolved, error)) return false;
    const MapDoc* savedMap = ed.mapById(resolved.mapId);
    if (!savedMap) return false;
    const QPoint safeHalfCell = clampHalfCellToMap(*savedMap, data.playerHalfCell);

    // Carregar é uma nova linha do tempo: nenhum comando posterior ao comando
    // de load da partida antiga deve continuar rodando.
    stopAllInterpreters(CancellationReason::LoadGame);
    m_pendingRuntimeActions.clear();
    m_pics.clearAll();
    m_picFx.clear();
    m_fogs.clear();
    m_subs.clearAll();
    m_state = data.state;
    m_playTimeSeconds=data.playTimeSeconds;
    const Dir direction = static_cast<Dir>(qBound(0, data.playerDirection, int(Dir::UpRight)));
    m_loadingGame = true;
    const bool transferred = transferToMap(resolved.mapId, resolved.cell, false, direction, error);
    m_loadingGame = false;
    if (!transferred) return false;
    m_world.restorePlayer(safeHalfCell, direction);
    restoreRuntimeSnapshot(data.runtime);
    return true;
}

bool GameSession::restartGame(QString* error)
{
    GameTransitionTarget resolved;
    if (!resolveGameTransitionTarget(ed, GameStateTransitionKind::Restart,
                                     ed.startMapId, ed.startPosition, &resolved, error)) return false;

    // Restart é uma NOVA partida dentro da mesma janela: encerra a linha do
    // tempo atual e limpa exclusivamente estado runtime/save, nunca o projeto.
    stopAllInterpreters(CancellationReason::RestartGame);
    m_pendingRuntimeActions.clear();
    m_commonSchedule.clear();
    m_commonScheduleTickSerial = 0;
    m_reservedCommonEvents.clear();
    m_reservedCommonSerial = 0;
    m_cutsceneRegionInside.clear();
    m_cutsceneRegionVisitFired.clear();
    m_cutsceneRegionPersistentFired.clear();
    m_autoLudoExecuted.clear();
    m_sensorLatched.clear();
    m_rememberedEventPositions.clear();
    m_footstepVariantHistory.clear();
    m_footstepSerial = 0;

    m_pics.clearAll();
    m_picFx.clear();
    m_fogs.clear();
    m_subs.clearAll();
    m_spriteFx.clear();
    m_screenTone = ScreenToneState();
    m_filters.reset();
    m_screenEffects.clear();
    m_camera = RuntimeCameraState();
    m_zoom = 2.0;
    m_mapTransition = MapTransition();
    m_panoramaOffsets.clear();
    m_panoramaAge = 0.0;
    m_weather = RuntimeWeatherState();
    m_lastThunderCycle = -1;
    m_cutsceneOverride = true;
    m_skipHeld = false;
    m_skipTriggered = false;
    m_skipHeldSec = 0.0;
    m_skipFade = 0.0;
    m_runtimeModalCommand = core::EventCommand();
    m_uiModal.clear();
    m_uiMenu.resetForGameStateTransition();
    m_uiBattle.clear();
    m_uiShop.clear();

    for (const QString& channel : m_audioChannels.keys())
        if (m_stopChannelAudio) m_stopChannelAudio(channel, 0);
    m_audioChannels.clear();
    if (m_stopVoiceAudio) m_stopVoiceAudio();

    m_keys.clear();
    m_actions.clear();
    m_mouseButtons = Qt::NoButton;
    m_mouseLeftPressedFrame = m_mouseLeftReleasedFrame = 0;
    m_mouseRightPressedFrame = m_mouseRightReleasedFrame = 0;
    m_mouseWheelDelta = 0;
    m_mouseDelta = QPointF();
    m_actionPressedFrame.clear();
    m_actionReleasedFrame.clear();
    m_actionHoldFrames.clear();
    m_confirmPending = false;
    m_wantClose = false;
    m_gameOver = false;
    m_loadingGame = true;
    m_playTimeSeconds = 0.0;
    m_stepsAtLastEncounter = 0;
    m_nextEncounterSteps = 0;
    m_randomEncounterBattle = false;
    m_state.resetForNewGame(ed);

    const bool transferred = transferToMap(resolved.mapId, resolved.cell, false, Dir::Down, error);
    m_loadingGame = false;
    if (!transferred) return false;
    m_overlayDirty = true;
    return true;
}

Interpreter* GameSession::visibleInterpreter()
{
    if(m_interp.message().visible||m_interp.choice().visible)return &m_interp;
    for(auto it=m_parallel.begin();it!=m_parallel.end();++it)if(it.value()&&(it.value()->message().visible||it.value()->choice().visible))return it.value().get();
    return &m_interp;
}

const Interpreter* GameSession::visibleInterpreter() const
{
    if(m_interp.message().visible||m_interp.choice().visible)return &m_interp;
    for(auto it=m_parallel.cbegin();it!=m_parallel.cend();++it)if(it.value()&&(it.value()->message().visible||it.value()->choice().visible))return it.value().get();
    return &m_interp;
}

Interpreter* GameSession::activeCutsceneInterpreter()
{
    // Primeiro, a região que está efetivamente apresentando UI ao jogador.
    // Depois Main e, por fim, a ordem estável do QMap de Interpreters
    // paralelos. Assim duas regiões simultâneas nunca recebem o mesmo input.
    if (m_interp.running() && m_interp.cutsceneActive() &&
        (m_interp.message().visible || m_interp.choice().visible))
        return &m_interp;
    for (auto it = m_parallel.begin(); it != m_parallel.end(); ++it)
        if (it.value() && it.value()->running() && it.value()->cutsceneActive() &&
            (it.value()->message().visible || it.value()->choice().visible))
            return it.value().get();
    if (m_interp.running() && m_interp.cutsceneActive()) return &m_interp;
    for (auto it = m_parallel.begin(); it != m_parallel.end(); ++it)
        if (it.value() && it.value()->running() && it.value()->cutsceneActive())
            return it.value().get();
    return nullptr;
}

const Interpreter* GameSession::activeCutsceneInterpreter() const
{
    if (m_interp.running() && m_interp.cutsceneActive() &&
        (m_interp.message().visible || m_interp.choice().visible))
        return &m_interp;
    for (auto it = m_parallel.cbegin(); it != m_parallel.cend(); ++it)
        if (it.value() && it.value()->running() && it.value()->cutsceneActive() &&
            (it.value()->message().visible || it.value()->choice().visible))
            return it.value().get();
    if (m_interp.running() && m_interp.cutsceneActive()) return &m_interp;
    for (auto it = m_parallel.cbegin(); it != m_parallel.cend(); ++it)
        if (it.value() && it.value()->running() && it.value()->cutsceneActive())
            return it.value().get();
    return nullptr;
}

bool GameSession::canSkipCutscene() const
{
    const Interpreter* target = activeCutsceneInterpreter();
    return ed.cutsceneSkip.enabled && m_cutsceneOverride && target &&
           target->cutsceneRegion().skipAllowed && target->cutsceneSkipReady();
}

GameAction GameSession::cutsceneSkipAction() const
{
    const Interpreter* target = activeCutsceneInterpreter();
    bool ok = false;
    const GameAction action = gameActionFromId(target ? target->cutsceneRegion().skipAction : QString(), &ok);
    return ok ? action : GameAction::SkipCutscene;
}

bool GameSession::anyInterpreterRunning() const
{
    if(m_interp.running())return true;
    for(auto it=m_parallel.cbegin();it!=m_parallel.cend();++it)if(it.value()&&it.value()->running())return true;
    return false;
}

void GameSession::syncParallelInterpreters()
{
    QSet<QString> desired;
    QSet<QString> activeAutoLudo;
    // Blocos Parallel inline vivem no mesmo pool de Interpreters e portanto
    // participam do Debugger/ordenação normais, mas sua vida é controlada
    // pelo ticket do bloco pai — não pela página/scheduler global.
    for (auto blockIt = m_inlineParallelBlocks.cbegin(); blockIt != m_inlineParallelBlocks.cend(); ++blockIt)
        for (const QString& key : blockIt.value().interpreterKeys) desired.insert(key);
    if(const MapDoc*d=ed.doc())for(const MapEvent&ev:d->events){
        const int page=m_state.choosePage(ev);if(page<0)continue;const EventPage&pg=ev.page(page);
        if(pg.trigger==EventTrigger::Parallel&&!pg.commands.isEmpty()){
            const QString key=QStringLiteral("map:%1:event:%2:page:%3").arg(d->id,ev.id).arg(page);desired.insert(key);auto ptr=m_parallel.value(key);
            if(!ptr){ptr=std::make_shared<Interpreter>(m_interp.style());configureInterpreter(*ptr);ptr->setDebugSource(key);m_parallel.insert(key,ptr);}
            if(!ptr->running()){ptr->setDebugSource(key);ptr->start(ev,page);ptr->setDebugSource(key);}
        }
        // Ludo System no estilo plugin do editores de RPG: tanto o Comando Ludo
        // dedicado quanto tags Ludo escritas em Comentários da página são
        // metadados automáticos e independem do gatilho do evento.
        for(int ci=0;ci<pg.commands.size();++ci){
            const EventCommand& source=pg.commands.at(ci);
            const QString signature = QString::fromUtf8(
                QJsonDocument(core::eventCommandToJson(source)).toJson(QJsonDocument::Compact));
            auto cached=m_autoLudoCache.constFind(signature);
            if(cached==m_autoLudoCache.cend()){
                QVector<core::LudoCommandParseResult> diagnostics;
                const QVector<EventCommand> normalized = core::ludoPageActivationCommands(source, &diagnostics);
                for (const core::LudoCommandParseResult& diagnostic : diagnostics)
                    if (!diagnostic.valid &&
                        (source.type == QLatin1String("ludo.command") || diagnostic.recognized))
                        qWarning().noquote() << "[LudoCommand]" << diagnostic.code << diagnostic.message
                                             << "map=" << d->id << "event=" << ev.id
                                             << "page=" << page << "command=" << ci;
                m_autoLudoCache.insert(signature, normalized);
                cached=m_autoLudoCache.constFind(signature);
            }
            const QVector<EventCommand>& commands=cached.value();
            for(int li=0;li<commands.size();++li){
                const EventCommand& command=commands.at(li);
                const QString key=QStringLiteral("ludoauto:%1:%2:%3:%4").arg(ev.id).arg(page).arg(ci).arg(li);
                activeAutoLudo.insert(key);
                auto ptr=m_parallel.value(key);
                if(!m_autoLudoExecuted.contains(key)){
                    if(!ptr){ptr=std::make_shared<Interpreter>(m_interp.style());configureInterpreter(*ptr);m_parallel.insert(key,ptr);}
                    core::EventExecutionContext context;
                    context.origin=core::EventExecutionOrigin::MapEvent;
                    context.mode=core::EventExecutionMode::OnPageActivated;
                    context.source=key;
                    context.mapId=d->id;
                    context.mapEventId=ev.id;
                    context.pageIndex=page;
                    ptr->setDebugSource(key);ptr->start(QVector<EventCommand>{command},context);ptr->setDebugSource(key);
                    m_autoLudoExecuted.insert(key);
                }
                if(ptr&&ptr->running())desired.insert(key);
            }
        }
    }
    for(const CommonEvent&ce:ed.commonEvents){
        if(ce.trigger!=CommonTrigger::Parallel||ce.commands.isEmpty())continue;
        const QString key=ce.id.isEmpty()?QStringLiteral("common:number:%1").arg(ce.number):QStringLiteral("common:%1").arg(ce.id);auto ptr=m_parallel.value(key);
        const bool ready=commonEventReady(ce);
        // Compatibilidade: o Parallel clássico (Enquanto verdadeiro) é interrompido
        // assim que a condição cai. OnTrue/Interval, por outro lado, representam
        // uma execução já agendada e deixam a chamada atual terminar normalmente.
        if(ptr&&ptr->running()&&(ce.schedulePolicy!=CommonSchedulePolicy::WhileTrue||ready))desired.insert(key);
        if(!ready)continue;
        desired.insert(key);
        if(!ptr){ptr=std::make_shared<Interpreter>(m_interp.style());configureInterpreter(*ptr);ptr->setDebugSource(key);m_parallel.insert(key,ptr);}
        if(!ptr->running()){ptr->setDebugSource(key);ptr->startCommonEvent(ce.number);ptr->setDebugSource(key);consumeCommonEventSchedule(ce);}
    }
    // Ao a página deixar de ser ativa, libera a chave; se ela voltar a ser
    // válida mais tarde, o efeito automático pode disparar novamente.
    for(auto it=m_autoLudoExecuted.begin();it!=m_autoLudoExecuted.end();){if(!activeAutoLudo.contains(*it))it=m_autoLudoExecuted.erase(it);else++it;}
    for(auto it=m_parallel.begin();it!=m_parallel.end();){if(!desired.contains(it.key())){if(it.value())it.value()->stop(CancellationReason::ContextRemoved);it=m_parallel.erase(it);}else ++it;}
}

void GameSession::updateParallelInterpreters(double dt,bool confirmEdge)
{
    Interpreter* receiver=visibleInterpreter();
    // Snapshot: um Interpreter filho pode iniciar outro Parallel aninhado
    // durante update(). Inserir no QMap enquanto o percorremos tornaria a
    // ordem dependente da implementação do container. Filhos novos começam
    // no próximo tick, de forma determinística.
    QVector<QPair<QString,std::shared_ptr<Interpreter>>> snapshot;
    snapshot.reserve(m_parallel.size());
    for(auto it=m_parallel.cbegin();it!=m_parallel.cend();++it)
        snapshot.push_back({it.key(),it.value()});
    for(const auto& entry:std::as_const(snapshot)){
        const auto current=m_parallel.constFind(entry.first);
        const auto& ptr=entry.second;
        if(current==m_parallel.cend()||current.value()!=ptr||!ptr||!ptr->running())continue;
        ptr->update(dt,confirmEdge&&receiver==ptr.get());
    }
}

bool GameSession::reserveCommonEvent(const QString& commonId, const QVariantMap& arguments, int priority)
{
    const CommonEvent* ce = ed.commonEventById(commonId);
    if (!ce || ce->commands.isEmpty()) return false;
    if (m_reservedCommonEvents.size() >= 1024) return false;
    ReservedCommonCall call;
    call.commonId = ce->id;
    call.arguments = arguments;
    call.priority = qBound(-1000, priority, 1000);
    call.serial = ++m_reservedCommonSerial;
    m_reservedCommonEvents.push_back(call); m_eventScheduler.reserve(EventLane::Common, call.commonId, call.priority);
    m_overlayDirty = true;
    return true;
}

bool GameSession::dispatchReservedCommonEvent()
{
    if (m_interp.running() || m_reservedCommonEvents.isEmpty()) return false;
    while (!m_reservedCommonEvents.isEmpty()) {
        int best = 0;
        for (int i = 1; i < m_reservedCommonEvents.size(); ++i) {
            const ReservedCommonCall& a = m_reservedCommonEvents.at(i);
            const ReservedCommonCall& b = m_reservedCommonEvents.at(best);
            if (a.priority > b.priority || (a.priority == b.priority && a.serial < b.serial)) best = i;
        }
        const ReservedCommonCall call = m_reservedCommonEvents.takeAt(best); m_eventScheduler.complete(call.serial);
        const CommonEvent* ce = ed.commonEventById(call.commonId);
        if (!ce || ce->commands.isEmpty()) continue; // alvo removido por hot reload/refatoração
        if (m_interp.startCommonEvent(ce->number, call.arguments)) {
            m_interp.setDebugSource(ce->id.isEmpty() ? QStringLiteral("common:number:%1").arg(ce->number)
                                                     : QStringLiteral("common:%1").arg(ce->id));
            return true;
        }
    }
    return false;
}

void GameSession::updateCutsceneRegions()
{
    const MapDoc* map = ed.doc();
    if (!map) { m_cutsceneRegionInside.clear(); return; }
    const QPoint player = m_world.playerCell();
    QSet<QString> nextInside;
    for (const CutsceneRegion& region : map->cutsceneRegions) {
        const QString key = map->id + QLatin1Char('\n') + region.id;
        if (!region.enabled) continue;
        const bool inside = region.tileArea.contains(player);
        const bool wasInside = m_cutsceneRegionInside.contains(key);
        if (inside) nextInside.insert(key);
        const QString edge = inside && !wasInside && region.triggerOnEnter
            ? QStringLiteral("enter")
            : (!inside && wasInside && region.triggerOnExit ? QStringLiteral("exit") : QString());
        if (edge.isEmpty() || region.commonEventId.isEmpty()) continue;
        QSet<QString>& fired = region.oneShotScope == CutsceneRegionOneShotScope::SaveGame
            ? m_cutsceneRegionPersistentFired : m_cutsceneRegionVisitFired;
        if (region.oneShot && fired.contains(key)) continue;
        const QVariantMap arguments{{QStringLiteral("cutsceneRegionId"), region.id},
                                    {QStringLiteral("cutsceneRegionName"), region.name},
                                    {QStringLiteral("cutsceneRegionEdge"), edge},
                                    {QStringLiteral("mapId"), map->id}};
        if (reserveCommonEvent(region.commonEventId, arguments, 100) && region.oneShot)
            fired.insert(key);
    }
    m_cutsceneRegionInside = nextInside;
}

bool GameSession::commonEventCondition(const CommonEvent& ce) const
{
    if(!ce.advancedTrigger)return m_state.switchOn(ce.switchId);
    if(ce.triggerLeft.isEmpty()||ce.triggerRight.isEmpty())return false;
    const QVariant left=m_interp.resolveSchedulerSource(ce.triggerLeft,ce.triggerValueType);
    const QVariant right=m_interp.resolveSchedulerSource(ce.triggerRight,ce.triggerValueType);
    return compareCommonValues(left,ce.triggerValueType,ce.triggerOp,right);
}

void GameSession::updateCommonEventScheduler()
{
    if(m_commonScheduleTickSerial==m_visualFrameSerial)return;
    m_commonScheduleTickSerial=m_visualFrameSerial;
    QSet<QString> live;
    for(const CommonEvent& ce:ed.commonEvents){
        if(ce.trigger==CommonTrigger::None)continue;
        const QString id=ce.id.isEmpty()?QString::number(ce.number):ce.id;live.insert(id);
        CommonScheduleRuntime& st=m_commonSchedule[id];
        const bool cond=commonEventCondition(ce);st.active=cond;
        if(ce.schedulePolicy==CommonSchedulePolicy::WhileTrue){st.pending=false;st.initialized=true;st.previousCondition=cond;continue;}
        if(ce.schedulePolicy==CommonSchedulePolicy::OnTrue){
            if(!st.initialized){st.initialized=true;if(cond)st.pending=true;}
            else if(cond&&!st.previousCondition)st.pending=true;
            st.previousCondition=cond;continue;
        }
        const int interval=qBound(1,ce.intervalFrames,360000);
        if(!cond){st.pending=false;st.framesUntilRun=0;}
        else if(!st.initialized||!st.previousCondition){st.pending=true;st.framesUntilRun=interval;}
        else if(st.framesUntilRun<=1){st.pending=true;st.framesUntilRun=interval;}
        else --st.framesUntilRun;
        st.initialized=true;st.previousCondition=cond;
    }
    for(auto it=m_commonSchedule.begin();it!=m_commonSchedule.end();){if(!live.contains(it.key()))it=m_commonSchedule.erase(it);else ++it;}
}

bool GameSession::commonEventReady(const CommonEvent& ce) const
{
    const QString id=ce.id.isEmpty()?QString::number(ce.number):ce.id;
    const auto it=m_commonSchedule.constFind(id);if(it==m_commonSchedule.cend())return false;
    return ce.schedulePolicy==CommonSchedulePolicy::WhileTrue?it->active:it->pending;
}

void GameSession::consumeCommonEventSchedule(const CommonEvent& ce)
{
    if(ce.schedulePolicy==CommonSchedulePolicy::WhileTrue)return;
    const QString id=ce.id.isEmpty()?QString::number(ce.number):ce.id;
    auto it=m_commonSchedule.find(id);if(it!=m_commonSchedule.end())it->pending=false;
}

bool GameSession::startAutorunIfNeeded()
{
    if(m_interp.running())return false;
    if(const MapDoc*d=ed.doc())for(const MapEvent&ev:d->events){const int page=m_state.choosePage(ev);if(page>=0){const EventPage&pg=ev.page(page);if(pg.trigger==EventTrigger::Autorun&&!pg.commands.isEmpty()){m_interp.start(ev,page);return true;}}}
    QVector<const CommonEvent*> eligible;
    for(const CommonEvent&ce:ed.commonEvents)if(ce.trigger==CommonTrigger::Autorun&&!ce.commands.isEmpty()&&commonEventReady(ce))eligible.push_back(&ce);
    std::sort(eligible.begin(),eligible.end(),[](const CommonEvent* a,const CommonEvent* b){return a->priority!=b->priority?a->priority>b->priority:a->number<b->number;});
    if(!eligible.isEmpty()){const CommonEvent& ce=*eligible.first();m_interp.startCommonEvent(ce.number);consumeCommonEventSchedule(ce);return true;}
    return false;
}

void GameSession::startTouchEventIfNeeded()
{
    if(m_interp.running())return;
    const QString id=m_world.takeTouchEvent();if(id.isEmpty())return;
    const MapEvent*ev=ed.findEvent(id);if(!ev)return;const int page=m_state.choosePage(*ev);if(page<0)return;
    const EventPage& pg=ev->page(page);const EventTrigger trigger=pg.trigger;
    if((trigger==EventTrigger::PlayerTouch||trigger==EventTrigger::EventTouch)&&!pg.commands.isEmpty()){
        prepareInteractionFacing(*ev,page);
        m_interp.start(*ev,page);
    }
}

void GameSession::applyRememberedEventPositions()
{
    const QString mapId=currentMapId();
    for(auto it=m_rememberedEventPositions.cbegin();it!=m_rememberedEventPositions.cend();++it)
        if(it->mapId==mapId)m_world.restoreEventPosition(it.key().section(QLatin1Char('\n'),1),it->halfCell);
}

bool GameSession::startDirectionalSensorIfNeeded()
{
    if(m_interp.running())return false;
    QSet<QString> inside;const MapEvent* pendingEvent=nullptr;int pendingPage=-1;
    const MapDoc* d=ed.doc();if(!d)return false;const QPoint player=m_world.playerCell();
    for(const MapEvent& ev:d->events){const int page=m_state.choosePage(ev);if(page<0)continue;const EventPage& pg=ev.page(page);
        if(pg.trigger!=EventTrigger::DirectionalSensor||pg.commands.isEmpty())continue;
        const QPoint origin=m_world.eventCell(ev.id);const int dx=player.x()-origin.x(),dy=player.y()-origin.y();
        if(!directionalSensorContains(pg.sensorRanges,dx,dy))continue;
        const QString key=QStringLiteral("%1:%2").arg(ev.id).arg(page);inside.insert(key);
        if(!m_sensorLatched.contains(key)&&!pendingEvent){pendingEvent=&ev;pendingPage=page;}
    }
    m_sensorLatched=inside;if(pendingEvent){m_interp.start(*pendingEvent,pendingPage);return true;}return false;
}


QRect GameSession::letterboxRect(const QSize& logica, const QSize& janela)
{
    if (logica.width() <= 0 || logica.height() <= 0 ||
        janela.width() <= 0 || janela.height() <= 0)
        return QRect(QPoint(0, 0), janela);

    // Escala contínua: maximizar realmente amplia o jogo até uma das bordas,
    // em vez de ficar preso em 1× enquanto não houver espaço para 2×.
    const double fator = qMin(double(janela.width()) / logica.width(),
                              double(janela.height()) / logica.height());
    const int w = qMax(1, qRound(logica.width() * fator));
    const int h = qMax(1, qRound(logica.height() * fator));
    return QRect((janela.width() - w) / 2, (janela.height() - h) / 2, w, h);
}

void GameSession::refreshPlayerPreferences()
{
    QSettings settings;
    const int defaultTextSpeed = ed.accessibility.enabled ? ed.accessibility.defaultTextSpeedPercent : 100;
    const int textSpeed = qBound(50, settings.value(QStringLiteral("game/textSpeedPercent"), defaultTextSpeed).toInt(), 200);
    const QString scaleFilter = settings.value(QStringLiteral("game/scaleFilter"), QStringLiteral("nearest")).toString();
    m_smoothScaleFilter = scaleFilter == QLatin1String("bilinear") || scaleFilter == QLatin1String("bicubic");
    m_uiScalePercent = ed.accessibility.enabled && ed.accessibility.allowUiScale
        ? qBound(100, settings.value(QStringLiteral("game/uiScalePercent"), ed.accessibility.defaultUiScalePercent).toInt(), 150)
        : 100;
    m_strongFocus = ed.accessibility.enabled && settings.value(QStringLiteral("game/strongFocus"), ed.accessibility.strongFocusDefault).toBool();
    const bool reducedMotion = ed.accessibility.enabled && ed.accessibility.allowReducedMotion;
    m_reduceShake = reducedMotion && settings.value(QStringLiteral("game/reduceShake"), ed.accessibility.reduceShakeDefault).toBool();
    m_reduceFlash = reducedMotion && settings.value(QStringLiteral("game/reduceFlash"), ed.accessibility.reduceFlashDefault).toBool();
    m_world.setReduceShake(m_reduceShake);
    m_gameUi.setReducedMotion(m_reduceShake, m_reduceFlash);
    const ui::UiTheme theme = accessibleUiTheme(ed, m_uiScalePercent, m_strongFocus);
    m_gameUi.setTheme(theme);
    MessageStyle style = m_interp.style();
    if (!theme.fontFamily.trimmed().isEmpty()) style.font.setFamily(theme.fontFamily.trimmed());
    style.font.setPixelSize(qMax(6, theme.fontSize));
    style.innerWidth = qMax(80, m_viewW - 24 - 2 * qMax(0, theme.paddingX));
    style.charsPerSecond = 45.0 * (textSpeed / 100.0);
    m_interp.setStyle(style);
    for (auto it = m_parallel.begin(); it != m_parallel.end(); ++it)
        if (it.value()) it.value()->setStyle(style);
}

QString GameSession::playerLocale() const
{
    return core::currentPlayerLocale(ed.localization);
}

QString GameSession::setPlayerLocale(const QString& requestedLocale)
{
    const QString resolved = core::setPlayerLocale(ed.localization, requestedLocale);
    if (!resolved.isEmpty()) refreshLocalizationState();
    return resolved;
}

void GameSession::refreshLocalizationState()
{
    m_uiMenu.refreshLocalization();
    m_uiBattle.refreshLocalization();
    m_uiShop.refreshLocalization();
    m_overlayDirty = true;
}

void GameSession::resize(int w, int h)
{
    m_viewW = qMax(1, w);
    m_viewH = qMax(1, h);
    // A quebra de linha depende da largura da janela: refazer o estilo aqui
    // evita texto vazando quando o jogador redimensiona.
    MessageStyle ms = m_interp.style();
    const ui::UiTheme theme = accessibleUiTheme(ed, m_uiScalePercent, m_strongFocus);
    m_gameUi.setTheme(theme);
    if (!theme.fontFamily.trimmed().isEmpty()) ms.font.setFamily(theme.fontFamily.trimmed());
    ms.font.setPixelSize(qMax(6, theme.fontSize));
    ms.innerWidth = qMax(80, m_viewW - 2 * 12 - 2 * qMax(0, theme.paddingX));
    m_interp.setStyle(ms);
    for(auto it=m_parallel.begin();it!=m_parallel.end();++it)if(it.value())it.value()->setStyle(ms);
    m_subs.setScreenWidth(m_viewW);
}


void GameSession::tick()
{
    RuntimeTraceScope tickTrace(&m_trace, QStringLiteral("GameSession.tick"));
    struct PictureTickSync { PictureManager& manager; ~PictureTickSync(){manager.refreshBindings();} } pictureTickSync{m_pics};
    ++m_visualFrameSerial;
    if (m_visualFrameSerial == 0) m_visualFrameSerial = 1;
    updateInputFrameState();
    const qint64 now = m_clock.nsecsElapsed();
    double dt = m_forcedDeltaSeconds >= 0.0 ? m_forcedDeltaSeconds : double(now - m_lastNs) / 1e9;
    m_lastNs = now;
    if (dt > 0.1) dt = 0.1;              // evita saltos após travadas
    m_playTimeSeconds+=qMax(0.0,dt);
    m_bubbles.update(dt);
    // O relógio mede apresentações reais e nunca transforma um intervalo quase
    // zero (troca de filtro/recriação de textura) em 2000 FPS no HUD.
    if (dt >= 1.0/500.0) {const double sample=qBound(1.0,1.0/dt,60.0);m_fps=m_fps<=0?sample:m_fps*.9+sample*.1;}
    int activeInterpreters = m_interp.running() ? 1 : 0;
    for (auto it=m_parallel.cbegin(); it!=m_parallel.cend(); ++it) if (it.value() && it.value()->running()) ++activeInterpreters;
    m_profiler.setRuntimeCounts(activeInterpreters, int(m_debugTrace.size()));

    // O estilo das legendas é relido a cada quadro: assim dá para deixar o
    // jogo aberto, mexer em "Configurar legendas…" e ver o efeito na hora, em
    // vez de fechar e apertar F5 de novo. É uma cópia de struct pequena.
    if (!(m_subsStyleCache == ed.subtitleStyle.toJson())) {
        m_subsStyleCache = ed.subtitleStyle.toJson();
        m_subs.setStyle(ed.subtitleStyle);
        QFont fs = m_font;
        if(!ed.subtitleStyle.fontFamily.isEmpty())fs.setFamily(ed.subtitleStyle.fontFamily);
        fs.setPixelSize(qMax(6, ed.subtitleStyle.fontSize));
        m_subs.setFont(fs);
    }

    // Borda da tecla de confirmar: a mensagem não pode passar voando só porque
    // o jogador segurou a tecla — e um toque rápido não pode se perder.
    const bool confirmEdge = m_confirmPending;
    m_confirmPending = false;

    if (m_gameOver) {
        updateOverlayDirty(dt);
        return;
    }
    // As imagens andam SEMPRE: um tween precisa continuar durante a cutscene
    // (é justamente nela que ele existe) e a física não pode congelar só
    // porque abriu uma caixa de mensagem.
    m_pics.update(dt);
    m_fogs.update(dt);
    updateLudo(dt);
    restoreInteractionFacingIfNeeded();
    updateDebugWatches();
    if (m_debugWatches.consumeBreakRequest()) m_eventDebugger.pause();
    // Bloco E: Pause congela a lógica do playtest na fronteira atual. O frame
    // continua sendo apresentado para que F8/diagnóstico permaneçam utilizáveis.
    if (m_eventDebugger.paused() && !m_eventDebugger.stepPending()) {
        m_world.update(0.0, 0, 0);
        m_subs.update(0.0);
        updateOverlayDirty(dt);
        return;
    }
    if(m_mapTransition.active){updateMapTransition(dt);m_subs.update(dt);updateOverlayDirty(dt);return;}

    // Janelas in-game da Fase 2 congelam o fluxo do evento e o movimento, mas
    // não congelam Pictures/Fog/efeitos visuais. Nenhum event loop de QDialog é
    // necessário: a sessão continua recebendo quadros normalmente.
    m_uiModal.update(dt);
    if (m_uiModal.completed()) finishUiModal();
    if (m_uiModal.active()) {
        m_world.update(dt, 0, 0);
        m_subs.update(dt);
        updateOverlayDirty(dt);
        return;
    }

    // Menu grande da Fase 3 também vive dentro do loop normal: animações,
    // Pictures, Fog e áudio continuam, mas o mundo e os eventos ficam pausados.
    m_uiMenu.update(dt);
    if (m_uiMenu.customActive()) {
        // A tela livre captura o input e pausa o controle do mapa, mas não
        // paralisa cegamente o Interpreter: isso permite o fluxo No-Code
        // Abrir UI -> Esperar/alterar estado -> Fechar UI. Se o comando foi
        // marcado como modal (wait), o próprio Interpreter fica aguardando
        // enquanto customActive() for verdadeiro.
        m_world.update(dt, 0, 0);
        updateCommonEventScheduler();
        syncParallelInterpreters();
        updateParallelInterpreters(dt, false);
        dispatchReservedCommonEvent();
        if (!processPendingRuntimeActions() && m_interp.running()) {
            m_interp.update(dt, false);
            processPendingRuntimeActions();
        }
        m_subs.update(dt);
        updateOverlayDirty(dt);
        return;
    }
    if (m_uiMenu.active()) {
        m_world.update(dt, 0, 0);
        m_subs.update(dt);
        updateOverlayDirty(dt);
        return;
    }

    // Batalha e Loja vivem no loop normal. A batalha também atualiza a Timeline
    // de animação No-Code: o hit frame, partículas/flash/shake e cues de SE
    // avançam com o mesmo relógio em CPU e QRhi.
    if (m_uiBattle.active()) {
        m_uiBattle.update(dt);
        for (const BattleSoundCue& cue : m_uiBattle.takeSoundEffects()) {
            if (!m_audio || cue.path.isEmpty()) continue;
            const QString path = QFileInfo(cue.path).isAbsolute()
                ? cue.path : QDir(ed.projectRoot()).filePath(cue.path);
            m_audio(path, qBound(0, cue.volume, 100));
        }
    }
    if (m_uiBattle.active() || m_uiShop.active()) {
        m_world.update(dt, 0, 0);
        m_subs.update(dt);
        updateOverlayDirty(dt);
        return;
    }
    if (m_randomEncounterBattle && m_uiBattle.finished()) {
        const BattleResult result = m_uiBattle.takeResult();
        m_randomEncounterBattle = false;
        if (result == BattleResult::Defeat) m_gameOver = true;
        m_overlayDirty = true;
        if (m_gameOver) { updateOverlayDirty(dt); return; }
    }

    // Regiões espaciais apenas reservam chamadas; a fila canônica decide a
    // ordem junto dos demais Eventos Comuns.
    updateCutsceneRegions();

    // Paralelos são sincronizados e avançados sempre, inclusive durante uma
    // mensagem/autorun principal. Só o interpretador cuja mensagem está
    // visível recebe a borda de confirmação.
    updateCommonEventScheduler();
    syncParallelInterpreters();
    Interpreter* confirmTarget=visibleInterpreter();
    updateParallelInterpreters(dt,confirmEdge);
    if (processPendingRuntimeActions()) {
        m_subs.update(dt); updateOverlayDirty(dt); return;
    }
    const bool reservedDispatched=dispatchReservedCommonEvent();
    const bool sensorDispatched=!reservedDispatched&&startDirectionalSensorIfNeeded();
    const bool autorunDispatched=!reservedDispatched&&!sensorDispatched&&startAutorunIfNeeded();
    if (processPendingRuntimeActions()) {
        m_subs.update(dt); updateOverlayDirty(dt); return;
    }

    if (m_interp.running()) {
        // Autorun, toque e tecla usam o canal principal e congelam o jogador.
        // Eventos paralelos acima continuam vivos.
        m_world.update(dt, 0, 0);
        m_interp.update(dt, confirmEdge&&confirmTarget==&m_interp);
        if (processPendingRuntimeActions()) {
            m_subs.update(dt); updateOverlayDirty(dt); return;
        }
        m_subs.update(dt);
        updateOverlayDirty(dt);
        return;
    }
    if(reservedDispatched||sensorDispatched||autorunDispatched){m_world.update(dt,0,0);m_subs.update(dt);updateOverlayDirty(dt);return;}
    m_subs.update(dt);
    // Uma legenda esperando confirmação consome a tecla ANTES de virar
    // conversa com NPC: senão o mesmo toque avançaria a legenda e abriria
    // um diálogo atrás dela.
    bool confirmSobrou = confirmEdge && confirmTarget==&m_interp;
    if (confirmSobrou && m_subs.confirm()) confirmSobrou = false;
    // Legenda com "congelar mapa" trava o personagem, como no plugin.
    if (m_subs.freezesMap()) {
        m_world.update(dt, 0, 0);
        updateOverlayDirty(dt);
        return;
    }
    if (confirmSobrou) tryInteract();
    if (processPendingRuntimeActions()) { updateOverlayDirty(dt); return; }
    if (m_interp.running()) { updateOverlayDirty(dt); return; }

    int dx = 0, dy = 0;
    if (held(GameAction::Left))  dx -= 1;
    if (held(GameAction::Right)) dx += 1;
    if (held(GameAction::Up))    dy -= 1;
    if (held(GameAction::Down))  dy += 1;

    m_world.update(dt, dx, dy);
    startTouchEventIfNeeded();
    if(!m_interp.running())startDirectionalSensorIfNeeded();
    if (processPendingRuntimeActions()) { updateOverlayDirty(dt); return; }
    if (!m_interp.running()) maybeRandomEncounter();
    updateOverlayDirty(dt);
}

void GameSession::maybeRandomEncounter()
{
    const MapDoc* map = ed.doc();
    if (!map || map->encounterSteps <= 0 || map->encounterTroopIds.isEmpty() || m_uiBattle.active() || m_randomEncounterBattle) return;
    if (m_nextEncounterSteps <= 0) m_nextEncounterSteps = qMax(1, map->encounterSteps);
    if (m_world.stepsTaken() - m_stepsAtLastEncounter < m_nextEncounterSteps) return;
    QVector<QString> valid; QVector<int> weights; int totalWeight=0;
    for(const QString& troopId:map->encounterTroopIds){const auto category=ed.database.constFind(QStringLiteral("troops"));if(category==ed.database.cend())break;for(const DatabaseRecord& troop:category.value())if(troop.id==troopId){const int w=qMax(1,troop.data.value(QStringLiteral("encounterWeight"),10).toInt());valid.push_back(troopId);weights.push_back(w);totalWeight+=w;break;}}
    if(valid.isEmpty())return;int roll=randomBounded(totalWeight),selected=0;for(int i=0;i<weights.size();++i){roll-=weights[i];if(roll<0){selected=i;break;}}
    m_keys.clear();
    m_stepsAtLastEncounter=m_world.stepsTaken();const int base=qMax(1,map->encounterSteps);m_nextEncounterSteps=qMax(1,base*(75+randomBounded(51))/100);
    if(m_battle){const int result=m_battle(valid[selected],true);if(result==int(BattleResult::Defeat))m_gameOver=true;}
    else {QString error;if(m_uiBattle.open(valid[selected],true,&error)){m_randomEncounterBattle=true;m_keys.clear();m_actions.clear();}else if(m_debugPresentation){m_hint=error;m_hintUntil=m_clock.elapsed()+2500;}}
    m_overlayDirty=true;
}

bool GameSession::held(GameAction a) const
{
    if (m_actions.contains(a)) return true;
    for (int k : ed.inputMap.keysFor(a))
        if (m_keys.contains(k)) return true;
    return false;
}

bool GameSession::inputPressed(GameAction a) const
{
    return m_actionPressedFrame.value(int(a),0)==m_visualFrameSerial;
}

bool GameSession::inputReleased(GameAction a) const
{
    return m_actionReleasedFrame.value(int(a),0)==m_visualFrameSerial;
}

int GameSession::inputHoldFrames(GameAction a) const
{
    return m_actionHoldFrames.value(int(a),0);
}

bool GameSession::inputActionMatches(GameAction a, const QString& state) const
{
    if(state==QLatin1String("pressed"))return inputPressed(a);
    if(state==QLatin1String("released"))return inputReleased(a);
    return held(a);
}

void GameSession::updateInputFrameState()
{
    for(GameAction action:allGameActions()){
        const int key=int(action);
        if(held(action))m_actionHoldFrames[key]=qMin(999999999,m_actionHoldFrames.value(key)+1);
        else m_actionHoldFrames.remove(key);
    }
}

void GameSession::setGamepadAnalog(bool connected,double x,double y,double leftTrigger,double rightTrigger)
{
    m_gamepadConnected=connected;
    m_gamepadAxisX=qBound(-1.0,x,1.0);m_gamepadAxisY=qBound(-1.0,y,1.0);
    m_gamepadLeftTrigger=qBound(0.0,leftTrigger,1.0);m_gamepadRightTrigger=qBound(0.0,rightTrigger,1.0);
}

void GameSession::prepareInteractionFacing(const MapEvent& event, int page)
{
    if (page < 0 || page >= event.pages.size()) return;
    const EventPage& pg = event.page(page);

    // O jogador mantém a compatibilidade histórica separada da opção
    // "Voltar à posição inicial" do evento.
    m_facingBeforeInteraction = m_world.facing();
    const QPoint delta = event.cell - m_world.playerCell();
    if (!delta.isNull())
        m_world.setFacing(qAbs(delta.x()) > qAbs(delta.y())
            ? (delta.x() < 0 ? Dir::Left : Dir::Right)
            : (delta.y() < 0 ? Dir::Up : Dir::Down));
    m_restoreFacingAfterInteraction = !pg.keepPlayerFacingAfterInteract;

    m_restoreEventFacingAfterInteraction = false;
    m_interactionEventId.clear();
    m_interactionMapId.clear();
    if (pg.restoreEventFacingAfterInteract) {
        int direction = 0, frame = 0;
        if (m_world.eventFacing(event.id, &direction, &frame)) {
            m_restoreEventFacingAfterInteraction = true;
            m_interactionEventId = event.id;
            m_interactionMapId = currentMapId();
            m_eventDirectionBeforeInteraction = direction;
            m_eventFrameBeforeInteraction = frame;
            m_eventHalfCellBeforeInteraction = m_world.eventHalfCell(event.id);
        }
    }

    // Direção Fixa é verificada dentro do World. Se estiver ativa, o evento
    // nem vira; o snapshot continua seguro e será restaurado sem efeito colateral.
    m_world.faceEventTowardPlayer(event.id);
}

void GameSession::restoreInteractionFacingIfNeeded()
{
    if (m_interp.running()) return;
    if (m_restoreFacingAfterInteraction) {
        m_world.setFacing(m_facingBeforeInteraction);
        m_restoreFacingAfterInteraction = false;
    }
    if (m_restoreEventFacingAfterInteraction) {
        if (!m_interactionEventId.isEmpty() && m_interactionMapId == currentMapId()) {
            // RC2.68: a opção agora cumpre literalmente "Voltar à posição inicial".
            // Primeiro estabilizamos a posição/rota, depois restauramos a pose.
            m_world.restoreEventPosition(m_interactionEventId, m_eventHalfCellBeforeInteraction);
            m_world.restoreEventFacing(m_interactionEventId, m_eventDirectionBeforeInteraction,
                                       m_eventFrameBeforeInteraction);
        }
        m_restoreEventFacingAfterInteraction = false;
        m_interactionEventId.clear();
        m_interactionMapId.clear();
    }
}

void GameSession::tryInteract()
{
    if (const MapEvent* ev = m_world.eventInFront()) {
        // Qual página vale agora? A última cujas condições batem — é o que
        // faz o NPC mudar de fala depois que a história avança.
        const int pagina = m_state.choosePage(*ev);
        if (pagina >= 0 && !ev->page(pagina).commands.isEmpty()) {
            const EventPage& pg=ev->page(pagina);
            prepareInteractionFacing(*ev,pagina);
            m_interp.start(*ev, pagina);
            return;
        }
    }
    // Sem evento à frente (ou evento sem comandos): nada acontece — e isso é
    // dito no HUD para não parecer que a tecla não funciona.
    if (m_debugPresentation) {
        m_hint = QCoreApplication::translate("GameSession", "nada para interagir aqui");
        m_hintUntil = m_clock.elapsed() + 1200;
    }
}

void GameSession::playUiSound(const QString& relativePath)
{
    if (!m_audio || relativePath.trimmed().isEmpty() || ed.gameUi.soundVolume <= 0) return;
    const QString path = QFileInfo(relativePath).isAbsolute()
        ? relativePath : QDir(ed.projectRoot()).filePath(relativePath);
    m_audio(path, qBound(0, ed.gameUi.soundVolume, 100));
}

void GameSession::openGameMenu(bool standalone)
{
    if (!canOpenMenu()) return;
    m_keys.clear();
    m_actions.clear();
    m_uiMenu.open(standalone);
    playUiSound(ed.gameUi.confirmSePath);
    m_overlayDirty = true;
}

bool GameSession::handleMenuAction(GameAction action)
{
    if (!m_uiMenu.active()) return false;
    const bool handled = m_uiMenu.handleAction(action);
    if (!handled) return false;
    if (action == GameAction::Confirm) playUiSound(ed.gameUi.confirmSePath);
    else if (action == GameAction::Cancel || action == GameAction::Quit) playUiSound(ed.gameUi.cancelSePath);
    else if (action == GameAction::Up || action == GameAction::Down ||
             action == GameAction::Left || action == GameAction::Right)
        playUiSound(ed.gameUi.cursorSePath);
    if (m_uiMenu.takeReturnToTitleRequest()) m_wantClose = true;
    m_overlayDirty = true;
    return true;
}

bool GameSession::handleUiAction(GameAction action)
{
    if (!m_uiModal.active()) return false;
    // Durante a animação de fechamento a janela ainda ocupa o foco; ignorar
    // novos comandos evita confirmar duas vezes no mesmo modal.
    if (!m_uiModal.acceptingInput()) return true;

    const int before = m_uiModal.type() == ui::UiModalType::NumberInput
        ? m_uiModal.numberValue() : m_uiModal.selected();
    if (action == GameAction::Up || action == GameAction::Left) {
        if (m_uiModal.type() == ui::UiModalType::NumberInput)
            m_uiModal.adjustNumber(-1);
        else
            m_uiModal.move(-1);
        const int after = m_uiModal.type() == ui::UiModalType::NumberInput
            ? m_uiModal.numberValue() : m_uiModal.selected();
        if (after != before) playUiSound(ed.gameUi.cursorSePath);
    } else if (action == GameAction::Down || action == GameAction::Right) {
        if (m_uiModal.type() == ui::UiModalType::NumberInput)
            m_uiModal.adjustNumber(1);
        else
            m_uiModal.move(1);
        const int after = m_uiModal.type() == ui::UiModalType::NumberInput
            ? m_uiModal.numberValue() : m_uiModal.selected();
        if (after != before) playUiSound(ed.gameUi.cursorSePath);
    } else if (action == GameAction::Confirm) {
        playUiSound(ed.gameUi.confirmSePath);
        m_uiModal.confirm();
    } else if (action == GameAction::Cancel || action == GameAction::Quit) {
        playUiSound(ed.gameUi.cancelSePath);
        m_uiModal.cancel();
    }
    m_overlayDirty = true;
    return true;
}


bool GameSession::handleBattleAction(GameAction action)
{
    if (!m_uiBattle.active()) return false;
    const int before = m_uiBattle.selected();
    const bool handled = m_uiBattle.handleAction(action);
    if (!handled) return false;
    if (action == GameAction::Confirm) playUiSound(ed.gameUi.confirmSePath);
    else if (action == GameAction::Cancel || action == GameAction::Quit) playUiSound(ed.gameUi.cancelSePath);
    else if ((action == GameAction::Up || action == GameAction::Down || action == GameAction::Left || action == GameAction::Right) && before != m_uiBattle.selected()) playUiSound(ed.gameUi.cursorSePath);
    m_overlayDirty = true;
    return true;
}

bool GameSession::handleShopAction(GameAction action)
{
    if (!m_uiShop.active()) return false;
    const int before = m_uiShop.selected();
    const int quantity = m_uiShop.quantity();
    const bool handled = m_uiShop.handleAction(action);
    if (!handled) return false;
    if (action == GameAction::Confirm) playUiSound(ed.gameUi.confirmSePath);
    else if (action == GameAction::Cancel || action == GameAction::Quit) playUiSound(ed.gameUi.cancelSePath);
    else if ((action == GameAction::Up || action == GameAction::Down || action == GameAction::Left || action == GameAction::Right) && (before != m_uiShop.selected() || quantity != m_uiShop.quantity())) playUiSound(ed.gameUi.cursorSePath);
    m_overlayDirty = true;
    return true;
}

void GameSession::keyPress(int k, bool autoRepeat, const QString& text)
{
    if(!autoRepeat){
        for(GameAction action:allGameActions())if(ed.inputMap.matches(action,k)&&!held(action))
            m_actionPressedFrame[int(action)]=m_visualFrameSerial+1;
        m_keys.insert(k);
    }
    const core::InputMap& im = ed.inputMap;
    if (m_uiBattle.active()) {
        if (!autoRepeat) for (GameAction action : {GameAction::Up,GameAction::Down,GameAction::Left,GameAction::Right,GameAction::Confirm,GameAction::Cancel,GameAction::Quit}) if (im.matches(action,k)) { handleBattleAction(action); return; }
        return;
    }
    if (m_uiShop.active()) {
        if (!autoRepeat) for (GameAction action : {GameAction::Up,GameAction::Down,GameAction::Left,GameAction::Right,GameAction::Confirm,GameAction::Cancel,GameAction::Quit}) if (im.matches(action,k)) { handleShopAction(action); return; }
        return;
    }
    if (m_uiMenu.customActive()) {
        if(!autoRepeat && m_uiMenu.handleTextKey(k,text)){m_overlayDirty=true;return;}
        if (!autoRepeat) {
            for (GameAction action : {GameAction::Up, GameAction::Down, GameAction::Left,
                                      GameAction::Right, GameAction::Confirm, GameAction::Cancel,
                                      GameAction::Quit}) {
                if (im.matches(action, k)) {
                    const QString before = m_uiMenu.focusedElementId();
                    m_uiMenu.handleCustomAction(action);
                    if (action == GameAction::Confirm) playUiSound(ed.gameUi.confirmSePath);
                    else if (action == GameAction::Cancel || action == GameAction::Quit) playUiSound(ed.gameUi.cancelSePath);
                    else if (before != m_uiMenu.focusedElementId()) playUiSound(ed.gameUi.cursorSePath);
                    m_overlayDirty = true;
                    return;
                }
            }
        }
        return;
    }
    if (m_uiMenu.active()) {
        if(!autoRepeat && m_uiMenu.handleTextKey(k,text)){m_overlayDirty=true;return;}
        if (!autoRepeat) {
            for (GameAction action : {GameAction::Up, GameAction::Down, GameAction::Left,
                                      GameAction::Right, GameAction::Confirm, GameAction::Cancel,
                                      GameAction::Quit}) {
                if (im.matches(action, k)) { handleMenuAction(action); return; }
            }
        }
        return;
    }
    if (m_uiModal.active()) {
        if (!autoRepeat && m_uiModal.type() == ui::UiModalType::TextInput && m_uiModal.acceptingInput()) {
            if (k == Qt::Key_Backspace || k == Qt::Key_Delete) {
                if (m_uiModal.backspaceText()) m_overlayDirty = true;
                return;
            }
            if (!text.isEmpty() && k != Qt::Key_Return && k != Qt::Key_Enter && k != Qt::Key_Escape) {
                if (m_uiModal.appendText(text)) m_overlayDirty = true;
                return;
            }
        }
        if (!autoRepeat) {
            for (GameAction action : {GameAction::Up, GameAction::Down, GameAction::Left,
                                      GameAction::Right, GameAction::Confirm, GameAction::Cancel,
                                      GameAction::Quit}) {
                if (im.matches(action, k)) { handleUiAction(action); return; }
            }
        }
        return;
    }
    if (m_gameOver) {
        if (!autoRepeat && (im.matches(GameAction::Confirm, k) ||
                            im.matches(GameAction::Cancel, k) ||
                            im.matches(GameAction::Quit, k)))
            m_wantClose = true;
        return;
    }
    if (!autoRepeat && ed.quickSaveEnabled && im.matches(GameAction::QuickSave, k) && canOpenMenu()) {
        QString error;
        const bool ok = saveGame(ed.quickSaveSlot, &error);
        m_hint = ok ? QCoreApplication::translate("GameSession", "Save rápido gravado no slot %1.").arg(ed.quickSaveSlot) : error;
        m_hintUntil = m_clock.elapsed() + 2500;
        m_overlayDirty = true;
        return;
    }
    if (!autoRepeat && ed.quickSaveEnabled && im.matches(GameAction::QuickLoad, k) && canOpenMenu()) {
        QString error;
        const bool ok = loadGame(ed.quickSaveSlot, &error);
        m_hint = ok ? QCoreApplication::translate("GameSession", "Save rápido carregado.") : error;
        m_hintUntil = m_clock.elapsed() + 2500;
        m_overlayDirty = true;
        return;
    }
    if(im.matches(cutsceneSkipAction(),k)&&!autoRepeat&&canSkipCutscene()){m_skipHeld=true;m_skipHeldSec=0;if(!ed.cutsceneSkip.holdToSkip)m_skipTriggered=true;return;}
    if (anyInterpreterRunning()) {
        Interpreter* input = visibleInterpreter();
        if (input && input->choice().visible) {
            for (GameAction action : {GameAction::Up, GameAction::Down, GameAction::Left, GameAction::Right}) {
                if (!im.matches(action, k)) continue;
                const int delta = choiceNavigationDelta(input->choice(), action);
                if (delta != 0) {
                    const int before = input->choice().selected;
                    input->moveChoice(delta);
                    if (input->choice().selected != before) playUiSound(ed.gameUi.cursorSePath);
                    m_overlayDirty = true;
                }
                return;
            }
            if (!autoRepeat && im.matches(GameAction::Cancel, k)) { playUiSound(ed.gameUi.cancelSePath); input->cancelChoice(); m_overlayDirty=true; return; }
            if (!autoRepeat && im.matches(GameAction::Confirm, k)) {
                if (!input->choice().disabled.value(input->choice().selected, false)) {
                    playUiSound(ed.gameUi.confirmSePath);
                    m_confirmPending=true;
                }
                return;
            }
        }
    }
    // Sair só quando não há mensagem aberta: senão Esc fecharia o jogo no
    // meio de uma fala, o que ninguém espera.
    if (im.matches(GameAction::Quit, k) && !anyInterpreterRunning()) { m_wantClose = true; return; }
    if (im.matches(GameAction::Cancel, k) && anyInterpreterRunning()) {
        // Cancelar durante a mensagem = avançar (mesmo efeito do confirmar).
        if (!autoRepeat) playUiSound(ed.gameUi.cancelSePath);
        m_confirmPending = true;
        return;
    }
    if (im.matches(GameAction::Confirm, k) && !autoRepeat) {
        if (anyInterpreterRunning()) playUiSound(ed.gameUi.confirmSePath);
        m_confirmPending = true;
    }
    if (im.matches(GameAction::ToggleHud, k)) { if (m_debugPresentation) m_showHud = !m_showHud; m_overlayDirty=true; return; }
    if (im.matches(GameAction::ZoomIn, k))  { m_zoom = qMin(6.0, m_zoom + 1.0);m_overlayDirty=true; return; }
    if (im.matches(GameAction::ZoomOut, k)) { m_zoom = qMax(1.0, m_zoom - 1.0);m_overlayDirty=true; return; }
    if (!autoRepeat) m_keys.insert(k);
}

void GameSession::keyRelease(int k, bool autoRepeat)
{
    if(!autoRepeat&&(ed.inputMap.matches(cutsceneSkipAction(),k)||ed.inputMap.matches(GameAction::SkipCutscene,k))){m_skipHeld=false;m_skipHeldSec=0;}
    if (!autoRepeat) {
        m_keys.remove(k);
        for(GameAction action:allGameActions())if(ed.inputMap.matches(action,k)&&!held(action))
            m_actionReleasedFrame[int(action)]=m_visualFrameSerial+1;
    }
}

void GameSession::actionPress(GameAction action)
{
    if (m_runtimeClock.deterministic()) m_replay.record(m_visualFrameSerial, core::gameActionId(action), true);
    if(!held(action))m_actionPressedFrame[int(action)]=m_visualFrameSerial+1;
    m_actions.insert(action);
    if (m_uiBattle.active()) { handleBattleAction(action); return; }
    if (m_uiShop.active()) { handleShopAction(action); return; }
    if (m_uiMenu.customActive()) {
        const QString before = m_uiMenu.focusedElementId();
        m_uiMenu.handleCustomAction(action);
        if (action == GameAction::Confirm) playUiSound(ed.gameUi.confirmSePath);
        else if (action == GameAction::Cancel || action == GameAction::Quit) playUiSound(ed.gameUi.cancelSePath);
        else if (before != m_uiMenu.focusedElementId()) playUiSound(ed.gameUi.cursorSePath);
        m_overlayDirty = true;
        return;
    }
    if (m_uiMenu.active()) { handleMenuAction(action); return; }
    if (m_uiModal.active()) { handleUiAction(action); return; }
    if (m_gameOver) {
        if (action == GameAction::Confirm || action == GameAction::Cancel || action == GameAction::Quit)
            m_wantClose = true;
        return;
    }
    if (ed.quickSaveEnabled && action == GameAction::QuickSave && canOpenMenu()) {
        QString error;const bool ok=saveGame(ed.quickSaveSlot,&error);
        m_hint=ok?QCoreApplication::translate("GameSession","Save rápido gravado no slot %1.").arg(ed.quickSaveSlot):error;
        m_hintUntil=m_clock.elapsed()+2500;m_overlayDirty=true;return;
    }
    if (ed.quickSaveEnabled && action == GameAction::QuickLoad && canOpenMenu()) {
        QString error;const bool ok=loadGame(ed.quickSaveSlot,&error);
        m_hint=ok?QCoreApplication::translate("GameSession","Save rápido carregado."):error;
        m_hintUntil=m_clock.elapsed()+2500;m_overlayDirty=true;return;
    }
    if(action==cutsceneSkipAction()&&canSkipCutscene()){m_skipHeld=true;m_skipHeldSec=0;if(!ed.cutsceneSkip.holdToSkip)m_skipTriggered=true;return;}
    if(anyInterpreterRunning()){
        Interpreter* input=visibleInterpreter();
        if(input&&input->choice().visible){
            const int delta = choiceNavigationDelta(input->choice(), action);
            if(delta!=0){const int before=input->choice().selected;input->moveChoice(delta);if(input->choice().selected!=before)playUiSound(ed.gameUi.cursorSePath);m_overlayDirty=true;return;}
            if(action==GameAction::Cancel){playUiSound(ed.gameUi.cancelSePath);input->cancelChoice();m_overlayDirty=true;return;}
            if(action==GameAction::Confirm){if(!input->choice().disabled.value(input->choice().selected,false)){playUiSound(ed.gameUi.confirmSePath);m_confirmPending=true;}return;}
        }
    }
    if(action==GameAction::Quit&&!anyInterpreterRunning()){m_wantClose=true;return;}
    if(action==GameAction::Cancel&&anyInterpreterRunning()){playUiSound(ed.gameUi.cancelSePath);m_confirmPending=true;return;}
    if(action==GameAction::Confirm){if(anyInterpreterRunning())playUiSound(ed.gameUi.confirmSePath);m_confirmPending=true;}
    if(action==GameAction::ToggleHud){if(m_debugPresentation)m_showHud=!m_showHud;m_overlayDirty=true;return;}
    if(action==GameAction::ZoomIn){m_zoom=qMin(6.0,m_zoom+1.0);m_overlayDirty=true;return;}
    if(action==GameAction::ZoomOut){m_zoom=qMax(1.0,m_zoom-1.0);m_overlayDirty=true;return;}
    m_actions.insert(action);
}

void GameSession::actionRelease(GameAction action)
{
    if (m_runtimeClock.deterministic()) m_replay.record(m_visualFrameSerial, core::gameActionId(action), false);
    if(action==cutsceneSkipAction()||action==GameAction::SkipCutscene){m_skipHeld=false;m_skipHeldSec=0;}
    m_actions.remove(action);
    if(!held(action))m_actionReleasedFrame[int(action)]=m_visualFrameSerial+1;
}

void GameSession::mousePress(const QPointF& logicalPosition, Qt::MouseButton button)
{
    if(button==Qt::LeftButton){if(!m_mouseButtons.testFlag(Qt::LeftButton))m_mouseLeftPressedFrame=m_visualFrameSerial+1;m_mouseButtons|=Qt::LeftButton;}
    if(button==Qt::RightButton){if(!m_mouseButtons.testFlag(Qt::RightButton))m_mouseRightPressedFrame=m_visualFrameSerial+1;m_mouseButtons|=Qt::RightButton;}
    m_mouseDelta = logicalPosition - m_mouseLogical;
    m_mouseLogical = logicalPosition;
    if(button!=Qt::LeftButton&&button!=Qt::RightButton)return;
    if(m_gameOver){m_wantClose=true;return;}
    if (m_uiBattle.active()) {
        if (button == Qt::RightButton) { handleBattleAction(GameAction::Cancel); return; }
        if (m_uiBattle.mode() == ui::UiBattleMode::Result) { handleBattleAction(GameAction::Confirm); return; }
        const QRectF arena(ed.gameUi.battleArenaRect.x()*m_viewW,ed.gameUi.battleArenaRect.y()*m_viewH,ed.gameUi.battleArenaRect.width()*m_viewW,ed.gameUi.battleArenaRect.height()*m_viewH);
        if (m_uiBattle.mode()==ui::UiBattleMode::EnemyTarget && arena.contains(logicalPosition)) {
            const int count=qMax(1,m_uiBattle.enemies().size());const int index=qBound(0,int((logicalPosition.x()-arena.left())/(arena.width()/count)),count-1);m_uiBattle.selectIndex(index);handleBattleAction(GameAction::Confirm);return;
        }
        const QRectF partyBox(ed.gameUi.battlePartyRect.x()*m_viewW,ed.gameUi.battlePartyRect.y()*m_viewH,ed.gameUi.battlePartyRect.width()*m_viewW,ed.gameUi.battlePartyRect.height()*m_viewH);
        const QRectF cmdBox(ed.gameUi.battleCommandRect.x()*m_viewW,ed.gameUi.battleCommandRect.y()*m_viewH,ed.gameUi.battleCommandRect.width()*m_viewW,ed.gameUi.battleCommandRect.height()*m_viewH);
        if(m_uiBattle.mode()==ui::UiBattleMode::AllyTarget && partyBox.contains(logicalPosition)){
            const int row=qMax(34,int((partyBox.height()-ed.gameUi.paddingY*2)/qMax(1,m_uiBattle.state().party().size())));const int idx=qBound(0,int((logicalPosition.y()-partyBox.top()-ed.gameUi.paddingY)/qMax(1,row)),qMax(0,m_uiBattle.state().party().size()-1));m_uiBattle.selectIndex(idx);handleBattleAction(GameAction::Confirm);return;
        }
        if(cmdBox.contains(logicalPosition)&&!m_uiBattle.entries().isEmpty()){
            const int maxRows=6,rowH=qMax(28,int((cmdBox.height()-72)/maxRows));int first=0;if(m_uiBattle.entries().size()>maxRows)first=qBound(0,m_uiBattle.selected()-maxRows/2,m_uiBattle.entries().size()-maxRows);const int vr=int((logicalPosition.y()-cmdBox.top()-38)/qMax(1,rowH));const int idx=first+vr;if(vr>=0&&vr<maxRows&&idx>=0&&idx<m_uiBattle.entries().size()){m_uiBattle.selectIndex(idx);handleBattleAction(GameAction::Confirm);}return;
        }
        return;
    }
    if (m_uiShop.active()) {
        if (button == Qt::RightButton) { handleShopAction(GameAction::Cancel); return; }
        const QRectF listBox(ed.gameUi.shopListRect.x()*m_viewW,ed.gameUi.shopListRect.y()*m_viewH,ed.gameUi.shopListRect.width()*m_viewW,ed.gameUi.shopListRect.height()*m_viewH);
        if(listBox.contains(logicalPosition)&&!m_uiShop.entries().isEmpty()){const int maxRows=9,rowH=qMax(30,int(listBox.height()/maxRows));int first=0;if(m_uiShop.entries().size()>maxRows)first=qBound(0,m_uiShop.selected()-maxRows/2,m_uiShop.entries().size()-maxRows);const int vr=int((logicalPosition.y()-listBox.top())/qMax(1,rowH));const int idx=first+vr;if(vr>=0&&vr<maxRows&&idx>=0&&idx<m_uiShop.entries().size()){m_uiShop.selectIndex(idx);handleShopAction(GameAction::Confirm);}return;}return;
    }
    if (m_uiMenu.customActive()) {
        if (button == Qt::RightButton) {
            playUiSound(ed.gameUi.cancelSePath);
            m_uiMenu.handleCustomAction(GameAction::Cancel);
            m_overlayDirty = true;
            return;
        }
        const QString before = m_uiMenu.focusedElementId();
        if (m_uiMenu.activateWidgetAt(logicalPosition, QSize(m_viewW, m_viewH))) {
            if (before != m_uiMenu.focusedElementId()) playUiSound(ed.gameUi.cursorSePath);
            playUiSound(ed.gameUi.confirmSePath);
        }
        m_overlayDirty = true;
        return;
    }
    if (m_uiMenu.active()) {
        if (button == Qt::RightButton) {
            handleMenuAction(GameAction::Cancel);
            return;
        }
        if (m_uiMenu.activateWidgetAt(logicalPosition, QSize(m_viewW, m_viewH))) {
            playUiSound(ed.gameUi.confirmSePath);
            m_overlayDirty = true;
            return;
        }
        const QRect menuList(int(ed.gameUi.menuListRect.x()*m_viewW),int(ed.gameUi.menuListRect.y()*m_viewH),
                             int(ed.gameUi.menuListRect.width()*m_viewW),int(ed.gameUi.menuListRect.height()*m_viewH));
        const int menuRow=qMax(32,menuList.height()/9);
        if (menuList.contains(logicalPosition.toPoint())) {
            const int visual = (int(logicalPosition.y()) - menuList.top() - 4) / qMax(1, menuRow);
            const int index = m_uiMenu.firstVisible(9) + visual;
            if (visual >= 0 && visual < 9 && index >= 0 && index < m_uiMenu.entries().size() &&
                m_uiMenu.entries().at(index).enabled) {
                const int before = m_uiMenu.selected();
                m_uiMenu.selectIndex(index);
                if (m_uiMenu.selected() != before) playUiSound(ed.gameUi.cursorSePath);
                handleMenuAction(GameAction::Confirm);
            }
        }
        m_overlayDirty = true;
        return;
    }
    if (m_uiModal.active()) {
        if (!m_uiModal.acceptingInput()) return;
        if (button == Qt::RightButton) {
            playUiSound(ed.gameUi.cancelSePath);
            m_uiModal.cancel();
            m_overlayDirty = true;
            return;
        }
        QFont uiFont = m_font;
        if (!ed.gameUi.fontFamily.trimmed().isEmpty()) uiFont.setFamily(ed.gameUi.fontFamily.trimmed());
        uiFont.setPixelSize(qMax(6, ed.gameUi.fontSize));
        if (m_uiModal.type() == ui::UiModalType::NumberInput ||
            m_uiModal.type() == ui::UiModalType::TextInput) {
            const QRect box = ui::GameUiLayer::modalGeometry(m_uiModal, uiFont, QSize(m_viewW, m_viewH),
                                                             ed.gameUi.paddingX + 6, ed.gameUi.paddingY + 4);
            if (box.contains(logicalPosition.toPoint())) {
                playUiSound(ed.gameUi.confirmSePath);
                m_uiModal.confirm();
                m_overlayDirty = true;
            }
            return;
        }
        const auto listGeometry = ui::GameUiLayer::modalListGeometry(
            m_uiModal, uiFont, QSize(m_viewW, m_viewH), ed.gameUi.paddingX + 6, ed.gameUi.paddingY + 4);
        if (listGeometry.first.contains(logicalPosition.toPoint())) {
            const int visualIndex = (int(logicalPosition.y()) - listGeometry.first.top()) / qMax(1, listGeometry.second);
            const int index = ui::GameUiLayer::modalFirstVisible(m_uiModal) + visualIndex;
            if (visualIndex >= 0 && visualIndex < 6 && index >= 0 && index < m_uiModal.labels().size()) {
                m_uiModal.selectIndex(index);
                playUiSound(ed.gameUi.confirmSePath);
                m_uiModal.confirm();
                m_overlayDirty = true;
            }
        }
        return;
    }
    Interpreter* input=visibleInterpreter();
    if(input&&input->choice().visible){
        if(button==Qt::RightButton){playUiSound(ed.gameUi.cancelSePath);input->cancelChoice();m_overlayDirty=true;return;}
        MessageStyle style=input->style();
        const ui::UiTheme theme=accessibleUiTheme(ed,m_uiScalePercent,m_strongFocus);
        const ui::UiResolvedStyle choiceStyle=ui::resolveNativeStyle(theme,ed,QStringLiteral("choices"),QStringLiteral("normal"),style.font);
        style.font=choiceStyle.font;
        if(!input->choice().fontFamily.isEmpty())style.font.setFamily(input->choice().fontFamily);
        if(input->choice().fontSize>0)style.font.setPixelSize(qBound(6,input->choice().fontSize,96));
        const int index=ui::GameUiLayer::choiceIndexAt(input->choice(),style,QSize(m_viewW,m_viewH),logicalPosition,choiceStyle.paddingX+2,choiceStyle.paddingY+2);
        if(index>=0&&!input->choice().disabled.value(index,false)){input->moveChoice(index-input->choice().selected);playUiSound(ed.gameUi.confirmSePath);m_confirmPending=true;m_overlayDirty=true;}
        return;
    }
    if(button==Qt::LeftButton){
        const QVector<const LivePicture*> pictures=m_pics.ordered();
        for(auto it=pictures.crbegin();it!=pictures.crend();++it){const LivePicture* live=*it;if(m_uiBattle.active()&&!live->def.duringBattle)continue;
            QString commonId=m_pics.interactionCommonEvent(live->def.number,true);if(commonId.isEmpty())commonId=m_pics.interactionCommonEvent(live->def.number,false);if(commonId.isEmpty())continue;
            const PictureAsset* asset=ed.pictureFor(live->def.assetId,live->def.assetName);if(!live->def.rich.enabled&&(!asset||!asset->isValid()))continue;
            const PictureFrame frame=m_picFx.build(*live,ed);if(!frame.valid)continue;const RuntimePictureTransform transform=makeRuntimePictureTransform(renderState(),*live,frame);bool invertible=false;const QTransform inverse=transform.screenTransform.inverted(&invertible);if(!invertible)continue;
            const QPointF local=inverse.map(logicalPosition);if(!transform.localRect.contains(local))continue;const QPoint pixel(qBound(0,int(local.x()),frame.image.width()-1),qBound(0,int(local.y()),frame.image.height()-1));if(frame.image.pixelColor(pixel).alpha()==0)continue;
            QVariantMap arguments{{QStringLiteral("pictureNumber"),live->def.number},{QStringLiteral("pictureName"),live->logicalName},{QStringLiteral("x"),logicalPosition.x()},{QStringLiteral("y"),logicalPosition.y()}};
            if(reserveCommonEvent(commonId,arguments,50)){m_overlayDirty=true;return;}
        }
    }
    if(input){if(button==Qt::LeftButton)playUiSound(ed.gameUi.confirmSePath);else playUiSound(ed.gameUi.cancelSePath);m_confirmPending=true;return;}
    if(button==Qt::LeftButton)m_confirmPending=true;
}

RuntimeRenderState GameSession::renderState() const
{
    const MapInfo& info = ed.mapInfo();
    return makeRuntimeRenderState(cameraTopLeft(), QSize(m_viewW, m_viewH),
                                  QSizeF(info.pixelWidth(), info.pixelHeight()),
                                  m_zoom, screenToneUniforms(m_screenTone),
                                  m_screenEffects.shakeOffset(m_reduceShake));
}

QPointF GameSession::cameraTopLeft() const
{
    const MapInfo& info = ed.mapInfo();
    const double safeZoom = normalizedRuntimeZoom(m_zoom);
    const double viewW = m_viewW / safeZoom, viewH = m_viewH / safeZoom;
    const QPointF pl = m_world.playerPixel();
    const QPointF center=m_camera.active?m_camera.center
                         :pl+QPointF(info.tileWidth/2.0,info.tileHeight/2.0);
    // Câmera centrada no alvo escolhido pelo Ludo Camera System.
    double cx = center.x() - viewW / 2.0;
    double cy = center.y() - viewH / 2.0;
    // ...mas presa às bordas do mapa (e centralizada se o mapa for menor).
    const double mw = info.pixelWidth(), mh = info.pixelHeight();
    cx = (mw <= viewW) ? (mw - viewW) / 2.0 : clampd(cx, 0.0, mw - viewW);
    cy = (mh <= viewH) ? (mh - viewH) / 2.0 : clampd(cy, 0.0, mh - viewH);

    // Pixel-perfect em zoom inteiro. QRhi e QPainter rasterizam os mesmos
    // quads, porém uma câmera numa fase fracionária (ex.: 0,25 px de mundo
    // em zoom 2x) pode fazer a GPU alternar 1/3 pixels físicos por texel,
    // enquanto o raster do QPainter aparenta a duplicação 2x correta. Fixar a
    // câmera à grade de 1/zoom mantém 1 pixel lógico -> N×N pixels físicos nos
    // dois backends e também evita shimmer durante o movimento.
    const double integerZoom = std::round(safeZoom);
    if (integerZoom >= 1.0 && qAbs(safeZoom - integerZoom) < 0.000001) {
        cx = std::round(cx * integerZoom) / integerZoom;
        cy = std::round(cy * integerZoom) / integerZoom;
        if (mw > viewW) cx = clampd(cx, 0.0, mw - viewW);
        if (mh > viewH) cy = clampd(cy, 0.0, mh - viewH);
    }
    return QPointF(cx, cy);
}

void GameSession::mouseMove(const QPointF& logicalPosition)
{
    m_mouseDelta = logicalPosition - m_mouseLogical;
    m_mouseLogical = logicalPosition;
    if(m_uiMenu.customActive()||m_uiMenu.active())if(m_uiMenu.dragWidgetAt(logicalPosition,QSize(m_viewW,m_viewH)))m_overlayDirty=true;
}

void GameSession::mouseRelease(Qt::MouseButton button)
{
    if(button==Qt::LeftButton){m_uiMenu.releasePointerWidget();m_mouseButtons.setFlag(Qt::LeftButton,false);m_mouseLeftReleasedFrame=m_visualFrameSerial+1;}
    if(button==Qt::RightButton){m_mouseButtons.setFlag(Qt::RightButton,false);m_mouseRightReleasedFrame=m_visualFrameSerial+1;}
}

void GameSession::mouseWheel(const QPointF& logicalPosition, int steps)
{
    m_mouseDelta = logicalPosition - m_mouseLogical;
    m_mouseLogical = logicalPosition;
    m_mouseWheelDelta = steps;
    if(steps==0)return;
    if(m_uiMenu.customActive()||m_uiMenu.active()){
        if(m_uiMenu.wheelWidgetAt(logicalPosition,QSize(m_viewW,m_viewH),steps)){m_overlayDirty=true;return;}
    }
}

void GameSession::drawPlayer(QPainter& p)
{
    if (m_world.playerTransparent() || m_world.playerOpacity() <= 0) return;
    const MapInfo& info = ed.mapInfo();
    const QPointF pos = m_world.playerVisualPixel();
    const double w = info.tileWidth, h = info.tileHeight;
    const Editor::PlayerSettings& ps = ed.player;

    if (const EventGraphic* overrideGraphic = m_world.playerGraphicOverride()) {
        EventGraphic g = *overrideGraphic;
        g.dir = qBound(0, int(cardinalOf(m_world.facing())), 3);
        g.frame = qBound(0, m_world.animFrame(), qMax(1, g.charsetCols) - 1);
        if (g.kind == EventGraphic::Charset && !g.charset.isNull()) {
            const QRect src = g.charsetFrameRect();
            const QRectF dst(pos.x() + (w - src.width()) / 2.0,
                             pos.y() + h - src.height(), src.width(), src.height());
            p.drawImage(dst, g.charset, src);
        }
        return;
    }

    // 0) Sombra opcional (Configuração de Personagem → Sombra). Fica sob os
    //    pés, na base da célula, e é desenhada antes do sprite.
    if (ps.shadow) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 70));
        p.drawEllipse(QRectF(pos.x() + w * 0.18, pos.y() + h * 0.80, w * 0.64, h * 0.18));
    }

    // 1) Charset configurado no projeto: linha = direção, coluna = quadro.
    if (ps.hasCharset()) {
        const QSize fs = ps.frameSize();
        if (fs.isValid() && fs.width() > 0 && fs.height() > 0) {
            // Onde ficam os quadros desta direção — inclusive no arranjo
            // "3 normais + 3 diagonais na mesma linha".
            const CharsetCell cc = charsetCellFor(m_world.facing(), ps);
            const int row = qBound(0, cc.row, ps.frameRows - 1);
            const int col = qBound(0, cc.colOffset + m_world.animFrame(), ps.frameCols - 1);
            const QRectF src(col * fs.width(), row * fs.height(), fs.width(), fs.height());
            // O sprite pode ser maior que o tile (personagem "alto"): fica
            // alinhado pelo pé, centralizado na célula, como no editores de RPG.
            const QRectF dst(pos.x() + (w - fs.width()) / 2.0,
                             pos.y() + h - fs.height(),
                             fs.width(), fs.height());
            if (m_charsetPm.isNull()) m_charsetPm = QPixmap::fromImage(ps.charset);
            p.drawPixmap(dst, m_charsetPm, src);
            return;
        }
    }

    // 2) Sem charset: boneco desenhado à mão. (Antes eu usava o tile
    //    selecionado na paleta, mas isso deixava o herói invisível quando o
    //    tile era grama — e ninguém adivinhava o porquê.)
    // Personagem provisório desenhado à mão, virado para a direção atual.
    const QRectF body(pos.x() + w * 0.22, pos.y() + h * 0.35, w * 0.56, h * 0.55);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#4a90d7"));
    p.drawRoundedRect(body, w * 0.14, w * 0.14);
    p.setBrush(QColor("#f2d3a8"));
    p.drawEllipse(QRectF(pos.x() + w * 0.28, pos.y() + h * 0.10, w * 0.44, h * 0.34));

    // olhos indicando para onde olha
    p.setBrush(QColor("#222"));
    const double ey = pos.y() + h * 0.24;
    double e1 = pos.x() + w * 0.38, e2 = pos.x() + w * 0.55;
    switch (cardinalOf(m_world.facing())) {
    case Dir::Left:  e1 = pos.x() + w * 0.32; e2 = pos.x() + w * 0.44; break;
    case Dir::Right: e1 = pos.x() + w * 0.50; e2 = pos.x() + w * 0.62; break;
    case Dir::Up:    return;                       // de costas: sem olhos
    default:         break;
    }
    p.drawEllipse(QRectF(e1, ey, w * 0.07, h * 0.07));
    p.drawEllipse(QRectF(e2, ey, w * 0.07, h * 0.07));
}

void GameSession::drawGameUi(QPainter& p)
{
    const Interpreter* active = visibleInterpreter();
    // Tema pode ser alterado no editor durante um playtest; copiar a struct é
    // barato e deixa o preview do projeto refletir a configuração atual.
    m_gameUi.setTheme(accessibleUiTheme(ed, m_uiScalePercent, m_strongFocus));
    m_gameUi.rebuild(active, &m_uiModal, &m_uiMenu, &m_uiBattle, &m_uiShop, m_uiMenu.customScreenId(), QSize(m_viewW, m_viewH), m_showHud,
                     m_clock.elapsed(), &ed.iconSet);
    ui::UiPainterRenderer::render(p, m_gameUi.canvas().drawList());
}

QPointF GameSession::speechBubbleAnchor(const SpeechBubble& bubble,const RuntimeRenderState& state) const
{
    if(bubble.notification)return QPointF();const MapInfo& info=ed.mapInfo();QPointF world;
    if(bubble.target==QLatin1String("player"))world=m_world.playerVisualPixel()+QPointF(info.tileWidth/2.0,0);
    else if(bubble.target.startsWith(QLatin1String("event:"))){const QString id=bubble.target.mid(6);bool exists=false;for(const MapEvent& event:ed.events())if(event.id==id){exists=true;break;}if(!exists){const double nan=std::numeric_limits<double>::quiet_NaN();return QPointF(nan,nan);}const QPoint cell=m_world.eventCell(id);world=QPointF((cell.x()+.5)*info.tileWidth,cell.y()*info.tileHeight);}
    else {const double nan=std::numeric_limits<double>::quiet_NaN();return QPointF(nan,nan);}return state.worldToScreen(world)+QPointF(bubble.offsetX,bubble.offsetY);
}

void GameSession::drawSpeechBubbles(QPainter& p,const RuntimeRenderState& state)
{
    int notificationIndex=0;for(const SpeechBubble& bubble:m_bubbles.active()){
        if(bubble.opacity<=0||bubble.text.isEmpty())continue;QFont font=m_bubbles.font();font.setPixelSize(qMax(8,bubble.fontSize));QFontMetricsF fm(font);const QString visible=bubble.text.left(bubble.revealed);QRectF measured=fm.boundingRect(QRectF(0,0,bubble.maxWidth-24,10000),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,bubble.text);const double speakerH=bubble.speaker.isEmpty()?0:fm.height()+2;const double contentWidth=qMax(measured.width(),fm.horizontalAdvance(bubble.speaker));const double width=qMin<double>(bubble.maxWidth,qMax(100.0,contentWidth+24));const double height=qMax(36.0,measured.height()+speakerH+18);QPointF anchor;
        if(bubble.notification){const QString pos=bubble.screenPosition.toLower();double x=pos.contains("left")?16:pos.contains("right")?m_viewW-width-16:(m_viewW-width)/2;double y=pos.startsWith("bottom")?m_viewH-height-16-notificationIndex*(height+8):16+notificationIndex*(height+8);anchor=QPointF(x+width/2,y+height);++notificationIndex;}else anchor=speechBubbleAnchor(bubble,state);if(!std::isfinite(anchor.x())||!std::isfinite(anchor.y()))continue;
        QRectF box(anchor.x()-width/2,anchor.y()-height-(bubble.notification?0:10),width,height);box.moveLeft(qBound(4.0,box.left(),qMax(4.0,m_viewW-box.width()-4.0)));box.moveTop(qBound(4.0,box.top(),qMax(4.0,m_viewH-box.height()-4.0)));p.save();p.setOpacity(qBound(0.0,bubble.opacity,1.0));p.setRenderHint(QPainter::Antialiasing,true);p.setPen(QPen(bubble.textColor,1));p.setBrush(bubble.bgColor);p.drawRoundedRect(box,10,10);
        if(!bubble.notification&&bubble.tailDirection!=QLatin1String("none")){const double cx=qBound(box.left()+16,anchor.x(),box.right()-16);QPolygonF tail;if(bubble.tailDirection==QLatin1String("up"))tail<<QPointF(cx-8,box.top())<<QPointF(cx+8,box.top())<<QPointF(anchor.x(),anchor.y());else tail<<QPointF(cx-8,box.bottom())<<QPointF(cx+8,box.bottom())<<QPointF(anchor.x(),anchor.y());p.drawPolygon(tail);}
        p.setFont(font);double y=box.top()+8;if(!bubble.speaker.isEmpty()){QFont bold=font;bold.setBold(true);p.setFont(bold);p.drawText(QRectF(box.left()+12,y,box.width()-24,fm.height()),Qt::AlignLeft|Qt::AlignVCenter,bubble.speaker);y+=speakerH;p.setFont(font);}p.drawText(QRectF(box.left()+12,y,box.width()-24,box.bottom()-y-8),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,visible);p.restore();}
}

QPair<QRect,int> GameSession::choiceGeometry(const Interpreter* active) const
{
    if (!active) return {};
    MessageStyle style = active->style();
    const ui::UiTheme theme = accessibleUiTheme(ed, m_uiScalePercent, m_strongFocus);
    const ui::UiResolvedStyle choiceStyle = ui::resolveNativeStyle(
        theme, ed, QStringLiteral("choices"), QStringLiteral("normal"), style.font);
    style.font = choiceStyle.font;
    if (!active->choice().fontFamily.isEmpty()) style.font.setFamily(active->choice().fontFamily);
    if (active->choice().fontSize > 0) style.font.setPixelSize(qBound(6, active->choice().fontSize, 96));
    return ui::GameUiLayer::choiceGeometry(active->choice(), style, QSize(m_viewW, m_viewH),
                                            choiceStyle.paddingX + 2, choiceStyle.paddingY + 2);
}


QPointF GameSession::subtitleAnchorPixel(const Subtitle& s) const
{
    // Legenda ancorada segue o personagem/evento NA TELA: converte a posição
    // do mapa para pixel de tela usando a mesma câmera e o mesmo zoom.
    const MapInfo& info = ed.mapInfo();
    const QPointF cam = cameraTopLeft();
    QPointF mundo;
    if (s.anchor == SubtitleAnchor::Player) {
        mundo = m_world.playerPixel() + QPointF(info.tileWidth / 2.0, 0);
    } else if (s.anchor == SubtitleAnchor::Event) {
        const MapDoc* d = ed.doc();
        const MapEvent* alvo = nullptr;
        if (d) {
            for (const MapEvent& e : d->events)
                if (e.id == s.anchorEventId || e.name == s.anchorEventId) { alvo = &e; break; }
        }
        if (!alvo) return QPointF(-1, -1);          // evento sumiu: cai no padrão
        const QPoint c = m_world.eventCell(alvo->id);
        mundo = QPointF((c.x() + 0.5) * info.tileWidth,
                        double(c.y() * info.tileHeight));
    } else {
        return QPointF(-1, -1);
    }
    return QPointF((mundo.x() - cam.x()) * m_zoom,
                   (mundo.y() - cam.y()) * m_zoom + s.anchorOffsetY);
}

void GameSession::drawSubtitles(QPainter& p)
{
    if (m_subs.empty()) return;

    const QVector<Subtitle>& lista = m_subs.subtitles();
    // As legendas mais antigas ficam por cima das mais novas? Não: empilhamos
    // de baixo para cima na ordem de criação, como o plugin faz.
    QHash<int,int> trackStacks;
    for (const Subtitle& s : lista) {
        if (s.opacity <= 0.0 || s.pages.isEmpty()) continue;
        const core::SubtitleStyle& st=s.style;int& pilha=trackStacks[int(s.track)];

        QFont f = m_subs.font();
        f.setPixelSize(qMax(6, s.fontSize));
        f.setBold(st.fontBold);
        f.setItalic(st.fontItalic);
        const QFontMetricsF fm(f);
        QFont fnome = f;
        fnome.setPixelSize(qMax(6, st.nameFontSize));
        fnome.setBold(true);
        const QFontMetricsF fmNome(fnome);

        const bool temNome = !s.speakerName.isEmpty();
        const double alturaNome = temNome ? fmNome.height() : 0.0;
        const double larguraNome = temNome ? fmNome.horizontalAdvance(s.speakerName) : 0.0;

        const double cw = qMax(s.textSize.width(), larguraNome) + 2 * st.paddingH;
        const double ch = s.textSize.height() + alturaNome + 2 * st.paddingV;

        // ---- posição da caixa ------------------------------------------------
        double x = 0, y = 0;
        const QPointF anc = subtitleAnchorPixel(s);
        if (anc.x() >= 0) {
            x = anc.x() - cw / 2.0;
            y = anc.y() - ch;
        } else {
            switch (s.boxAlign) {
            case core::SubtitleAlign::Left:   x = st.marginHorizontal; break;
            case core::SubtitleAlign::Right:  x = m_viewW - cw - st.marginHorizontal; break;
            case core::SubtitleAlign::Center: x = (m_viewW - cw) / 2.0; break;
            }
            switch (s.position) {
            case core::SubtitlePosition::Top:    y = st.marginBottom; break;
            case core::SubtitlePosition::Middle: y = (m_viewH - ch) / 2.0; break;
            case core::SubtitlePosition::Bottom: y = m_viewH - ch - st.marginBottom; break;
            }
            // várias legendas ao mesmo tempo: empilha sem sobrepor
            y += (s.position == core::SubtitlePosition::Top ? 1 : -1) * pilha * (ch + 8);
        }
        x += s.offsetX;
        y += s.offsetY;

        // ---- transição -------------------------------------------------------
        const bool entrando = (s.phase == Subtitle::FadeIn);
        QPointF tOff(0, 0), tEsc(1, 1);
        transitionState(entrando ? s.transitionIn : s.transitionOut,
                        s.transitionProgress, entrando, &tOff, &tEsc);

        p.save();
        p.setOpacity(qBound(0.0, s.opacity, 1.0));
        p.translate(x + cw / 2.0 + tOff.x(), y + ch / 2.0 + tOff.y());
        p.scale(qMax(0.01, tEsc.x()), qMax(0.01, tEsc.y()));
        p.translate(-cw / 2.0, -ch / 2.0);
        const QRectF caixa(0, 0, cw, ch);

        // ---- fundo -----------------------------------------------------------
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        switch (st.bgStyle) {
        case core::SubtitleBg::None: break;
        case core::SubtitleBg::Solid:
            p.setBrush(st.bgColor);
            p.drawRoundedRect(caixa, st.bgRadius, st.bgRadius);
            break;
        case core::SubtitleBg::Gradient: {
            QLinearGradient g(caixa.topLeft(), caixa.bottomLeft());
            QColor c1 = st.bgColor, c2 = st.bgColor;
            c2.setAlpha(qMax(0, c2.alpha() / 3));
            g.setColorAt(0.0, c1);
            g.setColorAt(1.0, c2);
            p.setBrush(g);
            p.drawRoundedRect(caixa, st.bgRadius, st.bgRadius);
            break;
        }
        case core::SubtitleBg::Glass: {
            // Sem blur de verdade (custaria caro): fundo claro translúcido com
            // borda luminosa, que é o que dá a leitura de "vidro".
            QColor c = st.bgColor;
            c.setAlpha(qMin(255, c.alpha() / 2 + 40));
            p.setBrush(c);
            p.drawRoundedRect(caixa, st.bgRadius, st.bgRadius);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 60), 1));
            p.drawRoundedRect(caixa.adjusted(1, 1, -1, -1), st.bgRadius, st.bgRadius);
            break;
        }
        }
        p.setRenderHint(QPainter::Antialiasing, false);

        // ---- nome de quem fala ------------------------------------------------
        double ty = st.paddingV;
        if (temNome) {
            p.setFont(fnome);
            p.setPen(s.nameColor);
            const int nameFlags = Qt::AlignVCenter |
                (st.nameAlign == core::SubtitleAlign::Center ? Qt::AlignHCenter :
                 st.nameAlign == core::SubtitleAlign::Right  ? Qt::AlignRight : Qt::AlignLeft);
            p.drawText(QRectF(st.paddingH, ty, cw - st.paddingH * 2, fmNome.height()),
                       nameFlags, s.speakerName);
            ty += fmNome.height();
        }

        // ---- texto -------------------------------------------------------------
        p.setFont(f);
        {
            TextDrawOpts o;
            o.color = s.fontColor;
            o.outlineColor = st.outlineColor;
            o.outlineWidth = st.outlineWidth;
            o.shadow = st.shadow;
            o.shadowColor = st.shadowColor;
            o.shadowOffset = QPointF(st.shadowOffsetX, st.shadowOffsetY);
            o.opacity = s.opacity;
            o.revealed = s.revealed;
            o.revealTimesSec = s.revealTimesSec;
            o.time = s.ageSec;
            o.exitTime = s.phase == Subtitle::FadeOut ? qMax(0.0, s.exitAgeSec) : -1.0;
            o.icons = &ed.iconSet;
            o.gradient = s.gradient;
            o.effects = s.effects;
            o.align = (s.textAlign == core::SubtitleAlign::Center) ? TextAlign::Center
                    : (s.textAlign == core::SubtitleAlign::Right)  ? TextAlign::Right
                                                                   : TextAlign::Left;
            drawTextPage(p, s.pages[0], f,
                         QRectF(st.paddingH, ty, cw - st.paddingH * 2, ch - ty), o);
        }


        // ---- indicador de continuar -------------------------------------------
        if (s.waitingForInput && st.inputIndicator) {
            const bool aceso = (m_clock.elapsed() / 400) % 2 == 0;
            if (aceso) {
                p.setPen(Qt::NoPen);
                p.setBrush(s.fontColor);
                const QPointF c(cw - st.paddingH / 2.0 - 8, ch - 8);
                const QPointF tri[3] = { c + QPointF(-5, -4), c + QPointF(5, -4), c + QPointF(0, 3) };
                p.drawPolygon(tri, 3);
            }
        }
        p.restore();
        ++pilha;
    }
    p.setOpacity(1.0);
}

void GameSession::appendScreenEffectQuads(SpriteBatcher& out) const
{
    const QRectF screen(0,0,m_viewW,m_viewH);
    const RuntimeScreenOverlay overlay=m_screenEffects.overlay(m_reduceFlash);
    if(overlay.flashColor.alpha()>0)out.addSolid(screen,overlay.flashColor,false,RuntimeCoordinateSpace::Screen);
    if(overlay.fadeColor.alpha()>0)out.addSolid(screen,overlay.fadeColor,false,RuntimeCoordinateSpace::Screen);
}

void GameSession::appendPresentationEffectQuads(SpriteBatcher& out) const
{
    const QRectF screen(0,0,m_viewW,m_viewH);
    if(m_skipFade>0)out.addSolid(screen,QColor(0,0,0,qRound(255*qBound(0.0,m_skipFade,1.0))),false,RuntimeCoordinateSpace::Screen);
    if(m_mapTransition.opacity>0)out.addSolid(screen,QColor(0,0,0,qRound(255*qBound(0.0,m_mapTransition.opacity,1.0))),false,RuntimeCoordinateSpace::Screen);
}

void GameSession::appendSubtitleQuads(SpriteBatcher& out, ImageProvider& provider)
{
    const QVector<Subtitle>& subtitles=m_subs.subtitles();
    QSet<QString> alive;
    QHash<int,int> trackStacks;
    for(const Subtitle&s:subtitles){
        if(s.opacity<=0.0||s.pages.isEmpty())continue;
        const core::SubtitleStyle& st=s.style;int& stack=trackStacks[int(s.track)];
        QFont font=m_subs.font();font.setPixelSize(qMax(6,s.fontSize));font.setBold(st.fontBold);font.setItalic(st.fontItalic);
        const QFontMetricsF fm(font);QFont nameFont=font;nameFont.setPixelSize(qMax(6,st.nameFontSize));nameFont.setBold(true);const QFontMetricsF nameFm(nameFont);
        const bool hasName=!s.speakerName.isEmpty();const double nameH=hasName?nameFm.height():0.0;const double nameW=hasName?nameFm.horizontalAdvance(s.speakerName):0.0;
        const double cw=qMax(s.textSize.width(),nameW)+2*st.paddingH;const double ch=s.textSize.height()+nameH+2*st.paddingV;
        if(cw<=0||ch<=0)continue;

        double x=0,y=0;const QPointF anchor=subtitleAnchorPixel(s);
        if(anchor.x()>=0){x=anchor.x()-cw/2.0;y=anchor.y()-ch;}
        else{
            if(s.boxAlign==core::SubtitleAlign::Left)x=st.marginHorizontal;
            else if(s.boxAlign==core::SubtitleAlign::Right)x=m_viewW-cw-st.marginHorizontal;
            else x=(m_viewW-cw)/2.0;
            if(s.position==core::SubtitlePosition::Top)y=st.marginBottom;
            else if(s.position==core::SubtitlePosition::Middle)y=(m_viewH-ch)/2.0;
            else y=m_viewH-ch-st.marginBottom;
            y+=(s.position==core::SubtitlePosition::Top?1:-1)*stack*(ch+8);
        }
        x+=s.offsetX;y+=s.offsetY;

        bool animatedText=s.effects.enabled();
        for(const TextLine&line:s.pages[0].lines)for(const TypedChar&c:line.chars)
            if(c.fx.kind!=TextFx::None){animatedText=true;break;}
        const int indicatorPhase=s.waitingForInput&&st.inputIndicator?int((m_clock.elapsed()/400)%2):0;
        const qint64 animationFrame=animatedText?qRound64(s.ageSec*60.0):0;
        const QString signature=QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
            .arg(s.revealed).arg(indicatorPhase).arg(animationFrame).arg(s.fontSize)
            .arg(s.fontColor.rgba()).arg(s.nameColor.rgba()).arg(cw,0,'f',2).arg(ch,0,'f',2)
            + QString::fromUtf8(QJsonDocument::fromVariant(s.gradient.toVariantMap()).toJson(QJsonDocument::Compact).toBase64())
            + QString::fromUtf8(QJsonDocument::fromVariant(s.effects.toVariantMap()).toJson(QJsonDocument::Compact).toBase64())
            +QString::fromLatin1(QJsonDocument(st.toJson()).toJson(QJsonDocument::Compact).toBase64());
        const QString key=QStringLiteral("img:subtitle:%1").arg(s.id);
        alive.insert(key);
        if(m_subtitleGpuSignatures.value(key)!=signature||!m_subtitleGpuImages.contains(key)){
            QImage image(QSize(qMax(1,qCeil(cw)),qMax(1,qCeil(ch))),QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
            QPainter p(&image);p.setRenderHint(QPainter::TextAntialiasing,true);const QRectF box(0,0,cw,ch);p.setPen(Qt::NoPen);
            if(st.bgStyle==core::SubtitleBg::Solid){p.setBrush(st.bgColor);p.drawRoundedRect(box,st.bgRadius,st.bgRadius);}
            else if(st.bgStyle==core::SubtitleBg::Gradient){QLinearGradient g(box.topLeft(),box.bottomLeft());QColor c2=st.bgColor;c2.setAlpha(qMax(0,c2.alpha()/3));g.setColorAt(0,st.bgColor);g.setColorAt(1,c2);p.setBrush(g);p.drawRoundedRect(box,st.bgRadius,st.bgRadius);}
            else if(st.bgStyle==core::SubtitleBg::Glass){QColor c=st.bgColor;c.setAlpha(qMin(255,c.alpha()/2+40));p.setBrush(c);p.drawRoundedRect(box,st.bgRadius,st.bgRadius);p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(255,255,255,60),1));p.drawRoundedRect(box.adjusted(1,1,-1,-1),st.bgRadius,st.bgRadius);}
            double ty=st.paddingV;
            if(hasName){p.setFont(nameFont);p.setPen(s.nameColor);const int nameFlags=Qt::AlignVCenter|(st.nameAlign==core::SubtitleAlign::Center?Qt::AlignHCenter:st.nameAlign==core::SubtitleAlign::Right?Qt::AlignRight:Qt::AlignLeft);p.drawText(QRectF(st.paddingH,ty,cw-st.paddingH*2,nameFm.height()),nameFlags,s.speakerName);ty+=nameFm.height();}
            TextDrawOpts opts;opts.color=s.fontColor;opts.outlineColor=st.outlineColor;opts.outlineWidth=st.outlineWidth;opts.shadow=st.shadow;opts.shadowColor=st.shadowColor;opts.shadowOffset=QPointF(st.shadowOffsetX,st.shadowOffsetY);opts.opacity=1.0;opts.revealed=s.revealed;opts.revealTimesSec=s.revealTimesSec;opts.time=s.ageSec;opts.exitTime=s.phase==Subtitle::FadeOut?qMax(0.0,s.exitAgeSec):-1.0;opts.icons=&ed.iconSet;opts.gradient=s.gradient;opts.effects=s.effects;opts.align=s.textAlign==core::SubtitleAlign::Center?TextAlign::Center:s.textAlign==core::SubtitleAlign::Right?TextAlign::Right:TextAlign::Left;
            drawTextPage(p,s.pages[0],font,QRectF(st.paddingH,ty,cw-st.paddingH*2,ch-ty),opts);
            if(indicatorPhase){p.setPen(Qt::NoPen);p.setBrush(s.fontColor);const QPointF c(cw-st.paddingH/2.0-8,ch-8);const QPointF tri[3]={c+QPointF(-5,-4),c+QPointF(5,-4),c+QPointF(0,3)};p.drawPolygon(tri,3);}
            p.end();m_subtitleGpuImages.insert(key,image);m_subtitleGpuSignatures.insert(key,signature);
        }
        provider.insert(key,m_subtitleGpuImages.value(key));
        const bool entering=s.phase==Subtitle::FadeIn;QPointF offset,scale;
        transitionState(entering?s.transitionIn:s.transitionOut,s.transitionProgress,entering,&offset,&scale);
        QTransform transform;transform.translate(x+cw/2.0+offset.x(),y+ch/2.0+offset.y());transform.scale(qMax(.01,scale.x()),qMax(.01,scale.y()));transform.translate(-cw/2.0,-ch/2.0);
        out.addTransformed(key,transform,QRectF(0,0,cw,ch),QRectF(0,0,m_subtitleGpuImages.value(key).width(),m_subtitleGpuImages.value(key).height()),m_subtitleGpuImages.value(key).size(),s.opacity,core::PictureBlend::Normal,false,RuntimeCoordinateSpace::Ui);
        ++stack;
    }
    for(auto it=m_subtitleGpuImages.begin();it!=m_subtitleGpuImages.end();)if(!alive.contains(it.key())){m_subtitleGpuSignatures.remove(it.key());it=m_subtitleGpuImages.erase(it);}else++it;
    for(auto it=provider.begin();it!=provider.end();)if(it.key().startsWith(QLatin1String("img:subtitle:"))&&!alive.contains(it.key()))it=provider.erase(it);else++it;
}

void GameSession::drawEvent(QPainter& p, const MapEvent& e)
{
    World::EventActorView view;
    if (!m_world.eventView(e, &view) || view.transparent || view.opacity <= 0) return;
    const MapInfo& info = ed.mapInfo();
    const EventGraphic& g = view.graphic;
    const QRectF cell(view.pixel, QSizeF(info.tileWidth, info.tileHeight));
    const QString target = QStringLiteral("event:") + e.id;
    p.save();
    const QPointF offset=spriteOffset(target)+eventPageVisualOffset(e),scale=spriteScale(target),pivot(cell.center().x(),cell.bottom());
    p.translate(offset);p.translate(pivot);p.scale(scale.x(),scale.y());p.translate(-pivot);
    p.setOpacity(view.opacity / 255.0*spriteOpacity(target,view.pixel));
    if (view.blend == QLatin1String("add")) p.setCompositionMode(QPainter::CompositionMode_Plus);
    else if (view.blend == QLatin1String("multiply")) p.setCompositionMode(QPainter::CompositionMode_Multiply);
    else if (view.blend == QLatin1String("screen")) p.setCompositionMode(QPainter::CompositionMode_Screen);
    if (g.kind == EventGraphic::Tile && g.tile.isValid()) {
        if (const Tileset* ts = ed.tilesetAt(g.tile.tilesetIdx))
            p.drawPixmap(cell, pixmapCache().pixmap(ed, g.tile.tilesetIdx),
                         QRectF(animatedTileRect(*ts, g.tile.tx, g.tile.ty, m_clock.elapsed(),
                                                quint32(qHash(e.id)))));
    } else if (g.kind == EventGraphic::Charset && !g.charset.isNull()) {
        const QRect src = g.charsetFrameRect();
        const QRectF dst(cell.x() + (cell.width() - src.width()) / 2.0,
                         cell.bottom() - src.height(), src.width(), src.height());
        p.drawImage(dst, g.charset, src);
    }
    p.restore();
}

void GameSession::drawActors(QPainter& p)
{
    const MapDoc* d = ed.doc();
    const QPointF pl = m_world.playerVisualPixel();
    // Junta jogador e eventos e ordena pelo pé (Y de baixo).
    struct Actor { int priority; double y; const MapEvent* ev; };
    QVector<Actor> actors;
    if (d)
        for (const MapEvent& e : d->events) {
            World::EventActorView v;
            if (m_world.eventView(e, &v)) {
                const int pr = v.priority == EventPriority::Below ? -1
                             : v.priority == EventPriority::Above ? 1 : 0;
                actors.push_back({ pr, v.pixel.y() + spriteOffset(QStringLiteral("event:")+e.id).y() + eventPageVisualOffset(e).y() + ed.mapInfo().tileHeight, &e });
            }
        }
    actors.push_back({ 0, pl.y() + spriteOffset(QStringLiteral("player")).y() + ed.mapInfo().tileHeight, nullptr });
    std::stable_sort(actors.begin(), actors.end(), [](const Actor& a, const Actor& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.y < b.y;
    });
    for (const Actor& a : actors) {
        if (a.ev) drawEvent(p, *a.ev);
        else {
            if(m_world.playerTransparent()||m_world.playerOpacity()<=0)continue;
            p.save();const QPointF offset=spriteOffset(QStringLiteral("player")),scale=spriteScale(QStringLiteral("player"));const QPointF pivot(pl.x()+ed.mapInfo().tileWidth/2.0,pl.y()+ed.mapInfo().tileHeight);p.translate(offset);p.translate(pivot);p.scale(scale.x(),scale.y());p.translate(-pivot);p.setOpacity(m_world.playerOpacity()/255.0*spriteOpacity(QStringLiteral("player"),pl));
            const QString blend=m_world.playerBlend();
            if(blend==QLatin1String("add"))p.setCompositionMode(QPainter::CompositionMode_Plus);
            else if(blend==QLatin1String("multiply"))p.setCompositionMode(QPainter::CompositionMode_Multiply);
            else if(blend==QLatin1String("screen"))p.setCompositionMode(QPainter::CompositionMode_Screen);
            drawPlayer(p);p.restore();
        }
    }
}

void GameSession::drawActorsByPriority(QPainter& p, EventPriority priority, bool includePlayer)
{
    struct Actor { double y; const MapEvent* ev; };
    QVector<Actor> actors;
    if (const MapDoc* d = ed.doc()) {
        for (const MapEvent& e : d->events) {
            World::EventActorView v;
            if (!m_world.eventView(e, &v) || v.priority != priority) continue;
            actors.push_back({v.pixel.y() + spriteOffset(QStringLiteral("event:") + e.id).y() + eventPageVisualOffset(e).y()
                                      + ed.mapInfo().tileHeight, &e});
        }
    }
    std::stable_sort(actors.begin(), actors.end(), [](const Actor& a, const Actor& b) { return a.y < b.y; });
    for (const Actor& actor : actors) if (actor.ev) drawEvent(p, *actor.ev);
    if (includePlayer) {
        const QPointF pl = m_world.playerVisualPixel();
        if(!m_world.playerTransparent() && m_world.playerOpacity() > 0) {
            p.save();const QPointF offset=spriteOffset(QStringLiteral("player")),scale=spriteScale(QStringLiteral("player"));const QPointF pivot(pl.x()+ed.mapInfo().tileWidth/2.0,pl.y()+ed.mapInfo().tileHeight);p.translate(offset);p.translate(pivot);p.scale(scale.x(),scale.y());p.translate(-pivot);p.setOpacity(m_world.playerOpacity()/255.0*spriteOpacity(QStringLiteral("player"),pl));
            const QString blend=m_world.playerBlend();if(blend==QLatin1String("add"))p.setCompositionMode(QPainter::CompositionMode_Plus);else if(blend==QLatin1String("multiply"))p.setCompositionMode(QPainter::CompositionMode_Multiply);else if(blend==QLatin1String("screen"))p.setCompositionMode(QPainter::CompositionMode_Screen);drawPlayer(p);p.restore();
        }
    }
}

void GameSession::drawStarActors(QPainter& p)
{
    SpriteBatcher stars, actors;
    ImageProvider& images = m_cpuDepthImages;
    ScenePass pass;
    pass.visible = p.clipBoundingRect();
    pass.animationTimeMs = m_clock.elapsed();
    pass.starTiles = true;
    pass.aboveLayers = false;
    pass.depthSortedTiles = true;
    pass.cellResolver = [this](const LayerPtr& layer,int x,int y) {
        return m_state.runtimeMapCell(ed,currentMapId(),layer->id,x,y);
    };
    buildSceneBatches(ed, pass, stars, images);
    appendActorQuads(actors, images, 0);

    QVector<const DrawBatch*> ordered;
    ordered.reserve(stars.batches().size() + actors.batches().size());
    // O ator entra primeiro no empate: o ★ ainda o cobre no contato. Assim que
    // os pés cruzam a borda inferior, o ator passa à frente após um tile.
    for (const DrawBatch& batch : actors.batches()) ordered.push_back(&batch);
    for (const DrawBatch& batch : stars.batches()) ordered.push_back(&batch);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const DrawBatch* a, const DrawBatch* b) {
                         return a->depth < b->depth;
                     });

    for (const DrawBatch* batch : ordered) {
        const QImage image = images.value(batch->texture);
        if (image.isNull()) continue;
        p.save();
        switch (batch->blend) {
        case PictureBlend::Add:      p.setCompositionMode(QPainter::CompositionMode_Plus); break;
        case PictureBlend::Multiply: p.setCompositionMode(QPainter::CompositionMode_Multiply); break;
        case PictureBlend::Screen:   p.setCompositionMode(QPainter::CompositionMode_Screen); break;
        case PictureBlend::Normal:   p.setCompositionMode(QPainter::CompositionMode_SourceOver); break;
        }
        for (int i = 0; i + 5 < batch->verts.size(); i += 6) {
            const QuadVertex& tl = batch->verts[i];
            const QuadVertex& br = batch->verts[i + 4];
            const QRectF dst(tl.x, tl.y, br.x - tl.x, br.y - tl.y);
            const QRectF src(tl.u * image.width(), tl.v * image.height(),
                             (br.u - tl.u) * image.width(),
                             (br.v - tl.v) * image.height());
            p.setOpacity(qBound(0.0, double(tl.a), 1.0));
            p.drawImage(dst, image, src);
        }
        p.restore();
    }
}

void GameSession::drawScene(QPainter& p, const RuntimeRenderState& state)
{
    const MapInfo& info = ed.mapInfo();
    p.fillRect(state.screenRect(), QColor("#0d0d0d"));

    const QPointF cam = state.camera;

    // Fundo físico do mapa continua em World Space. Panorama vem logo depois,
    // mas monta a própria transformação a partir do RuntimePanoramaLayout:
    // parallax normal = World; "Fixar na tela" = Screen, sem herdar zoom/câmera.
    p.save();
    p.setTransform(state.worldToScreenTransform(), false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setClipRect(state.cameraWorldRect());
    p.fillRect(QRectF(0, 0, info.pixelWidth(), info.pixelHeight()), info.background);
    p.restore();

    drawPanorama(p,cam,false,state);
    drawPanorama(p,cam,true,state);

    // Tiles, atores e camadas do mapa voltam ao contrato World -> Screen único.
    p.save();
    p.setTransform(state.worldToScreenTransform(), false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setClipRect(state.cameraWorldRect());

    auto isStar  = [this](const TileRef& t) { return ed.isStarMarked(t.tilesetIdx, t.tx, t.ty); };
    auto isAbove = [](const LayerPtr& l) { return l->zMode == QLatin1String("above"); };
    auto isBelow = [](const LayerPtr& l) { return l->zMode != QLatin1String("above"); };

    RenderOptions below;
    below.drawObjectFrames = false;
    below.animationTimeMs = m_clock.elapsed();
    below.tileFilter = [isStar](const TileRef& t) { return !isStar(t); };
    below.layerFilter = isBelow;
    below.cellResolver = [this](const LayerPtr& layer,int x,int y) {
        return m_state.runtimeMapCell(ed,currentMapId(),layer->id,x,y);
    };
    drawLayerTree(p, ed, ed.layers(), 1.0, below);

    drawStarActors(p);

    RenderOptions above;
    above.drawObjectFrames = false;
    above.animationTimeMs = m_clock.elapsed();
    above.layerFilter = isAbove;
    above.cellResolver = below.cellResolver;
    drawLayerTree(p, ed, ed.layers(), 1.0, above);

    p.restore();

    m_fogs.draw(p, state);
}

void GameSession::applyScreenTone(QImage& image) const
{
    core::applyScreenTone(image, m_screenTone.red(), m_screenTone.green(),
                          m_screenTone.blue(), m_screenTone.gray());
}

void GameSession::drawCpuFrame(QImage& frame)
{
    // O frame final é também uma fronteira de sincronização. Isso garante que
    // Testar Mapa/Testar Jogo/LudoPlayer recebam o clima do MapDoc mesmo se o
    // primeiro paint acontecer antes do primeiro tick da sessão.
    syncWeatherFromCurrentMap();

    const QSize wanted(m_viewW, m_viewH);
    if (frame.size() != wanted || frame.format() != QImage::Format_ARGB32_Premultiplied)
        frame = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);

    const RuntimeRenderState state = renderState();

    // RC2.66: Screen Tone passa a ser aplicado POR ETAPA do mundo. Isso é
    // necessário para que uma Picture possa realmente ficar entre Parallax,
    // Tiles, Eventos, Clima etc. sem o tone global reaplicar por cima dela.
    // Pictures continuam obedecendo PictureDef::affectedByTone dentro de
    // drawPictures(), portanto CPU e QRhi compartilham o mesmo contrato.
    QImage stage(wanted, QImage::Format_ARGB32_Premultiplied);
    const bool filtersActive = m_filters.hasActiveFilters();
    const bool fullFrameFilter = filtersActive && m_filters.fullFrameEligible();
    const bool scopedFilters = filtersActive && !fullFrameFilter;
    auto compositeDomainStage = [&](core::FilterRenderDomain domain, auto drawStage) {
        stage.fill(Qt::transparent);
        {
            QPainter sp(&stage);
            drawStage(sp);
        }
        if (scopedFilters && m_filters.affects(domain))
            m_filters.applyCpu(stage, m_filterCpuScratch, domain);
        QPainter out(&frame);
        out.drawImage(QPoint(0, 0), stage);
    };
    auto compositeTonedStage = [&](auto drawStage) {
        stage.fill(Qt::transparent);
        {
            QPainter sp(&stage);
            drawStage(sp);
        }
        if (hasScreenTone()) applyScreenTone(stage);
        if (scopedFilters && m_filters.affects(core::FilterRenderDomain::World))
            m_filters.applyCpu(stage, m_filterCpuScratch, core::FilterRenderDomain::World);
        QPainter out(&frame);
        out.drawImage(QPoint(0, 0), stage);
    };
    auto drawPictureLayer = [&](PictureLayer layer) {
        if (scopedFilters && m_filters.affects(core::FilterRenderDomain::Pictures)) {
            compositeDomainStage(core::FilterRenderDomain::Pictures,
                                 [&](QPainter& pp) { drawPictures(pp, layer, state); });
        } else {
            QPainter pp(&frame);
            drawPictures(pp, layer, state);
        }
    };
    auto beginWorld = [&](QPainter& p) {
        p.setTransform(state.worldToScreenTransform(), false);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setClipRect(state.cameraWorldRect());
    };

    const MapInfo& info = ed.mapInfo();
    const QPointF cam = state.camera;
    auto isStar  = [this](const TileRef& t) { return ed.isStarMarked(t.tilesetIdx, t.tx, t.ty); };
    auto isAbove = [](const LayerPtr& l) { return l->zMode == QLatin1String("above"); };
    auto isBelow = [](const LayerPtr& l) { return l->zMode != QLatin1String("above"); };
    const auto runtimeCell = [this](const LayerPtr& layer,int x,int y) {
        return m_state.runtimeMapCell(ed,currentMapId(),layer->id,x,y);
    };

    // 0 — acima do Parallax.
    compositeTonedStage([&](QPainter& p) {
        p.fillRect(state.screenRect(), QColor("#0d0d0d"));
        p.save();
        beginWorld(p);
        p.fillRect(QRectF(0, 0, info.pixelWidth(), info.pixelHeight()), info.background);
        p.restore();
        drawPanorama(p, cam, false, state);
        drawPanorama(p, cam, true, state);
    });
    drawPictureLayer(PictureLayer::AboveParallax);

    // 1 — acima dos tiles baixos, antes de eventos Below.
    compositeTonedStage([&](QPainter& p) {
        p.save(); beginWorld(p);
        RenderOptions opts;
        opts.drawObjectFrames = false;
        opts.animationTimeMs = m_clock.elapsed();
        opts.tileFilter = [isStar](const TileRef& t) { return !isStar(t); };
        opts.layerFilter = isBelow;
        opts.cellResolver = runtimeCell;
        drawLayerTree(p, ed, ed.layers(), 1.0, opts);
        p.restore();
    });
    drawPictureLayer(PictureLayer::BelowTiles);

    // 2 — entre eventos Below e os atores no mesmo nível do jogador.
    compositeTonedStage([&](QPainter& p) {
        p.save(); beginWorld(p);
        drawActorsByPriority(p, EventPriority::Below, false);
        p.restore();
    });
    drawPictureLayer(PictureLayer::BelowEvents);

    // 3 — depois de jogador/eventos Same + tiles ★ ordenados por profundidade.
    compositeTonedStage([&](QPainter& p) {
        p.save(); beginWorld(p);
        drawStarActors(p);
        p.restore();
    });
    drawPictureLayer(PictureLayer::SameAsPlayer);

    // 4 — acima das camadas de tiles marcadas como Above.
    compositeTonedStage([&](QPainter& p) {
        p.save(); beginWorld(p);
        RenderOptions opts;
        opts.drawObjectFrames = false;
        opts.animationTimeMs = m_clock.elapsed();
        opts.layerFilter = isAbove;
        opts.cellResolver = runtimeCell;
        drawLayerTree(p, ed, ed.layers(), 1.0, opts);
        p.restore();
    });
    drawPictureLayer(PictureLayer::AboveTiles);

    // 5 — acima dos eventos de prioridade Above.
    compositeTonedStage([&](QPainter& p) {
        p.save(); beginWorld(p);
        drawActorsByPriority(p, EventPriority::Above, false);
        p.restore();
    });
    drawPictureLayer(PictureLayer::AboveEvents);

    // Fog e Weather pertencem ao mundo e recebem Screen Tone antes da Picture 6.
    compositeTonedStage([&](QPainter& p) { m_fogs.draw(p, state); });
    compositeTonedStage([&](QPainter& p) { drawWeatherLayer(p, state); });
    drawPictureLayer(PictureLayer::AboveWeather);

    // Overlay cuida de 7/8/9. Com escopo parcial o CPU separa os mesmos
    // domínios do QRhi para Pictures/HUD nunca herdarem o filtro do World.
    if (!scopedFilters) {
        QPainter overlayPainter(&frame);
        drawOverlay(overlayPainter, state);
        if (fullFrameFilter)
            m_filters.applyCpu(frame, m_filterCpuScratch, core::FilterRenderDomain::World);
    } else {
        // Screen effects pertencem ao World; subtitles são HUD.
        compositeDomainStage(core::FilterRenderDomain::World,
                             [&](QPainter& p) { drawScreenEffects(p); });
        compositeDomainStage(core::FilterRenderDomain::Hud,
                             [&](QPainter& p) { drawSubtitles(p); });
        drawPictureLayer(PictureLayer::AboveAnimations);

        compositeDomainStage(core::FilterRenderDomain::Hud, [&](QPainter& p) {
            p.setRenderHint(QPainter::TextAntialiasing, true);
            drawSpeechBubbles(p, state);
            drawGameUi(p);
            drawMoveRouteDebugOverlay(p, state);
            if (m_gameOver) {
                drawGameOver(p);
                return;
            }
            if (m_clock.elapsed() > m_hintUntil) m_hint.clear();
            if (!m_debugPresentation || !m_showHud) {
                if (!m_hint.isEmpty()) {
                    p.setPen(QColor("#ffd45e"));
                    p.drawText(QRect(8, m_viewH - 24, m_viewW - 16, 22),
                               Qt::AlignVCenter | Qt::AlignLeft, m_hint);
                }
                return;
            }
            const QPoint hc = m_world.playerHalfCell();
            const QString cellTxt = QStringLiteral("%1,%2")
                                        .arg(QString::number(hc.x() / 2.0),
                                             QString::number(hc.y() / 2.0));
            const QString hud = QCoreApplication::translate("GameSession",
                                    "célula %1   ·   %2 passos   ·   %3 fps (%4 ms)   ·   zoom %5×")
                                    .arg(cellTxt).arg(m_world.stepsTaken())
                                    .arg(int(m_fps)).arg(QString::number(m_frameMs, 'f', 1))
                                    .arg(int(m_zoom));
            const core::InputMap& im = ed.inputMap;
            const QString help = m_hint.isEmpty()
                ? QCoreApplication::translate("GameSession", "%1 andam   ·   %2 fala/avança   ·   %3 zoom   ·   %4 esconde   ·   %5 menu")
                      .arg(QCoreApplication::translate("GameSession", "setas/WASD"), im.describe(GameAction::Confirm),
                           im.describe(GameAction::ZoomIn), im.describe(GameAction::ToggleHud),
                           im.describe(GameAction::Cancel))
                : m_hint;
            QFont f = m_font; f.setPixelSize(12); p.setFont(f);
            p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 150));
            p.drawRect(QRect(0, 0, m_viewW, 22));
            p.drawRect(QRect(0, m_viewH - 22, m_viewW, 22));
            p.setPen(QColor("#ddd"));
            p.drawText(QRect(8, 0, m_viewW - 16, 22), Qt::AlignVCenter | Qt::AlignLeft, hud);
            p.setPen(QColor("#999"));
            p.drawText(QRect(8, m_viewH - 22, m_viewW - 16, 22), Qt::AlignVCenter | Qt::AlignLeft, help);
        });

        drawPictureLayer(PictureLayer::AboveMessage);
        if (!m_gameOver) {
            compositeDomainStage(core::FilterRenderDomain::Hud, [&](QPainter& p) {
                drawCutsceneSkip(p);
                drawMapTransition(p);
            });
        }
        drawPictureLayer(PictureLayer::AboveTimers);
    }
}
void GameSession::draw(QPainter& p)
{
    QImage frame(QSize(m_viewW, m_viewH), QImage::Format_ARGB32_Premultiplied);
    drawCpuFrame(frame);
    p.drawImage(QPoint(0, 0), frame);
}

const QImage& GameSession::imagemComAlfa(const QString& chave, const QImage& original, int alfa)
{
    QPair<int, QImage>& e = m_picAlfaCache[chave];
    if (e.first == alfa && !e.second.isNull()) return e.second;
    QImage img = original.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter q(&img);
    q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    q.fillRect(img.rect(), QColor(0, 0, 0, qBound(0, alfa, 255)));
    q.end();
    e.first = alfa;
    e.second = img;
    return e.second;
}

void GameSession::drawPictures(QPainter& p, PictureLayer camada, const RuntimeRenderState& state)
{
    if (!m_picturesNoOverlay) return;      // o caminho de GPU desenha em quads
    const QVector<const LivePicture*> lista = m_pics.ordered();
    if (lista.isEmpty()) return;

    p.save();
    for (const LivePicture* lp : lista) {
        const PictureDef& d = lp->def;
        if (d.layer != camada) continue;
        if (m_uiBattle.active() && !d.duringBattle) continue;
        // ATENÇÃO: NÃO exigir imagem da biblioteca aqui. Uma picture de TEXTO
        // não tem asset nenhum — a imagem dela é desenhada na hora. Esta
        // checagem ficou de antes do texto rico existir e engolia as cinco
        // pictures de texto sem avisar: o interpretador rodava, os slots
        // apareciam na lista, e a tela ficava vazia.
        const PictureAsset* a = ed.pictureFor(d.assetId, d.assetName);
        if (!d.rich.enabled && (!a || !a->isValid())) continue;

        // Efeitos: a imagem chega pronta (borda, brilho, máscara, onda,
        // brilho deslizante) junto com a opacidade e os ajustes da transição.
        const PictureFrame quadro = m_picFx.build(*lp, ed);
        if (!quadro.valid) continue;
        const double op = quadro.opacity;
        if (op <= 0.0) continue;

        QImage tonedPicture;
        const QImage* pictureImage = &quadro.image;
        if (d.affectedByTone && hasScreenTone()) {
            tonedPicture = quadro.image;
            applyScreenTone(tonedPicture);
            pictureImage = &tonedPicture;
        }

        const RuntimePictureTransform transform = makeRuntimePictureTransform(state, *lp, quadro);
        if (!transform.valid) continue;

        // Pivot/âncora, rotação, escala, câmera/zoom e offsets agora vêm da
        // mesma regra usada pelo QRhi. QPainter não mantém fórmula paralela.
        p.setTransform(transform.screenTransform, false);
        p.setOpacity(op / 255.0);
        switch (d.blend) {
        case PictureBlend::Add:      p.setCompositionMode(QPainter::CompositionMode_Plus); break;
        case PictureBlend::Multiply: p.setCompositionMode(QPainter::CompositionMode_Multiply); break;
        case PictureBlend::Screen:   p.setCompositionMode(QPainter::CompositionMode_Screen); break;
        case PictureBlend::Normal:   p.setCompositionMode(QPainter::CompositionMode_SourceOver); break;
        }
        // Texto rasterizado precisa de interpolação suave quando a picture é
        // escalada/rotacionada, mesmo que a opção de pixel art esteja
        // desligada para as imagens comuns.
        p.setRenderHint(QPainter::SmoothPixmapTransform, d.smooth || d.rich.enabled);
        // Mistura não-normal com opacidade: a opacidade entra NOS PIXELS, não
        // no painter (ver imagemComAlfa — é o que casa com o caminho de GPU).
        if (d.blend != PictureBlend::Normal && op < 254.5) {
            p.setOpacity(1.0);
            const QString alphaKey = (a ? a->id : QStringLiteral("rich")) + QString::number(d.number);
            if (d.affectedByTone && hasScreenTone()) {
                // Não usa o cache de alfa aqui: a tonalidade pode interpolar
                // a cada quadro e a imagem-fonte muda mesmo com o mesmo alfa.
                QImage alphaImage = pictureImage->convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QPainter alphaPainter(&alphaImage);
                alphaPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                alphaPainter.fillRect(alphaImage.rect(), QColor(0, 0, 0, qBound(0, int(op), 255)));
                alphaPainter.end();
                p.drawImage(QPointF(0, 0), alphaImage);
            } else {
                p.drawImage(QPointF(0, 0),
                            imagemComAlfa(alphaKey, *pictureImage, int(op)));
            }
        } else {
            p.drawImage(QPointF(0, 0), *pictureImage);
        }
        p.resetTransform();
    }
    p.setOpacity(1.0);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.restore();
}

void GameSession::drawOverlay(QPainter& p)
{
    drawOverlay(p, renderState());
}

void GameSession::drawMoveRouteDebugOverlay(QPainter& p, const RuntimeRenderState& state) const
{
    if(!m_debugPresentation||!m_showMoveRouteDebug)return;
    const QVector<MoveRouteDebugEntry> entries=m_world.moveRouteDebugEntries();
    if(entries.isEmpty())return;
    const double tw=qMax(1,ed.mapInfo().tileWidth),th=qMax(1,ed.mapInfo().tileHeight);
    auto huScreen=[&](const QPoint& hu){return state.worldToScreen(QPointF(hu.x()*tw/2.0+tw/2.0,hu.y()*th/2.0+th/2.0));};
    p.save();p.setRenderHint(QPainter::Antialiasing,true);
    for(const MoveRouteDebugEntry& entry:entries){
        const QPointF actor=huScreen(entry.actorPositionHU);
        if(entry.hasPathTarget){
            QPainterPath path;path.moveTo(actor);for(const QPoint& node:entry.pathNodesHU)path.lineTo(huScreen(node));
            p.setPen(QPen(QColor(78,198,255,220),2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawPath(path);
            const QPointF target=huScreen(entry.pathTargetHU);p.setPen(QPen(QColor(255,218,88),2));p.drawEllipse(target,6,6);p.drawLine(target+QPointF(-9,0),target+QPointF(9,0));p.drawLine(target+QPointF(0,-9),target+QPointF(0,9));
        }
        p.setPen(QPen(entry.lastFailure.isEmpty()?QColor(130,255,171):QColor(255,112,112),2));p.setBrush(Qt::NoBrush);p.drawEllipse(actor,5,5);
        QString label=QStringLiteral("%1 · ticket %2 · #%3 %4 · %5%6")
            .arg(entry.target).arg(entry.ticket).arg(entry.commandIndex+1).arg(entry.commandType).arg(entry.state)
            .arg(entry.pathStatus.isEmpty()?QString():QStringLiteral(" · A*: ")+entry.pathStatus);
        if(!entry.lastFailure.isEmpty())label+=QStringLiteral(" · falha: ")+entry.lastFailure;
        QFont f=p.font();f.setPointSize(qMax(7,f.pointSize()-1));p.setFont(f);
        const QRectF textRect(actor+QPointF(8,-20),QSizeF(360,18));p.fillRect(textRect.adjusted(-3,-1,3,1),QColor(0,0,0,155));
        p.setPen(Qt::white);p.drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter,label);
    }
    p.restore();
}

void GameSession::drawOverlay(QPainter& p, const RuntimeRenderState& state)
{
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // RC2.66 — Layers 0..6 já foram intercaladas com o mundo em drawCpuFrame.
    // Layer 7 fica acima dos efeitos/subtítulos de apresentação, mas ainda
    // abaixo da UI; 8 fica acima da UI/mensagem; 9 é sempre a última Picture.
    if(!m_gpuOverlayCompositing){drawScreenEffects(p);drawSubtitles(p);}
    drawPictures(p, PictureLayer::AboveAnimations, state);
    drawSpeechBubbles(p,state);
    drawGameUi(p);

    // Debug/HUD e transições representam apresentação/timers, portanto ficam
    // entre as layers 8 e 9.
    drawMoveRouteDebugOverlay(p, state);

    if (m_gameOver) {
        drawGameOver(p);
        drawPictures(p, PictureLayer::AboveMessage, state);
        drawPictures(p, PictureLayer::AboveTimers, state);
        return;
    }

    if (m_clock.elapsed() > m_hintUntil) m_hint.clear();
    if (!m_debugPresentation || !m_showHud) {
        if (!m_hint.isEmpty()) {
            p.setPen(QColor("#ffd45e"));
            p.drawText(QRect(8, m_viewH - 24, m_viewW - 16, 22),
                       Qt::AlignVCenter | Qt::AlignLeft, m_hint);
        }
        drawPictures(p, PictureLayer::AboveMessage, state);
        drawCutsceneSkip(p);
        if(!m_gpuOverlayCompositing)drawMapTransition(p);
        drawPictures(p, PictureLayer::AboveTimers, state);
        return;
    }

    const QPoint hc = m_world.playerHalfCell();
    const QString cellTxt = QStringLiteral("%1,%2")
                                .arg(QString::number(hc.x() / 2.0),
                                     QString::number(hc.y() / 2.0));
    const QString hud = QCoreApplication::translate("GameSession",
                            "célula %1   ·   %2 passos   ·   %3 fps (%4 ms)   ·   zoom %5×")
                            .arg(cellTxt).arg(m_world.stepsTaken())
                            .arg(int(m_fps)).arg(QString::number(m_frameMs, 'f', 1))
                            .arg(int(m_zoom));
    const core::InputMap& im = ed.inputMap;
    const QString help = m_hint.isEmpty()
        ? QCoreApplication::translate("GameSession", "%1 andam   ·   %2 fala/avança   ·   %3 zoom   ·   %4 esconde   ·   %5 menu")
              .arg(QCoreApplication::translate("GameSession", "setas/WASD"), im.describe(GameAction::Confirm),
                   im.describe(GameAction::ZoomIn), im.describe(GameAction::ToggleHud),
                   im.describe(GameAction::Cancel))
        : m_hint;
    QFont f = m_font;
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRect(QRect(0, 0, m_viewW, 22));
    p.drawRect(QRect(0, m_viewH - 22, m_viewW, 22));
    p.setPen(QColor("#ddd"));
    p.drawText(QRect(8, 0, m_viewW - 16, 22), Qt::AlignVCenter | Qt::AlignLeft, hud);
    p.setPen(QColor("#999"));
    p.drawText(QRect(8, m_viewH - 22, m_viewW - 16, 22), Qt::AlignVCenter | Qt::AlignLeft, help);

    drawPictures(p, PictureLayer::AboveMessage, state);
    drawCutsceneSkip(p);
    if(!m_gpuOverlayCompositing)drawMapTransition(p);
    drawPictures(p, PictureLayer::AboveTimers, state);
}
void GameSession::drawWeatherLayer(QPainter& p) const
{
    drawWeatherLayer(p, renderState());
}

void GameSession::drawWeatherLayer(QPainter& p, const RuntimeRenderState& state) const
{
    drawWeather(p, state);
}

RuntimeWeatherFrame GameSession::weatherFrame() const
{
    return weatherFrame(renderState());
}

RuntimeWeatherFrame GameSession::weatherFrame(const RuntimeRenderState& state) const
{
    return buildRuntimeWeatherFrame(m_weather, state);
}

void GameSession::drawWeather(QPainter& p, const RuntimeRenderState& state) const
{
    // CPU consome a fotografia RuntimeWeatherFrame oficial; clipping/projeção
    // ficam no mesmo módulo usado para montar o batch QRhi.
    drawRuntimeWeatherFrame(p, weatherFrame(state), state);
}

void GameSession::appendWeatherQuads(SpriteBatcher& out)
{
    appendWeatherQuads(out, renderState());
}

void GameSession::appendWeatherQuads(SpriteBatcher& out, const RuntimeRenderState& state)
{
    // O frame de renderização também é uma fronteira de sincronização: cobre
    // o primeiro frame de Testar Mapa/Testar Jogo/LudoPlayer.
    syncWeatherFromCurrentMap();
    const RuntimeWeatherFrame frame = weatherFrame(state);
    // QRhi consome exatamente a mesma fotografia/ordem de primitivas do CPU.
    appendRuntimeWeatherFrame(out, frame);
}

void GameSession::appendRuntimeWeatherQuads(SpriteBatcher& out)
{
    appendRuntimeWeatherQuads(out, renderState());
}

void GameSession::appendRuntimeWeatherQuads(SpriteBatcher& out, const RuntimeRenderState& state)
{
    appendWeatherQuads(out, state);
}

void GameSession::drawGameOver(QPainter& p) const
{
    p.save();
    p.fillRect(QRect(0, 0, m_viewW, m_viewH), QColor(0, 0, 0, 225));
    QFont title = m_font;
    title.setBold(true);
    title.setPixelSize(qMax(34, m_viewH / 9));
    p.setFont(title);
    p.setPen(QColor("#f2f2f2"));
    p.drawText(QRect(20, m_viewH / 3, m_viewW - 40, m_viewH / 4),
               Qt::AlignCenter, QCoreApplication::translate("GameSession", "GAME OVER"));
    QFont hint = m_font;
    hint.setPixelSize(qMax(14, m_viewH / 32));
    p.setFont(hint);
    p.setPen(QColor("#aeb7c4"));
    p.drawText(QRect(20, m_viewH * 2 / 3, m_viewW - 40, 50), Qt::AlignCenter,
               QCoreApplication::translate("GameSession", "Pressione confirmar para voltar ao título"));
    p.restore();
}

// ============================================================================
//  Atores em quads (caminho de GPU)
// ============================================================================
namespace {

/// Sombra desenhada uma vez numa imagem: elipse não existe na GPU sem shader
/// próprio, então vira textura.
QImage imagemSombra()
{
    QImage img(64, 32, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawEllipse(QRectF(0, 0, 64, 32));
    return img;
}

} // namespace

void GameSession::appendFogQuads(SpriteBatcher& out, ImageProvider& prov)
{
    appendFogQuads(out, prov, renderState());
}

void GameSession::appendFogQuads(SpriteBatcher& out, ImageProvider& prov, const RuntimeRenderState& state)
{
    m_fogs.appendQuads(out, prov, state);
}

void GameSession::appendActorQuads(SpriteBatcher& out, ImageProvider& prov)
{
    appendActorQuads(out, prov, 2);
}

void GameSession::appendActorQuads(SpriteBatcher& out, ImageProvider& prov, int priorityFilter)
{
    const MapInfo& info = ed.mapInfo();
    const double w = info.tileWidth, h = info.tileHeight;
    const Editor::PlayerSettings& ps = ed.player;
    const QPointF pos = m_world.playerVisualPixel();

    struct Ator { int priority; double y; const MapEvent* ev; };
    QVector<Ator> atores;
    if (const MapDoc* d = ed.doc()) for (const MapEvent& e : d->events) {
        World::EventActorView v;
        if (m_world.eventView(e,&v)) {
            const int pr = v.priority == EventPriority::Below ? -1
                         : v.priority == EventPriority::Above ? 1 : 0;
            atores.push_back({pr, v.pixel.y()+spriteOffset(QStringLiteral("event:")+e.id).y()+eventPageVisualOffset(e).y()+h, &e});
        }
    }
    atores.push_back({0, pos.y()+spriteOffset(QStringLiteral("player")).y()+h, nullptr});
    std::stable_sort(atores.begin(), atores.end(), [](const Ator& a, const Ator& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.y < b.y;
    });

    for (const Ator& a : atores) {
        if (priorityFilter != 2 && a.priority != priorityFilter) continue;
        const double drawDepth = actorWorldDepth(a.priority, a.y);
        if (a.ev) {
            World::EventActorView v;
            if (!m_world.eventView(*a.ev, &v) || v.transparent || v.opacity <= 0) continue;
            const EventGraphic& g = v.graphic;
            const QString spriteTarget=QStringLiteral("event:")+a.ev->id;
            QRectF cell(v.pixel, QSizeF(w, h));
            cell.translate(eventPageVisualOffset(*a.ev));
            const PictureBlend blend = v.blend == QLatin1String("add") ? PictureBlend::Add
                                     : v.blend == QLatin1String("multiply") ? PictureBlend::Multiply
                                     : v.blend == QLatin1String("screen") ? PictureBlend::Screen
                                                                          : PictureBlend::Normal;
            if (g.kind == EventGraphic::Tile && g.tile.isValid()) {
                if (const Tileset* ts = ed.tilesetAt(g.tile.tilesetIdx)) {
                    const QString key = TextureKey::tileset(g.tile.tilesetIdx);
                    if (!prov.contains(key)) prov.insert(key, ts->image);
                    out.add(key, transformedSpriteRect(spriteTarget,cell), QRectF(animatedTileRect(*ts, g.tile.tx, g.tile.ty, m_clock.elapsed(),
                                                               quint32(qHash(a.ev->id)))),
                            ts->image.size(), v.opacity / 255.0*spriteOpacity(QStringLiteral("event:")+a.ev->id,v.pixel), QColor(), blend,
                            true, RuntimeCoordinateSpace::World, drawDepth);
                }
            } else if (g.kind == EventGraphic::Charset && !g.charset.isNull()) {
                const QRect src = g.charsetFrameRect();
                const QRectF dst(cell.x() + (cell.width() - src.width()) / 2.0,
                                 cell.bottom() - src.height(), src.width(), src.height());
                const QString key = TextureKey::image(
                    QStringLiteral("evchar:%1:%2:%3:%4").arg(a.ev->id).arg(v.page)
                                                        .arg(g.characterIndex)
                                                        .arg(g.charset.cacheKey()));
                if (!prov.contains(key)) prov.insert(key, g.charset);
                out.add(key, transformedSpriteRect(spriteTarget,dst), QRectF(src), g.charset.size(), v.opacity / 255.0*spriteOpacity(QStringLiteral("event:")+a.ev->id,v.pixel),
                        QColor(), blend, true, RuntimeCoordinateSpace::World, drawDepth);
            }
            continue;
        }

        // ---- jogador ---------------------------------------------------
        if(m_world.playerTransparent()||m_world.playerOpacity()<=0)continue;
        const double playerOp=m_world.playerOpacity()/255.0*spriteOpacity(QStringLiteral("player"),pos);
        const QString playerBlendId=m_world.playerBlend();
        const PictureBlend playerBlend=playerBlendId==QLatin1String("add")?PictureBlend::Add
            :playerBlendId==QLatin1String("multiply")?PictureBlend::Multiply
            :playerBlendId==QLatin1String("screen")?PictureBlend::Screen:PictureBlend::Normal;
        if (ps.shadow) {
            const QString key = TextureKey::image(QStringLiteral("sombra"));
            if (!prov.contains(key)) prov.insert(key, imagemSombra());
            out.add(key, transformedSpriteRect(QStringLiteral("player"),QRectF(pos.x() + w * 0.18, pos.y() + h * 0.80, w * 0.64, h * 0.18)),
                    QRectF(0, 0, 64, 32), QSize(64, 32),playerOp,QColor(),playerBlend,
                    true, RuntimeCoordinateSpace::World, drawDepth);
        }
        if(const EventGraphic* overrideGraphic=m_world.playerGraphicOverride()){
            EventGraphic g=*overrideGraphic;g.dir=qBound(0,int(cardinalOf(m_world.facing())),3);g.frame=qBound(0,m_world.animFrame(),qMax(1,g.charsetCols)-1);
            if(g.kind==EventGraphic::Charset&&!g.charset.isNull()){
                const QRect src=g.charsetFrameRect();const QRectF dst(pos.x()+(w-src.width())/2.0,pos.y()+h-src.height(),src.width(),src.height());
                const QString key=TextureKey::image(QStringLiteral("player-route:%1").arg(g.charset.cacheKey()));if(!prov.contains(key))prov.insert(key,g.charset);out.add(key,transformedSpriteRect(QStringLiteral("player"),dst),QRectF(src),g.charset.size(),playerOp,QColor(),playerBlend,true,RuntimeCoordinateSpace::World,drawDepth);
            }
            continue;
        }
        if (ps.hasCharset()) {
            const QSize fs = ps.frameSize();
            if (fs.isValid() && fs.width() > 0 && fs.height() > 0) {
                const CharsetCell cc = charsetCellFor(m_world.facing(), ps);
                const int row = qBound(0, cc.row, ps.frameRows - 1);
                const int col = qBound(0, cc.colOffset + m_world.animFrame(), ps.frameCols - 1);
                const QRectF src(col * fs.width(), row * fs.height(), fs.width(), fs.height());
                const QRectF dst(pos.x() + (w - fs.width()) / 2.0,
                                 pos.y() + h - fs.height(), fs.width(), fs.height());
                const QString key=TextureKey::image(QStringLiteral("charset"));if(!prov.contains(key))prov.insert(key,ps.charset);out.add(key,transformedSpriteRect(QStringLiteral("player"),dst),src,ps.charset.size(),playerOp,QColor(),playerBlend,true,RuntimeCoordinateSpace::World,drawDepth);
                continue;
            }
        }
        // Sem charset: o bonequinho provisório é desenhado numa imagem (a GPU
        // não tem "elipse"), e ela é refeita só quando a direção muda.
        const QString key = TextureKey::image(
            QStringLiteral("heroi:%1").arg(int(cardinalOf(m_world.facing()))));
        if (!prov.contains(key)) {
            QImage img(int(w), int(h), QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter ip(&img);
            ip.setRenderHint(QPainter::Antialiasing, true);
            ip.translate(-pos.x(), -pos.y());
            const bool sombraAntes = ps.shadow;
            const_cast<Editor::PlayerSettings&>(ps).shadow = false;  // já desenhada acima
            drawPlayer(ip);
            const_cast<Editor::PlayerSettings&>(ps).shadow = sombraAntes;
            prov.insert(key, img);
        }
        out.add(key, transformedSpriteRect(QStringLiteral("player"),QRectF(pos.x(), pos.y(), w, h)), QRectF(0, 0, w, h), QSize(int(w), int(h)),playerOp,QColor(),playerBlend,true,RuntimeCoordinateSpace::World,drawDepth);
    }
}

void GameSession::appendPictureQuads(SpriteBatcher& out, ImageProvider& prov, PictureLayer camada)
{
    appendPictureQuads(out, prov, camada, renderState());
}

void GameSession::appendPictureQuads(SpriteBatcher& out, ImageProvider& prov, PictureLayer camada,
                                     const RuntimeRenderState& state)
{
    const QVector<const LivePicture*> lista = m_pics.ordered();
    if (lista.isEmpty()) return;

    for (const LivePicture* lp : lista) {
        const PictureDef& d = lp->def;
        if (d.layer != camada) continue;
        if (m_uiBattle.active() && !d.duringBattle) continue;
        const PictureAsset* a = ed.pictureFor(d.assetId, d.assetName);
        if (!d.rich.enabled && (!a || !a->isValid())) continue;   // ver drawPictures
        const PictureFrame quadro = m_picFx.build(*lp, ed);
        if (!quadro.valid) continue;
        const double op = quadro.opacity;
        if (op <= 0.0) continue;

        // A textura é por SLOT (e não por imagem): duas pictures da mesma arte
        // podem ter efeitos diferentes, e cada uma precisa da sua composição.
        const QString chave = TextureKey::image(
            QStringLiteral("pic:%1:%2").arg(d.number).arg(a ? a->id : QStringLiteral("txt")));
        prov.insert(chave, quadro.image);

        const RuntimePictureTransform transform = makeRuntimePictureTransform(state, *lp, quadro);
        if (!transform.valid) continue;

        // O batch recebe a transformação no espaço declarado. O QRhi projeta
        // World somente no renderer; Screen permanece imune à câmera/zoom.
        out.addTransformed(chave, transform.declaredSpaceTransform, transform.localRect,
                           QRectF(QPointF(0, 0), QSizeF(quadro.image.size())),
                           quadro.image.size(), op / 255.0, d.blend, d.affectedByTone,
                           transform.space, 0.0, d.smooth);
    }
}


} // namespace game
