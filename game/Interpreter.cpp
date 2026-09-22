#include "Interpreter.h"
#include "game/ui/UiNavigation.h"
#include "game/Fog.h"
#include "game/TextEffectRuntime.h"
#include "core/GameData.h"
#include "core/CommandRegistry.h"
#include "core/EventCommandCodec.h"
#include "core/ConditionTree.h"
#include "core/ExpressionEvaluator.h"
#include "core/GameValueRegistry.h"
#include "core/ProjectIO.h"
#include "core/Picture.h"
#include "core/NoCodePlugin.h"
#include "core/LudoCommandSystem.h"
#include "core/DialogueContent.h"
#include <QDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMetaType>
#include <QRandomGenerator>
#include <QFontMetrics>
#include <cmath>
#include <QSettings>
#include <QRegularExpression>
#include <QDebug>
#include <QtGlobal>
#include <atomic>
using namespace core;
namespace game {
QString awaitableKindId(AwaitableKind kind)
{
    switch (kind) {
    case AwaitableKind::None: return QStringLiteral("none");
    case AwaitableKind::Frames: return QStringLiteral("frames");
    case AwaitableKind::Picture: return QStringLiteral("picture");
    case AwaitableKind::MoveRoute: return QStringLiteral("moveRoute");
    case AwaitableKind::Runtime: return QStringLiteral("runtime");
    case AwaitableKind::Subtitles: return QStringLiteral("subtitles");
    case AwaitableKind::Condition: return QStringLiteral("condition");
    case AwaitableKind::ParallelBlock: return QStringLiteral("parallelBlock");
    }
    return QStringLiteral("none");
}
QString cancellationReasonId(CancellationReason reason)
{
    switch (reason) {
    case CancellationReason::Stop: return QStringLiteral("stop");
    case CancellationReason::CutsceneSkip: return QStringLiteral("cutsceneSkip");
    case CancellationReason::MapTransfer: return QStringLiteral("mapTransfer");
    case CancellationReason::LoadGame: return QStringLiteral("loadGame");
    case CancellationReason::RestartGame: return QStringLiteral("restartGame");
    case CancellationReason::ReturnTitle: return QStringLiteral("returnTitle");
    case CancellationReason::GameOver: return QStringLiteral("gameOver");
    case CancellationReason::ContextRemoved: return QStringLiteral("contextRemoved");
    }
    return QStringLiteral("stop");
}
namespace {
std::atomic<quint64> g_runtimeCommandTicket{1};
core::EventExecutionMode executionModeForTrigger(core::EventTrigger trigger)
{
    if (trigger == core::EventTrigger::Autorun) return core::EventExecutionMode::Autorun;
    if (trigger == core::EventTrigger::Parallel) return core::EventExecutionMode::Parallel;
    return core::EventExecutionMode::Normal;
}
core::EventExecutionMode executionModeForCommonTrigger(core::CommonTrigger trigger)
{
    if (trigger == core::CommonTrigger::Autorun) return core::EventExecutionMode::Autorun;
    if (trigger == core::CommonTrigger::Parallel) return core::EventExecutionMode::Parallel;
    return core::EventExecutionMode::Normal;
}
bool commandRunsInMode(core::EventExecutionMode commandMode, core::EventExecutionMode frameMode)
{
    return commandMode == core::EventExecutionMode::Normal || commandMode == frameMode;
}
CommonValueType customFieldCommonType(CustomDatabaseFieldType type)
{
    switch (type) {
    case CustomDatabaseFieldType::Number: return CommonValueType::Number;
    case CustomDatabaseFieldType::Boolean: return CommonValueType::Boolean;
    case CustomDatabaseFieldType::Text:
    case CustomDatabaseFieldType::RecordReference: return CommonValueType::Text;
    }
    return CommonValueType::Text;
}

void markMessageGlyphBirths(MessageView& view, const QVector<TypedChar>& chars,
                            int rawBegin, int rawEnd, double timeSec)
{
    const int rawCount = int(chars.size());
    rawBegin = qBound(0, rawBegin, rawCount);
    rawEnd = qBound(rawBegin, rawEnd, rawCount);
    int glyph = 0;
    for (int i = 0; i < rawEnd; ++i) {
        if (!chars.at(i).isDrawable()) continue;
        if (i >= rawBegin && glyph < view.revealTimesSec.size() && view.revealTimesSec.at(glyph) < 0.0)
            view.revealTimesSec[glyph] = qMax(0.0, timeSec);
        ++glyph;
    }
    int visible = 0;
    for (double born : view.revealTimesSec) if (born >= 0.0) ++visible;
    view.drawableRevealed = visible;
}
}

BoxPosition boxPositionFromId(const QString& id)
{
    if (id == QLatin1String("top"))    return BoxPosition::Top;
    if (id == QLatin1String("middle")) return BoxPosition::Middle;
    return BoxPosition::Bottom;
}

QString boxPositionId(BoxPosition p)
{
    switch (p) {
    case BoxPosition::Top:    return QStringLiteral("top");
    case BoxPosition::Middle: return QStringLiteral("middle");
    case BoxPosition::Bottom: return QStringLiteral("bottom");
    }
    return QStringLiteral("bottom");
}

Interpreter::Interpreter(const MessageStyle& style) : m_style(style) {}

void Interpreter::start(const MapEvent& ev, int pageIndex)
{
    // ATENÇÃO à ordem: o id precisa existir ANTES do primeiro comando rodar.
    // Antes ele era atribuído depois de `start(cmds)` — e um evento cujo
    // PRIMEIRO comando fosse "ligar interruptor próprio" simplesmente não
    // gravava nada (o baú nunca lembrava que tinha sido aberto).
    stop();
    m_lastCancellationReason = QStringLiteral("none");
    m_eventId = ev.id;
    const QVector<EventCommand>& cmds = ev.page(pageIndex).commands;
    if (cmds.isEmpty()) return;
    Frame frame;
    frame.cmds = cmds;
    frame.index = 0;
    frame.eventId = ev.id;
    frame.mapId = (m_ed && m_ed->doc()) ? m_ed->doc()->id : QString();
    frame.pageIndex = pageIndex;
    frame.executionMode = executionModeForTrigger(ev.page(pageIndex).trigger);
    frame.source = QStringLiteral("map:%1:event:%2").arg(frame.mapId, ev.id);
    m_stack.push_back(frame);
    m_running = true;
    m_instantBudget = 0;
    m_yieldPending = false;
    m_debugBlocked = false;
    m_debugSource = frame.source;
    beginCommand();
}

void Interpreter::start(const QVector<EventCommand>& cmds)
{
    core::EventExecutionContext context;
    context.mapId = (m_ed && m_ed->doc()) ? m_ed->doc()->id : QString();
    context.source = QStringLiteral("list");
    start(cmds, context);
}

void Interpreter::start(const QVector<EventCommand>& cmds, const core::EventExecutionContext& context)
{
    stop();
    m_lastCancellationReason = QStringLiteral("none");
    if (cmds.isEmpty()) return;          // evento sem comando: nada acontece
    Frame frame;
    frame.cmds = cmds;
    frame.index = 0;
    frame.mapId = !context.mapId.isEmpty() ? context.mapId
                                            : ((m_ed && m_ed->doc()) ? m_ed->doc()->id : QString());
    frame.eventId = context.mapEventId;
    frame.pageIndex = context.pageIndex;
    frame.executionMode = context.mode;
    frame.source = context.source.isEmpty() ? QStringLiteral("list") : context.source;
    m_stack.push_back(frame);
    m_eventId = frame.eventId;
    m_running = true;
    m_instantBudget = 0;
    m_yieldPending = false;
    m_debugBlocked = false;
    m_debugSource = frame.source;
    beginCommand();
}

bool Interpreter::startCommonEvent(int numero, const QVariantMap& arguments)
{
    if (!m_ed) return false;
    const CommonEvent* ce = m_ed->commonEventByNumber(numero);
    if (!ce || ce->commands.isEmpty()) return false;
    stop();
    m_lastCancellationReason = QStringLiteral("none");
    m_debugSource = ce->id.isEmpty() ? QStringLiteral("common:number:%1").arg(numero)
                                     : QStringLiteral("common:%1").arg(ce->id);
    if (!pushCommonEvent(*ce, arguments, QVariantMap(), true)) return false;
    m_running = true;
    m_instantBudget = 0;
    m_yieldPending = false;
    m_debugBlocked = false;
    beginCommand();
    return true;
}

QVariant Interpreter::coerceCommonValue(const QVariant& value, CommonValueType type) const
{
    return core::normalizeCommonValue(value, type);
}

int Interpreter::commonFrameIndex() const
{
    for (int i = m_stack.size() - 1; i >= 0; --i)
        if (m_stack.at(i).commonFrame) return i;
    return -1;
}

QVariant Interpreter::commonValue(const QString& id) const
{
    const int index = commonFrameIndex();
    if (id.isEmpty() || index < 0) return QVariant();
    return m_stack.at(index).commonValues.value(id);
}

CommonValueType Interpreter::commonValueType(const QString& id, CommonValueType fallback) const
{
    const int index = commonFrameIndex();
    if (id.isEmpty() || index < 0 || !m_ed) return fallback;
    const CommonEvent* ce = m_ed->commonEventByNumber(m_stack.at(index).commonEventNumber);
    if (!ce) return fallback;
    for (const CommonEventParameter& def : ce->parameters) if (def.id == id) return def.type;
    for (const CommonEventLocal& def : ce->locals) if (def.id == id) return def.type;
    return fallback;
}

QVariant Interpreter::resolveValueSpec(const QVariantMap& spec, CommonValueType type) const
{
    return resolveValueSpecDepth(spec, type, 0);
}

QVariant Interpreter::resolveValueSpecDepth(const QVariantMap& spec, CommonValueType type, int depth) const
{
    if (depth > 8) return coerceCommonValue(QVariant(), type);
    const QString source = spec.value(QStringLiteral("source"), QStringLiteral("constant")).toString();
    QVariant value;
    if (source == QLatin1String("expression") ||
        (source == QLatin1String("constant") && type != CommonValueType::Text &&
         looksLikeExpression(spec.value(QStringLiteral("value")).toString()))) {
        const QString expression = source == QLatin1String("expression")
            ? spec.value(QStringLiteral("expression")).toString()
            : spec.value(QStringLiteral("value")).toString();
        ExpressionContext context;
        context.resolveFunction = [this](const QString& name, const QVariantList& arguments,
                                         bool* handled, QString* error) -> QVariant {
            const QString function=name.toLower();
            if(function==QLatin1String("v")||function==QLatin1String("s")||function==QLatin1String("switch")){
                if(arguments.size()!=1){if(error)*error=QObject::tr("A função %1 espera um ID.").arg(name);if(handled)*handled=true;return {};}
                const int id=arguments.first().toInt();if(handled)*handled=true;
                if(function==QLatin1String("v"))return m_state?QVariant(m_state->variable(id)):QVariant(0);
                if(function==QLatin1String("s"))return m_state?QVariant(m_state->stringValue(id)):QVariant(QString());
                return m_state?QVariant(m_state->switchOn(id)):QVariant(false);
            }
            if(!isKnownUniversalExpressionFunction(name))return {};
            if(handled)*handled=true;
            QString queryError;const QVariantMap query=gameValueQueryForExpressionFunction(name,arguments,&queryError);
            if(!queryError.isEmpty()){if(error)*error=queryError;return {};}
            return m_gameValueHook&&!query.isEmpty()?m_gameValueHook(query):QVariant();
        };
        const ExpressionResult evaluated=evaluateExpression(expression,context);
        value=evaluated.ok()?evaluated.value:QVariant();
    } else if (source == QLatin1String("variable")) {
        value = m_state ? QVariant(m_state->variable(spec.value(QStringLiteral("variableId"), 1).toInt())) : QVariant(0);
    } else if (source == QLatin1String("switch")) {
        value = m_state ? QVariant(m_state->switchOn(spec.value(QStringLiteral("switchId"), 1).toInt())) : QVariant(false);
    } else if (source == QLatin1String("string")) {
        value = m_state ? QVariant(m_state->stringValue(spec.value(QStringLiteral("stringId"), 1).toInt())) : QVariant(QString());
    } else if (source == QLatin1String("commonValue")) {
        value = commonValue(spec.value(QStringLiteral("commonValueId")).toString());
    } else if (source == QLatin1String("gameValue")) {
        value = m_gameValueHook ? m_gameValueHook(spec.value(QStringLiteral("query")).toMap()) : QVariant();
    } else if (source == QLatin1String("databaseField")) {
        const QString databaseId = spec.value(QStringLiteral("databaseId")).toString();
        QString recordId = spec.value(QStringLiteral("recordId")).toString();
        const QVariantMap recordSpec = spec.value(QStringLiteral("recordSpec")).toMap();
        if (!recordSpec.isEmpty()) recordId = resolveValueSpecDepth(recordSpec, CommonValueType::Text, depth + 1).toString();
        const QString fieldId = spec.value(QStringLiteral("fieldId")).toString();
        value = (m_state && m_ed) ? m_state->customDatabaseValue(*m_ed, databaseId, recordId, fieldId) : QVariant();
    } else if (source == QLatin1String("random")) {
        const int a = spec.value(QStringLiteral("minimum"), 0).toInt();
        const int b = spec.value(QStringLiteral("maximum"), 0).toInt();
        value = (b > a) ? a + QRandomGenerator::global()->bounded(b - a + 1) : a;
    } else if (source == QLatin1String("stringMetric")) {
        const QString text = m_state ? m_state->stringValue(spec.value(QStringLiteral("stringId"), 1).toInt()) : QString();
        const QString operation = spec.value(QStringLiteral("operation"), QStringLiteral("toNumber")).toString();
        if (operation == QLatin1String("length")) value = text.size();
        else if (operation == QLatin1String("lineCount")) value = text.isEmpty() ? 0 : text.count(QLatin1Char('\n')) + 1;
        else if (operation == QLatin1String("indexOf")) value = text.indexOf(spec.value(QStringLiteral("needle")).toString());
        else if (operation == QLatin1String("count")) {
            const QString needle = spec.value(QStringLiteral("needle")).toString();
            value = needle.isEmpty() ? 0 : text.count(needle);
        } else {
            bool ok = false;
            const double number = text.trimmed().toDouble(&ok);
            value = ok && std::isfinite(number) ? qRound64(number) : 0;
        }
    } else if (source == QLatin1String("numberText")) {
        value = QString::number(m_state ? m_state->variable(spec.value(QStringLiteral("variableId"), 1).toInt()) : 0);
    } else if (source == QLatin1String("switchText")) {
        value = (m_state && m_state->switchOn(spec.value(QStringLiteral("switchId"), 1).toInt()))
            ? QStringLiteral("true") : QStringLiteral("false");
    } else {
        value = spec.value(QStringLiteral("value"));
    }
    return coerceCommonValue(value, type);
}

void Interpreter::applyValueTarget(const QVariantMap& target, const QVariant& value, CommonValueType type)
{
    const QString kind = target.value(QStringLiteral("target"), QStringLiteral("none")).toString();
    const QVariant normalized = coerceCommonValue(value, type);
    if (kind == QLatin1String("variable")) {
        if (m_state) m_state->setVariable(target.value(QStringLiteral("id"), 1).toInt(), normalized.toInt());
    } else if (kind == QLatin1String("switch")) {
        if (m_state) m_state->setSwitch(target.value(QStringLiteral("id"), 1).toInt(), normalized.toBool());
    } else if (kind == QLatin1String("string")) {
        if (m_state) m_state->setStringValue(target.value(QStringLiteral("id"), 1).toInt(), normalized.toString());
    } else if (kind == QLatin1String("commonValue")) {
        const int index = commonFrameIndex();
        if (index >= 0) {
            const QString id = target.value(QStringLiteral("commonValueId")).toString();
            if (!id.isEmpty() && m_stack[index].commonValues.contains(id))
                m_stack[index].commonValues[id] = coerceCommonValue(normalized, commonValueType(id, type));
        }
    }
}

bool Interpreter::pushCommonEvent(const CommonEvent& ce, const QVariantMap& argumentSpecs,
                                  const QVariantMap& returnTarget, bool specsAreResolved)
{
    if (ce.commands.isEmpty() || m_stack.size() >= 16) return false;
    Frame frame;
    frame.cmds = ce.commands;
    frame.index = 0;
    frame.commonEventId = ce.id;
    frame.commonEventNumber = ce.number;
    frame.commonFrame = true;
    frame.returnTarget = returnTarget;
    frame.returnValue = coerceCommonValue(ce.returnValue.defaultValue, ce.returnValue.type);
    frame.returnType = ce.returnValue.type;
    frame.returnEnabled = ce.returnValue.enabled;
    frame.mapId = activeMapId();
    frame.eventId = activeEventId(); // Common Event herda "Este Evento" do chamador.
    frame.source = ce.id.isEmpty() ? QStringLiteral("common:number:%1").arg(ce.number)
                                   : QStringLiteral("common:%1").arg(ce.id);
    frame.executionMode = m_stack.isEmpty() ? executionModeForCommonTrigger(ce.trigger)
                                             : m_stack.last().executionMode;

    for (const CommonEventParameter& parameter : ce.parameters) {
        QVariant value = parameter.defaultValue;
        if (argumentSpecs.contains(parameter.id)) {
            if (specsAreResolved) value = coerceCommonValue(argumentSpecs.value(parameter.id), parameter.type);
            else value = resolveCommonSource(argumentSpecs.value(parameter.id).toMap(), parameter.type);
        }
        frame.commonValues.insert(parameter.id, coerceCommonValue(value, parameter.type));
    }
    for (const CommonEventLocal& local : ce.locals)
        frame.commonValues.insert(local.id, coerceCommonValue(local.initialValue, local.type));

    m_stack.push_back(frame);
    return true;
}


QString Interpreter::activeEventId() const
{
    for (int i = m_stack.size() - 1; i >= 0; --i)
        if (!m_stack.at(i).eventId.isEmpty()) return m_stack.at(i).eventId;
    return m_eventId;
}

QString Interpreter::activeMapId() const
{
    for (int i = m_stack.size() - 1; i >= 0; --i)
        if (!m_stack.at(i).mapId.isEmpty()) return m_stack.at(i).mapId;
    return (m_ed && m_ed->doc()) ? m_ed->doc()->id : QString();
}

QString Interpreter::currentEventId() const { return activeEventId(); }
QString Interpreter::currentMapId() const { return activeMapId(); }

core::EventExecutionContext Interpreter::executionContext() const
{
    core::EventExecutionContext context;
    if (m_stack.isEmpty()) {
        context.mapId = (m_ed && m_ed->doc()) ? m_ed->doc()->id : QString();
        context.mapEventId = m_eventId;
        return context;
    }

    const Frame& frame = m_stack.last();
    context.mode = frame.executionMode;
    context.source = frame.source;
    if (context.source.isEmpty())
        for (int i = m_stack.size() - 2; i >= 0; --i)
            if (!m_stack.at(i).source.isEmpty()) { context.source = m_stack.at(i).source; break; }
    context.mapId = activeMapId();
    context.mapEventId = activeEventId();
    context.pageIndex = frame.pageIndex;
    context.commandIndex = frame.index;
    context.callDepth = m_stack.size();

    int commonFrameIndex = -1;
    for (int i = m_stack.size() - 1; i >= 0; --i) {
        const Frame& candidate = m_stack.at(i);
        if (!candidate.commonFrame) continue;
        commonFrameIndex = i;
        context.commonEventId = candidate.commonEventId;
        context.commonEventNumber = candidate.commonEventNumber;
        break;
    }
    context.origin = context.hasCommonEvent() ? core::EventExecutionOrigin::CommonEvent
                                              : (context.hasMapEvent() ? core::EventExecutionOrigin::MapEvent
                                                                       : core::EventExecutionOrigin::DetachedList);

    // Caller pertence ao dono lógico do contexto. Frames inline de Choices e
    // plugins não viram um novo "chamador"; eles apenas executam dentro do
    // Map/Common Event que os originou.
    const int callerIndex = commonFrameIndex >= 0 ? commonFrameIndex - 1 : -1;
    if (callerIndex >= 0) {
        const Frame& caller = m_stack.at(callerIndex);
        context.callerSource = caller.source;
        context.callerMapId = caller.mapId;
        context.callerMapEventId = caller.eventId;
        for (int i = callerIndex; i >= 0; --i)
            if (m_stack.at(i).commonFrame) {
                context.callerCommonEventId = m_stack.at(i).commonEventId;
                break;
            }
    }
    return context;
}

core::EventTargetResolution Interpreter::resolveEventTarget(const QString& requested,
                                                             bool allowPosition,
                                                             bool verifyExplicitEvent) const
{
    core::EventTargetResolveOptions options;
    options.allowPosition = allowPosition;
    options.verifyExplicitEvent = verifyExplicitEvent;
    const core::EventTargetResolution resolution = core::resolveEventTarget(requested, executionContext(), m_ed, options);
    emitDiagnosticTrace(QStringLiteral("target.resolve"),
                        {{QStringLiteral("requested"), requested},
                         {QStringLiteral("canonical"), resolution.canonical},
                         {QStringLiteral("eventId"), resolution.eventId},
                         {QStringLiteral("valid"), resolution.valid()},
                         {QStringLiteral("error"), core::eventTargetErrorId(resolution.error)}});
    return resolution;
}

void Interpreter::emitDiagnosticTrace(const QString& phase, const QVariantMap& details) const
{
    if (!m_diagnosticTraceHook) return;
    QVariantMap payload = details;
    const core::EventExecutionContext context = executionContext();
    payload.insert(QStringLiteral("source"), context.source);
    payload.insert(QStringLiteral("mapId"), context.mapId);
    payload.insert(QStringLiteral("eventId"), context.mapEventId);
    payload.insert(QStringLiteral("commandIndex"), context.commandIndex);
    payload.insert(QStringLiteral("callDepth"), context.callDepth);
    m_diagnosticTraceHook(phase, payload);
}

EventRuntimeDebugState Interpreter::runtimeDebugState() const
{
    EventRuntimeDebugState state;
    const core::EventExecutionContext context = executionContext();
    state.current.source = context.source;
    state.current.mapId = context.mapId;
    state.current.eventId = context.mapEventId;
    state.current.commandIndex = context.commandIndex;
    state.current.callDepth = context.callDepth;
    const CutsceneRegionInfo cutscene = cutsceneRegion();
    state.cutsceneDepth = cutscene.nestingDepth;
    state.cutsceneSource = cutscene.source;
    state.awaitableKind = awaitableKindId(m_awaitable.kind);
    state.cancellationReason = m_lastCancellationReason;
    state.awaitableDetails = {
        {QStringLiteral("framesRemaining"), m_awaitable.framesRemaining},
        {QStringLiteral("pictureTarget"), m_awaitable.pictureTarget},
        {QStringLiteral("routeTarget"), m_awaitable.routeTarget},
        {QStringLiteral("routeTicket"), double(m_awaitable.routeTicket)},
        {QStringLiteral("timeoutFramesRemaining"), m_awaitable.timeoutFramesRemaining},
        {QStringLiteral("parallelTicket"), double(m_awaitable.parallelTicket)},
        {QStringLiteral("resumeCommandIndex"), m_awaitable.resumeCommandIndex}
    };
    return state;
}

bool Interpreter::pushMapEvent(const MapEvent& ev, int pageIndex, const QString& mapId)
{
    if (m_stack.size() >= 16 || pageIndex < 0 || pageIndex >= ev.pages.size()) return false;
    const QVector<EventCommand>& commands = ev.page(pageIndex).commands;
    if (commands.isEmpty()) return false;
    Frame frame;
    frame.cmds = commands;
    frame.index = 0;
    frame.mapId = mapId;
    frame.eventId = ev.id;
    frame.pageIndex = pageIndex;
    frame.executionMode = m_stack.isEmpty() ? executionModeForTrigger(ev.page(pageIndex).trigger)
                                             : m_stack.last().executionMode;
    frame.source = QStringLiteral("map:%1:event:%2").arg(mapId, ev.id);
    m_stack.push_back(frame);
    return true;
}

bool Interpreter::pushInlineCommands(const QVector<EventCommand>& commands)
{
    if (commands.isEmpty() || m_stack.size() >= 16) return false;
    Frame frame;
    frame.cmds = commands;
    frame.index = 0;
    if (!m_stack.isEmpty()) {
        const Frame& parent = m_stack.last();
        frame.source = parent.source;
        frame.mapId = parent.mapId;
        frame.eventId = parent.eventId;
        frame.pageIndex = parent.pageIndex;
        frame.executionMode = parent.executionMode;
    } else {
        frame.mapId = (m_ed && m_ed->doc()) ? m_ed->doc()->id : QString();
        frame.source = QStringLiteral("inline");
    }
    m_stack.push_back(frame);
    return true;
}

QVector<Interpreter::DebugFrameInfo> Interpreter::debugCallStack() const
{
    QVector<DebugFrameInfo> result;
    result.reserve(m_stack.size());
    for (const Frame& frame : m_stack) {
        DebugFrameInfo info;
        info.source = frame.source;
        info.mapId = frame.mapId;
        info.eventId = frame.eventId;
        info.commonEventId = frame.commonEventId;
        info.commonEventNumber = frame.commonEventNumber;
        info.pageIndex = frame.pageIndex;
        info.commandIndex = frame.index;
        info.commonFrame = frame.commonFrame;
        info.values = frame.commonValues;
        result.push_back(info);
    }
    return result;
}

bool Interpreter::canHotReloadTo(const core::Editor& target, QStringList* diagnostics) const
{
    for (const Frame& frame : m_stack) {
        if (frame.commonFrame) {
            const CommonEvent* ce = !frame.commonEventId.isEmpty()
                ? target.commonEventById(frame.commonEventId)
                : target.commonEventByNumber(frame.commonEventNumber);
            if (!ce) {
                if (diagnostics) diagnostics->push_back(
                    QStringLiteral("Common Event ativo não existe mais: %1").arg(frame.commonEventId));
                return false;
            }
            continue;
        }
        if (!frame.eventId.isEmpty()) {
            const MapDoc* map = nullptr;
            for (const MapDoc& doc : target.docs)
                if (doc.id == frame.mapId) { map = &doc; break; }
            if (!map) {
                if (diagnostics) diagnostics->push_back(
                    QStringLiteral("Mapa de frame ativo não existe mais: %1").arg(frame.mapId));
                return false;
            }
            const MapEvent* event = nullptr;
            for (const MapEvent& candidate : map->events)
                if (candidate.id == frame.eventId) { event = &candidate; break; }
            if (!event || frame.pageIndex < 0 || frame.pageIndex >= event->pages.size()) {
                if (diagnostics) diagnostics->push_back(
                    QStringLiteral("Map Event/página de frame ativo não existe mais: %1").arg(frame.eventId));
                return false;
            }
        }
    }
    return true;
}

bool Interpreter::hotReload(QStringList* diagnostics)
{
    if (!m_ed) return false;

    // O hot reload precisa ser atômico: um frame inválido não pode deixar metade da
    // pilha apontando para o modelo novo e metade para o modelo antigo.
    QVector<Frame> reloaded = m_stack;
    for (Frame& frame : reloaded) {
        if (frame.commonFrame) {
            const CommonEvent* ce = !frame.commonEventId.isEmpty() ? m_ed->commonEventById(frame.commonEventId)
                                                                  : m_ed->commonEventByNumber(frame.commonEventNumber);
            if (!ce) {
                if (diagnostics) diagnostics->push_back(QStringLiteral("Common Event ativo não existe mais: %1").arg(frame.commonEventId));
                return false;
            }
            frame.commonEventId = ce->id;
            frame.commonEventNumber = ce->number;
            frame.cmds = ce->commands;
            frame.returnEnabled = ce->returnValue.enabled;
            frame.returnType = ce->returnValue.type;
            frame.source = ce->id.isEmpty() ? QStringLiteral("common:number:%1").arg(ce->number)
                                            : QStringLiteral("common:%1").arg(ce->id);
            // Campos novos da assinatura entram sem apagar parâmetros/locais vivos.
            for (const CommonEventParameter& def : ce->parameters)
                if (!frame.commonValues.contains(def.id)) frame.commonValues.insert(def.id, coerceCommonValue(def.defaultValue, def.type));
            for (const CommonEventLocal& def : ce->locals)
                if (!frame.commonValues.contains(def.id)) frame.commonValues.insert(def.id, coerceCommonValue(def.initialValue, def.type));
        } else if (!frame.eventId.isEmpty()) {
            const MapDoc* map = nullptr;
            for (const MapDoc& doc : m_ed->docs) if (doc.id == frame.mapId) { map = &doc; break; }
            if (!map) {
                if (diagnostics) diagnostics->push_back(QStringLiteral("Mapa de frame ativo não existe mais: %1").arg(frame.mapId));
                return false;
            }
            const MapEvent* event = nullptr;
            for (const MapEvent& candidate : map->events) if (candidate.id == frame.eventId) { event = &candidate; break; }
            if (!event || frame.pageIndex < 0 || frame.pageIndex >= event->pages.size()) {
                if (diagnostics) diagnostics->push_back(QStringLiteral("Map Event/página de frame ativo não existe mais: %1").arg(frame.eventId));
                return false;
            }
            frame.cmds = event->page(frame.pageIndex).commands;
            frame.source = QStringLiteral("map:%1:event:%2").arg(frame.mapId, frame.eventId);
        } else {
            if (diagnostics) diagnostics->push_back(QStringLiteral("Lista solta ativa preservada sem remapeamento."));
            continue;
        }
        if (frame.cmds.isEmpty()) frame.index = -1;
        else frame.index = qBound(0, frame.index, frame.cmds.size() - 1);
    }

    while (!reloaded.isEmpty() && reloaded.last().cmds.isEmpty()) reloaded.removeLast();
    m_stack = std::move(reloaded);
    if (m_stack.isEmpty()) { stop(); return true; }
    m_eventId = activeEventId();
    m_debugSource = m_stack.last().source;
    m_debugBlocked = false;
    m_yieldPending = false;
    m_instantBudget = 0;
    m_skippingCutscene = false;
    m_cutsceneSkipFrameIndex = -1;
    m_cutsceneSkipEndIndex = -1;
    m_cutsceneSkipSource.clear();
    return true;
}

void Interpreter::applyCommonReturn(const QVariantMap& target, const QVariant& value)
{
    const QString kind = target.value(QStringLiteral("target"), QStringLiteral("none")).toString();
    CommonValueType type = CommonValueType::Number;
    if (kind == QLatin1String("switch")) type = CommonValueType::Boolean;
    else if (kind == QLatin1String("string")) type = CommonValueType::Text;
    else if (kind == QLatin1String("commonValue")) type = commonValueType(target.value(QStringLiteral("commonValueId")).toString());
    applyValueTarget(target, value, type);
}

void Interpreter::returnFromCommon(const QVariant& value)
{
    const int index = commonFrameIndex();
    if (index < 0) { nextCommand(); return; }
    const Frame finished = m_stack.at(index);
    while (m_stack.size() > index) m_stack.removeLast();
    if (finished.returnEnabled)
        applyCommonReturn(finished.returnTarget, coerceCommonValue(value, finished.returnType));
    if (m_stack.isEmpty()) { stop(); return; }
    ++m_stack.last().index;
    if (m_stack.last().index < m_stack.last().cmds.size()) beginCommand();
    else nextCommand();
}

const EventCommand* Interpreter::current() const
{
    if (m_stack.isEmpty()) return nullptr;
    const Frame& f = m_stack.last();
    if (f.index < 0 || f.index >= f.cmds.size()) return nullptr;
    return &f.cmds[f.index];
}

Interpreter::CutsceneRegionInfo Interpreter::cutsceneRegion() const
{
    CutsceneRegionInfo best;
    int activeDepth = 0;

    // Cada frame é analisado no seu próprio ExecutionMode. Um marcador
    // OnPageActivated, por exemplo, jamais abre uma região no fluxo Normal.
    // Frames superiores podem herdar uma região aberta pelo chamador: isso é
    // justamente o que faz Common Events participarem da mesma cutscene.
    for (int si = 0; si < m_stack.size(); ++si) {
        const Frame& frame = m_stack.at(si);
        if (frame.cmds.isEmpty() || frame.index < 0) continue;
        const int limit = qMin(frame.index, frame.cmds.size() - 1);
        struct OpenBegin { int index = -1; QVariantMap settings; };
        QVector<OpenBegin> openBegins;
        QVariantMap pendingSettings;
        for (int i = 0; i <= limit; ++i) {
            const EventCommand& command = frame.cmds.at(i);
            if (!commandRunsInMode(command.executionMode, frame.executionMode)) continue;
            if (command.type == QLatin1String("ludo.cutscene.settings")) {
                pendingSettings = command.params;
            } else if (command.type == QLatin1String("ludo.cutscene.begin")) {
                openBegins.push_back({i, pendingSettings});
                pendingSettings.clear();
            } else if (command.type == QLatin1String("ludo.cutscene.end") && !openBegins.isEmpty()) {
                openBegins.removeLast();
            }
        }

        // Só uma região com End pareado é pulável. Begin sem End fica para o
        // Validator diagnosticar e nunca transforma o resto do evento em uma
        // cutscene implícita. Entre regiões aninhadas vence a mais interna.
        for (int oi = 0; oi < openBegins.size(); ++oi) {
            const OpenBegin open = openBegins.at(oi);
            const int beginIndex = open.index;
            int depth = 1;
            int endIndex = -1;
            for (int i = beginIndex + 1; i < frame.cmds.size(); ++i) {
                const EventCommand& command = frame.cmds.at(i);
                if (!commandRunsInMode(command.executionMode, frame.executionMode)) continue;
                if (command.type == QLatin1String("ludo.cutscene.begin")) {
                    ++depth;
                } else if (command.type == QLatin1String("ludo.cutscene.end")) {
                    if (--depth == 0) { endIndex = i; break; }
                }
            }
            if (endIndex < 0) continue;
            ++activeDepth;
            if (oi == openBegins.size() - 1) {
                best.active = true;
                best.frameIndex = si;
                best.beginIndex = beginIndex;
                best.endIndex = endIndex;
                best.source = frame.source;
                best.skipAllowed = open.settings.value(QStringLiteral("skipAllowed"), true).toBool();
                best.skipAction = open.settings.value(QStringLiteral("skipAction"), QStringLiteral("skipCutscene")).toString();
                bool validAction = false;
                gameActionFromId(best.skipAction, &validAction);
                if (!validAction) best.skipAction = QStringLiteral("skipCutscene");
                best.nestingPolicy = open.settings.value(QStringLiteral("nesting"), QStringLiteral("allow")).toString();
            }
        }
    }
    if (best.active) best.nestingDepth = activeDepth;
    return best;
}

bool Interpreter::cutsceneSkipReady() const
{
    if (!cutsceneActive()) return false;
    // Choice é uma decisão lógica, não apresentação descartável. Se o skip
    // chegar até ela, a aceleração fica pausada enquanto o jogador escolhe.
    if (m_choice.visible) return false;

    // Awaitables Runtime incluem tanto tweens visuais quanto modais, batalha,
    // shop e input. Só os tipos marcados pelo CommandRegistry como temporais
    // podem ser cancelados por CutsceneSkip sem perder resultado lógico.
    if (m_awaitable.kind == AwaitableKind::Runtime) {
        const EventCommand* command = current();
        if (command && !CommandRegistry::shouldSkipDuringCutscene(command->type))
            return false;
    }
    return true;
}

bool Interpreter::skipCutscene()
{
    const CutsceneRegionInfo region = cutsceneRegion();
    if (!region.active || !cutsceneSkipReady()) return false;

    // Não saltamos o cursor diretamente para o End: isso descartaria
    // switches/variáveis/inventário/retornos e deixaria o estado diferente
    // de uma execução normal. Em vez disso, cancelamos a espera atual e
    // atravessamos a região em modo acelerado.
    cancelActiveWork(CancellationReason::CutsceneSkip);
    m_skippingCutscene = true;
    m_cutsceneSkipFrameIndex = region.frameIndex;
    m_cutsceneSkipEndIndex = region.endIndex;
    m_cutsceneSkipSource = region.source;
    m_instantBudget = 0;
    nextCommand();
    return true;
}

void Interpreter::cancelActiveWork(CancellationReason reason)
{
    m_lastCancellationReason = cancellationReasonId(reason);
    emitDiagnosticTrace(QStringLiteral("cancellation"),
                        {{QStringLiteral("reason"), m_lastCancellationReason},
                         {QStringLiteral("awaitable"), awaitableKindId(m_awaitable.kind)}});
    if (m_awaitable.kind == AwaitableKind::MoveRoute && m_moveRouteControlHook &&
        !m_awaitable.routeTarget.isEmpty())
        m_moveRouteControlHook(m_awaitable.routeTarget, MoveRouteControlAction::Cancel);
    if (m_awaitable.kind == AwaitableKind::ParallelBlock && m_parallelBlockCancelHook &&
        m_awaitable.parallelTicket != 0)
        m_parallelBlockCancelHook(m_awaitable.parallelTicket, reason);
    clearAwaitable();
    m_view = MessageView();
    m_choice = ChoiceView();
    m_pages.clear();
    m_charAcc = 0.0;
    m_waitSec = 0.0;
}

void Interpreter::stop(CancellationReason reason)
{
    cancelActiveWork(reason);
    m_running = false;
    m_debugBlocked = false;
    m_stack.clear();
    m_eventId.clear();
    m_instantBudget = 0;
    m_yieldPending = false;
    m_skippingCutscene = false;
    m_cutsceneSkipFrameIndex = -1;
    m_cutsceneSkipEndIndex = -1;
    m_cutsceneSkipSource.clear();
}

QVector<TypedChar> Interpreter::flatten(const TextPage& p) const
{
    QVector<TypedChar> all;
    for (const TextLine& l : p.lines)
        for (const TypedChar& c : l.chars) all.push_back(c);
    return all;
}

void Interpreter::beginPage(int i)
{
    m_pageIdx = i;
    m_view.visible = true;
    m_view.page = m_pages.value(i);
    m_view.revealed = 0;
    m_view.drawableRevealed = 0;
    m_view.revealTimesSec = QVector<double>(m_view.page.drawableCount(), -1.0);
    m_view.waitingKey = false;
    m_view.lastPage = (i >= m_pages.size() - 1);
    m_view.effectTimeSec = 0.0;
    m_view.exitTimeSec = -1.0;
    m_view.exiting = false;
    m_charAcc = 0.0;
    m_waitSec = 0.0;
}

void Interpreter::beginCommand()
{
    // Listas sem espera são rápidas, mas não podem monopolizar a thread. Isto
    // também torna seguro um laço visual criado com Rótulo/Pular.
    if (++m_instantBudget > kMaxInstantCommands) {
        m_yieldPending = true;
        return;
    }

    const EventCommand* atual = current();
    if (!atual) { stop(); return; }
    const EventCommand& storedCommand = *atual;
    const EventCommand c = resolveCommandValueFields(storedCommand);
    const core::EventExecutionMode frameMode = m_stack.isEmpty()
        ? core::EventExecutionMode::Normal : m_stack.last().executionMode;
    if (!commandRunsInMode(c.executionMode, frameMode)) {
        nextCommand();
        return;
    }

    if (m_skippingCutscene) {
        const bool ownerAlive = m_cutsceneSkipFrameIndex >= 0 &&
            m_cutsceneSkipFrameIndex < m_stack.size() &&
            m_cutsceneSkipEndIndex >= 0 &&
            m_stack.at(m_cutsceneSkipFrameIndex).source == m_cutsceneSkipSource;
        if (!ownerAlive || m_stack.at(m_cutsceneSkipFrameIndex).index > m_cutsceneSkipEndIndex) {
            m_skippingCutscene = false;
            m_cutsceneSkipFrameIndex = -1;
            m_cutsceneSkipEndIndex = -1;
            m_cutsceneSkipSource.clear();
        } else if (m_stack.size() - 1 == m_cutsceneSkipFrameIndex &&
                   m_stack.last().index == m_cutsceneSkipEndIndex) {
            // O End pareado é executado normalmente; a aceleração termina
            // antes dele, então uma região externa continua ativa e pulável.
            m_skippingCutscene = false;
            m_cutsceneSkipFrameIndex = -1;
            m_cutsceneSkipEndIndex = -1;
            m_cutsceneSkipSource.clear();
        }
    }
    const int debugIndex = m_stack.last().index;
    QString activeDebugSource = m_debugSource;
    for (int i = m_stack.size() - 1; i >= 0; --i)
        if (!m_stack.at(i).source.isEmpty()) { activeDebugSource = m_stack.at(i).source; break; }
    if (m_debugGateHook && !m_debugGateHook(activeDebugSource, activeEventId(), debugIndex, m_stack.size(), c)) {
        m_debugBlocked = true;
        return;
    }
    m_debugBlocked = false;
    if (m_traceHook) m_traceHook(activeEventId(), debugIndex, m_stack.size(), c);

    // Bloco A / 3.23.0: tipos desconhecidos continuam sendo tolerados para
    // compatibilidade, porém são diagnosticados UMA vez por Interpreter.
    // O ProjectValidator também os mostra antes da exportação.
    if (!CommandRegistry::isKnown(c.type) && !m_reportedUnknownTypes.contains(c.type)) {
        m_reportedUnknownTypes.insert(c.type);
        const int index = m_stack.isEmpty() ? -1 : m_stack.last().index;
        if (m_unknownCommandHook) m_unknownCommandHook(c.type, activeEventId(), index);
        else qWarning().noquote() << "LUDO: comando de evento desconhecido:" << c.type
                                  << "evento=" << activeEventId() << "indice=" << index;
    }

    // A lista de comandos que não devem segurar um "skip" mora no registro
    // central. Antes ela era duplicada neste método e podia divergir do editor.
    if (m_skippingCutscene && CommandRegistry::shouldSkipDuringCutscene(c.type)) {
        nextCommand();
        return;
    }

    // Blocks 5-7: command families can migrate out of this god-file one by one.
    // No registered handler means byte-for-byte legacy control flow below.
    const CommandDispatchResult modularResult = m_commandDispatcher.dispatch(c, *this);
    if (modularResult == CommandDispatchResult::Advance) { nextCommand(); return; }
    if (modularResult == CommandDispatchResult::Await) return;

    if (handleTextCommand(c)) return;
    if (handleRuntimeCommand(c)) return;
    if (handleFogCommand(c)) return;
    if (handleMoveRouteCommand(c)) return;
    if (handleFlowCommand(c)) return;
    if (handlePluginCommand(c)) return;

    if (CommandRegistry::domain(c.type) == CommandDomain::Picture) {
        if (runPictureCommand(c)) return; // ficou esperando um tween
        nextCommand();
        return;
    }

    // Comando desconhecido (ou ainda não implementado): segue adiante em vez
    // de travar o jogo.
    nextCommand();
}

bool Interpreter::handleTextCommand(const EventCommand& c)
{
    if (CommandRegistry::domain(c.type) != CommandDomain::Text) return false;
    const QVariantMap& p = c.params;

    if (c.type == QLatin1String("dialogue.fastForward")) {
        m_dialogueFastForwardSpeed = qBound(1.0, p.value(QStringLiteral("speed"), 1.0).toDouble(), 20.0);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("dialogue.skipMode")) {
        m_dialogueSkipMode = p.value(QStringLiteral("enabled"), true).toBool();
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("message")) {
        const DialogueContent dialogue=DialogueContent::fromVariantMap(p.value(QStringLiteral("dialogueContent")).toMap());
        const SpeakerProfile* profile=m_ed?m_ed->speakerDatabase.findById(dialogue.speakerId):nullptr;
        const QString fallbackText = dialogue.text.isEmpty()?p.value(QStringLiteral("text")).toString():dialogue.text;
        const QString requestedLocale = m_ed ? core::currentPlayerLocale(m_ed->localization) : QString();
        const QString localizedText = m_ed ? m_ed->localization.resolve(p.value(QStringLiteral("localizationKey")).toString(), fallbackText, requestedLocale) : fallbackText;
        const QString text = resolveInputPrompts(resolveDataTokens(resolveInlineLocalization(localizedText)));
        QFont messageFont = m_style.font;
        if(profile&&!profile->font.isEmpty())messageFont.setFamily(profile->font);
        const int commandFontSize = qBound(0, p.value(QStringLiteral("fontSize"), 0).toInt(), 96);
        if (commandFontSize > 0) messageFont.setPixelSize(qMax(6, commandFontSize));
        const QString overflow = p.value(QStringLiteral("overflow"), QStringLiteral("scroll")).toString().trimmed().toLower();
        auto relayout = [&] {
            return layoutMessage(text, messageFont, m_style.innerWidth, m_style.maxLines,
                                 [this](int id) { return resolveVariable(id); },
                                 m_ed ? &m_ed->iconSet : nullptr);
        };
        m_pages = relayout();
        if (overflow == QLatin1String("shrink")) {
            int size = messageFont.pixelSize() > 0 ? messageFont.pixelSize() : QFontMetrics(messageFont).height();
            while (m_pages.size() > 1 && size > 8) { messageFont.setPixelSize(--size); m_pages = relayout(); }
        } else if (overflow == QLatin1String("truncate") && m_pages.size() > 1) {
            m_pages = {m_pages.first()};
        }
        m_view = MessageView();
        m_view.fontSize = messageFont.pixelSize() > 0 ? messageFont.pixelSize() : commandFontSize;
        m_view.gradient = core::TextGradientSpec::fromVariantMap(p.value(QStringLiteral("textGradient")).toMap());
        m_view.effects = core::TextEffectStack::fromVariantMap(p.value(QStringLiteral("textEffects")).toMap());
        if(!dialogue.textEffectPreset.isEmpty()&&m_ed){bool found=false;TextEffectPhaseSpec phase=builtInTextEffectPreset(dialogue.textEffectPreset,&found);if(!found&&m_ed->textEffectPresets.contains(dialogue.textEffectPreset)){phase=m_ed->textEffectPresets.value(dialogue.textEffectPreset).phase;found=true;}if(found)m_view.effects.entrance=phase;}
        const QString fallbackSpeaker = profile?profile->name:p.value(QStringLiteral("speaker")).toString().trimmed();
        m_view.speaker = resolveDataTokens(m_ed ? m_ed->localization.resolve(p.value(QStringLiteral("speakerLocalizationKey")).toString(), fallbackSpeaker, requestedLocale).trimmed() : fallbackSpeaker);
        m_view.speakerId=dialogue.speakerId;m_view.expression=dialogue.expression;
        if(profile){m_view.portrait=profile->portraitForExpression(dialogue.expression);m_view.portraitPosition=profile->portraitPosition;m_view.fontFamily=profile->font;m_view.nameColor=profile->nameColor;m_view.textColor=profile->textColor;if(profile->metadata.value(QStringLiteral("portraitHidden")).toBool())m_view.portrait.clear();}
        if(!m_view.portrait.isEmpty()){const QString portraitPath=QFileInfo(m_view.portrait).isAbsolute()?m_view.portrait:(m_ed?QDir(m_ed->projectRoot()).filePath(m_view.portrait):m_view.portrait);m_view.portraitImage.load(portraitPath);}
        m_view.position = boxPositionFromId(
            p.value(QStringLiteral("position"), QStringLiteral("bottom")).toString());
        m_view.offsetX = p.value(QStringLiteral("offsetX")).toInt();
        m_view.offsetY = p.value(QStringLiteral("offsetY")).toInt();
        if (m_dialogueHistoryHook) m_dialogueHistoryHook(text, m_view.speaker, m_view.speakerId,
                                                        activeMapId(), activeEventId());
        QString voice=dialogue.voiceFile;if(profile&&!profile->voicePrefix.isEmpty()&&!voice.isEmpty()&&!QFileInfo(voice).isAbsolute())voice=QDir(profile->voicePrefix).filePath(voice);
        if(!voice.isEmpty()&&m_messageVoiceHook)m_messageVoiceHook(voice,qBound(0,p.value(QStringLiteral("voiceVolume"),100).toInt(),100));
        if (m_pages.isEmpty()) { nextCommand(); return true; }
        beginPage(0);
        return true;
    }

    if (c.type == QLatin1String("choice.show")) {
        QStringList choices = p.value(QStringLiteral("choices")).toStringList();
        if (choices.isEmpty())
            for (const QVariant& value : p.value(QStringLiteral("choices")).toList())
                if (!value.toString().trimmed().isEmpty()) choices.push_back(value.toString().trimmed());
        if (m_ed) {
            const QStringList choiceKeys = p.value(QStringLiteral("choiceLocalizationKeys")).toStringList();
            const QString requestedLocale = core::currentPlayerLocale(m_ed->localization);
            for (int i = 0; i < choices.size() && i < choiceKeys.size(); ++i)
                choices[i] = m_ed->localization.resolve(choiceKeys.at(i), choices.at(i), requestedLocale);
        }
        for (QString& choice : choices) choice = resolveDataTokens(choice);
        choices.removeAll(QString());
        if (choices.isEmpty()) { nextCommand(); return true; }
        m_choice.visible = true;
        m_choice.options = choices.mid(0, 8);
        m_choice.gradient = core::TextGradientSpec::fromVariantMap(p.value(QStringLiteral("textGradient")).toMap());
        m_choice.effects = core::TextEffectStack::fromVariantMap(p.value(QStringLiteral("textEffects")).toMap());
        m_choice.richPages.clear();
        for (const QString& option : m_choice.options) {
            const QVector<TextPage> pages = layoutMessage(option, m_style.font, 4000.0, 1,
                [this](int id) { return resolveVariable(id); }, m_ed ? &m_ed->iconSet : nullptr);
            m_choice.richPages.push_back(pages.isEmpty() ? TextPage() : pages.first());
        }
        m_choice.selected = qBound(0, p.value(QStringLiteral("default"), 0).toInt(),
                                   m_choice.options.size() - 1);
        m_choice.cancelValue = p.value(QStringLiteral("cancelValue"), 0).toInt();
        m_choice.resultVariable = p.value(QStringLiteral("resultVariable"), 0).toInt();
        m_choice.position = p.value(QStringLiteral("position"), QStringLiteral("center-right")).toString().trimmed().toLower();
        m_choice.offsetX = qBound(-10000, p.value(QStringLiteral("offsetX"), 0).toInt(), 10000);
        m_choice.offsetY = qBound(-10000, p.value(QStringLiteral("offsetY"), 0).toInt(), 10000);
        m_choice.layout = p.value(QStringLiteral("layout"), QStringLiteral("vertical")).toString().trimmed().toLower();
        if (m_choice.layout != QLatin1String("horizontal") && m_choice.layout != QLatin1String("grid")) m_choice.layout = QStringLiteral("vertical");
        m_choice.columns = qBound(1, p.value(QStringLiteral("columns"), 2).toInt(), 8);
        m_choice.spacingX = qBound(0, p.value(QStringLiteral("spacingX"), 6).toInt(), 128);
        m_choice.spacingY = qBound(0, p.value(QStringLiteral("spacingY"), 4).toInt(), 128);
        m_choice.alignment = p.value(QStringLiteral("alignment"), QStringLiteral("left")).toString().trimmed().toLower();
        if (m_choice.alignment != QLatin1String("center") && m_choice.alignment != QLatin1String("right")) m_choice.alignment = QStringLiteral("left");
        m_choice.boxMode = p.value(QStringLiteral("boxMode"), QStringLiteral("theme")).toString().trimmed().toLower();
        if (m_choice.boxMode != QLatin1String("transparent")) m_choice.boxMode = QStringLiteral("theme");
        m_choice.fontFamily = p.value(QStringLiteral("fontFamily")).toString().trimmed().left(256);
        m_choice.fontSize = qBound(0, p.value(QStringLiteral("fontSize"), 0).toInt(), 96);
        m_choice.disabled.clear();
        m_choice.staticDisabled.clear();
        const QVariantList disabledValues = p.value(QStringLiteral("disabledChoices")).toList();
        for (int di = 0; di < m_choice.options.size(); ++di) {
            const bool disabled = di < disabledValues.size() ? disabledValues.at(di).toBool() : false;
            m_choice.staticDisabled.push_back(disabled);
            m_choice.disabled.push_back(disabled);
        }
        if (m_choice.disabled.size() < m_choice.options.size()) m_choice.disabled.resize(m_choice.options.size());
        m_choice.conditions.clear();
        m_choice.disabledLabels.clear();
        m_choice.disabledReasons.clear();
        const QVariantList conditionValues = p.value(QStringLiteral("conditions")).toList();
        for (int di = 0; di < m_choice.options.size(); ++di) {
            const QVariantMap spec = di < conditionValues.size() ? conditionValues.at(di).toMap() : QVariantMap();
            QVariantMap tree = spec.value(QStringLiteral("condition")).toMap();
            if (tree.isEmpty()) tree = spec.value(QStringLiteral("conditionTree")).toMap();
            if (tree.isEmpty() && core::isConditionTreeNode(spec)) tree = spec;
            m_choice.conditions.push_back(tree);
            m_choice.disabledLabels.push_back(resolveDataTokens(spec.value(QStringLiteral("disabledLabel")).toString()));
            m_choice.disabledReasons.push_back(resolveDataTokens(spec.value(QStringLiteral("disabledReason")).toString()));
        }
        m_choice.showDisabledReason = p.value(QStringLiteral("showDisabledReason"), true).toBool();
        m_choice.timeLimit = qBound(0.0, p.value(QStringLiteral("timeLimit"), 0.0).toDouble(), 86400.0);
        m_choice.timeRemaining = m_choice.timeLimit;
        m_choice.defaultChoice = qBound(-1, p.value(QStringLiteral("defaultChoice"), -1).toInt(), m_choice.options.size() - 1);
        refreshChoiceConditions();
        if (m_choice.selected < m_choice.disabled.size() && m_choice.disabled.at(m_choice.selected)) {
            for (int di = 0; di < m_choice.options.size(); ++di) if (!m_choice.disabled.value(di)) { m_choice.selected = di; break; }
        }
        const QVariantList branchValues = p.value(QStringLiteral("branches")).toList();
        for (int bi=0; bi<m_choice.options.size(); ++bi) {
            QVector<core::EventCommand> branch;
            if (bi < branchValues.size())
                branch = core::eventCommandsFromVariantList(branchValues.at(bi).toList());
            m_choice.branches.push_back(branch);
        }
        return true;
    }

    if (c.type == QLatin1String("subtitle.show") || c.type == QLatin1String("subtitle.enqueue")) {
        beginSubtitle(c);
        return true;
    }
    if (c.type == QLatin1String("subtitle.configure")) {
        if (m_subs) {
            const QVariantMap styleMap = p.value(QStringLiteral("style")).toMap();
            if (!styleMap.isEmpty())
                m_subs->setStyle(core::SubtitleStyle::fromJson(QJsonObject::fromVariantMap(styleMap)));
        }
        nextCommand();
        return true;
    }
    if (c.type == QLatin1String("subtitle.clear")) {
        if (m_subs) m_subs->clearAll();
        nextCommand();
        return true;
    }
    if (c.type == QLatin1String("subtitle.clearFade")) {
        if (m_subs) m_subs->clearWithFade();
        nextCommand();
        return true;
    }
    if(c.type==QLatin1String("subtitle.clearQueue")){
        if(m_subs)m_subs->clearQueue(core::subtitleTrackFromId(p.value(QStringLiteral("track"),QStringLiteral("dialogue")).toString()));nextCommand();return true;
    }
    if (c.type == QLatin1String("subtitle.wait")) {
        if (m_subs && m_subs->busy()) { awaitSubtitles(); return true; }
        nextCommand();
        return true;
    }

    return false;
}

bool Interpreter::handleRuntimeCommand(const EventCommand& c)
{
    EventCommand runtime = c;
    if (c.type == QLatin1String("ludo.command")) {
        core::LudoCommandParseResult parsed;
        runtime = core::normalizeLegacyLudoCommand(c, &parsed);
        if (!parsed.valid) {
            qWarning().noquote() << "[LudoCommand]" << parsed.code << parsed.message
                                 << "source=" << executionContext().source;
            nextCommand();
            return true;
        }
    }

    const core::EventExecutionContext context = executionContext();
    if (!commandRunsInMode(runtime.executionMode, context.mode)) {
        nextCommand();
        return true;
    }

    // Begin/End são marcadores estruturais do próprio Interpreter. A região
    // ativa é derivada da pilha e do ExecutionMode; encaminhá-los para a
    // GameSession os transformaria novamente em comandos ornamentais.
    if (runtime.type == QLatin1String("ludo.cutscene.settings") ||
        runtime.type == QLatin1String("ludo.cutscene.begin") ||
        runtime.type == QLatin1String("ludo.cutscene.end")) {
        nextCommand();
        return true;
    }

    // Bloco F: mudar idioma é um comando No-Code autocontido. Ele não precisa
    // atravessar o GameSession: todos os resolvedores de texto leem a mesma
    // preferência persistente e passam a usar o novo idioma no próximo draw/comando.
    if (runtime.type == QLatin1String("localization.set")) {
        if (m_ed && m_ed->localization.enabled) {
            QString requested = runtime.params.value(QStringLiteral("locale")).toString().trimmed();
            const QString resolved = core::setPlayerLocale(m_ed->localization, requested);
            if (!resolved.isEmpty() && m_localeChangedHook) m_localeChangedHook(resolved);
        }
        nextCommand();
        return true;
    }

    if (!CommandRegistry::isRuntimeForwarded(runtime.type)) return false;

    runtime.params[QStringLiteral("_eventId")] = context.mapEventId; // compatibilidade com runtimes antigos
    runtime.params[QStringLiteral("_contextMapId")] = context.mapId;
    runtime.params[QStringLiteral("_contextSource")] = context.source;
    runtime.params[QStringLiteral("_contextCommonEventId")] = context.commonEventId;
    runtime.params[QStringLiteral("_contextExecutionMode")] = core::eventExecutionModeId(context.mode);

    // Alvos de mundo passam por uma única autoridade antes de alcançar a
    // GameSession. O runtime recebe somente player/event:<id>/position; nunca
    // precisa reinterpretar "self" de maneira diferente por subsistema.
    const bool cameraTarget = runtime.type == QLatin1String("ludo.camera.move") ||
                              runtime.type == QLatin1String("ludo.camera.moveOnly");
    const bool spriteTarget = runtime.type.startsWith(QLatin1String("ludo.sprite."));
    if (cameraTarget || spriteTarget) {
        const QString fallback = cameraTarget ? QStringLiteral("player") : QStringLiteral("self");
        const QString requested = runtime.params.value(QStringLiteral("target"), fallback).toString();
        const core::EventTargetResolution target = resolveEventTarget(requested, cameraTarget, true);
        if (!target.valid()) {
            qWarning().noquote() << "[EventTarget]" << runtime.type << requested
                                 << core::eventTargetErrorId(target.error)
                                 << "source=" << context.source;
            nextCommand();
            return true;
        }
        runtime.params[QStringLiteral("target")] = target.canonical;
    }

    // Identidade da execução, não do tipo. Dois eventos paralelos podem abrir
    // o mesmo tipo de modal sem compartilhar resultado nem espera.
    runtime.params[QStringLiteral("_runtimeTicket")]=QVariant::fromValue<qulonglong>(
        g_runtimeCommandTicket.fetch_add(1,std::memory_order_relaxed));
    const CommandResult result = m_runtimeCommandHook
        ? m_runtimeCommandHook(runtime, true)
        : CommandResult::Rejected;
    if (result == CommandResult::Waiting || result == CommandResult::DeferredCommit) {
        // DeferredCommit mantém o Interpreter no comando atual até o ticket
        // sair da fila da GameSession. Isso cria uma barreira de commit real:
        // o próximo If/Value/Common Return não pode observar estado antigo,
        // inclusive em Interpreters Paralelos iniciados no mesmo tick.
        awaitRuntime(runtime);
        return true;
    }
    nextCommand();
    return true;
}

bool Interpreter::handleFogCommand(const EventCommand& c)
{
    if (CommandRegistry::domain(c.type) != CommandDomain::Fog) return false;
    const QVariantMap& p = c.params;
    if (m_fogs) {
        const int slot = qBound(1, p.value(QStringLiteral("slot"), 1).toInt(), 5);
        if (c.type == QLatin1String("fog.show")) {
            FogDef f; f.slot = slot; f.enabled = true;
            f.image = io::dataUriToImage(p.value(QStringLiteral("image")).toString());
            f.sourcePath = p.value(QStringLiteral("source")).toString();
            if (f.image.isNull() && m_ed && !f.sourcePath.isEmpty()) {
                f.image = m_ed->preloadedRuntimeImage(f.sourcePath);
                if (f.image.isNull()) f.image.load(QDir(m_ed->projectRoot()).filePath(f.sourcePath));
            }
            f.blend = fogBlendFromId(p.value(QStringLiteral("blend"), QStringLiteral("normal")).toString());
            f.opacity = qBound(0, p.value(QStringLiteral("opacity"), 180).toInt(), 255);
            f.scrollX = p.value(QStringLiteral("scrollX")).toDouble();
            f.scrollY = p.value(QStringLiteral("scrollY")).toDouble();
            f.zoom = qBound(.1, p.value(QStringLiteral("zoom"), 1.0).toDouble(), 5.0);
            f.tileRepeat = p.value(QStringLiteral("tileRepeat"), true).toBool();
            f.fadeInFrames = qMax(0, p.value(QStringLiteral("fadeIn")).toInt());
            m_fogs->show(f);
        } else if (c.type == QLatin1String("fog.enable")) {
            if (m_ed && m_ed->doc())
                for (const FogDef& f : m_ed->doc()->environment.fogs)
                    if (f.slot == slot) { m_fogs->show(f); break; }
        } else if (c.type == QLatin1String("fog.disable")) m_fogs->remove(slot, 0);
        else if (c.type == QLatin1String("fog.remove")) m_fogs->remove(slot, p.value(QStringLiteral("duration")).toInt());
        else if (c.type == QLatin1String("fog.opacity") || c.type == QLatin1String("fog.fadeIn"))
            m_fogs->setOpacity(slot, p.value(QStringLiteral("opacity"), 255).toInt(), p.value(QStringLiteral("duration")).toInt());
        else if (c.type == QLatin1String("fog.blend")) m_fogs->setBlend(slot, fogBlendFromId(p.value(QStringLiteral("blend")).toString()));
        else if (c.type == QLatin1String("fog.scroll")) m_fogs->setScroll(slot, p.value(QStringLiteral("scrollX")).toDouble(), p.value(QStringLiteral("scrollY")).toDouble());
        else if (c.type == QLatin1String("fog.fadeOut")) m_fogs->remove(slot, p.value(QStringLiteral("duration"), 60).toInt());
        else if (c.type == QLatin1String("fog.clear")) m_fogs->clear();
    }
    nextCommand();
    return true;
}

bool Interpreter::handleMoveRouteCommand(const EventCommand& c)
{
    if (CommandRegistry::domain(c.type) != CommandDomain::Movement) return false;
    if (c.type == QLatin1String("move.route")) {
        MoveRoute route = moveRouteFromMap(c.params.value(QStringLiteral("route")).toMap());
        const core::EventTargetResolution resolution = resolveEventTarget(route.target, false, true);
        if (!resolution.valid()) {
            qWarning().noquote() << "[EventTarget] move.route" << route.target
                                 << core::eventTargetErrorId(resolution.error)
                                 << "source=" << executionContext().source;
            nextCommand();
            return true;
        }
        const QString target = resolution.canonical;
        const MoveRouteTicket ticket = m_moveRouteStartHook ? m_moveRouteStartHook(target, route) : 0;
        if (route.waitForCompletion && ticket != 0) {
            awaitMoveRoute(target, ticket);
            return true;
        }
        nextCommand();
        return true;
    }
    if (c.type == QLatin1String("move.route.control")) {
        const QString requested = c.params.value(QStringLiteral("target"), QStringLiteral("self")).toString();
        const core::EventTargetResolution resolution = resolveEventTarget(requested, false, true);
        if (!resolution.valid()) {
            qWarning().noquote() << "[EventTarget] move.route.control" << requested
                                 << core::eventTargetErrorId(resolution.error)
                                 << "source=" << executionContext().source;
            nextCommand();
            return true;
        }
        const QString target = resolution.canonical;
        const MoveRouteControlAction action = moveRouteControlActionFromId(
            c.params.value(QStringLiteral("action"), QStringLiteral("pause")).toString());
        if (m_moveRouteControlHook) m_moveRouteControlHook(target, action);
        nextCommand();
        return true;
    }
    nextCommand();
    return true;
}

bool Interpreter::handleFlowCommand(const EventCommand& c)
{
    if (CommandRegistry::domain(c.type) != CommandDomain::Flow) return false;
    const QVariantMap& p = c.params;

    const auto resolveRecordId = [&](const QString& specKey, const QString& idKey) {
        const QVariantMap spec = p.value(specKey).toMap();
        return spec.isEmpty() ? p.value(idKey).toString()
                              : resolveValueSpec(spec, CommonValueType::Text).toString();
    };

    const auto resolveNumberParam = [&](const QString& key, int fallback = 0) {
        const QVariant raw = p.value(key);
        const QVariantMap spec = raw.toMap();
        return spec.isEmpty() ? (raw.isValid() ? raw.toInt() : fallback)
                              : resolveValueSpec(spec, CommonValueType::Number).toInt();
    };
    const auto resolveRuntimeMapId = [&]() {
        const QString fixed = p.value(QStringLiteral("mapId")).toString();
        return !fixed.isEmpty() ? fixed : (m_ed && m_ed->doc() ? m_ed->doc()->id : QString());
    };

    if (c.type == QLatin1String("parallel.begin")) {
        if (m_stack.isEmpty()) return true;
        Frame& frame = m_stack.last();
        const int beginIndex = frame.index;
        int endIndex = -1, parallelDepth = 0;
        for (int i = beginIndex + 1; i < frame.cmds.size(); ++i) {
            const QString type = frame.cmds.at(i).type;
            if (type == QLatin1String("parallel.begin")) ++parallelDepth;
            else if (type == QLatin1String("parallel.end")) {
                if (parallelDepth == 0) { endIndex = i; break; }
                --parallelDepth;
            }
        }
        if (endIndex < 0) { nextCommand(); return true; }

        // Cada comando/estrutura diretamente dentro do bloco vira uma tarefa.
        // Estruturas (If/Loop/Repeat/Database Each/Parallel aninhado) viajam
        // inteiras para o Interpreter filho, preservando sua semântica.
        const auto structuralEnd = [&](int start) {
            const QString type = frame.cmds.at(start).type;
            QString open, close;
            if (type == QLatin1String("if")) { open = QStringLiteral("if"); close = QStringLiteral("endIf"); }
            else if (type == QLatin1String("loop.begin")) { open = QStringLiteral("loop.begin"); close = QStringLiteral("loop.end"); }
            else if (type == QLatin1String("repeat.begin")) { open = QStringLiteral("repeat.begin"); close = QStringLiteral("repeat.end"); }
            else if (type == QLatin1String("database.each")) { open = QStringLiteral("database.each"); close = QStringLiteral("database.each.end"); }
            else if (type == QLatin1String("parallel.begin")) { open = QStringLiteral("parallel.begin"); close = QStringLiteral("parallel.end"); }
            else return start;
            int depth = 0;
            for (int i = start; i < endIndex; ++i) {
                const QString currentType = frame.cmds.at(i).type;
                if (currentType == open) ++depth;
                else if (currentType == close && --depth == 0) return i;
            }
            return start;
        };

        QVector<QVector<EventCommand>> tasks;
        for (int i = beginIndex + 1; i < endIndex;) {
            if (frame.cmds.at(i).type == QLatin1String("parallel.end")) break;
            const int taskEnd = qBound(i, structuralEnd(i), endIndex - 1);
            QVector<EventCommand> task;
            task.reserve(taskEnd - i + 1);
            for (int j = i; j <= taskEnd; ++j) task.push_back(frame.cmds.at(j));
            if (!task.isEmpty()) tasks.push_back(std::move(task));
            i = taskEnd + 1;
        }

        if (tasks.isEmpty() || !m_parallelBlockStartHook) {
            // Headless/compatibilidade: sem scheduler paralelo, o conteúdo
            // continua sequencialmente em vez de desaparecer silenciosamente.
            nextCommand();
            return true;
        }
        const quint64 ticket = m_parallelBlockStartHook(tasks, executionContext());
        if (ticket == 0) { nextCommand(); return true; }
        awaitParallelBlock(ticket, endIndex);
        return true;
    }
    if (c.type == QLatin1String("parallel.end")) { nextCommand(); return true; }

    if (c.type == QLatin1String("map.runtime.tile")) {
        if (m_state && m_ed) {
            QString ignored;
            m_state->setRuntimeMapTile(*m_ed, resolveRuntimeMapId(), p.value(QStringLiteral("layerId")).toString(),
                resolveNumberParam(QStringLiteral("x")), resolveNumberParam(QStringLiteral("y")),
                p.value(QStringLiteral("tilesetId")).toString(), p.value(QStringLiteral("tx")).toInt(), p.value(QStringLiteral("ty")).toInt(),
                p.value(QStringLiteral("clear"),false).toBool(), &ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.fill")) {
        if (m_state && m_ed) {
            QString ignored;
            m_state->fillRuntimeMapArea(*m_ed, resolveRuntimeMapId(), p.value(QStringLiteral("layerId")).toString(),
                resolveNumberParam(QStringLiteral("x")), resolveNumberParam(QStringLiteral("y")),
                resolveNumberParam(QStringLiteral("width"),1), resolveNumberParam(QStringLiteral("height"),1),
                p.value(QStringLiteral("tilesetId")).toString(), p.value(QStringLiteral("tx")).toInt(), p.value(QStringLiteral("ty")).toInt(),
                p.value(QStringLiteral("clear"),false).toBool(), &ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.copy")) {
        if (m_state && m_ed) {
            QString ignored;
            m_state->copyRuntimeMapArea(*m_ed, resolveRuntimeMapId(), p.value(QStringLiteral("sourceLayerId")).toString(),
                resolveNumberParam(QStringLiteral("sourceX")), resolveNumberParam(QStringLiteral("sourceY")),
                resolveNumberParam(QStringLiteral("width"),1), resolveNumberParam(QStringLiteral("height"),1),
                p.value(QStringLiteral("targetLayerId")).toString(), resolveNumberParam(QStringLiteral("targetX")),
                resolveNumberParam(QStringLiteral("targetY")), &ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.passage")) {
        if (m_state && m_ed) {
            const QString mapId=resolveRuntimeMapId();const int x=resolveNumberParam(QStringLiteral("x")),y=resolveNumberParam(QStringLiteral("y"));
            const int passageMask = p.contains(QStringLiteral("mask"))
                ? p.value(QStringLiteral("mask")).toInt()
                : -1;
            if(passageMask<0)m_state->resetRuntimeMapPassage(mapId,x,y);
            else {QString ignored;m_state->setRuntimeMapPassage(*m_ed,mapId,x,y,passageMask,&ignored);}
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.terrain")) {
        if (m_state && m_ed) {
            const QString mapId=resolveRuntimeMapId();const int x=resolveNumberParam(QStringLiteral("x")),y=resolveNumberParam(QStringLiteral("y"));
            const int terrain=resolveNumberParam(QStringLiteral("terrain"));QString ignored;m_state->setRuntimeMapTerrain(*m_ed,mapId,x,y,terrain,&ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.tileset")) {
        if (m_state && m_ed) {
            const QString mapId=resolveRuntimeMapId(),source=p.value(QStringLiteral("sourceTilesetId")).toString(),target=p.value(QStringLiteral("targetTilesetId")).toString();
            if(target.isEmpty())m_state->resetRuntimeMapTilesetRemap(mapId,source);else{QString ignored;m_state->setRuntimeMapTilesetRemap(*m_ed,mapId,source,target,&ignored);}
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.runtime.reset")) {
        if (m_state && m_ed) {
            const QString mapId=resolveRuntimeMapId();const QString scope=p.value(QStringLiteral("scope"),QStringLiteral("cell")).toString();
            if(scope==QLatin1String("map"))m_state->resetRuntimeMap(mapId);
            else if(scope==QLatin1String("area"))m_state->resetRuntimeMapArea(*m_ed,mapId,resolveNumberParam(QStringLiteral("x")),resolveNumberParam(QStringLiteral("y")),resolveNumberParam(QStringLiteral("width"),1),resolveNumberParam(QStringLiteral("height"),1));
            else if(scope==QLatin1String("tileset"))m_state->resetRuntimeMapTilesetRemap(mapId,p.value(QStringLiteral("sourceTilesetId")).toString());
            else m_state->resetRuntimeMapCell(mapId,p.value(QStringLiteral("layerId")).toString(),resolveNumberParam(QStringLiteral("x")),resolveNumberParam(QStringLiteral("y")),true,true);
        }
        nextCommand(); return true;
    }

    if (c.type == QLatin1String("database.get")) {
        if (m_state && m_ed) {
            const QString databaseId = p.value(QStringLiteral("databaseId")).toString();
            const CustomDatabaseDefinition* database = m_ed->customDatabase(databaseId);
            const QString fieldId = p.value(QStringLiteral("fieldId")).toString();
            const CustomDatabaseField* field = database ? customDatabaseFieldById(*database, fieldId) : nullptr;
            if (field) {
                const QString recordId = resolveRecordId(QStringLiteral("recordSpec"), QStringLiteral("recordId"));
                applyValueTarget(p.value(QStringLiteral("target")).toMap(),
                                 m_state->customDatabaseValue(*m_ed, databaseId, recordId, fieldId),
                                 customFieldCommonType(field->type));
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.set")) {
        if (m_state && m_ed) {
            const QString databaseId = p.value(QStringLiteral("databaseId")).toString();
            const CustomDatabaseDefinition* database = m_ed->customDatabase(databaseId);
            const QString fieldId = p.value(QStringLiteral("fieldId")).toString();
            const CustomDatabaseField* field = database ? customDatabaseFieldById(*database, fieldId) : nullptr;
            if (field) {
                const QString recordId = resolveRecordId(QStringLiteral("recordSpec"), QStringLiteral("recordId"));
                const QVariant value = resolveValueSpec(p.value(QStringLiteral("sourceSpec")).toMap(),
                                                        customFieldCommonType(field->type));
                QString ignored;
                m_state->setCustomDatabaseValue(*m_ed, databaseId, recordId, fieldId, value, &ignored);
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.find")) {
        if (m_state && m_ed) {
            const QString databaseId = p.value(QStringLiteral("databaseId")).toString();
            const CustomDatabaseDefinition* database = m_ed->customDatabase(databaseId);
            const QString fieldId = p.value(QStringLiteral("fieldId")).toString();
            const CustomDatabaseField* field = database ? customDatabaseFieldById(*database, fieldId) : nullptr;
            if (field) {
                const QVariant needle = resolveValueSpec(p.value(QStringLiteral("sourceSpec")).toMap(),
                                                         customFieldCommonType(field->type));
                const QString recordId = m_state->findCustomDatabaseRecord(
                    *m_ed, databaseId, fieldId,
                    p.value(QStringLiteral("operation"), QStringLiteral("equals")).toString(), needle);
                applyValueTarget(p.value(QStringLiteral("target")).toMap(), recordId, CommonValueType::Text);
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.count")) {
        const int count = (m_state && m_ed)
            ? m_state->customDatabaseRecordCount(*m_ed, p.value(QStringLiteral("databaseId")).toString()) : 0;
        applyValueTarget(p.value(QStringLiteral("target")).toMap(), count, CommonValueType::Number);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.exists")) {
        const QString recordId = resolveRecordId(QStringLiteral("recordSpec"), QStringLiteral("recordId"));
        const bool exists = (m_state && m_ed) && m_state->customDatabaseRecordExists(
            *m_ed, p.value(QStringLiteral("databaseId")).toString(), recordId);
        applyValueTarget(p.value(QStringLiteral("target")).toMap(), exists, CommonValueType::Boolean);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.copy")) {
        if (m_state && m_ed) {
            const QString databaseId = p.value(QStringLiteral("databaseId")).toString();
            const QString sourceId = resolveRecordId(QStringLiteral("sourceRecordSpec"), QStringLiteral("sourceRecordId"));
            const QString targetId = resolveRecordId(QStringLiteral("targetRecordSpec"), QStringLiteral("targetRecordId"));
            QString ignored;
            m_state->copyCustomDatabaseRecord(*m_ed, databaseId, sourceId, targetId, &ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.reset")) {
        if (m_state && m_ed) {
            QString ignored;
            m_state->resetCustomDatabaseRecord(*m_ed, p.value(QStringLiteral("databaseId")).toString(),
                resolveRecordId(QStringLiteral("recordSpec"), QStringLiteral("recordId")), &ignored);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.recordInfo")) {
        const QString databaseId = p.value(QStringLiteral("databaseId")).toString();
        const QString recordId = resolveRecordId(QStringLiteral("recordSpec"), QStringLiteral("recordId"));
        const QString info = p.value(QStringLiteral("info"), QStringLiteral("id")).toString();
        QVariant result;
        CommonValueType resultType = CommonValueType::Text;
        if (m_ed) if (const CustomDatabaseDefinition* database = m_ed->customDatabase(databaseId))
            if (const CustomDatabaseRecord* record = customDatabaseRecordById(*database, recordId)) {
                if (info == QLatin1String("number")) { result = record->number; resultType = CommonValueType::Number; }
                else if (info == QLatin1String("name")) result = record->name;
                else result = record->id;
            }
        applyValueTarget(p.value(QStringLiteral("target")).toMap(), result, resultType);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.each")) {
        if (!m_state || !m_ed || m_stack.isEmpty()) { nextCommand(); return true; }
        Frame& frame = m_stack.last();
        const int beginIndex = frame.index;
        auto stateIt = frame.databaseEachStates.find(beginIndex);
        if (stateIt == frame.databaseEachStates.end()) {
            DatabaseEachState state;
            state.recordIds = m_state->customDatabaseRecordIds(*m_ed, p.value(QStringLiteral("databaseId")).toString());
            state.target = p.value(QStringLiteral("target")).toMap();
            frame.databaseEachStates.insert(beginIndex, state);
            stateIt = frame.databaseEachStates.find(beginIndex);
        }
        if (stateIt->position >= stateIt->recordIds.size()) {
            frame.databaseEachStates.remove(beginIndex);
            int depth = 0;
            for (int i = beginIndex + 1; i < frame.cmds.size(); ++i) {
                const QString& type = frame.cmds[i].type;
                if (type == QLatin1String("database.each")) ++depth;
                else if (type == QLatin1String("database.each.end")) {
                    if (depth == 0) { frame.index = i; nextCommand(); return true; }
                    --depth;
                }
            }
            nextCommand(); return true;
        }
        applyValueTarget(stateIt->target, stateIt->recordIds.at(stateIt->position), CommonValueType::Text);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("database.each.end")) {
        if (m_stack.isEmpty()) { nextCommand(); return true; }
        Frame& frame = m_stack.last();
        int depth = 0;
        for (int i = frame.index - 1; i >= 0; --i) {
            const QString& type = frame.cmds[i].type;
            if (type == QLatin1String("database.each.end")) ++depth;
            else if (type == QLatin1String("database.each")) {
                if (depth == 0) {
                    auto stateIt = frame.databaseEachStates.find(i);
                    if (stateIt != frame.databaseEachStates.end()) ++stateIt->position;
                    frame.index = i; beginCommand(); return true;
                }
                --depth;
            }
        }
        nextCommand(); return true;
    }

    if (c.type == QLatin1String("switch.set")) {
        if (m_state) {
            const int id = p.value(QStringLiteral("id"), 1).toInt();
            const QString op = p.value(QStringLiteral("value"), QStringLiteral("on")).toString();
            if (op == QLatin1String("toggle")) {
                m_state->toggleSwitch(id);
            } else {
                QVariantMap spec = p.value(QStringLiteral("sourceSpec")).toMap();
                if (spec.isEmpty()) {
                    const QString legacySource = p.value(QStringLiteral("source")).toString();
                    if (legacySource == QLatin1String("commonValue"))
                        spec = QVariantMap{{QStringLiteral("source"),QStringLiteral("commonValue")},
                                           {QStringLiteral("commonValueId"),p.value(QStringLiteral("commonValueId"))}};
                    else
                        spec = QVariantMap{{QStringLiteral("source"),QStringLiteral("constant")},
                                           {QStringLiteral("value"),op == QLatin1String("on")}};
                }
                m_state->setSwitch(id, resolveValueSpec(spec, CommonValueType::Boolean).toBool());
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("selfSwitch.set")) {
        const core::EventExecutionContext context = executionContext();
        if (m_state && context.hasMapEvent()) {
            const QString letter = p.value(QStringLiteral("letter"), QStringLiteral("A")).toString();
            const QString op = p.value(QStringLiteral("value"), QStringLiteral("on")).toString();
            const bool currentValue = m_state->selfSwitch(context.mapEventId, letter);
            m_state->setSelfSwitch(context.mapEventId, letter,
                op == QLatin1String("toggle") ? !currentValue : op == QLatin1String("on"));
        } else if (!context.hasMapEvent()) {
            qWarning().noquote() << "[EventContext] selfSwitch.set this-event-unavailable source=" << context.source;
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("variable.set")) {
        if (m_state) {
            const int id = p.value(QStringLiteral("id"), 1).toInt();
            QVariantMap spec = p.value(QStringLiteral("sourceSpec")).toMap();
            if (spec.isEmpty()) {
                const QString legacySource = p.value(QStringLiteral("source"), QStringLiteral("const")).toString();
                spec[QStringLiteral("source")] = legacySource == QLatin1String("const") ? QStringLiteral("constant") : legacySource;
                spec[QStringLiteral("value")] = p.value(QStringLiteral("value"), 0);
                spec[QStringLiteral("variableId")] = p.value(QStringLiteral("valueVariable"), 1);
                spec[QStringLiteral("commonValueId")] = p.value(QStringLiteral("commonValueId"));
                spec[QStringLiteral("minimum")] = p.value(QStringLiteral("randomMin"), 0);
                spec[QStringLiteral("maximum")] = p.value(QStringLiteral("randomMax"), 0);
            }
            const int value = resolveValueSpec(spec, CommonValueType::Number).toInt();
            const QString op = p.value(QStringLiteral("op"), QStringLiteral("=")).toString();
            const int lastId = qMax(id, p.value(QStringLiteral("rangeEndId"), id).toInt());
            for (int currentId = id; currentId <= lastId && currentId - id <= 10000; ++currentId) {
                const int before = m_state->variable(currentId);
                qint64 result = value;
                if (op == QLatin1String("+")) result = qint64(before) + value;
                else if (op == QLatin1String("-")) result = qint64(before) - value;
                else if (op == QLatin1String("*")) result = qint64(before) * value;
                else if (op == QLatin1String("/")) result = value != 0 ? before / value : before;
                else if (op == QLatin1String("%")) result = value != 0 ? before % value : before;
                else if (op == QLatin1String("min")) result = qMin(before, value);
                else if (op == QLatin1String("max")) result = qMax(before, value);
                else if (op == QLatin1String("abs")) result = qAbs(value);
                m_state->setVariable(currentId, int(qBound<qint64>(qint64(-999999999), result, qint64(999999999))));
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("string.set")) {
        if (m_state) {
            const int id = p.value(QStringLiteral("id"), 1).toInt();
            QVariantMap spec = p.value(QStringLiteral("sourceSpec")).toMap();
            if (spec.isEmpty()) spec = QVariantMap{{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),p.value(QStringLiteral("value"))}};
            const QString incoming = resolveValueSpec(spec, CommonValueType::Text).toString();
            const QString before = m_state->stringValue(id);
            const QString op = p.value(QStringLiteral("op"), QStringLiteral("=")).toString();
            QString result = incoming;
            if (op == QLatin1String("+")) result = before + incoming;
            else if (op == QLatin1String("prepend")) result = incoming + before;
            else if (op == QLatin1String("insert")) {
                result = before;
                result.insert(qBound(0, p.value(QStringLiteral("index"), 0).toInt(), int(before.size())), incoming);
            } else if (op == QLatin1String("replace")) {
                result = before;
                const QString search = p.value(QStringLiteral("search")).toString();
                if (!search.isEmpty()) result.replace(search, incoming);
            } else if (op == QLatin1String("remove")) {
                result = before;
                if (!incoming.isEmpty()) result.replace(incoming, QString());
            } else if (op == QLatin1String("clear")) result.clear();
            else if (op == QLatin1String("trim")) result = before.trimmed();
            else if (op == QLatin1String("upper")) result = before.toUpper();
            else if (op == QLatin1String("lower")) result = before.toLower();
            else if (op == QLatin1String("substring")) result = before.mid(qMax(0, p.value(QStringLiteral("index"), 0).toInt()), qMax(1, p.value(QStringLiteral("length"), 1).toInt()));
            else if (op == QLatin1String("charAt")) {
                const int index = p.value(QStringLiteral("index"), 0).toInt();
                result = index >= 0 && index < before.size() ? QString(before.at(index)) : QString();
            } else if (op == QLatin1String("lineAt")) {
                const int line = qMax(0, p.value(QStringLiteral("index"), 0).toInt());
                result = before.section(QLatin1Char('\n'), line, line);
            } else if (op == QLatin1String("firstLine")) result = before.section(QLatin1Char('\n'), 0, 0);
            else if (op == QLatin1String("cutFirstLine")) result = before.section(QLatin1Char('\n'), 1);
            else if (op == QLatin1String("cutFirstChar")) result = before.mid(1);
            m_state->setStringValue(id, result.left(65535));
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("value.get")) {
        const QVariantMap query = p.value(QStringLiteral("query")).toMap();
        const CommonValueType type = commonValueTypeFromId(p.value(QStringLiteral("valueType"), QStringLiteral("number")).toString());
        const QVariant value = m_gameValueHook ? m_gameValueHook(query) : QVariant();
        applyValueTarget(p.value(QStringLiteral("target")).toMap(), value, type);
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("variable.math")) {
        if (m_state) {
            const int firstId = p.value(QStringLiteral("id"), 1).toInt();
            const int lastId = qMax(firstId, p.value(QStringLiteral("rangeEndId"), firstId).toInt());
            const QString op = p.value(QStringLiteral("operation"), QStringLiteral("add")).toString();
            constexpr double kPi = 3.141592653589793238462643383279502884;
            double out = 0.0;
            if (op == QLatin1String("expression")) {
                const QVariantList terms = p.value(QStringLiteral("terms")).toList();
                if (!terms.isEmpty()) {
                    out = resolveValueSpec(terms.first().toMap().value(QStringLiteral("sourceSpec")).toMap(), CommonValueType::Number).toDouble();
                    const int limit = qMin(int(terms.size()), 32);
                    for (int i = 1; i < limit; ++i) {
                        const QVariantMap term = terms.at(i).toMap();
                        const double rhs = resolveValueSpec(term.value(QStringLiteral("sourceSpec")).toMap(), CommonValueType::Number).toDouble();
                        const QString step = term.value(QStringLiteral("op"), QStringLiteral("add")).toString();
                        if (step == QLatin1String("add")) out += rhs;
                        else if (step == QLatin1String("sub")) out -= rhs;
                        else if (step == QLatin1String("mul")) out *= rhs;
                        else if (step == QLatin1String("div")) { if (rhs != 0.0) out /= rhs; }
                        else if (step == QLatin1String("mod")) { if (rhs != 0.0) out = std::fmod(out, rhs); }
                        else if (step == QLatin1String("min")) out = qMin(out, rhs);
                        else if (step == QLatin1String("max")) out = qMax(out, rhs);
                        else if (step == QLatin1String("pow")) out = std::pow(out, rhs);
                    }
                }
            } else {
                const QVariantMap aSpec = p.value(QStringLiteral("a")).toMap();
                const QVariantMap bSpec = p.value(QStringLiteral("b")).toMap();
                const QVariantMap cSpec = p.value(QStringLiteral("c")).toMap();
                const double a = resolveValueSpec(aSpec, CommonValueType::Number).toDouble();
                const double b = resolveValueSpec(bSpec, CommonValueType::Number).toDouble();
                const double cv = resolveValueSpec(cSpec, CommonValueType::Number).toDouble();
                out = a;
                if (op == QLatin1String("add")) out = a + b;
                else if (op == QLatin1String("sub")) out = a - b;
                else if (op == QLatin1String("mul")) out = a * b;
                else if (op == QLatin1String("div")) out = b != 0.0 ? a / b : a;
                else if (op == QLatin1String("mod")) out = b != 0.0 ? std::fmod(a, b) : a;
                else if (op == QLatin1String("min")) out = qMin(a, b);
                else if (op == QLatin1String("max")) out = qMax(a, b);
                else if (op == QLatin1String("pow")) out = std::pow(a, b);
                else if (op == QLatin1String("sqrt")) out = a >= 0.0 ? std::sqrt(a) : 0.0;
                else if (op == QLatin1String("abs")) out = std::abs(a);
                else if (op == QLatin1String("round")) out = std::round(a);
                else if (op == QLatin1String("floor")) out = std::floor(a);
                else if (op == QLatin1String("ceil")) out = std::ceil(a);
                else if (op == QLatin1String("sin")) out = std::sin(a * kPi / 180.0) * 1000.0;
                else if (op == QLatin1String("cos")) out = std::cos(a * kPi / 180.0) * 1000.0;
                else if (op == QLatin1String("atan2")) out = std::atan2(a, b) * 180.0 / kPi;
                else if (op == QLatin1String("clamp")) out = qBound(qMin(b, cv), a, qMax(b, cv));
            }
            const qint64 rounded = std::isfinite(out)
                ? qRound64(qBound(-999999999.0, out, 999999999.0))
                : (out < 0.0 ? -999999999LL : 999999999LL);
            const int result = int(qBound<qint64>(qint64(-999999999), rounded, qint64(999999999)));
            for (int id = firstId; id <= lastId && id - firstId <= 10000; ++id) m_state->setVariable(id, result);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("common.local.set")) {
        const int commonIndex = commonFrameIndex();
        if (commonIndex >= 0) {
            Frame& frame = m_stack[commonIndex];
            const QString id = p.value(QStringLiteral("id")).toString();
            CommonValueType type = CommonValueType::Number;
            if (m_ed) if (const CommonEvent* ce = m_ed->commonEventByNumber(frame.commonEventNumber)) {
                for (const CommonEventParameter& def : ce->parameters) if (def.id == id) { type = def.type; break; }
                for (const CommonEventLocal& def : ce->locals) if (def.id == id) { type = def.type; break; }
            }
            QVariantMap sourceSpec = p.value(QStringLiteral("sourceSpec")).toMap();
            if (sourceSpec.isEmpty()) {
                sourceSpec[QStringLiteral("source")] = p.value(QStringLiteral("source"), QStringLiteral("constant"));
                sourceSpec[QStringLiteral("value")] = p.value(QStringLiteral("value"));
                sourceSpec[QStringLiteral("variableId")] = p.value(QStringLiteral("variableId"));
                sourceSpec[QStringLiteral("switchId")] = p.value(QStringLiteral("switchId"));
                sourceSpec[QStringLiteral("commonValueId")] = p.value(QStringLiteral("commonValueId"));
            }
            const QVariant incoming = resolveCommonSource(sourceSpec, type);
            const QString op = p.value(QStringLiteral("op"), QStringLiteral("=")).toString();
            if (type == CommonValueType::Number) {
                const int before = frame.commonValues.value(id).toInt();
                const int value = incoming.toInt();
                int result = value;
                if (op == QLatin1String("+")) result = before + value;
                else if (op == QLatin1String("-")) result = before - value;
                else if (op == QLatin1String("*")) result = before * value;
                else if (op == QLatin1String("/")) result = value ? before / value : before;
                else if (op == QLatin1String("%")) result = value ? before % value : before;
                frame.commonValues[id] = result;
            } else if (type == CommonValueType::Boolean && op == QLatin1String("toggle")) {
                frame.commonValues[id] = !frame.commonValues.value(id).toBool();
            } else if (type == CommonValueType::Text && op == QLatin1String("+")) {
                frame.commonValues[id] = frame.commonValues.value(id).toString() + incoming.toString();
            } else {
                frame.commonValues[id] = coerceCommonValue(incoming, type);
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("common.return")) {
        const int commonIndex = commonFrameIndex();
        if (commonIndex >= 0) {
            CommonValueType type = CommonValueType::Number;
            QVariant defaultValue = 0;
            if (m_ed) if (const CommonEvent* ce = m_ed->commonEventByNumber(m_stack.at(commonIndex).commonEventNumber)) {
                type = ce->returnValue.type;
                defaultValue = ce->returnValue.defaultValue;
            }
            QVariantMap sourceSpec = p.value(QStringLiteral("sourceSpec")).toMap();
            QVariant value = defaultValue;
            if (!sourceSpec.isEmpty()) value = resolveCommonSource(sourceSpec, type);
            else if (p.contains(QStringLiteral("commonValueId"))) value = coerceCommonValue(commonValue(p.value(QStringLiteral("commonValueId")).toString()), type);
            else if (p.contains(QStringLiteral("value"))) value = coerceCommonValue(p.value(QStringLiteral("value")), type);
            returnFromCommon(value);
            return true;
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("if")) {
        if (evaluateCondition(c)) nextCommand(); else skipToElseOrEnd();
        return true;
    }
    if (c.type == QLatin1String("else")) { skipToEnd(); return true; }
    if (c.type == QLatin1String("endIf") || c.type == QLatin1String("loop.begin") ||
        c.type == QLatin1String("label") || c.type == QLatin1String("comment")) {
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("loop.end")) {
        Frame& frame = m_stack.last();
        int depth = 0;
        for (int i = frame.index - 1; i >= 0; --i) {
            const QString& type = frame.cmds[i].type;
            if (type == QLatin1String("loop.end")) ++depth;
            else if (type == QLatin1String("loop.begin")) {
                if (depth == 0) { frame.index = i; beginCommand(); return true; }
                --depth;
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("loop.break") || c.type == QLatin1String("repeat.break")) {
        Frame& frame = m_stack.last();
        int loopDepth = 0, repeatDepth = 0;
        for (int i = frame.index + 1; i < frame.cmds.size(); ++i) {
            const QString& type = frame.cmds[i].type;
            if (type == QLatin1String("loop.begin")) ++loopDepth;
            else if (type == QLatin1String("repeat.begin")) ++repeatDepth;
            else if (type == QLatin1String("loop.end")) {
                if (loopDepth == 0 && repeatDepth == 0) { frame.index = i; nextCommand(); return true; }
                if (loopDepth > 0) --loopDepth;
            } else if (type == QLatin1String("repeat.end")) {
                if (loopDepth == 0 && repeatDepth == 0) {
                    // descarta o estado do repeat externo que estamos abandonando
                    int d = 0;
                    for (int b = frame.index - 1; b >= 0; --b) {
                        if (frame.cmds[b].type == QLatin1String("repeat.end")) ++d;
                        else if (frame.cmds[b].type == QLatin1String("repeat.begin")) {
                            if (d == 0) { frame.repeatStates.remove(b); break; }
                            --d;
                        }
                    }
                    frame.index = i; nextCommand(); return true;
                }
                if (repeatDepth > 0) --repeatDepth;
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("jump")) {
        const QString target = p.value(QStringLiteral("label")).toString();
        Frame& frame = m_stack.last();
        for (int i = 0; i < frame.cmds.size(); ++i) {
            if (frame.cmds[i].type == QLatin1String("label") &&
                frame.cmds[i].params.value(QStringLiteral("name")).toString() == target) {
                frame.index = i;
                beginCommand();
                return true;
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("wait.until")) {
        if (evaluateCondition(c)) nextCommand();
        else awaitCondition(c, p.value(QStringLiteral("timeoutFrames"), 0).toInt());
        return true;
    }
    if (c.type == QLatin1String("wait")) {
        const int frames = qMax(0, p.value(QStringLiteral("frames"), 30).toInt());
        if (frames > 0) awaitFrames(frames);
        else nextCommand();
        return true;
    }
    if (c.type == QLatin1String("repeat.begin")) {
        Frame& frame = m_stack.last();
        RepeatState& state = frame.repeatStates[frame.index];
        if (state.iteration <= 0) {
            QVariantMap countSpec = p.value(QStringLiteral("countSpec")).toMap();
            if (countSpec.isEmpty()) countSpec = QVariantMap{{QStringLiteral("source"),QStringLiteral("constant")},
                                                             {QStringLiteral("value"),p.value(QStringLiteral("count"),1)}};
            const int count = qBound(0, resolveValueSpec(countSpec, CommonValueType::Number).toInt(), 1000000);
            state.remaining = count;
            state.iteration = count > 0 ? 1 : 0;
            state.indexTarget = p.value(QStringLiteral("indexTarget")).toMap();
            if (count <= 0) {
                int depth = 0;
                for (int i = frame.index + 1; i < frame.cmds.size(); ++i) {
                    const QString& type = frame.cmds[i].type;
                    if (type == QLatin1String("repeat.begin")) ++depth;
                    else if (type == QLatin1String("repeat.end")) {
                        if (depth == 0) { frame.repeatStates.remove(frame.index); frame.index = i; nextCommand(); return true; }
                        --depth;
                    }
                }
                frame.repeatStates.remove(frame.index); nextCommand(); return true;
            }
            if (!state.indexTarget.isEmpty()) applyValueTarget(state.indexTarget, state.iteration, CommonValueType::Number);
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("repeat.end")) {
        Frame& frame = m_stack.last();
        int depth = 0;
        for (int i = frame.index - 1; i >= 0; --i) {
            const QString& type = frame.cmds[i].type;
            if (type == QLatin1String("repeat.end")) ++depth;
            else if (type == QLatin1String("repeat.begin")) {
                if (depth == 0) {
                    auto it = frame.repeatStates.find(i);
                    if (it == frame.repeatStates.end()) { frame.index = i; beginCommand(); return true; }
                    if (it->remaining > 1) {
                        --it->remaining; ++it->iteration;
                        if (!it->indexTarget.isEmpty()) applyValueTarget(it->indexTarget, it->iteration, CommonValueType::Number);
                        frame.index = i; nextCommand(); return true;
                    }
                    frame.repeatStates.erase(it); nextCommand(); return true;
                }
                --depth;
            }
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("map.event.call")) {
        if (!m_ed || !m_state || !m_ed->doc()) { nextCommand(); return true; }
        const QString requestedMap = p.value(QStringLiteral("mapId")).toString();
        const QString mapId = requestedMap.isEmpty() ? m_ed->doc()->id : requestedMap;
        // Map Event é contexto de mundo vivo, não uma função de dados. A chamada
        // cruzada é rejeitada; teleporte continua sendo a fronteira correta.
        if (mapId != m_ed->doc()->id) { nextCommand(); return true; }
        const QString eventId = p.value(QStringLiteral("eventId")).toString();
        const MapEvent* target = m_ed->findEvent(eventId);
        if (!target) { nextCommand(); return true; }
        int pageIndex = p.value(QStringLiteral("pageIndex"), -1).toInt();
        if (pageIndex < 0) pageIndex = m_state->choosePage(*target);
        if (pushMapEvent(*target, pageIndex, mapId)) { beginCommand(); return true; }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("common.reserve")) {
        const QString commonId = p.value(QStringLiteral("commonId")).toString();
        const int number = p.value(QStringLiteral("number"), 0).toInt();
        const CommonEvent* ce = m_ed ? (!commonId.isEmpty() ? m_ed->commonEventById(commonId)
                                                            : m_ed->commonEventByNumber(number)) : nullptr;
        if (ce && m_reserveCommonEventHook) {
            const QVariantMap specs = p.value(QStringLiteral("arguments")).toMap();
            QVariantMap resolved;
            for (const CommonEventParameter& def : ce->parameters) {
                QVariant value = def.defaultValue;
                if (specs.contains(def.id)) value = resolveCommonSource(specs.value(def.id).toMap(), def.type);
                resolved.insert(def.id, coerceCommonValue(value, def.type));
            }
            m_reserveCommonEventHook(ce->id, resolved, qBound(-1000, p.value(QStringLiteral("priority"),0).toInt(), 1000));
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("flow.exit")) {
        const QString scope = p.value(QStringLiteral("scope"), QStringLiteral("frame")).toString();
        if (scope == QLatin1String("all")) { stop(); return true; }
        if (scope == QLatin1String("common")) {
            const int commonIndex = commonFrameIndex();
            if (commonIndex >= 0) {
                CommonValueType type = m_stack.at(commonIndex).returnType;
                QVariant value = m_stack.at(commonIndex).returnValue;
                const QVariantMap sourceSpec = p.value(QStringLiteral("sourceSpec")).toMap();
                if (!sourceSpec.isEmpty()) value = resolveCommonSource(sourceSpec, type);
                returnFromCommon(value); return true;
            }
            nextCommand(); return true;
        }
        int targetIndex = m_stack.size() - 1;
        if (scope == QLatin1String("map")) {
            targetIndex = -1;
            for (int i = m_stack.size() - 1; i >= 0; --i)
                if (!m_stack.at(i).commonFrame && !m_stack.at(i).eventId.isEmpty()) { targetIndex = i; break; }
            if (targetIndex < 0) { nextCommand(); return true; }
        }
        if (targetIndex >= 0 && targetIndex < m_stack.size()) {
            const bool targetCommon = m_stack.at(targetIndex).commonFrame;
            if (targetCommon) {
                const QVariant value = m_stack.at(targetIndex).returnValue;
                returnFromCommon(value); return true;
            }
            while (m_stack.size() > targetIndex + 1) m_stack.removeLast();
            m_stack.removeLast();
            if (m_stack.isEmpty()) { stop(); return true; }
            ++m_stack.last().index;
            if (m_stack.last().index < m_stack.last().cmds.size()) beginCommand(); else nextCommand();
            return true;
        }
        nextCommand(); return true;
    }
    if (c.type == QLatin1String("common.call")) {
        const QString commonId = p.value(QStringLiteral("commonId")).toString();
        const int number = p.value(QStringLiteral("number"), 0).toInt();
        const CommonEvent* ce = m_ed ? (!commonId.isEmpty() ? m_ed->commonEventById(commonId)
                                                            : m_ed->commonEventByNumber(number)) : nullptr;
        if (ce && ce->commands.isEmpty()) {
            if (ce->returnValue.enabled)
                applyCommonReturn(p.value(QStringLiteral("returnTarget")).toMap(),
                                  coerceCommonValue(ce->returnValue.defaultValue, ce->returnValue.type));
            nextCommand(); return true;
        }
        if (ce && pushCommonEvent(*ce, p.value(QStringLiteral("arguments")).toMap(),
                                  p.value(QStringLiteral("returnTarget")).toMap(), false)) {
            beginCommand();
            return true;
        }
        nextCommand(); return true;
    }
    return false;
}

bool Interpreter::handlePluginCommand(const EventCommand& c)
{
    if (CommandRegistry::domain(c.type) != CommandDomain::Plugin) return false;
    if (!m_ed) { nextCommand(); return true; }

    const QVariantMap& p = c.params;
    const PluginInvocationResolution resolution = resolvePluginInvocation(
        m_ed->plugins,
        p.value(QStringLiteral("pluginId")).toString(),
        p.value(QStringLiteral("commandId")).toString(),
        p.value(QStringLiteral("arguments")).toMap());

    if (!resolution.ready()) {
        qWarning().noquote() << QStringLiteral("[LUDO/plugin.call] %1: %2")
            .arg(pluginInvocationStatusId(resolution.status), resolution.error);
        nextCommand();
        return true;
    }

    if (m_stack.size() < 16 && pushInlineCommands(resolution.expandedCommands)) {
        beginCommand();
        return true;
    }
    nextCommand();
    return true;
}

bool Interpreter::runPictureCommand(const EventCommand& c)
{
    if (!m_pics) return false;                 // sem gerenciador: ignora
    const QVariantMap& p = c.params;
    const QString logicalName=p.value(QStringLiteral("logicalName")).toString().trimmed();
    int numero = qMax(1, p.value(QStringLiteral("number"), 1).toInt());
    if(!p.contains(QStringLiteral("number"))&&!logicalName.isEmpty()){
        const int named=m_pics->findByName(logicalName);if(named>0)numero=named;
    }
    // Duração vem em QUADROS (é o que o editores de RPG mostra ao autor) e vira
    // segundos aqui dentro — o runtime inteiro trabalha em segundos.
    const double dur = qMax(0, p.value(QStringLiteral("duration"), 0).toInt()) / 60.0;
    const PictureEase ease = pictureEaseFromId(
        p.value(QStringLiteral("ease"), QStringLiteral("linear")).toString());
    const bool esperar = p.value(QStringLiteral("wait"), false).toBool();

    auto talvezEsperar = [&](int alvo) {
        if (esperar && m_pics->busy(alvo)) { awaitPicture(alvo); return true; }
        return false;
    };

    auto dynamicSpec = [&](const QString& key) -> QVariantMap {
        const QVariant value=p.value(key);
        QVariantMap spec=value.toMap();
        if(!spec.isEmpty()&&spec.contains(QStringLiteral("source")))return spec;
        if(value.metaType().id()==QMetaType::QString&&looksLikeExpression(value.toString()))
            return {{QStringLiteral("source"),QStringLiteral("expression")},{QStringLiteral("expression"),value.toString()}};
        return {};
    };
    auto prepareDynamicPicture = [&](PictureDef& definition) {
        const struct Field { const char* key; PictureProp prop; double PictureDef::*member; } fields[] = {
            {"x",PictureProp::X,&PictureDef::x},{"y",PictureProp::Y,&PictureDef::y},
            {"opacity",PictureProp::Opacity,&PictureDef::opacity}
        };
        for(const Field& field:fields){
            const QVariantMap spec=dynamicSpec(QString::fromLatin1(field.key));
            if(!spec.isEmpty())definition.*(field.member)=resolveValueSpec(spec,CommonValueType::Number).toDouble();
        }
    };
    auto bindDynamicPicture = [&](int slot) {
        const struct Binding { const char* key; PictureProp prop; } bindings[]={{"x",PictureProp::X},{"y",PictureProp::Y},{"opacity",PictureProp::Opacity}};
        for(const Binding& binding:bindings){const QVariantMap spec=dynamicSpec(QString::fromLatin1(binding.key));if(!spec.isEmpty())m_pics->bindDynamicValue(slot,binding.prop,spec);}
    };

    if(c.type==QLatin1String("picture.timeline.define")){
        QVariantMap timelineParams=p;QVariantList frames=p.value(QStringLiteral("keyframes")).toList();
        if(frames.isEmpty()){const QJsonDocument json=QJsonDocument::fromJson(p.value(QStringLiteral("keyframes_json")).toString().toUtf8());if(json.isArray())frames=json.array().toVariantList();}
        timelineParams[QStringLiteral("duration")]=qMax(1,p.value(QStringLiteral("duration"),60).toInt())/60.0;
        for(QVariant& value:frames){QVariantMap frame=value.toMap();frame[QStringLiteral("time")]=qMax(0,frame.value(QStringLiteral("time")).toInt())/60.0;value=frame;}
        timelineParams[QStringLiteral("keyframes")]=frames;m_pics->defineTimeline(PictureTimeline::fromVariantMap(timelineParams));return false;
    }
    if(c.type==QLatin1String("picture.timeline.play")){m_pics->playTimeline(numero,p.value(QStringLiteral("name")).toString());return talvezEsperar(numero);}
    if(c.type==QLatin1String("picture.timeline.stop")){m_pics->stopTimeline(numero);return false;}
    if(c.type==QLatin1String("picture.onTouch")){m_pics->setOnTouch(numero,p.value(QStringLiteral("commonEventId")).toString());return false;}
    if(c.type==QLatin1String("picture.onClick")){m_pics->setOnClick(numero,p.value(QStringLiteral("commonEventId")).toString());return false;}

    if (c.type == QLatin1String("picture.text")) {
        // Mesma coisa que "mostrar imagem", mas a imagem é o texto desenhado.
        PictureDef d = PictureDef::fromParams(p);
        d.number = numero;
        d.rich = PictureRichText::fromParams(p.value(QStringLiteral("rich")).toMap());
        if(m_ed){
            const QString locale=core::currentPlayerLocale(m_ed->localization);
            d.rich.text=m_ed->localization.resolve(
                p.value(QStringLiteral("localizationKey")).toString(),d.rich.text,locale);
        }
        d.rich.text = resolveDataTokens(d.rich.text);
        d.rich.enabled = true;
        m_pics->show(d);
        return false;
    }
    if (c.type == QLatin1String("picture.show") || c.type == QLatin1String("picture.showByName")) {
        PictureDef d = PictureDef::fromParams(p);
        prepareDynamicPicture(d);
        if(c.type==QLatin1String("picture.showByName")){
            if(!p.contains(QStringLiteral("number")))d.number=0;else d.number=numero;
            numero=m_pics->showByName(logicalName,d);
        }else{
            d.number = numero;
            m_pics->show(d);
            if(!logicalName.isEmpty())m_pics->setLogicalName(numero,logicalName);
        }
        if(p.contains(QStringLiteral("group")))m_pics->setGroup(numero,p.value(QStringLiteral("group")).toString());
        bindDynamicPicture(numero);
        return false;
    }
    if(c.type==QLatin1String("picture.setGroup")){
        m_pics->setGroup(numero,p.value(QStringLiteral("group")).toString());return false;
    }
    if(c.type==QLatin1String("picture.eraseGroup")){
        m_pics->eraseGroup(p.value(QStringLiteral("group")).toString());return false;
    }
    if(c.type==QLatin1String("picture.moveGroup")){
        QMap<PictureProp,double> targets;
        const struct Field{const char* key;PictureProp prop;}fields[]={{"x",PictureProp::X},{"y",PictureProp::Y},{"scaleX",PictureProp::ScaleX},{"scaleY",PictureProp::ScaleY},{"opacity",PictureProp::Opacity},{"angle",PictureProp::Angle}};
        for(const Field& field:fields){const QString key=QString::fromLatin1(field.key);if(p.contains(key))targets.insert(field.prop,p.value(key).toDouble());}
        m_pics->moveGroup(p.value(QStringLiteral("group")).toString(),targets,dur,ease);
        if(esperar&&m_pics->anyBusy()){awaitPicture(-1);return true;}return false;
    }
    if(c.type==QLatin1String("picture.attach")){
        PictureAttachment attachment=PictureAttachment::fromVariantMap(p);
        QString target=p.value(QStringLiteral("target")).toString().trimmed();
        if(target.startsWith(QLatin1String("picture:"))){
            bool numeric=false;int parent=target.mid(8).toInt(&numeric);
            if(!numeric)parent=m_pics->findByName(target.mid(8));
            attachment.parentId=qMax(0,parent);attachment.target.clear();
        }else if(p.contains(QStringLiteral("parentName"))){attachment.parentId=m_pics->findByName(p.value(QStringLiteral("parentName")).toString());attachment.target.clear();}
        m_pics->attach(numero,attachment);return false;
    }
    if(c.type==QLatin1String("picture.detach")){m_pics->detach(numero);return false;}
    if (c.type == QLatin1String("picture.move")) {
        // Só as propriedades presentes no comando são animadas: mover em X
        // não pode zerar a opacidade que outro comando ajustou.
        QMap<PictureProp, double> alvos;
        auto talvez = [&](const char* chave, PictureProp prop) {
            const QString k = QString::fromLatin1(chave);
            if (p.contains(k)) alvos.insert(prop, p.value(k).toDouble());
        };
        talvez("x", PictureProp::X);
        talvez("y", PictureProp::Y);
        talvez("scaleX", PictureProp::ScaleX);
        talvez("scaleY", PictureProp::ScaleY);
        talvez("opacity", PictureProp::Opacity);
        talvez("angle", PictureProp::Angle);
        m_pics->moveTo(numero, alvos, dur, ease);
        return talvezEsperar(numero);
    }
    if (c.type == QLatin1String("picture.tween")) {
        const PictureProp prop = picturePropFromId(
            p.value(QStringLiteral("prop"), QStringLiteral("x")).toString());
        m_pics->tween(numero, prop, p.value(QStringLiteral("target"), 0.0).toDouble(), dur, ease);
        return talvezEsperar(numero);
    }
    if (c.type == QLatin1String("picture.zoomIn") ||
        c.type == QLatin1String("picture.zoomOut")) {
        // Zoom de Picture é apenas um preset estrutural sobre ScaleX/ScaleY.
        // A âncora/pivot não é recalculada aqui: RuntimePictureTransform faz a
        // escala em torno do mesmo pivot usado por rotação, flip e transições.
        const double defaultScale = c.type == QLatin1String("picture.zoomIn") ? 150.0 : 50.0;
        QMap<PictureProp, double> targets;
        targets.insert(PictureProp::ScaleX,
                       qMax(0.01, p.value(QStringLiteral("scaleX"), defaultScale).toDouble()));
        targets.insert(PictureProp::ScaleY,
                       qMax(0.01, p.value(QStringLiteral("scaleY"), defaultScale).toDouble()));
        m_pics->moveTo(numero, targets, dur, ease);
        return talvezEsperar(numero);
    }
    if (c.type == QLatin1String("picture.physics")) {
        if (p.value(QStringLiteral("remove"), false).toBool())
            m_pics->setPhysics(numero, 0, 12, 0, 10, 0, 0, 8);
        else
            m_pics->setPhysics(numero,
                               p.value(QStringLiteral("floatSpeed"), 0.0).toDouble(),
                               p.value(QStringLiteral("floatRange"), 12.0).toDouble(),
                               p.value(QStringLiteral("swaySpeed"), 0.0).toDouble(),
                               p.value(QStringLiteral("swayRange"), 10.0).toDouble(),
                               p.value(QStringLiteral("spinSpeed"), 0.0).toDouble(),
                               p.value(QStringLiteral("pulseSpeed"), 0.0).toDouble(),
                               p.value(QStringLiteral("pulseRange"), 8.0).toDouble());
        return false;
    }
    if (c.type == QLatin1String("picture.anchor")) {
        m_pics->setAnchor(numero,
                          pictureAnchorFromId(p.value(QStringLiteral("anchor")).toString()),
                          p.value(QStringLiteral("anchorX"), 0.0).toDouble(),
                          p.value(QStringLiteral("anchorY"), 0.0).toDouble());
        return false;
    }
    if (c.type == QLatin1String("picture.effects")) {
        m_pics->setEffects(numero, PictureEffects::fromParams(
            p.value(QStringLiteral("fx")).toMap()));
        return false;
    }
    if (c.type == QLatin1String("picture.negative")) {
        if (LivePicture* live = m_pics->at(numero)) {
            PictureEffects fx = live->def.fx;
            fx.negative.enabled = p.value(QStringLiteral("enabled"), true).toBool();
            fx.negative.strength = fx.negative.enabled ? qBound(0.0, p.value(QStringLiteral("strength"), 1.0).toDouble(), 1.0) : 0.0;
            fx.negative.transitionFrames = qMax(0, p.value(QStringLiteral("duration"), 0).toInt());
            m_pics->setEffects(numero, fx);
        }
        if (p.value(QStringLiteral("wait"), false).toBool() && m_pics->busy(numero)) { awaitPicture(numero); return true; }
        return false;
    }
    if (c.type == QLatin1String("picture.flip")) {
        m_pics->setFlip(numero,
                        p.value(QStringLiteral("flipH"), false).toBool(),
                        p.value(QStringLiteral("flipV"), false).toBool());
        return false;
    }
    if (c.type == QLatin1String("picture.display")) {
        m_pics->setDisplaySettings(numero,
            pictureSpaceFromId(p.value(QStringLiteral("space"), QStringLiteral("screen")).toString()),
            pictureLayerFromId(p.value(QStringLiteral("layer"), QStringLiteral("below-message")).toString()),
            p.value(QStringLiteral("duringBattle"), true).toBool(),
            p.value(QStringLiteral("eraseOnMapChange"), false).toBool(),
            p.value(QStringLiteral("affectedByTone"), false).toBool());
        return false;
    }
    if (c.type == QLatin1String("picture.clearEffects")) {
        m_pics->clearEffects(numero);
        return false;
    }
    if (c.type == QLatin1String("picture.transitionOut")) {
        const PictureTransition tipo = pictureTransitionFromId(
            p.value(QStringLiteral("transition")).toString());
        const int quadros = p.value(QStringLiteral("duration"), 30).toInt();
        m_pics->startTransitionOut(numero, tipo, quadros);
        // waitForEnd: a cutscene só continua depois que a imagem sumiu.
        if (esperar && m_pics->busy(numero)) { awaitPicture(numero); return true; }
        return false;
    }
    if (c.type == QLatin1String("picture.erase")) {
        // número 0 = apagar todas (atalho clássico do editores de RPG).
        if (p.value(QStringLiteral("number"), 1).toInt() <= 0) m_pics->clearAll();
        else m_pics->erase(numero);
        return false;
    }
    if (c.type == QLatin1String("picture.eraseAll")) {
        m_pics->clearAll();
        return false;
    }
    if (c.type == QLatin1String("picture.wait")) {
        // Espera as animações de um slot (ou de todos, com número 0).
        const int alvo = p.value(QStringLiteral("number"), 0).toInt();
        if (alvo <= 0) { if (m_pics->anyBusy()) { awaitPicture(-1); return true; } }
        else if (m_pics->busy(alvo)) { awaitPicture(alvo); return true; }
        return false;
    }
    return false;
}

void Interpreter::beginSubtitle(const EventCommand& c)
{
    if (!m_subs) { nextCommand(); return; }
    const QVariantMap& p = c.params;
    SubtitleRequest r;
    r.track=core::subtitleTrackFromId(p.value(QStringLiteral("track"),QStringLiteral("dialogue")).toString());
    const QString requestedLocale = m_ed ? core::currentPlayerLocale(m_ed->localization) : QString();
    const QString fallbackText=p.value(QStringLiteral("text")).toString();
    const QString fallbackSpeaker=p.value(QStringLiteral("speaker")).toString();
    r.text = resolveInputPrompts(resolveDataTokens(m_ed ? m_ed->localization.resolve(p.value(QStringLiteral("localizationKey")).toString(), fallbackText, requestedLocale) : fallbackText));
    r.speakerName = resolveDataTokens(m_ed ? m_ed->localization.resolve(p.value(QStringLiteral("speakerLocalizationKey")).toString(), fallbackSpeaker, requestedLocale) : fallbackSpeaker);
    if (p.contains(QStringLiteral("duration")))
        r.durationFrames = p.value(QStringLiteral("duration")).toInt();
    if (p.contains(QStringLiteral("waitForInput"))) {
        r.hasWaitForInput = true;
        r.waitForInput = p.value(QStringLiteral("waitForInput")).toBool();
    }
    if (p.contains(QStringLiteral("typewriter"))) {
        r.hasTypewriter = true;
        r.typewriter = p.value(QStringLiteral("typewriter")).toBool();
    }
    if (p.contains(QStringLiteral("typewriterSpeed")))
        r.typewriterSpeed = p.value(QStringLiteral("typewriterSpeed")).toDouble();
    if (p.contains(QStringLiteral("position"))) {
        r.hasPosition = true;
        r.position = core::subtitlePositionFromId(p.value(QStringLiteral("position")).toString());
    }
    if (p.contains(QStringLiteral("textAlign"))) {
        r.hasTextAlign = true;
        r.textAlign = core::subtitleAlignFromId(p.value(QStringLiteral("textAlign")).toString());
    }
    if (p.contains(QStringLiteral("boxAlign"))) {
        r.hasBoxAlign = true;
        r.boxAlign = core::subtitleAlignFromId(p.value(QStringLiteral("boxAlign")).toString());
    }
    if (p.contains(QStringLiteral("transitionIn"))) {
        r.hasTransitionIn = true;
        r.transitionIn = core::subtitleTransitionFromId(p.value(QStringLiteral("transitionIn")).toString());
    }
    if (p.contains(QStringLiteral("transitionOut"))) {
        r.hasTransitionOut = true;
        r.transitionOut = core::subtitleTransitionFromId(p.value(QStringLiteral("transitionOut")).toString());
    }
    if (p.contains(QStringLiteral("fontColor")))
        r.fontColor = QColor(p.value(QStringLiteral("fontColor")).toString());
    if (p.contains(QStringLiteral("nameColor")))
        r.nameColor = QColor(p.value(QStringLiteral("nameColor")).toString());
    if (p.contains(QStringLiteral("fontSize")))
        r.fontSize = p.value(QStringLiteral("fontSize")).toInt();
    r.gradient = core::TextGradientSpec::fromVariantMap(p.value(QStringLiteral("textGradient")).toMap());
    r.effects = core::TextEffectStack::fromVariantMap(p.value(QStringLiteral("textEffects")).toMap());

    const QString anc = p.value(QStringLiteral("anchor")).toString();
    if (anc == QLatin1String("player")) r.anchor = SubtitleAnchor::Player;
    else if (anc == QLatin1String("event")) {
        r.anchor = SubtitleAnchor::Event;
        r.anchorEventId = p.value(QStringLiteral("anchorEventId")).toString();
    }
    if (p.contains(QStringLiteral("anchorOffsetY")))
        r.anchorOffsetY = p.value(QStringLiteral("anchorOffsetY")).toInt();
    r.offsetX = p.value(QStringLiteral("offsetX")).toInt();
    r.offsetY = p.value(QStringLiteral("offsetY")).toInt();
    r.voiceFile = p.value(QStringLiteral("voiceFile")).toString();
    if (p.contains(QStringLiteral("voiceVolume")))
        r.voiceVolume = p.value(QStringLiteral("voiceVolume")).toInt();
    r.soundEffect = p.value(QStringLiteral("soundEffect")).toString();
    if (p.contains(QStringLiteral("soundVolume")))
        r.soundVolume = p.value(QStringLiteral("soundVolume")).toInt();

    if(c.type==QLatin1String("subtitle.enqueue"))m_subs->enqueue(r);else m_subs->show(r);

    // "waitForEnd" segura o evento até a legenda sumir; sem ele, a cutscene
    // segue e as próximas linhas podem entrar por cima (é assim no plugin).
    const bool esperar = p.value(QStringLiteral("waitForEnd")).toBool()
                         || (r.hasWaitForInput && r.waitForInput)
                         || (!r.hasWaitForInput && m_subs->style().waitForInput);
    if (esperar) { awaitSubtitles(); return; }
    nextCommand();
}

void Interpreter::awaitFrames(int frames)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::Frames;
    m_awaitable.framesRemaining = qMax(0, frames);
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("frames")}, {QStringLiteral("frames"), m_awaitable.framesRemaining}});
}

void Interpreter::awaitPicture(int target)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::Picture;
    m_awaitable.pictureTarget = target;
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("picture")}, {QStringLiteral("target"), target}});
}

void Interpreter::awaitMoveRoute(QString target, MoveRouteTicket ticket)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::MoveRoute;
    m_awaitable.routeTarget = std::move(target);
    m_awaitable.routeTicket = ticket;
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("moveRoute")}, {QStringLiteral("target"), m_awaitable.routeTarget}, {QStringLiteral("ticket"), double(ticket)}});
}

void Interpreter::awaitRuntime(EventCommand command)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::Runtime;
    m_awaitable.command = std::move(command);
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("runtime")}, {QStringLiteral("command"), m_awaitable.command.type}});
}

void Interpreter::awaitSubtitles()
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::Subtitles;
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("subtitles")}});
}

void Interpreter::awaitCondition(EventCommand command, int timeoutFrames)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::Condition;
    m_awaitable.command = std::move(command);
    m_awaitable.timeoutFramesRemaining = qMax(0, timeoutFrames);
    emitDiagnosticTrace(QStringLiteral("awaitable.start"), {{QStringLiteral("kind"), QStringLiteral("condition")}, {QStringLiteral("timeoutFrames"), m_awaitable.timeoutFramesRemaining}});
}

void Interpreter::awaitParallelBlock(quint64 ticket, int resumeCommandIndex)
{
    m_awaitable.clear();
    m_awaitable.kind = AwaitableKind::ParallelBlock;
    m_awaitable.parallelTicket = ticket;
    m_awaitable.resumeCommandIndex = resumeCommandIndex;
    emitDiagnosticTrace(QStringLiteral("parallel.ticket"), {{QStringLiteral("ticket"), double(ticket)}, {QStringLiteral("resumeCommandIndex"), resumeCommandIndex}});
}

void Interpreter::clearAwaitable()
{
    if (m_awaitable.active())
        emitDiagnosticTrace(QStringLiteral("awaitable.finish"),
                            {{QStringLiteral("kind"), awaitableKindId(m_awaitable.kind)},
                             {QStringLiteral("parallelTicket"), double(m_awaitable.parallelTicket)}});
    m_awaitable.clear();
}

bool Interpreter::updateAwaitable(double dt, bool confirmEdge)
{
    if (!m_awaitable.active()) return false;

    constexpr double kLogicalFrame = 1.0 / 60.0;
    const auto consumeLogicalFrames = [dt](double& accumulator) {
        accumulator += qMax(0.0, dt);
        const int elapsed = int((accumulator + 1e-9) / kLogicalFrame);
        if (elapsed > 0) accumulator -= elapsed * kLogicalFrame;
        return elapsed;
    };

    switch (m_awaitable.kind) {
    case AwaitableKind::Frames: {
        const int elapsed = consumeLogicalFrames(m_awaitable.frameAccumulator);
        if (elapsed <= 0) return true;
        m_awaitable.framesRemaining -= elapsed;
        if (m_awaitable.framesRemaining > 0) return true;
        clearAwaitable(); nextCommand(); return true;
    }
    case AwaitableKind::Picture: {
        const int target = m_awaitable.pictureTarget;
        const bool busy = m_pics && (target < 0 ? m_pics->anyBusy() : m_pics->busy(target));
        if (busy) return true;
        clearAwaitable(); nextCommand(); return true;
    }
    case AwaitableKind::MoveRoute: {
        const MoveRouteTicketState state = m_moveRouteStateHook
            ? m_moveRouteStateHook(m_awaitable.routeTarget, m_awaitable.routeTicket)
            : MoveRouteTicketState::Unknown;
        if (state == MoveRouteTicketState::Pending || state == MoveRouteTicketState::Running ||
            state == MoveRouteTicketState::Paused) return true;
        clearAwaitable(); nextCommand(); return true;
    }
    case AwaitableKind::Runtime: {
        const EventCommand command = m_awaitable.command;
        const CommandResult result = m_runtimeCommandHook
            ? m_runtimeCommandHook(command, false)
            : CommandResult::Rejected;
        if (result == CommandResult::Waiting || result == CommandResult::DeferredCommit) return true;
        clearAwaitable(); nextCommand(); return true;
    }
    case AwaitableKind::Subtitles:
        updateSubtitleWait(confirmEdge);
        return true;
    case AwaitableKind::Condition: {
        const EventCommand condition = m_awaitable.command;
        if (evaluateCondition(condition)) {
            clearAwaitable(); nextCommand(); return true;
        }
        if (m_awaitable.timeoutFramesRemaining <= 0) return true;
        const int elapsed = consumeLogicalFrames(m_awaitable.timeoutAccumulator);
        if (elapsed <= 0) return true;
        m_awaitable.timeoutFramesRemaining -= elapsed;
        if (m_awaitable.timeoutFramesRemaining > 0) return true;
        // Timeout é deliberadamente não-fatal: Wait Until é uma barreira de
        // fluxo, não uma ramificação. O Event Debugger consegue distinguir o
        // awaitable Condition e o autor pode usar um If logo depois se quiser.
        clearAwaitable(); nextCommand(); return true;
    }
    case AwaitableKind::ParallelBlock: {
        const quint64 ticket = m_awaitable.parallelTicket;
        if (ticket != 0 && m_parallelBlockStateHook && m_parallelBlockStateHook(ticket)) return true;
        const int resume = m_awaitable.resumeCommandIndex;
        clearAwaitable();
        if (!m_stack.isEmpty() && resume >= 0 && resume < m_stack.last().cmds.size())
            m_stack.last().index = resume;
        nextCommand();
        return true;
    }
    case AwaitableKind::None:
        break;
    }
    return false;
}

void Interpreter::updateSubtitleWait(bool confirmEdge)
{
    if (!m_subs) { clearAwaitable(); nextCommand(); return; }
    if (confirmEdge) m_subs->confirm();
    if (!m_subs->busy()) {
        clearAwaitable();
        nextCommand();
    }
}

void Interpreter::nextCommand()
{
    m_view = MessageView();
    m_choice = ChoiceView();
    clearAwaitable();
    m_pages.clear();
    // Avança no quadro atual; se ele acabou, volta para quem chamou (evento
    // comum que termina devolve o controle ao evento de origem).
    while (!m_stack.isEmpty()) {
        Frame& f = m_stack.last();
        ++f.index;
        if (f.index < f.cmds.size()) { beginCommand(); return; }
        const Frame finished = f;
        m_stack.removeLast();
        if (finished.commonFrame && finished.returnEnabled)
            applyCommonReturn(finished.returnTarget, coerceCommonValue(finished.returnValue, finished.returnType));
    }
    stop();
}

void Interpreter::skipToElseOrEnd()
{
    Frame& f = m_stack.last();
    int nivel = 0;
    for (int i = f.index + 1; i < f.cmds.size(); ++i) {
        const QString& t = f.cmds[i].type;
        if (t == QLatin1String("if")) ++nivel;
        else if (t == QLatin1String("endIf")) {
            if (nivel == 0) { f.index = i; beginCommand(); return; }
            --nivel;
        } else if (t == QLatin1String("else") && nivel == 0) {
            f.index = i + 1;                       // entra no ramo do senão
            if (f.index < f.cmds.size()) beginCommand();
            else nextCommand();
            return;
        }
    }
    nextCommand();                                  // sem fim: encerra o quadro
}

void Interpreter::skipToEnd()
{
    Frame& f = m_stack.last();
    int nivel = 0;
    for (int i = f.index + 1; i < f.cmds.size(); ++i) {
        const QString& t = f.cmds[i].type;
        if (t == QLatin1String("if")) ++nivel;
        else if (t == QLatin1String("endIf")) {
            if (nivel == 0) { f.index = i; beginCommand(); return; }
            --nivel;
        }
    }
    nextCommand();
}

EventCommand Interpreter::resolveCommandValueFields(const EventCommand& command) const
{
    EventCommand resolved = command;
    for (const core::CommandValueFieldDescriptor& field : core::commandValueFields(command.type)) {
        const QVariant raw = command.params.value(field.parameter);
        if (!core::isValueSpec(raw)) continue;
        resolved.params[field.parameter] = resolveValueSpec(raw.toMap(), field.type);
    }
    return resolved;
}

bool Interpreter::evaluateConditionPredicate(const QVariantMap& p) const
{
    if (!m_state) return false;
    const QString tipo = p.value(QStringLiteral("kind"), QStringLiteral("switch")).toString();
    if (tipo == QLatin1String("switch")) {
        const bool esperado = p.value(QStringLiteral("value"), true).toBool();
        return m_state->switchOn(p.value(QStringLiteral("id"), 1).toInt()) == esperado;
    }
    if (tipo == QLatin1String("selfSwitch")) {
        const core::EventExecutionContext context = executionContext();
        if (!context.hasMapEvent()) return false;
        const bool esperado = p.value(QStringLiteral("value"), true).toBool();
        return m_state->selfSwitch(context.mapEventId,
                                   p.value(QStringLiteral("letter"), QStringLiteral("A")).toString())
               == esperado;
    }
    if (tipo == QLatin1String("variable")) {
        const int esq = m_state->variable(p.value(QStringLiteral("id"), 1).toInt());
        QVariantMap rightSpec = p.value(QStringLiteral("rightSpec")).toMap();
        if (rightSpec.isEmpty()) {
            const QString source = p.value(QStringLiteral("source")).toString();
            if (source == QLatin1String("variable"))
                rightSpec = {{QStringLiteral("source"),QStringLiteral("variable")},{QStringLiteral("variableId"),p.value(QStringLiteral("valueVariable"),1)}};
            else if (source == QLatin1String("commonValue"))
                rightSpec = {{QStringLiteral("source"),QStringLiteral("commonValue")},{QStringLiteral("commonValueId"),p.value(QStringLiteral("commonValueId"))}};
            else rightSpec = core::valueSpecFromLegacy(p.value(QStringLiteral("value"), 0), 0);
        }
        const int dir = resolveValueSpec(rightSpec, CommonValueType::Number).toInt();
        return core::compareWithOp(esq, p.value(QStringLiteral("op"),
                                                QStringLiteral(">=")).toString(), dir);
    }
    if (tipo == QLatin1String("string")) {
        const QString left = m_state->stringValue(p.value(QStringLiteral("id"), 1).toInt());
        QVariantMap rightSpec = p.value(QStringLiteral("rightSpec")).toMap();
        const QString right = rightSpec.isEmpty()
            ? p.value(QStringLiteral("value")).toString()
            : resolveValueSpec(rightSpec, CommonValueType::Text).toString();
        const QString op = p.value(QStringLiteral("op"), QStringLiteral("==")).toString();
        if (op == QLatin1String("!=")) return left != right;
        if (op == QLatin1String("contains")) return left.contains(right);
        if (op == QLatin1String("startsWith")) return left.startsWith(right);
        if (op == QLatin1String("endsWith")) return left.endsWith(right);
        return left == right;
    }
    if (tipo == QLatin1String("commonValue")) {
        const QVariant leftValue = commonValue(p.value(QStringLiteral("id")).toString());
        const CommonValueType valueType = commonValueType(p.value(QStringLiteral("id")).toString(), CommonValueType::Number);
        const QString op = p.value(QStringLiteral("op"), QStringLiteral("==")).toString();
        QVariantMap rightSpec = p.value(QStringLiteral("rightSpec")).toMap();
        if (rightSpec.isEmpty()) {
            const QString source = p.value(QStringLiteral("source")).toString();
            if (source == QLatin1String("variable"))
                rightSpec = {{QStringLiteral("source"),QStringLiteral("variable")},{QStringLiteral("variableId"),p.value(QStringLiteral("valueVariable"),1)}};
            else if (source == QLatin1String("commonValue"))
                rightSpec = {{QStringLiteral("source"),QStringLiteral("commonValue")},{QStringLiteral("commonValueId"),p.value(QStringLiteral("valueCommonId"))}};
            else rightSpec = core::valueSpecFromLegacy(p.value(QStringLiteral("value")), p.value(QStringLiteral("value")));
        }
        const QVariant rightValue = resolveValueSpec(rightSpec, valueType);
        if (leftValue.metaType().id() == QMetaType::Bool)
            return op == QLatin1String("!=") ? leftValue.toBool() != rightValue.toBool() : leftValue.toBool() == rightValue.toBool();
        if (leftValue.metaType().id() == QMetaType::QString) {
            const QString a = leftValue.toString(), b = rightValue.toString();
            if (op == QLatin1String("!=")) return a != b;
            if (op == QLatin1String("contains")) return a.contains(b);
            if (op == QLatin1String("startsWith")) return a.startsWith(b);
            if (op == QLatin1String("endsWith")) return a.endsWith(b);
            return a == b;
        }
        return core::compareWithOp(leftValue.toInt(), op, rightValue.toInt());
    }
    if (tipo == QLatin1String("random")) return QRandomGenerator::global()->bounded(100) < qBound(0,p.value(QStringLiteral("chance"),50).toInt(),100);
    if (tipo == QLatin1String("gold")) {
        const QVariantMap rightSpec = p.value(QStringLiteral("rightSpec")).toMap();
        const int right = rightSpec.isEmpty() ? p.value(QStringLiteral("value")).toInt()
                                              : resolveValueSpec(rightSpec, CommonValueType::Number).toInt();
        return core::compareWithOp(m_state->gold(),p.value(QStringLiteral("op"),QStringLiteral(">=")).toString(),right);
    }
    if (tipo == QLatin1String("item")) {
        const QVariantMap amountSpec = p.value(QStringLiteral("amountSpec")).toMap();
        const int amount = amountSpec.isEmpty() ? p.value(QStringLiteral("amount"),1).toInt()
                                                : resolveValueSpec(amountSpec, CommonValueType::Number).toInt();
        return m_state->itemCount(p.value(QStringLiteral("itemId")).toString()) >= amount;
    }
    EventCommand predicateCommand{QStringLiteral("if"), p};
    if (m_conditionHook) return m_conditionHook(predicateCommand);
    return false;
}

bool Interpreter::evaluateCondition(const EventCommand& c) const
{
    const QVariantMap& params = c.params;
    const QVariantMap explicitTree = params.value(QStringLiteral("conditionTree")).toMap();
    if (!explicitTree.isEmpty()) {
        const bool result = core::evaluateConditionTree(explicitTree, [this](const QVariantMap& predicate) {
            return evaluateConditionPredicate(predicate);
        });
        emitDiagnosticTrace(QStringLiteral("condition.evaluate"),
                            {{QStringLiteral("result"), result}, {QStringLiteral("tree"), true}});
        return result;
    }
    const bool result = evaluateConditionPredicate(params);
    emitDiagnosticTrace(QStringLiteral("condition.evaluate"),
                        {{QStringLiteral("result"), result},
                         {QStringLiteral("kind"), params.value(QStringLiteral("kind")).toString()}});
    return result;
}

QString Interpreter::resolveDataTokens(QString text) const
{
    // Strings globais sao dados de primeira classe. O token \s[n] pode ser
    // usado nas mesmas superficies de texto que as variaveis numericas:
    // mensagens, escolhas, legendas e Pictures de texto.
    if (!m_state) return text;
    QRegularExpression stringToken(QStringLiteral("\\\\s\\[(\\d+)\\]"),
                                   QRegularExpression::CaseInsensitiveOption);
    auto matches = stringToken.globalMatch(text);
    QVector<QPair<QPair<int,int>,QString>> replacements;
    while (matches.hasNext()) {
        const auto match = matches.next();
        replacements.push_back({{match.capturedStart(), match.capturedLength()},
                                m_state->stringValue(match.captured(1).toInt())});
    }
    for (int i = replacements.size() - 1; i >= 0; --i)
        text.replace(replacements[i].first.first, replacements[i].first.second,
                     replacements[i].second);
    return text;
}

QString Interpreter::resolveInputPrompts(QString text) const
{
    if(!m_ed||!m_ed->inputSystem.adaptivePrompts)return text;
    QRegularExpression re(QStringLiteral("\\\\KEY\\[([^\\]]+)\\]"),QRegularExpression::CaseInsensitiveOption);auto it=re.globalMatch(text);QVector<QPair<QPair<int,int>,QString>> reps;
    const QString& activeController=m_inputController?*m_inputController:m_ed->inputSystem.forcedController;
    while(it.hasNext()){const auto m=it.next();bool ok=false;const GameAction a=gameActionFromId(m.captured(1),&ok);if(!ok)continue;const QString id=gameActionId(a);const bool controller=activeController==QLatin1String("generic")&&m_ed->inputSystem.controllerPrompts;const int icon=controller?m_ed->inputSystem.controllerIcons.value(id,-1):m_ed->inputSystem.keyboardIcons.value(id,-1);QString prompt;if(icon>=0&&m_ed->iconSet.iconRect(icon).isValid())prompt=QStringLiteral("\\I[%1]").arg(icon);else if(controller)prompt=QStringLiteral("[Botão %1]").arg(m_ed->inputSystem.controllerButtons.value(id,-1));else prompt=QStringLiteral("[%1]").arg(m_ed->inputMap.describe(a));reps.push_back({{m.capturedStart(),m.capturedLength()},prompt});}
    for(int i=reps.size()-1;i>=0;--i)text.replace(reps[i].first.first,reps[i].first.second,reps[i].second);return text;
}

QString Interpreter::resolveInlineLocalization(QString text) const
{
    if (!m_ed) return text;
    // \LOC[chave] usa a própria chave como fallback. \LOC[chave|texto]
    // permite que o evento continue legível quando a tradução estiver ausente.
    QRegularExpression token(QStringLiteral("\\\\LOC\\[([^\\]|]+)(?:\\|([^\\]]*))?\\]"),
                             QRegularExpression::CaseInsensitiveOption);
    auto matches = token.globalMatch(text);
    QVector<QPair<QPair<int, int>, QString>> replacements;
    const QString locale = core::currentPlayerLocale(m_ed->localization);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString key = match.captured(1).trimmed();
        const QString fallback = match.captured(2).isNull() ? key : match.captured(2);
        replacements.push_back({{match.capturedStart(), match.capturedLength()},
                                m_ed->localization.resolve(key, fallback, locale)});
    }
    for (int i = replacements.size() - 1; i >= 0; --i)
        text.replace(replacements[i].first.first, replacements[i].first.second,
                     replacements[i].second);
    return text;
}

QString Interpreter::resolveVariable(int id) const
{
    return m_state ? QString::number(m_state->variable(id)) : QStringLiteral("0");
}

void Interpreter::update(double dt, bool confirmEdge)
{
    if (!m_running) return;
    m_instantBudget = 0;
    if (m_debugBlocked) { beginCommand(); return; }
    if (m_yieldPending) {
        m_yieldPending = false;
        beginCommand();
        return;
    }
    if (updateAwaitable(dt, confirmEdge)) return;
    if (m_choice.visible) {
        const double safeDt = qMax(0.0, dt);
        m_choice.effectTimeSec += safeDt;
        refreshChoiceConditions();
        if (m_choice.exiting) {
            m_choice.exitTimeSec = qMax(0.0, m_choice.exitTimeSec) + safeDt;
            double span = 0.0;
            for (const TextPage& page : m_choice.richPages)
                span = qMax(span, textEffectPhaseSpanSec(m_choice.effects.exit,
                                                        page.drawableCount(),
                                                        textPageWordCount(page)));
            if (m_choice.exitTimeSec + 1e-9 >= span) {
                const int value = m_choice.pendingResult;
                completeChoice(value);
            }
            return;
        }
        if (m_choice.timeLimit > 0.0) {
            m_choice.timeRemaining = qMax(0.0, m_choice.timeRemaining - safeDt);
            if (m_choice.timeRemaining <= 1e-9) { expireChoiceTimer(); return; }
        }
        if (confirmEdge && !m_choice.disabled.value(m_choice.selected, false)) finishChoice(m_choice.selected + 1);
        return;
    }
    if (m_view.visible) updateMessage(dt, confirmEdge);
}

void Interpreter::refreshChoiceConditions()
{
    if (!m_choice.visible) return;
    const int count = m_choice.options.size();
    m_choice.disabled.resize(count);
    for (int i = 0; i < count; ++i) {
        bool disabled = m_choice.staticDisabled.value(i, false);
        const QVariantMap tree = m_choice.conditions.value(i);
        if (!tree.isEmpty()) {
            const bool allowed = core::evaluateConditionTree(tree, [this](const QVariantMap& predicate) {
                return evaluateConditionPredicate(predicate);
            });
            disabled = disabled || !allowed;
        }
        m_choice.disabled[i] = disabled;
    }
    if (m_choice.disabled.value(m_choice.selected, false)) {
        for (int i = 0; i < count; ++i) {
            if (!m_choice.disabled.value(i, false)) { m_choice.selected = i; break; }
        }
    }
}

void Interpreter::expireChoiceTimer()
{
    if (!m_choice.visible || m_choice.exiting) return;
    int index = m_choice.defaultChoice;
    if (index >= 0 && !m_choice.disabled.value(index, false)) { finishChoice(index + 1); return; }
    if (index < 0 && m_choice.cancelValue >= 0) { finishChoice(m_choice.cancelValue); return; }
    for (int i = 0; i < m_choice.options.size(); ++i) {
        if (!m_choice.disabled.value(i, false)) { finishChoice(i + 1); return; }
    }
    // Todas bloqueadas e cancelamento proibido: conclui sem ramo para não
    // deixar o evento preso para sempre.
    finishChoice(0);
}

void Interpreter::moveChoice(int delta)
{
    if (!m_choice.visible || m_choice.exiting || m_choice.options.isEmpty() || delta == 0) return;
    const int count = m_choice.options.size();
    int candidate = m_choice.selected;
    for (int tries = 0; tries < count; ++tries) {
        candidate = ui::moveFocusIndex(candidate, count, delta, true);
        if (!m_choice.disabled.value(candidate, false)) { m_choice.selected = candidate; return; }
    }
}

void Interpreter::cancelChoice()
{
    if (!m_choice.visible || m_choice.exiting || m_choice.cancelValue < 0) return;
    finishChoice(m_choice.cancelValue);
}

void Interpreter::finishChoice(int value)
{
    if (!m_choice.visible || m_choice.exiting) return;
    if (m_choice.effects.exit.enabled) {
        m_choice.exiting = true;
        m_choice.exitTimeSec = 0.0;
        m_choice.pendingResult = value;
        return;
    }
    completeChoice(value);
}

void Interpreter::completeChoice(int value)
{
    if (!m_choice.visible) return;
    const int variable = m_choice.resultVariable;
    if (m_state && variable > 0) m_state->setVariable(variable, value);
    QVector<core::EventCommand> branch;
    const int branchIndex=value-1;
    if(branchIndex>=0 && branchIndex<m_choice.branches.size()) branch=m_choice.branches.at(branchIndex);
    m_choice = ChoiceView();
    if (pushInlineCommands(branch)) { beginCommand(); return; }
    nextCommand();
}

void Interpreter::updateMessage(double dt, bool confirmEdge)
{
    const double safeDt = qMax(0.0, dt);
    m_view.effectTimeSec += safeDt;
    const QVector<TypedChar> chars = flatten(m_view.page);
    const int total = chars.size();

    // ---- fase de saída ----------------------------------------------------
    // A última página não some no mesmo frame do Confirm. A mesma fase Exit
    // configurada no Editor é avaliada pelo TextEffectRuntime e o Interpreter
    // só avança o fluxo quando o último alvo staggered terminou.
    if (m_view.exiting) {
        m_view.exitTimeSec = qMax(0.0, m_view.exitTimeSec) + safeDt;
        const double span = textEffectPhaseSpanSec(m_view.effects.exit,
                                                   m_view.page.drawableCount(),
                                                   textPageWordCount(m_view.page));
        if (m_view.exitTimeSec + 1e-9 >= span) nextCommand();
        return;
    }

    // ---- já revelou tudo (ou parou num \!): espera a tecla ----------------
    if (m_view.waitingKey) {
        if (!confirmEdge) return;
        m_view.waitingKey = false;
        if (m_view.revealed >= total) {
            if (m_pageIdx + 1 < m_pages.size()) {
                beginPage(m_pageIdx + 1);
            } else if (m_view.effects.exit.enabled) {
                m_view.exiting = true;
                m_view.exitTimeSec = 0.0;
            } else {
                nextCommand();
            }
            return;
        }
        // Era um \! no meio da frase: risca a marca, senão a próxima volta ao
        // laço de digitação pararia nela de novo — e a fala nunca terminaria.
        int k = m_view.revealed;
        for (TextLine& l : m_view.page.lines) {
            if (k < l.chars.size()) { l.chars[k].waitKey = false; break; }
            k -= l.chars.size();
        }
        return;
    }

    // ---- apertou no meio da digitação: mostra a página inteira -----------
    if (confirmEdge) {
        // Uma espera \! no meio não pode ser pulada: revela até ela.
        int limite = total;
        for (int i = m_view.revealed; i < total; ++i)
            if (chars[i].waitKey && i > m_view.revealed) { limite = i; break; }
        const int oldRevealed = m_view.revealed;
        m_view.revealed = limite;
        markMessageGlyphBirths(m_view, chars, oldRevealed, m_view.revealed, m_view.effectTimeSec);
        m_waitSec = 0.0;
        m_charAcc = 0.0;
        if (m_view.revealed >= total) m_view.waitingKey = true;
        return;
    }

    // ---- pausa pendente (\. e \|) ----------------------------------------
    if (m_waitSec > 0.0) {
        m_waitSec -= dt;
        if (m_waitSec > 0.0) return;
        dt = -m_waitSec;              // sobra do quadro continua digitando
        m_waitSec = 0.0;
    }

    // ---- digitação --------------------------------------------------------
    double charsPerSecond = m_style.charsPerSecond;
    const double presetCps = textEffectTypewriterCharsPerSecond(m_view.effects.entrance);
    if (presetCps > 0.0) charsPerSecond = presetCps;
    m_charAcc += dt * qMax(1.0, charsPerSecond) * m_dialogueFastForwardSpeed;
    if (m_dialogueSkipMode) m_charAcc = qMax(m_charAcc, double(total + 1));
    while (m_view.revealed < total) {
        const TypedChar& c = chars[m_view.revealed];
        if (c.waitKey && m_view.revealed > 0) { m_view.waitingKey = true; return; }
        if (c.pauseSec > 0.0) {
            m_waitSec = c.pauseSec;
            // Consome a pausa: apaga a marca na página, senão a letra ficaria
            // pausando de novo a cada quadro e o texto nunca andaria.
            int k = m_view.revealed;
            for (TextLine& l : m_view.page.lines) {
                if (k < l.chars.size()) { l.chars[k].pauseSec = 0.0; break; }
                k -= l.chars.size();
            }
            return;
        }
        if (!c.instant && m_charAcc < 1.0) break;
        if (!c.instant) m_charAcc -= 1.0;
        const int oldRevealed = m_view.revealed;
        ++m_view.revealed;
        markMessageGlyphBirths(m_view, chars, oldRevealed, m_view.revealed, m_view.effectTimeSec);
    }
    if (m_view.revealed >= total) m_view.waitingKey = true;
}

} // namespace game
