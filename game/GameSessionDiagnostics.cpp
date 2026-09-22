#include "GameSession.h"
#include "PictureLayerRenderGraph.h"

#include "core/ProjectValidator.h"
#include "core/GameValueRegistry.h"
#include "core/ResourceManager.h"
#include "core/RuntimeProjectSnapshot.h"
#include "core/Version.h"
#include "core/ZipWriter.h"
#include "game/RuntimeBackendMatrix.h"
#include "game/RuntimeGpuPolicy.h"
#include "game/RuntimePersistence.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

#include <algorithm>

namespace game {
namespace {

QString menuScreenName(ui::UiMenuScreen screen)
{
    switch (screen) {
    case ui::UiMenuScreen::Closed: return QStringLiteral("closed");
    case ui::UiMenuScreen::Main: return QStringLiteral("main");
    case ui::UiMenuScreen::Status: return QStringLiteral("status");
    case ui::UiMenuScreen::Inventory: return QStringLiteral("inventory");
    case ui::UiMenuScreen::InventoryTarget: return QStringLiteral("inventory-target");
    case ui::UiMenuScreen::EquipmentActor: return QStringLiteral("equipment-actor");
    case ui::UiMenuScreen::EquipmentSlot: return QStringLiteral("equipment-slot");
    case ui::UiMenuScreen::EquipmentItem: return QStringLiteral("equipment-item");
    case ui::UiMenuScreen::SkillsActor: return QStringLiteral("skills-actor");
    case ui::UiMenuScreen::Skills: return QStringLiteral("skills");
    case ui::UiMenuScreen::SkillTarget: return QStringLiteral("skill-target");
    case ui::UiMenuScreen::Quests: return QStringLiteral("quests");
    case ui::UiMenuScreen::Save: return QStringLiteral("save");
    case ui::UiMenuScreen::Load: return QStringLiteral("load");
    case ui::UiMenuScreen::SaveOverwrite: return QStringLiteral("save-overwrite");
    case ui::UiMenuScreen::Settings: return QStringLiteral("settings");
    }
    return QStringLiteral("unknown");
}

bool debugValueCompare(const QVariant& left, const QString& op, const QVariant& right)
{
    if (op == QLatin1String("contains")) return left.toString().contains(right.toString());
    if (op == QLatin1String("startsWith")) return left.toString().startsWith(right.toString());
    if (op == QLatin1String("endsWith")) return left.toString().endsWith(right.toString());
    bool aOk=false,bOk=false;const double a=left.toDouble(&aOk),b=right.toDouble(&bOk);
    if(aOk&&bOk){if(op==QLatin1String(">"))return a>b;if(op==QLatin1String(">="))return a>=b;if(op==QLatin1String("<"))return a<b;if(op==QLatin1String("<="))return a<=b;if(op==QLatin1String("!="))return a!=b;return a==b;}
    return op==QLatin1String("!=") ? left.toString()!=right.toString() : left.toString()==right.toString();
}

QJsonObject locationToJson(const DebugCommandLocation& location)
{
    return QJsonObject{{QStringLiteral("source"), location.source},
                       {QStringLiteral("mapId"), location.mapId},
                       {QStringLiteral("eventId"), location.eventId},
                       {QStringLiteral("commandType"), location.commandType},
                       {QStringLiteral("commandIndex"), location.commandIndex},
                       {QStringLiteral("callDepth"), location.callDepth}};
}

QString frameSequenceIssueId(core::FrameSequenceIssue issue)
{
    switch (issue) {
    case core::FrameSequenceIssue::None: return QStringLiteral("none");
    case core::FrameSequenceIssue::ImageUnavailable: return QStringLiteral("image-unavailable");
    case core::FrameSequenceIssue::CountExceedsGrid: return QStringLiteral("count-exceeds-grid");
    case core::FrameSequenceIssue::GridExceedsImage: return QStringLiteral("grid-exceeds-image");
    case core::FrameSequenceIssue::ImageNotDivisible: return QStringLiteral("image-not-divisible");
    }
    return QStringLiteral("unknown");
}

} // namespace

void GameSession::debugPause()
{
    m_eventDebugger.pause();
    m_overlayDirty = true;
}

void GameSession::debugContinue()
{
    m_eventDebugger.continueRun();
    m_overlayDirty = true;
}

void GameSession::debugStep() { debugStepInto(); }

void GameSession::debugStepInto()
{
    m_eventDebugger.stepInto();
    m_overlayDirty = true;
}

void GameSession::debugStepOver()
{
    m_eventDebugger.stepOver();
    m_overlayDirty = true;
}

void GameSession::debugStepOut()
{
    m_eventDebugger.stepOut();
    m_overlayDirty = true;
}

void GameSession::debugPumpOneCommand()
{
    const QString wantedSource = m_eventDebugger.current().source;
    m_eventDebugger.stepInto();
    syncParallelInterpreters();
    const auto containsSource=[&](const Interpreter& interpreter){
        if(wantedSource.isEmpty())return true;
        for(const Interpreter::DebugFrameInfo& frame:interpreter.debugCallStack())if(frame.source==wantedSource)return true;
        return false;
    };
    bool pumped = false;
    if (m_interp.running() && containsSource(m_interp)) {
        m_interp.update(0.0, false);
        pumped = true;
    }
    if (!pumped) {
        for (auto it=m_parallel.begin();it!=m_parallel.end();++it) {
            const auto& interpreter = it.value();
            if (!interpreter || !interpreter->running()) continue;
            if (!containsSource(*interpreter)) continue;
            interpreter->update(0.0, false); pumped = true; break;
        }
    }
    if (!pumped && m_interp.running()) m_interp.update(0.0, false);
    processPendingRuntimeActions();
    m_profiler.setRuntimeCounts((m_interp.running() ? 1 : 0) + int(m_parallel.size()), int(m_debugTrace.size()));
    m_overlayDirty = true;
}

void GameSession::debugPumpStepOver()
{
    m_eventDebugger.stepOver();
    syncParallelInterpreters();
    Interpreter* target = nullptr;
    const QString wanted = m_eventDebugger.current().source;
    const auto containsSource=[&](const Interpreter& interpreter){
        for(const Interpreter::DebugFrameInfo& frame:interpreter.debugCallStack())if(frame.source==wanted)return true;return false;
    };
    if(m_interp.running()&&(wanted.isEmpty()||containsSource(m_interp)))target=&m_interp;
    if(!target)for(auto it=m_parallel.begin();it!=m_parallel.end();++it)if(it.value()&&it.value()->running()&&(wanted.isEmpty()||containsSource(*it.value()))){target=it.value().get();break;}
    if(target)target->update(0.0,false);
    processPendingRuntimeActions();m_overlayDirty=true;
}

void GameSession::debugPumpStepOut()
{
    m_eventDebugger.stepOut();
    syncParallelInterpreters();
    Interpreter* target = nullptr;
    const QString wanted = m_eventDebugger.current().source;
    const auto containsSource=[&](const Interpreter& interpreter){
        for(const Interpreter::DebugFrameInfo& frame:interpreter.debugCallStack())if(frame.source==wanted)return true;return false;
    };
    if(m_interp.running()&&(wanted.isEmpty()||containsSource(m_interp)))target=&m_interp;
    if(!target)for(auto it=m_parallel.begin();it!=m_parallel.end();++it)if(it.value()&&it.value()->running()&&(wanted.isEmpty()||containsSource(*it.value()))){target=it.value().get();break;}
    if(target)target->update(0.0,false);
    processPendingRuntimeActions();m_overlayDirty=true;
}

QVector<Interpreter::DebugFrameInfo> GameSession::debugCallStack() const
{
    const QString wanted=m_eventDebugger.current().source;
    const auto stackContains=[&](const QVector<Interpreter::DebugFrameInfo>& stack){for(const auto& f:stack)if(f.source==wanted)return true;return false;};
    if(m_interp.running()){const auto stack=m_interp.debugCallStack();if(wanted.isEmpty()||stackContains(stack))return stack;}
    for(auto it=m_parallel.cbegin();it!=m_parallel.cend();++it)if(it.value()&&it.value()->running()){const auto stack=it.value()->debugCallStack();if(wanted.isEmpty()||stackContains(stack))return stack;}
    return {};
}

bool GameSession::hotReload(QStringList* diagnostics)
{
    QStringList local;

    // Em F5/F6 o runtime é um snapshot. Se houver Editor vivo associado,
    // primeiro construímos o novo modelo executável fora da sessão e validamos
    // TODOS os frames ativos contra ele. Nenhum GameState ou frame é mutado
    // antes dessa preflight passar.
    if (m_hotReloadSource && m_hotReloadSource.data() != &ed) {
        QString snapshotError;
        std::unique_ptr<core::Editor> fresh = core::makeRuntimeEditorSnapshot(*m_hotReloadSource, &snapshotError);
        if (!fresh) {
            local.push_back(snapshotError.isEmpty() ? QStringLiteral("Hot Reload: não foi possível criar Runtime Snapshot.") : snapshotError);
            if (diagnostics) *diagnostics = local;
            return false;
        }

        const QString liveMapId = ed.doc() ? ed.doc()->id : QString();
        const int liveMapIndex = liveMapId.isEmpty() ? -1 : fresh->mapIndexById(liveMapId);
        if (!liveMapId.isEmpty() && liveMapIndex < 0) {
            local.push_back(QStringLiteral("Hot Reload recusado: o mapa runtime ativo não existe mais: %1").arg(liveMapId));
            if (diagnostics) *diagnostics = local;
            return false;
        }
        if (liveMapIndex >= 0) {
            const core::MapInfo& nextMap = fresh->docs.at(liveMapIndex).map;
            const QPoint playerHalf = m_world.playerHalfCell();
            const int maxPlayerHx = qMax(0, nextMap.width * 2 - 2);
            const int maxPlayerHy = qMax(0, nextMap.height * 2 - 2);
            if (playerHalf.x() > maxPlayerHx || playerHalf.y() > maxPlayerHy) {
                local.push_back(QStringLiteral("Hot Reload recusado: o novo tamanho do mapa deixaria o jogador fora dos limites."));
                if (diagnostics) *diagnostics = local;
                return false;
            }
            const QJsonArray liveEvents = m_world.runtimeState().value(QStringLiteral("events")).toArray();
            const int maxEventHx = qMax(0, nextMap.width * 2 - 1);
            const int maxEventHy = qMax(0, nextMap.height * 2 - 1);
            for (const QJsonValue& value : liveEvents) {
                const QJsonObject event = value.toObject();
                if (event.value(QStringLiteral("xHalf")).toInt() > maxEventHx ||
                    event.value(QStringLiteral("yHalf")).toInt() > maxEventHy) {
                    local.push_back(QStringLiteral("Hot Reload recusado: o novo tamanho do mapa deixaria um evento vivo fora dos limites."));
                    if (diagnostics) *diagnostics = local;
                    return false;
                }
            }
        }
        if (!m_interp.canHotReloadTo(*fresh, &local)) {
            if (diagnostics) *diagnostics = local;
            return false;
        }
        for (auto it=m_parallel.cbegin(); it!=m_parallel.cend(); ++it) {
            if (it.value() && !it.value()->canHotReloadTo(*fresh, &local)) {
                if (diagnostics) *diagnostics = local;
                return false;
            }
        }

        if (!core::refreshRuntimeEditorSnapshot(ed, *m_hotReloadSource, &snapshotError)) {
            local.push_back(snapshotError.isEmpty() ? QStringLiteral("Hot Reload: falha ao atualizar Runtime Snapshot.") : snapshotError);
            if (diagnostics) *diagnostics = local;
            return false;
        }
        local.push_back(QStringLiteral("Runtime Snapshot atualizado a partir do Editor vivo."));
    }

    // A preflight acima torna o commit dos frames determinístico. O estado vivo
    // continua sendo o mesmo objeto: apenas símbolos/DBs/mapas novos são
    // completados e referências de comandos são reassociadas por IDs estáveis.
    bool ok=m_interp.hotReload(&local);
    for(auto it=m_parallel.begin();it!=m_parallel.end();){
        if(!it.value()){it=m_parallel.erase(it);continue;}
        QStringList frameIssues;
        if(!it.value()->hotReload(&frameIssues)){
            local.append(frameIssues);it.value()->stop();it=m_parallel.erase(it);ok=false;continue;
        }
        local.append(frameIssues);++it;
    }
    if (!ok) {
        if (diagnostics) *diagnostics = local;
        return false;
    }

    m_state.reconcileLoadedSave(ed);
    m_state.ensureCustomDatabasesFrom(ed);
    m_state.ensureRuntimeMapsFrom(ed);
    // Caches derivados nunca são estado de gameplay. Recrie-os a partir do
    // modelo novo para que Hot Reload não misture dados antigos e novos.
    ed.resources().invalidateCaches();
    m_autoLudoCache.clear();
    m_subtitleGpuImages.clear();
    m_subtitleGpuSignatures.clear();
    m_picAlfaCache.clear();
    for(auto it=m_reservedCommonEvents.begin();it!=m_reservedCommonEvents.end();){
        if(!ed.commonEventById(it->commonId)){
            local.push_back(QStringLiteral("Reserva removida porque o Common Event não existe mais: %1").arg(it->commonId));
            it=m_reservedCommonEvents.erase(it);
        } else ++it;
    }

    // O mapa/posição/rotas permanecem exatamente vivos. Alterações que
    // encolheriam o mapa sobre atores ativos já foram recusadas na preflight.
    m_world.config.from(ed.player);
    refreshPlayerPreferences();
    m_subs.setStyle(ed.subtitleStyle);
    refreshLocalizationState();
    m_commonScheduleTickSerial=0;
    updateCommonEventScheduler();
    syncParallelInterpreters();

    // O Editor runtime é QObject e seus consumidores (QRhi/ResourceManager)
    // já escutam estes sinais. Emiti-los aqui invalida revisão/caches depois da
    // troca atômica, evitando modelo novo com mesh/textura antiga.
    ed.mapChanged();
    ed.layersChanged();
    ed.tilesetsChanged();
    ed.wangChanged();
    ed.picturesChanged();
    ed.docsChanged();
    ed.projectChanged();
    m_overlayDirty=true;
    if(diagnostics)*diagnostics=local;
    return true;
}

QStringList GameSession::activeCommonEvents() const
{
    QStringList result;
    const auto appendSource = [&](const QString& source) {
        if (!source.startsWith(QLatin1String("common:"))) return;
        const QString token=source.mid(QStringLiteral("common:").size());
        const core::CommonEvent* common=nullptr;
        if(token.startsWith(QLatin1String("number:"))) common=ed.commonEventByNumber(token.mid(7).toInt());
        else common=ed.commonEventById(token);
        const QString label = common ? QStringLiteral("%1 — %2").arg(common->number).arg(common->name) : token;
        if (!result.contains(label)) result.push_back(label);
    };
    if (m_interp.running()) appendSource(m_interp.debugSource());
    for (auto it=m_parallel.cbegin(); it!=m_parallel.cend(); ++it)
        if (it.value() && it.value()->running()) appendSource(it.value()->debugSource());
    for(const core::CommonEvent& common:ed.commonEvents){
        const QString id=common.id.isEmpty()?QString::number(common.number):common.id;const auto st=m_commonSchedule.constFind(id);
        if(st==m_commonSchedule.cend()||!st->pending)continue;
        const QString label=QStringLiteral("%1 — %2 [pendente]").arg(common.number).arg(common.name);if(!result.contains(label))result.push_back(label);
    }
    for(const ReservedCommonCall& call:m_reservedCommonEvents)if(const core::CommonEvent* common=ed.commonEventById(call.commonId)){
        const QString label=QStringLiteral("%1 — %2 [reservado p=%3]").arg(common->number).arg(common->name).arg(call.priority);if(!result.contains(label))result.push_back(label);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

quint64 GameSession::addDebugWatch(DebugWatchKind kind, const QString& reference, const QString& label)
{
    const quint64 serial=m_debugWatches.add(kind,reference,label);
    updateDebugWatches();
    return serial;
}

QVariant GameSession::resolveDebugWatch(const DebugWatch& watch, const Interpreter* context) const
{
    if(watch.kind==DebugWatchKind::Variable)return m_state.variable(watch.reference.toInt());
    if(watch.kind==DebugWatchKind::Switch)return m_state.switchOn(watch.reference.toInt());
    if(watch.kind==DebugWatchKind::String)return m_state.stringValue(watch.reference.toInt());
    const Interpreter* interpreter=context?context:visibleInterpreter();
    if(!interpreter||!core::isKnownGameValue(watch.reference))return {};
    QVariantMap query{{QStringLiteral("key"),watch.reference}};
    const core::GameValueDescriptor* descriptor=core::gameValueDescriptor(watch.reference);
    if(descriptor&&descriptor->needsEvent)query.insert(QStringLiteral("eventId"),interpreter->currentEventId());
    const QVariantMap spec{{QStringLiteral("source"),QStringLiteral("gameValue")},{QStringLiteral("query"),query}};
    return interpreter->resolveSchedulerSource(spec,core::gameValueType(watch.reference));
}

void GameSession::updateDebugWatches()
{
    const Interpreter* context=visibleInterpreter();
    m_debugWatches.update([this,context](const DebugWatch& watch){return resolveDebugWatch(watch,context);});
}

bool GameSession::evaluateDebugBreakpointCondition(const QVariantMap& condition,
                                                   const Interpreter& context) const
{
    DebugWatch watch;
    const quint64 serial=condition.value(QStringLiteral("watchSerial")).toULongLong();
    if(serial){for(const DebugWatch& candidate:m_debugWatches.watches())if(candidate.serial==serial){watch=candidate;break;}}
    if(watch.reference.isEmpty()){
        watch.kind=debugWatchKindFromId(condition.value(QStringLiteral("kind")).toString());
        watch.reference=condition.value(QStringLiteral("reference")).toString();
    }
    if(watch.reference.isEmpty())return false;
    const QVariant current=resolveDebugWatch(watch,&context);
    return debugValueCompare(current,condition.value(QStringLiteral("op"),QStringLiteral("==")).toString(),condition.value(QStringLiteral("value")));
}

EventRuntimeDebugState GameSession::eventRuntimeDebugState() const
{
    EventRuntimeDebugState result;
    if(const Interpreter* interpreter=visibleInterpreter())result=interpreter->runtimeDebugState();
    const EventRuntimeDebugState recorded=m_eventDebugger.runtimeState();
    result.current=m_eventDebugger.current();
    result.targetResolution=recorded.targetResolution;
    result.conditionEvaluation=recorded.conditionEvaluation;
    QList<quint64> tickets=m_inlineParallelBlocks.keys();std::sort(tickets.begin(),tickets.end());
    for(quint64 ticket:tickets)result.parallelTickets.push_back(ticket);
    return result;
}

void GameSession::appendDebugTrace(QString phase, QString source, QString eventId,
                                   int commandIndex, int callDepth, QString commandType,
                                   QVariantMap details)
{
    DebugTraceEntry entry;entry.sequence=++m_debugSequence;entry.phase=std::move(phase);entry.source=std::move(source);
    entry.mapId=currentMapId();entry.eventId=std::move(eventId);entry.commandType=std::move(commandType);
    entry.commandIndex=commandIndex;entry.callDepth=callDepth;entry.details=std::move(details);m_debugTrace.push_back(std::move(entry));
    if(m_debugTrace.size()>1000)m_debugTrace.remove(0,m_debugTrace.size()-1000);
}

QJsonArray GameSession::debugTraceJson(const QString& filter) const
{
    QJsonArray result;const QString needle=filter.trimmed();
    for(const DebugTraceEntry& item:m_debugTrace){
        const QString searchable=QStringLiteral("%1 %2 %3 %4 %5").arg(item.phase,item.source,item.mapId,item.eventId,item.commandType);
        if(!needle.isEmpty()&&!searchable.contains(needle,Qt::CaseInsensitive))continue;
        result.append(QJsonObject{{QStringLiteral("sequence"),double(item.sequence)},{QStringLiteral("phase"),item.phase},{QStringLiteral("source"),item.source},
                                  {QStringLiteral("mapId"),item.mapId},{QStringLiteral("eventId"),item.eventId},{QStringLiteral("commandType"),item.commandType},
                                  {QStringLiteral("commandIndex"),item.commandIndex},{QStringLiteral("callDepth"),item.callDepth},
                                  {QStringLiteral("details"),QJsonObject::fromVariantMap(item.details)}});
    }
    return result;
}

QString GameSession::debugTraceText(const QString& filter) const
{
    QString result;
    for(const QJsonValue& value:debugTraceJson(filter)){
        const QJsonObject item=value.toObject();
        result+=QStringLiteral("%1\t%2\t%3\t%4\t%5\t#%6\tdepth=%7\t%8\n")
            .arg(item.value(QStringLiteral("sequence")).toInt()).arg(item.value(QStringLiteral("phase")).toString(),item.value(QStringLiteral("source")).toString(),
                 item.value(QStringLiteral("mapId")).toString(),item.value(QStringLiteral("eventId")).toString())
            .arg(item.value(QStringLiteral("commandIndex")).toInt()+1).arg(item.value(QStringLiteral("callDepth")).toInt())
            .arg(QString::fromUtf8(QJsonDocument(item.value(QStringLiteral("details")).toObject()).toJson(QJsonDocument::Compact)));
    }
    return result;
}

QString GameSession::debugUiScreen() const
{
    if (m_uiBattle.active()) return QStringLiteral("battle");
    if (m_uiShop.active()) return QStringLiteral("shop");
    if (m_uiModal.active()) return QStringLiteral("modal");
    if (m_uiMenu.customActive()) return QStringLiteral("custom:") + m_uiMenu.customScreenId();
    if (m_uiMenu.active()) return QStringLiteral("menu:") + menuScreenName(m_uiMenu.screen());
    return QStringLiteral("gameplay");
}

QString GameSession::debugUiFocus() const
{
    if (m_uiMenu.customActive() || m_uiMenu.active()) {
        const QString focus = m_uiMenu.focusedElementId();
        if (!focus.isEmpty()) return focus;
        return QStringLiteral("index:%1").arg(m_uiMenu.selected());
    }
    if (m_uiBattle.active()) return QStringLiteral("battle-index:%1").arg(m_uiBattle.selected());
    if (m_uiShop.active()) return QStringLiteral("shop-index:%1").arg(m_uiShop.selected());
    return QString();
}

void GameSession::setFrameCost(double ms)
{
    m_frameMs = qMax(0.0, ms);
    m_profiler.submitFrame(m_frameMs, m_fps);
}

void GameSession::setRenderStats(const QString& renderer, int drawCalls, int quads)
{
    m_profiler.setRenderStats(renderer, drawCalls, quads);
}

void GameSession::setProfileStageTiming(const QString& stage, double ms)
{
    m_profiler.setStageTiming(stage, ms);
}

void GameSession::setFramePacingStats(int lateFrames, int totalFrames, double lateRatio,
                                      double lastOverrunMs, double worstOverrunMs)
{
    m_profiler.setFramePacingStats(lateFrames, totalFrames, lateRatio, lastOverrunMs, worstOverrunMs);
}

void GameSession::setRenderWorkStats(qint64 textureUploadBytes, qint64 vertexUploadBytes,
                                     int textureUploads, int mapCacheHits, int mapCacheMisses,
                                     int pipelineChanges, int shaderResourceChanges,
                                     qint64 staticMapVertexUploadBytes,
                                     qint64 dynamicVertexUploadBytes,
                                     int gpuMapMeshBuilds)
{
    m_profiler.setRenderWorkStats(textureUploadBytes, vertexUploadBytes, textureUploads,
                                  mapCacheHits, mapCacheMisses, pipelineChanges, shaderResourceChanges,
                                  staticMapVertexUploadBytes, dynamicVertexUploadBytes, gpuMapMeshBuilds);
}

QJsonObject GameSession::diagnosticSnapshot() const
{
    QJsonObject root;
    root.insert(QStringLiteral("engineVersion"), QString::fromLatin1(core::version::Engine));
    root.insert(QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("projectName"), ed.projectName);
    root.insert(QStringLiteral("projectFormat"), core::version::ProjectFormat);
    root.insert(QStringLiteral("saveFormat"), core::version::SaveFormat);
    root.insert(QStringLiteral("mapId"), currentMapId());
    root.insert(QStringLiteral("mapName"), ed.doc() ? ed.doc()->name : QString());
    root.insert(QStringLiteral("playerX"), m_world.playerCell().x());
    root.insert(QStringLiteral("playerY"), m_world.playerCell().y());
    root.insert(QStringLiteral("uiScreen"), debugUiScreen());
    root.insert(QStringLiteral("uiFocus"), debugUiFocus());
    root.insert(QStringLiteral("debugPaused"), debugPaused());
    root.insert(QStringLiteral("debugBlocked"), debugBlocked());
    root.insert(QStringLiteral("currentCommand"), locationToJson(debugLocation()));
    root.insert(QStringLiteral("profiler"), profilerToJson(profilerSnapshot()));
    root.insert(QStringLiteral("activeCommonEvents"), QJsonArray::fromStringList(activeCommonEvents()));
    root.insert(QStringLiteral("breakpoints"), QJsonArray::fromStringList(debugBreakpoints()));
    QJsonArray scheduler;for(const core::CommonEvent& common:ed.commonEvents){const QString id=common.id.isEmpty()?QString::number(common.number):common.id;const auto st=m_commonSchedule.constFind(id);if(st==m_commonSchedule.cend())continue;scheduler.append(QJsonObject{{QStringLiteral("id"),id},{QStringLiteral("number"),common.number},{QStringLiteral("name"),common.name},{QStringLiteral("trigger"),core::commonTriggerId(common.trigger)},{QStringLiteral("policy"),core::commonSchedulePolicyId(common.schedulePolicy)},{QStringLiteral("priority"),common.priority},{QStringLiteral("active"),st->active},{QStringLiteral("pending"),st->pending},{QStringLiteral("framesUntilRun"),st->framesUntilRun}});}root.insert(QStringLiteral("commonScheduler"),scheduler);
    QJsonArray actions;for(core::GameAction action:core::allGameActions())actions.append(QJsonObject{{QStringLiteral("id"),core::gameActionId(action)},{QStringLiteral("held"),inputHeld(action)},{QStringLiteral("pressed"),inputPressed(action)},{QStringLiteral("released"),inputReleased(action)},{QStringLiteral("holdFrames"),inputHoldFrames(action)}});root.insert(QStringLiteral("inputActions"),actions);
    root.insert(QStringLiteral("inputAnalog"),QJsonObject{{QStringLiteral("connected"),m_gamepadConnected},{QStringLiteral("gamepadConnected"),m_gamepadConnected},{QStringLiteral("x"),m_gamepadAxisX},{QStringLiteral("y"),m_gamepadAxisY},{QStringLiteral("leftTrigger"),m_gamepadLeftTrigger},{QStringLiteral("rightTrigger"),m_gamepadRightTrigger}});
    const QJsonObject stateSnapshot = m_state.toJson();
    root.insert(QStringLiteral("runtimeMaps"), stateSnapshot.value(QStringLiteral("runtimeMaps")));
    root.insert(QStringLiteral("runtimeMapContract"), QJsonObject{
        {QStringLiteral("deltaOverlay"), true},
        {QStringLiteral("projectMapImmutable"), true},
        {QStringLiteral("stableMapLayerTilesetIds"), true},
        {QStringLiteral("cellTileOverrides"), true},
        {QStringLiteral("passageOverrides"), true},
        {QStringLiteral("terrainOverrides"), true},
        {QStringLiteral("tilesetRemap"), true},
        {QStringLiteral("runtimeRevision"), QString::number(m_state.runtimeMapRevision())},
        {QStringLiteral("overrideCount"), m_state.runtimeMapOverrideCount()},
        {QStringLiteral("saveLoad"), true},
        {QStringLiteral("sharedCpuGpuCollision"), true}
    });

    const RuntimeVisualFrame visual = visualFrame();
    const RuntimeRenderState& render = visual.renderState;
    const QRectF visible = render.visibleWorldRect();
    const RuntimeWeatherFrame weatherFrameSnapshot = weatherFrame(render);
    root.insert(QStringLiteral("weatherRuntime"), QJsonObject{
        {QStringLiteral("type"), m_weather.typeId()},
        {QStringLiteral("intensity"), m_weather.intensity},
        {QStringLiteral("elapsed"), m_weather.elapsed},
        {QStringLiteral("runtimeOverride"), m_weather.runtimeOverride},
        {QStringLiteral("stateType"), QStringLiteral("core::WeatherState")},
        {QStringLiteral("sharedMapCommandState"), true},
        {QStringLiteral("frameDataSharedCpuGpu"), true},
        {QStringLiteral("editorPreview"), false},
        {QStringLiteral("coordinateSpace"), QStringLiteral("World")},
        {QStringLiteral("visualStage"), QString::fromLatin1(runtimeVisualStageName(RuntimeVisualStage::Weather))},
        {QStringLiteral("pictureLayersInterleaved"), true},
        {QStringLiteral("pictureLayer6AboveWeather"),
            pictureLayerRenderAnchor(core::PictureLayer::AboveWeather) == PictureLayerRenderAnchor::AfterWeather},
        {QStringLiteral("uiAboveWeather"),
            runtimeVisualStageBefore(RuntimeVisualStage::Weather, RuntimeVisualStage::Ui)},
        {QStringLiteral("primitives"), weatherFrameSnapshot.primitives.size()},
        {QStringLiteral("particles"), weatherFrameSnapshot.particleCount},
        {QStringLiteral("fieldCellsVisited"), weatherFrameSnapshot.fieldCellsVisited},
        {QStringLiteral("generationWorld"), QJsonObject{
            {QStringLiteral("x"), weatherFrameSnapshot.generationWorld.x()},
            {QStringLiteral("y"), weatherFrameSnapshot.generationWorld.y()},
            {QStringLiteral("w"), weatherFrameSnapshot.generationWorld.width()},
            {QStringLiteral("h"), weatherFrameSnapshot.generationWorld.height()}
        }}
    });

    QJsonArray pictureSequences;
    for (const LivePicture* live : m_pics.ordered()) {
        if (!live || !(live->def.animated || live->def.frameCount > 1)) continue;
        const core::PictureAsset* asset = live->def.rich.enabled
            ? nullptr : ed.pictureFor(live->def.assetId, live->def.assetName);
        const core::FrameSequenceValidation validation = asset
            ? live->frameValidation(asset->image.size())
            : core::FrameSequenceValidation{};
        pictureSequences.append(QJsonObject{
            {QStringLiteral("slot"), live->def.number},
            {QStringLiteral("startFrame"), live->framePlayback.startFrame},
            {QStringLiteral("currentFrame"), live->effectiveFrameIndex()},
            {QStringLiteral("requestedCount"), live->framePlayback.requestedCount},
            {QStringLiteral("count"), live->framePlayback.count},
            {QStringLiteral("fps"), live->framePlayback.fps},
            {QStringLiteral("accumulated"), live->framePlayback.accumulated},
            {QStringLiteral("loop"), live->framePlayback.loop},
            {QStringLiteral("playing"), live->framePlayback.playing},
            {QStringLiteral("paused"), live->framePlayback.paused},
            {QStringLiteral("fixedFrame"), live->framePlayback.fixedFrame},
            {QStringLiteral("mode"), live->framePlayback.modeId()},
            {QStringLiteral("independentClock"), live->framePlayback.initialized},
            {QStringLiteral("assetAvailable"), live->def.rich.enabled || asset != nullptr},
            {QStringLiteral("layoutValid"), live->def.rich.enabled ||
                 (asset && live->frameLayoutValid(asset->image.size()))},
            {QStringLiteral("configurationValid"), live->def.rich.enabled ||
                 (asset && validation.valid())},
            {QStringLiteral("validationIssue"), live->def.rich.enabled
                 ? QStringLiteral("none")
                 : (asset ? frameSequenceIssueId(validation.issue) : QStringLiteral("image-unavailable"))}
        });
    }
    root.insert(QStringLiteral("pictureSequences"), pictureSequences);
    const World::SpatialCacheDiagnostics spatial = m_world.spatialCacheDiagnostics();
    root.insert(QStringLiteral("spatialCaches"), QJsonObject{
        {QStringLiteral("collisionCells"), spatial.collisionCells},
        {QStringLiteral("indexedEventCells"), spatial.indexedEventCells},
        {QStringLiteral("indexedEventEntries"), spatial.indexedEventEntries},
        {QStringLiteral("collisionRebuilds"), QString::number(spatial.collisionRebuilds)},
        {QStringLiteral("eventIndexRebuilds"), QString::number(spatial.eventIndexRebuilds)},
        {QStringLiteral("eventIndexMoves"), QString::number(spatial.eventIndexMoves)},
        {QStringLiteral("derivedNotSerialized"), true}
    });
    root.insert(QStringLiteral("movementRoutes"), m_world.moveRouteDiagnostics());
    root.insert(QStringLiteral("movementRouteContract"), QJsonObject{
        {QStringLiteral("runtimePayloadVersion"), MoveRouteRuntime::RuntimePayloadVersion},
        {QStringLiteral("explicitRuntimeState"), true},
        {QStringLiteral("sharedPlayerEventExecutor"), true},
        {QStringLiteral("separateCustomForcedState"), true},
        {QStringLiteral("stableTickets"), true},
        {QStringLiteral("ticketJsonEncoding"), QStringLiteral("uint64-string")},
        {QStringLiteral("queue"), true},
        {QStringLiteral("pauseResumeCancel"), true},
        {QStringLiteral("pauseFreezesPhysicalMovement"), true},
        {QStringLiteral("blockedPolicies"), QJsonArray{QStringLiteral("wait"),QStringLiteral("skip"),QStringLiteral("cancel")}},
        {QStringLiteral("startModes"), QJsonArray{QStringLiteral("replace"),QStringLiteral("queue")}},
        {QStringLiteral("saveLoad"), true},
        {QStringLiteral("canonicalCommandContract"), true},
        {QStringLiteral("unknownCommandFailsExplicitly"), true},
        {QStringLiteral("legacySkipIfBlockedMigration"), true},
        {QStringLiteral("sharedPathfinder"), true},
        {QStringLiteral("pathfinderAlgorithm"), QStringLiteral("A*")},
        {QStringLiteral("pathCoordinateUnit"), QStringLiteral("half-cell")},
        {QStringLiteral("pathBehaviors"), QJsonArray{QStringLiteral("reach"),QStringLiteral("follow"),QStringLiteral("flee"),QStringLiteral("keepDistance")}},
        {QStringLiteral("dynamicTargetReplan"), true},
        {QStringLiteral("blockedPathReplan"), true},
        {QStringLiteral("pathCacheSerialized"), false},
        {QStringLiteral("pathSearchNodeLimit"), true},
        {QStringLiteral("pathIncremental"), true},
        {QStringLiteral("pathExpansionBudgetPerTick"), MovePathSearchState::DefaultExpansionBudgetPerTick},
        {QStringLiteral("pathSearchStateSerialized"), false},
        {QStringLiteral("editorPreviewUsesGameWorld"), true},
        {QStringLiteral("previewUsesRuntimeExecutor"), true},
        {QStringLiteral("projectLocalPresets"), true},
        {QStringLiteral("presetsAffectRuntimePayload"), false},
        {QStringLiteral("debugPathOverlay"), true},
        {QStringLiteral("debugPathOverlaySharedCpuQrhi"), true}
    });
    root.insert(QStringLiteral("persistenceContract"), QJsonObject{
        {QStringLiteral("runtimePayloadVersion"), RuntimeSnapshotPayloadVersion},
        {QStringLiteral("saveFormat"), core::version::SaveFormat},
        {QStringLiteral("pictures"), true},
        {QStringLiteral("weather"), true},
        {QStringLiteral("camera"), true},
        {QStringLiteral("cameraTweenPhase"), true},
        {QStringLiteral("cameraMoving"), m_camera.moving},
        {QStringLiteral("cameraElapsed"), m_camera.elapsed},
        {QStringLiteral("cameraDuration"), m_camera.duration},
        {QStringLiteral("screenTone"), true},
        {QStringLiteral("toneTransitioning"), m_screenTone.isTransitioning()},
        {QStringLiteral("screenEffects"), true},
        {QStringLiteral("filters"), true},
        {QStringLiteral("filterStackVersion"), 3},
        {QStringLiteral("movementRoutes"), true},
        {QStringLiteral("runtimeMaps"), true},
        {QStringLiteral("runtimeMapDeltaStorage"), true},
        {QStringLiteral("reservedCommonEvents"), true},
        {QStringLiteral("reservedCommonEventPriorityFifo"), true},
        {QStringLiteral("legacyRuntimeDefaults"), true},
        {QStringLiteral("runtimeProjectParity"), true},
        {QStringLiteral("backendMatrixComplete"), runtimeBackendMatrixComplete()},
        {QStringLiteral("backendMatrix"), runtimeBackendMatrixJson()}
    });

    QJsonArray debugStack;
    for(const Interpreter::DebugFrameInfo& frame:debugCallStack()){
        QJsonObject item{{QStringLiteral("source"),frame.source},{QStringLiteral("mapId"),frame.mapId},{QStringLiteral("eventId"),frame.eventId},
                         {QStringLiteral("commonEventId"),frame.commonEventId},{QStringLiteral("commonEventNumber"),frame.commonEventNumber},
                         {QStringLiteral("pageIndex"),frame.pageIndex},{QStringLiteral("commandIndex"),frame.commandIndex},{QStringLiteral("commonFrame"),frame.commonFrame}};
        item.insert(QStringLiteral("values"),QJsonObject::fromVariantMap(frame.values));debugStack.append(item);
    }
    const EventRuntimeDebugState runtimeDebug=eventRuntimeDebugState();
    QJsonArray watchJson;for(const DebugWatchSample& sample:debugWatchSamples())watchJson.append(QJsonObject{
        {QStringLiteral("serial"),double(sample.watch.serial)},{QStringLiteral("kind"),debugWatchKindId(sample.watch.kind)},
        {QStringLiteral("reference"),sample.watch.reference},{QStringLiteral("label"),sample.watch.label},
        {QStringLiteral("value"),QJsonValue::fromVariant(sample.value)},{QStringLiteral("changed"),sample.changed},
        {QStringLiteral("breakOnChange"),sample.watch.breakOnChange},{QStringLiteral("breakOperator"),sample.watch.breakOperator},
        {QStringLiteral("breakValue"),QJsonValue::fromVariant(sample.watch.breakValue)}});
    QJsonArray parallelTickets;for(quint64 ticket:runtimeDebug.parallelTickets)parallelTickets.append(double(ticket));
    root.insert(QStringLiteral("debuggerContract"),QJsonObject{
        {QStringLiteral("version"),3},{QStringLiteral("stepInto"),true},{QStringLiteral("stepOver"),true},{QStringLiteral("stepOut"),true},
        {QStringLiteral("stableFrameIds"),true},{QStringLiteral("typedCommonValues"),true},{QStringLiteral("hotReload"),true},
        {QStringLiteral("hotReloadPreservesGameState"),true},{QStringLiteral("hotReloadRemapsFrames"),true},{QStringLiteral("callStack"),debugStack},
        {QStringLiteral("cutsceneDepth"),runtimeDebug.cutsceneDepth},{QStringLiteral("cutsceneSource"),runtimeDebug.cutsceneSource},
        {QStringLiteral("awaitableKind"),runtimeDebug.awaitableKind},{QStringLiteral("awaitable"),QJsonObject::fromVariantMap(runtimeDebug.awaitableDetails)},
        {QStringLiteral("cancellationReason"),runtimeDebug.cancellationReason},{QStringLiteral("parallelTickets"),parallelTickets},
        {QStringLiteral("targetResolution"),runtimeDebug.targetResolution},{QStringLiteral("conditionEvaluation"),runtimeDebug.conditionEvaluation},
        {QStringLiteral("watches"),watchJson},{QStringLiteral("conditionalBreakpoints"),true},{QStringLiteral("liveInspector"),true}
    });

    QJsonArray coordinateSpaces;
    for (RuntimeCoordinateSpace space : {RuntimeCoordinateSpace::World, RuntimeCoordinateSpace::Camera,
                                         RuntimeCoordinateSpace::Screen, RuntimeCoordinateSpace::Ui})
        coordinateSpaces.append(QString::fromLatin1(runtimeCoordinateSpaceName(space)));
    QJsonArray visualStages;
    for (RuntimeVisualStage stage : runtimeVisualStageOrder())
        visualStages.append(QString::fromLatin1(runtimeVisualStageName(stage)));
    QJsonArray pictureLayerStages;
    for (const PictureLayerRenderContract& contract : pictureLayerRenderGraph()) {
        pictureLayerStages.append(QJsonObject{
            {QStringLiteral("layer"), pictureLayerIndex(contract.layer)},
            {QStringLiteral("anchor"), QString::fromLatin1(pictureLayerRenderAnchorName(contract.anchor))}
        });
    }

    root.insert(QStringLiteral("renderContract"), QJsonObject{
        {QStringLiteral("gpuFirst"), true},
        {QStringLiteral("productRuntime"), QStringLiteral("QRhi")},
        {QStringLiteral("gpuContractVersion"), RuntimeGpuContractVersion},
        {QStringLiteral("visualFrameSerial"), qint64(visual.serial)},
        {QStringLiteral("cameraX"), render.camera.x()},
        {QStringLiteral("cameraY"), render.camera.y()},
        {QStringLiteral("viewportW"), render.viewport.width()},
        {QStringLiteral("viewportH"), render.viewport.height()},
        {QStringLiteral("mapW"), render.mapSize.width()},
        {QStringLiteral("mapH"), render.mapSize.height()},
        {QStringLiteral("zoom"), render.zoom},
        {QStringLiteral("visibleWorldX"), visible.x()},
        {QStringLiteral("visibleWorldY"), visible.y()},
        {QStringLiteral("visibleWorldW"), visible.width()},
        {QStringLiteral("visibleWorldH"), visible.height()},
        {QStringLiteral("coordinateSpaces"), coordinateSpaces},
        {QStringLiteral("projectionChain"), QStringLiteral("World->Camera->Screen")},
        {QStringLiteral("singleFrameSnapshot"), true},
        {QStringLiteral("runtimeMapCellResolver"), true},
        {QStringLiteral("runtimeMapRevision"), QString::number(m_state.runtimeMapRevision())},
        {QStringLiteral("stageSpaceValidation"), true},
        {QStringLiteral("fogUsesOfficialProjection"), true},
        {QStringLiteral("pictureTransform"), QJsonObject{
             {QStringLiteral("sharedCpuGpu"), true},
             {QStringLiteral("sharedEditorPreview"), true},
             {QStringLiteral("anchorBasis"), QStringLiteral("OriginalImage")},
             {QStringLiteral("pivotEqualsAnchor"), true},
             {QStringLiteral("flipInsideTransform"), true},
             {QStringLiteral("transitionsUseTransform"), true},
             {QStringLiteral("zoomCommands"), true},
             {QStringLiteral("effectOffsetSpace"), QStringLiteral("Screen")}
         }},
        {QStringLiteral("pictureSequence"), QJsonObject{
             {QStringLiteral("independentClock"), true},
             {QStringLiteral("explicitPausedState"), true},
             {QStringLiteral("explicitFixedFrame"), true},
             {QStringLiteral("saveLoadState"), true},
             {QStringLiteral("playbackPayloadVersion"), 2},
             {QStringLiteral("sharedValidation"), QStringLiteral("FrameSequence::validate")},
             {QStringLiteral("nonDivisibleRejected"), true},
             {QStringLiteral("invalidLayoutFallback"), QStringLiteral("OriginalImage")}
         }},
        {QStringLiteral("panorama"), QJsonObject{
             {QStringLiteral("sharedCpuGpu"), true},
             {QStringLiteral("fixedSpace"), QStringLiteral("Screen")},
             {QStringLiteral("parallaxSpace"), QStringLiteral("World")},
             {QStringLiteral("tiledCoverage"), true},
             {QStringLiteral("loopControlsAutoScroll"), true},
             {QStringLiteral("fixedIgnoresCameraZoom"), true}
         }},
        {QStringLiteral("screenEffects"), QJsonObject{
             {QStringLiteral("sharedCpuGpuState"), true},
             {QStringLiteral("worldShakeViaRenderState"), true},
             {QStringLiteral("screenUiIgnoreCameraZoomShake"), true},
             {QStringLiteral("toneAfterWorldComposition"), false},
             {QStringLiteral("tonePerWorldStage"), true},
             {QStringLiteral("toneRespectsPictureAffectedFlag"), true},
             {QStringLiteral("globalTonePass"), false},
             {QStringLiteral("weatherReceivesTone"), true},
             {QStringLiteral("toneAfterWeather"), true},
             {QStringLiteral("toneBeforeWeather"), false},
             {QStringLiteral("flashFadeBeforeUi"), true},
             {QStringLiteral("transferFadeAfterUi"), true},
             {QStringLiteral("flashActive"), m_screenEffects.flashActive()},
             {QStringLiteral("fadeActive"), m_screenEffects.fadeActive()},
             {QStringLiteral("shakeActive"), m_screenEffects.shakeActive()},
             {QStringLiteral("shakeX"), render.screenOffset.x()},
             {QStringLiteral("shakeY"), render.screenOffset.y()}
         }},
        {QStringLiteral("filters"), QJsonObject{
             {QStringLiteral("version"), 3},
             {QStringLiteral("slotMin"), core::LudoFilterMinSlot},
             {QStringLiteral("slotMax"), core::LudoFilterMaxSlot},
             {QStringLiteral("activeCount"), m_filters.activeFilterCount()},
             {QStringLiteral("active"), m_filters.hasActiveFilters()},
             {QStringLiteral("transitioning"), m_filters.isTransitioning()},
             {QStringLiteral("defaultScope"), QStringLiteral("world")},
             {QStringLiteral("fullFrameEligible"), m_filters.fullFrameEligible()},
             {QStringLiteral("gpuFusedShaderStack"), true},
             {QStringLiteral("gpuSelectiveDomainPipelines"), true},
             {QStringLiteral("worldNeighborhoodComposited"), true},
             {QStringLiteral("scanlineNaturalSweepCycle"), true},
             {QStringLiteral("gaussianBlurKernel"), true},
             {QStringLiteral("inactiveUsesLegacyPostPipe"), true},
             {QStringLiteral("extraFramebuffer"), false},
             {QStringLiteral("extraFilterPass"), false},
             {QStringLiteral("noiseTextureUpload"), false},
             {QStringLiteral("chromatic"), QJsonObject::fromVariantMap(m_filters.chromaticAberration().toVariantMap())},
             {QStringLiteral("noise"), QJsonObject::fromVariantMap(m_filters.noise().toVariantMap())},
             {QStringLiteral("scanlines"), QJsonObject::fromVariantMap(m_filters.scanlines().toVariantMap())},
             {QStringLiteral("vignette"), QJsonObject::fromVariantMap(m_filters.vignette().toVariantMap())},
             {QStringLiteral("blur"), QJsonObject::fromVariantMap(m_filters.blur().toVariantMap())},
             {QStringLiteral("tiltShift"), QJsonObject::fromVariantMap(m_filters.tiltShift().toVariantMap())},
             {QStringLiteral("activeSlots"), QJsonArray::fromStringList(m_filters.activeSlotDescriptions())}
         }},
        {QStringLiteral("stages"), visualStages},
        {QStringLiteral("pictureLayerGraph"), pictureLayerStages}
    });

    QJsonArray memory;
    for (const core::SwitchDef& item : ed.switches)
        memory.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("switch")},
                                  {QStringLiteral("id"), item.id},
                                  {QStringLiteral("name"), item.name},
                                  {QStringLiteral("value"), m_state.switchOn(item.id)}});
    for (const core::VariableDef& item : ed.variables)
        memory.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("variable")},
                                  {QStringLiteral("id"), item.id},
                                  {QStringLiteral("name"), item.name},
                                  {QStringLiteral("value"), m_state.variable(item.id)}});
    root.insert(QStringLiteral("memory"), memory);

    root.insert(QStringLiteral("eventTrace"), debugTraceJson());

    const core::ProjectValidationResult validation = core::ProjectValidator::validate(ed);
    QJsonArray issues;
    for (const core::ValidationIssue& issue : validation.issues)
        issues.append(QJsonObject{{QStringLiteral("severity"), core::validationSeverityLabel(issue.severity)},
                                  {QStringLiteral("code"), issue.code},
                                  {QStringLiteral("location"), issue.location},
                                  {QStringLiteral("message"), issue.message}});
    root.insert(QStringLiteral("validation"), QJsonObject{{QStringLiteral("errors"), validation.errorCount},
                                                           {QStringLiteral("warnings"), validation.warningCount},
                                                           {QStringLiteral("info"), validation.infoCount},
                                                           {QStringLiteral("issues"), issues}});
    return root;
}

bool GameSession::writeDiagnosticBundle(const QString& zipPath, QString* error) const
{
    const QJsonObject snapshot = diagnosticSnapshot();
    const QByteArray json = QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
    const QString traceText=debugTraceText();
    const RuntimeProfilerSnapshot p = profilerSnapshot();
    QString profilerText = QStringLiteral(
        "renderer=%1\nfps=%2\nframe_ms=%3\navg_ms=%4\np50_ms=%5\np95_ms=%6\np99_ms=%7\nmax_ms=%8\nsamples=%9\ndraw_calls=%10\nquads=%11\ntexture_uploads=%12\ntexture_upload_bytes=%13\nvertex_upload_bytes=%14\ndynamic_vertex_upload_bytes=%15\nstatic_map_vertex_upload_bytes=%16\ngpu_map_mesh_builds=%17\nmap_cache_hits=%18\nmap_cache_misses=%19\npipeline_changes=%20\nshader_resource_changes=%21\n")
        .arg(p.renderer).arg(p.fps,0,'f',2).arg(p.frameMs,0,'f',3).arg(p.averageFrameMs,0,'f',3)
        .arg(p.p50FrameMs,0,'f',3).arg(p.p95FrameMs,0,'f',3).arg(p.p99FrameMs,0,'f',3)
        .arg(p.maxFrameMs,0,'f',3).arg(p.sampleCount).arg(p.drawCalls).arg(p.quads).arg(p.textureUploads)
        .arg(p.textureUploadBytes).arg(p.vertexUploadBytes).arg(p.dynamicVertexUploadBytes)
        .arg(p.staticMapVertexUploadBytes).arg(p.gpuMapMeshBuilds).arg(p.mapCacheHits).arg(p.mapCacheMisses)
        .arg(p.pipelineChanges).arg(p.shaderResourceChanges);
    QStringList stageNames=p.stageMs.keys(); stageNames.sort(Qt::CaseInsensitive);
    for(const QString& stage:stageNames)
        profilerText += QStringLiteral("stage.%1_ms=%2\n").arg(stage).arg(p.stageMs.value(stage),0,'f',3);
    const QByteArray profiler = profilerText.toUtf8();
    const QVector<core::zip::Entry> files = {
        {QStringLiteral("diagnostic.json"), json},
        {QStringLiteral("event-trace.tsv"), traceText.toUtf8()},
        {QStringLiteral("runtime-profiler.txt"), profiler}
    };
    return core::zip::write(zipPath, files, error);
}

} // namespace game
