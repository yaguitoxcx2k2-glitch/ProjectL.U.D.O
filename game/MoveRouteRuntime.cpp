#include "MoveRouteRuntime.h"

#include <QJsonArray>
#include <QtGlobal>
#include <cmath>

namespace game {
namespace {

double finiteNonNegative(double value)
{
    return std::isfinite(value) ? qMax(0.0, value) : 0.0;
}

MoveRouteTicket jsonTicket(const QJsonValue& value)
{
    // Tickets são quint64. JSON Number usa double e perde precisão acima de
    // 2^53; payload novo grava string decimal e ainda lê o formato numérico
    // legado para manter compatibilidade.
    if (value.isString()) {
        bool ok=false;
        const qulonglong ticket=value.toString().toULongLong(&ok);
        return ok ? MoveRouteTicket(ticket) : 0;
    }
    const double raw = value.toDouble(0.0);
    if (!std::isfinite(raw) || raw <= 0.0) return 0;
    return MoveRouteTicket(raw);
}

} // namespace

QString moveRouteTicketStateId(MoveRouteTicketState state)
{
    switch (state) {
    case MoveRouteTicketState::Pending:   return QStringLiteral("pending");
    case MoveRouteTicketState::Running:   return QStringLiteral("running");
    case MoveRouteTicketState::Paused:    return QStringLiteral("paused");
    case MoveRouteTicketState::Completed: return QStringLiteral("completed");
    case MoveRouteTicketState::Cancelled: return QStringLiteral("cancelled");
    case MoveRouteTicketState::Failed:    return QStringLiteral("failed");
    case MoveRouteTicketState::Unknown:   break;
    }
    return QStringLiteral("unknown");
}

MoveRouteControlAction moveRouteControlActionFromId(const QString& id)
{
    const QString value = id.trimmed().toLower();
    if (value == QLatin1String("pause")) return MoveRouteControlAction::Pause;
    if (value == QLatin1String("resume")) return MoveRouteControlAction::Resume;
    if (value == QLatin1String("cancel")) return MoveRouteControlAction::Cancel;
    if (value == QLatin1String("clearqueue")) return MoveRouteControlAction::ClearQueue;
    return MoveRouteControlAction::Invalid;
}

QString moveRouteControlActionId(MoveRouteControlAction action)
{
    switch (action) {
    case MoveRouteControlAction::Invalid:    return QStringLiteral("invalid");
    case MoveRouteControlAction::Pause:      return QStringLiteral("pause");
    case MoveRouteControlAction::Resume:     return QStringLiteral("resume");
    case MoveRouteControlAction::Cancel:     return QStringLiteral("cancel");
    case MoveRouteControlAction::ClearQueue: return QStringLiteral("clearQueue");
    }
    return QStringLiteral("pause");
}

bool MoveRouteRuntime::start(const MoveRouteRequest& request, core::MoveRouteStartMode mode)
{
    if (request.ticket == 0 || request.route.commands.isEmpty()) return false;
    if (mode == core::MoveRouteStartMode::Queue && m_current) {
        m_queue.enqueue(request);
        return true;
    }
    if (mode == core::MoveRouteStartMode::Queue && !m_current) {
        m_current = request;
        normalizeCurrent();
        return true;
    }

    cancelAll();
    m_current = request;
    normalizeCurrent();
    return true;
}

void MoveRouteRuntime::startAutonomous(const core::MoveRoute& route)
{
    cancelAll();
    if (route.commands.isEmpty()) return;
    MoveRouteRequest request;
    request.ticket = 0; // rotas de página não são esperadas pelo Interpreter
    request.route = route;
    // Rota da página é sempre local/autônoma. Campos copiados de comandos
    // forçados ou de projetos antigos são saneados na fronteira do runtime.
    request.route.target = QStringLiteral("self");
    request.route.startMode = core::MoveRouteStartMode::Replace;
    request.route.waitForCompletion = false;
    request.sourceEventId.clear();
    m_current = request;
    normalizeCurrent();
}

bool MoveRouteRuntime::ready() const
{
    return m_current && !m_paused && !m_awaitingMovement && !m_advanceAfterWait &&
           m_waitSeconds <= 0.0 && m_cooldownSeconds <= 0.0 && currentCommand();
}

const core::MoveCommand* MoveRouteRuntime::currentCommand() const
{
    if (!m_current) return nullptr;
    const auto& commands = m_current->route.commands;
    if (m_commandIndex < 0 || m_commandIndex >= commands.size()) return nullptr;
    return &commands.at(m_commandIndex);
}

void MoveRouteRuntime::update(double dt)
{
    // Diagnóstico O6 é por tick: se não houver avanço de A* neste update, o
    // contador deve mostrar zero em vez de repetir o frame anterior.
    m_pathExpandedThisTick = 0;
    if (!m_current || m_paused || dt <= 0.0) return;
    if (m_waitSeconds > 0.0) {
        m_waitSeconds = qMax(0.0, m_waitSeconds - dt);
        if (m_waitSeconds <= 0.0 && m_advanceAfterWait) {
            const double cooldown = m_deferredCooldown;
            m_advanceAfterWait = false;
            m_deferredCooldown = 0.0;
            advance(cooldown);
        }
        return;
    }
    if (m_cooldownSeconds > 0.0)
        m_cooldownSeconds = qMax(0.0, m_cooldownSeconds - dt);
}

void MoveRouteRuntime::advance(double cooldownSeconds)
{
    if (!m_current) return;
    m_awaitingMovement = false;
    m_advanceAfterMovement = true;
    m_advanceAfterWait = false;
    m_waitSeconds = 0.0;
    clearPathPlan();
    m_deferredCooldown = 0.0;
    m_blockedAttempts = 0;

    ++m_commandIndex;
    if (m_commandIndex < m_current->route.commands.size()) {
        m_cooldownSeconds = finiteNonNegative(cooldownSeconds);
        return;
    }
    if (m_current->route.repeat && !m_current->route.commands.isEmpty()) {
        m_commandIndex = 0;
        m_cooldownSeconds = finiteNonNegative(cooldownSeconds);
        return;
    }

    finishCurrent(MoveRouteTicketState::Completed);
    if (m_current) m_cooldownSeconds = finiteNonNegative(cooldownSeconds);
}

void MoveRouteRuntime::deferAdvance(double waitSeconds, double cooldownAfterWait)
{
    if (!m_current) return;
    m_waitSeconds = finiteNonNegative(waitSeconds);
    m_deferredCooldown = finiteNonNegative(cooldownAfterWait);
    m_advanceAfterWait = true;
    if (m_waitSeconds <= 0.0) {
        m_advanceAfterWait = false;
        const double cooldown = m_deferredCooldown;
        m_deferredCooldown = 0.0;
        advance(cooldown);
    }
}

void MoveRouteRuntime::markMovementStarted(bool advanceCommandWhenFinished)
{
    if (!m_current) return;
    m_awaitingMovement = true;
    m_advanceAfterMovement = advanceCommandWhenFinished;
    m_blockedAttempts = 0;
}

void MoveRouteRuntime::notifyMovementFinished(double cooldownSeconds)
{
    if (!m_current || !m_awaitingMovement) return;
    m_awaitingMovement = false;
    const bool advanceCommand=m_advanceAfterMovement;
    m_advanceAfterMovement=true;
    if(advanceCommand) advance(cooldownSeconds);
    else m_cooldownSeconds=finiteNonNegative(cooldownSeconds);
}

void MoveRouteRuntime::deferRetry(double delaySeconds)
{
    if(!m_current)return;
    m_cooldownSeconds=finiteNonNegative(delaySeconds);
}

void MoveRouteRuntime::handleBlocked(double retryDelaySeconds, double skipCooldownSeconds,
                                     const QString& reason)
{
    if (!m_current) return;
    ++m_blockedAttempts;
    clearPathPlan(reason);
    switch (m_current->route.blockedPolicy) {
    case core::MoveRouteBlockedPolicy::Wait:
        m_cooldownSeconds = finiteNonNegative(retryDelaySeconds);
        break;
    case core::MoveRouteBlockedPolicy::Skip:
        advance(skipCooldownSeconds);
        break;
    case core::MoveRouteBlockedPolicy::Cancel:
        fail(reason.trimmed().isEmpty()?QStringLiteral("blocked"):reason.trimmed());
        break;
    }
}

void MoveRouteRuntime::setPathPlan(const QPoint& targetHU,const QVector<QPoint>& nodesHU,
                                   int expandedNodes,const QString& status)
{
    if(!m_current)return;
    const bool continuingSearch=m_pathCache.commandIndex==m_commandIndex &&
        m_pathCache.targetHU==targetHU && m_pathCache.status==QLatin1String("searching");
    const int priorReplans=(m_pathCache.commandIndex==m_commandIndex)?m_pathCache.replans:0;
    m_pathCache.commandIndex=m_commandIndex;
    m_pathCache.targetHU=targetHU;
    m_pathCache.nodesHU=nodesHU;
    m_pathCache.replans=continuingSearch?qMax(1,priorReplans):priorReplans+1;
    m_pathCache.expandedNodes=qMax(0,expandedNodes);
    m_pathCache.status=status;
}

void MoveRouteRuntime::beginPathSearch(const QPoint& targetHU)
{
    if(!m_current)return;
    const int priorReplans=(m_pathCache.commandIndex==m_commandIndex)?m_pathCache.replans:0;
    m_pathCache.commandIndex=m_commandIndex;
    m_pathCache.targetHU=targetHU;
    m_pathCache.nodesHU.clear();
    m_pathCache.replans=priorReplans+1;
    m_pathCache.expandedNodes=0;
    m_pathCache.status=QStringLiteral("searching");
    m_pathExpandedThisTick=0;
}

void MoveRouteRuntime::updatePathSearchProgress(int expandedNodes,int expandedThisTick,const QString& status)
{
    if(!m_current)return;
    m_pathCache.expandedNodes=qMax(0,expandedNodes);
    m_pathCache.status=status;
    m_pathExpandedThisTick=qMax(0,expandedThisTick);
}

void MoveRouteRuntime::consumePathNode()
{
    if(!m_pathCache.nodesHU.isEmpty())m_pathCache.nodesHU.removeFirst();
}

void MoveRouteRuntime::clearPathPlan(const QString& status)
{
    const int replans=m_pathCache.replans;
    m_pathCache=MoveRoutePathCache{};
    m_pathCache.replans=replans;
    m_pathCache.status=status;
    m_pathSearch.reset();
    m_pathExpandedThisTick=0;
}

void MoveRouteRuntime::fail(const QString& reason)
{
    finishCurrent(MoveRouteTicketState::Failed, reason.trimmed().isEmpty()
                  ? QStringLiteral("route-failed") : reason.trimmed());
}

bool MoveRouteRuntime::pause()
{
    if (!m_current || m_paused) return false;
    m_paused = true;
    return true;
}

bool MoveRouteRuntime::resume()
{
    if (!m_current || !m_paused) return false;
    m_paused = false;
    return true;
}

bool MoveRouteRuntime::cancelCurrent(bool startNext)
{
    if (!m_current) return false;
    finishCurrent(MoveRouteTicketState::Cancelled, QString(), startNext);
    return true;
}

void MoveRouteRuntime::clearQueue()
{
    while (!m_queue.isEmpty()) {
        const MoveRouteRequest request = m_queue.dequeue();
        remember(request.ticket, MoveRouteTicketState::Cancelled);
    }
}

void MoveRouteRuntime::cancelAll()
{
    if (m_current) finishCurrent(MoveRouteTicketState::Cancelled, QString(), false);
    clearQueue();
    m_current.reset();
    m_commandIndex = 0;
    m_waitSeconds = m_cooldownSeconds = m_deferredCooldown = 0.0;
    m_advanceAfterWait = m_awaitingMovement = m_paused = false;
    m_advanceAfterMovement = true;
    m_blockedAttempts = 0;
    m_pathCache = MoveRoutePathCache{};
    m_pathSearch.reset();
    m_pathExpandedThisTick = 0;
}

void MoveRouteRuntime::reset()
{
    cancelAll();
    m_history.clear();
    m_historyOrder.clear();
    m_lastFailure.clear();
}

MoveRouteTicketState MoveRouteRuntime::ticketState(MoveRouteTicket ticket) const
{
    if (ticket == 0) return MoveRouteTicketState::Unknown;
    if (m_current && m_current->ticket == ticket)
        return m_paused ? MoveRouteTicketState::Paused : MoveRouteTicketState::Running;
    for (const MoveRouteRequest& request : m_queue)
        if (request.ticket == ticket) return MoveRouteTicketState::Pending;
    return m_history.value(ticket, MoveRouteTicketState::Unknown);
}

QJsonObject MoveRouteRuntime::requestToJson(const MoveRouteRequest& request)
{
    return QJsonObject{
        {QStringLiteral("ticket"), QString::number(request.ticket)},
        {QStringLiteral("route"), QJsonObject::fromVariantMap(core::moveRouteToMap(request.route))},
        {QStringLiteral("sourceEventId"), request.sourceEventId}
    };
}

MoveRouteRequest MoveRouteRuntime::requestFromJson(const QJsonObject& object)
{
    MoveRouteRequest request;
    request.ticket = jsonTicket(object.value(QStringLiteral("ticket")));
    request.route = core::moveRouteFromMap(object.value(QStringLiteral("route")).toObject().toVariantMap());
    request.sourceEventId = object.value(QStringLiteral("sourceEventId")).toString();
    return request;
}

QJsonObject MoveRouteRuntime::toJson() const
{
    QJsonArray queue;
    for (const MoveRouteRequest& request : m_queue) queue.append(requestToJson(request));
    QJsonObject out{
        {QStringLiteral("version"), RuntimePayloadVersion},
        {QStringLiteral("commandIndex"), m_commandIndex},
        {QStringLiteral("waitSeconds"), finiteNonNegative(m_waitSeconds)},
        {QStringLiteral("cooldownSeconds"), finiteNonNegative(m_cooldownSeconds)},
        {QStringLiteral("deferredCooldown"), finiteNonNegative(m_deferredCooldown)},
        {QStringLiteral("advanceAfterWait"), m_advanceAfterWait},
        {QStringLiteral("awaitingMovement"), m_awaitingMovement},
        {QStringLiteral("paused"), m_paused},
        {QStringLiteral("blockedAttempts"), qMax(0, m_blockedAttempts)},
        {QStringLiteral("lastFailure"), m_lastFailure},
        {QStringLiteral("queue"), queue}
    };
    if (m_current) out.insert(QStringLiteral("current"), requestToJson(*m_current));
    return out;
}

void MoveRouteRuntime::fromJson(const QJsonObject& object, bool allowAutonomousTicket)
{
    reset();
    if (object.isEmpty()) return;
    const int version = object.value(QStringLiteral("version")).toInt(0);
    // Payload futuro não é interpretado por aproximação. Isso é preferível a
    // executar uma rota com semântica que esta build ainda não conhece.
    if (version > RuntimePayloadVersion) {
        m_lastFailure = QStringLiteral("unsupported-runtime-payload");
        return;
    }

    MoveRouteRequest current = requestFromJson(object.value(QStringLiteral("current")).toObject());
    const bool currentTicketValid = current.ticket != 0 || allowAutonomousTicket;
    if (currentTicketValid && !current.route.commands.isEmpty()) {
        if (current.ticket == 0) {
            current.route.target = QStringLiteral("self");
            current.route.startMode = core::MoveRouteStartMode::Replace;
            current.route.waitForCompletion = false;
        }
        m_current = current;
        m_commandIndex = qBound(0, object.value(QStringLiteral("commandIndex")).toInt(),
                                qMax(0, current.route.commands.size() - 1));
        m_waitSeconds = finiteNonNegative(object.value(QStringLiteral("waitSeconds")).toDouble());
        m_cooldownSeconds = finiteNonNegative(object.value(QStringLiteral("cooldownSeconds")).toDouble());
        m_deferredCooldown = finiteNonNegative(object.value(QStringLiteral("deferredCooldown")).toDouble());
        m_advanceAfterWait = object.value(QStringLiteral("advanceAfterWait")).toBool(false) && m_waitSeconds > 0.0;
        // Movimento físico é restaurado numa fronteira estável pelo World. Se o
        // save foi feito no meio de um passo, o comando é repetido em vez de
        // avançar silenciosamente ou ficar esperando um movimento inexistente.
        m_awaitingMovement = false;
        m_advanceAfterMovement = true;
        m_pathCache = MoveRoutePathCache{};
        m_pathSearch.reset();
        m_pathExpandedThisTick = 0;
        m_paused = object.value(QStringLiteral("paused")).toBool(false);
        m_blockedAttempts = qMax(0, object.value(QStringLiteral("blockedAttempts")).toInt());
        m_lastFailure = object.value(QStringLiteral("lastFailure")).toString();
    }
    for (const QJsonValue& value : object.value(QStringLiteral("queue")).toArray()) {
        MoveRouteRequest request = requestFromJson(value.toObject());
        // Rota autônoma não possui fila. Em runtimes forçados, ticket 0 é
        // inválido/corrompido e é descartado em vez de criar estado fantasma.
        if (request.ticket != 0 && !request.route.commands.isEmpty()) m_queue.enqueue(request);
    }
    if (!m_current) activateNext();
}

QJsonObject MoveRouteRuntime::diagnostics() const
{
    QJsonObject out{
        {QStringLiteral("active"), bool(m_current)},
        {QStringLiteral("busy"), busy()},
        {QStringLiteral("paused"), paused()},
        {QStringLiteral("awaitingMovement"), awaitingMovement()},
        {QStringLiteral("commandIndex"), m_commandIndex},
        {QStringLiteral("queuedCount"), m_queue.size()},
        {QStringLiteral("waitSeconds"), finiteNonNegative(m_waitSeconds)},
        {QStringLiteral("cooldownSeconds"), finiteNonNegative(m_cooldownSeconds)},
        {QStringLiteral("blockedAttempts"), qMax(0, m_blockedAttempts)},
        {QStringLiteral("lastFailure"), m_lastFailure}
    };
    if(m_pathCache.commandIndex>=0){
        out.insert(QStringLiteral("pathStatus"),m_pathCache.status);
        out.insert(QStringLiteral("pathCachedNodes"),m_pathCache.nodesHU.size());
        out.insert(QStringLiteral("pathReplans"),m_pathCache.replans);
        out.insert(QStringLiteral("pathExpandedNodes"),m_pathCache.expandedNodes);
        out.insert(QStringLiteral("pathExpandedThisTick"),m_pathExpandedThisTick);
        out.insert(QStringLiteral("pathSearching"),m_pathCache.status==QLatin1String("searching"));
        out.insert(QStringLiteral("pathExpansionBudgetPerTick"),MovePathSearchState::DefaultExpansionBudgetPerTick);
        out.insert(QStringLiteral("pathTargetXHU"),m_pathCache.targetHU.x());
        out.insert(QStringLiteral("pathTargetYHU"),m_pathCache.targetHU.y());
    }
    if (m_current) {
        const core::MoveCommand* command = currentCommand();
        out.insert(QStringLiteral("ticket"), QString::number(m_current->ticket));
        out.insert(QStringLiteral("state"), m_current->ticket == 0
            ? QStringLiteral("autonomous") : moveRouteTicketStateId(ticketState(m_current->ticket)));
        out.insert(QStringLiteral("target"), m_current->route.target);
        out.insert(QStringLiteral("commandType"), command ? command->type : QString());
        out.insert(QStringLiteral("blockedPolicy"), core::moveRouteBlockedPolicyId(m_current->route.blockedPolicy));
        out.insert(QStringLiteral("startMode"), core::moveRouteStartModeId(m_current->route.startMode));
        out.insert(QStringLiteral("sourceEventId"), m_current->sourceEventId);
    } else {
        out.insert(QStringLiteral("ticket"), QStringLiteral("0"));
        out.insert(QStringLiteral("state"), QStringLiteral("idle"));
    }
    return out;
}

MoveRouteTicket MoveRouteRuntime::highestTicket() const
{
    MoveRouteTicket highest = m_current ? m_current->ticket : 0;
    for (const MoveRouteRequest& request : m_queue) highest = qMax(highest, request.ticket);
    for (auto it = m_history.constBegin(); it != m_history.constEnd(); ++it) highest = qMax(highest, it.key());
    return highest;
}

void MoveRouteRuntime::normalizeCurrent()
{
    m_commandIndex = 0;
    m_waitSeconds = m_cooldownSeconds = m_deferredCooldown = 0.0;
    m_advanceAfterWait = m_awaitingMovement = m_paused = false;
    m_advanceAfterMovement = true;
    m_blockedAttempts = 0;
    m_pathCache = MoveRoutePathCache{};
    m_pathSearch.reset();
    m_pathExpandedThisTick = 0;
    m_lastFailure.clear();
}

void MoveRouteRuntime::activateNext()
{
    if (m_current || m_queue.isEmpty()) return;
    m_current = m_queue.dequeue();
    normalizeCurrent();
}

void MoveRouteRuntime::finishCurrent(MoveRouteTicketState terminalState, const QString& reason,
                                     bool startNext)
{
    if (!m_current) return;
    remember(m_current->ticket, terminalState);
    if (terminalState == MoveRouteTicketState::Failed) m_lastFailure = reason;
    m_current.reset();
    m_commandIndex = 0;
    m_waitSeconds = m_cooldownSeconds = m_deferredCooldown = 0.0;
    m_advanceAfterWait = m_awaitingMovement = m_paused = false;
    m_advanceAfterMovement = true;
    m_blockedAttempts = 0;
    m_pathCache = MoveRoutePathCache{};
    m_pathSearch.reset();
    m_pathExpandedThisTick = 0;
    if (startNext) activateNext();
}

void MoveRouteRuntime::remember(MoveRouteTicket ticket, MoveRouteTicketState state)
{
    if (ticket == 0) return;
    if (!m_history.contains(ticket)) m_historyOrder.enqueue(ticket);
    m_history.insert(ticket, state);
    while (m_historyOrder.size() > 64) {
        const MoveRouteTicket old = m_historyOrder.dequeue();
        m_history.remove(old);
    }
}

} // namespace game
