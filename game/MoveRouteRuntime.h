// ============================================================================
// MoveRouteRuntime.h — estado explícito e persistível das rotas de movimento.
//
// RC2.20 / Rota A: separa definição (core::MoveRoute) de execução. Jogador e
// eventos passam a compartilhar a mesma máquina de estado para fila, pausa,
// cancelamento, bloqueio, espera e tickets de conclusão.
// ============================================================================
#pragma once

#include "core/EventModel.h"
#include "game/MovePathfinding.h"

#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QString>
#include <QVector>
#include <optional>

namespace game {

using MoveRouteTicket = quint64;

enum class MoveRouteTicketState {
    Unknown,
    Pending,
    Running,
    Paused,
    Completed,
    Cancelled,
    Failed
};

QString moveRouteTicketStateId(MoveRouteTicketState state);

enum class MoveRouteControlAction { Invalid, Pause, Resume, Cancel, ClearQueue };
MoveRouteControlAction moveRouteControlActionFromId(const QString& id);
QString moveRouteControlActionId(MoveRouteControlAction action);

/// Uma solicitação de rota. sourceEventId é necessário para comandos relativos
/// quando o alvo é o jogador (aproximar/afastar/olhar para o evento chamador).
struct MoveRouteRequest {
    MoveRouteTicket ticket = 0;
    core::MoveRoute route;
    QString sourceEventId;
};

/// Cache efêmero de pathfinding. Não é serializado: após Save/Load o caminho é
/// recalculado da posição restaurada, evitando consumir um passo que estava no
/// meio da animação quando o save foi feito.
struct MoveRoutePathCache {
    int commandIndex = -1;
    QPoint targetHU;
    QVector<QPoint> nodesHU;
    int replans = 0;
    int expandedNodes = 0;
    QString status;
};

/// Máquina de estado independente de qualquer sprite/ator.
///
/// Invariantes:
/// - no máximo uma rota atual;
/// - fila nunca contém ticket 0;
/// - commandIndex sempre aponta para um comando válido enquanto current existe;
/// - espera/cooldown/movimento são estados explícitos, não inferidos pelo ator;
/// - tickets terminais ficam em histórico limitado para o Interpreter não
///   entrar em deadlock quando uma rota foi substituída/cancelada.
class MoveRouteRuntime
{
public:
    static constexpr int RuntimePayloadVersion = 1;

    bool start(const MoveRouteRequest& request, core::MoveRouteStartMode mode);
    void startAutonomous(const core::MoveRoute& route);

    bool busy() const { return m_current.has_value() || !m_queue.isEmpty(); }
    bool hasCurrent() const { return m_current.has_value(); }
    bool paused() const { return m_paused && m_current.has_value(); }
    bool ready() const;
    bool awaitingMovement() const { return m_current.has_value() && m_awaitingMovement; }

    const core::MoveRoute* route() const
    { return m_current ? &m_current->route : nullptr; }
    const core::MoveCommand* currentCommand() const;
    QString sourceEventId() const { return m_current ? m_current->sourceEventId : QString(); }
    MoveRouteTicket currentTicket() const { return m_current ? m_current->ticket : 0; }
    int commandIndex() const { return m_commandIndex; }
    int queuedCount() const { return m_queue.size(); }
    double waitSeconds() const { return m_waitSeconds; }
    double cooldownSeconds() const { return m_cooldownSeconds; }
    int blockedAttempts() const { return m_blockedAttempts; }
    int pathExpandedThisTick() const { return m_pathExpandedThisTick; }
    QString lastFailure() const { return m_lastFailure; }

    void update(double dt);
    void advance(double cooldownSeconds = 0.0);
    void deferAdvance(double waitSeconds, double cooldownAfterWait = 0.0);
    void markMovementStarted(bool advanceCommandWhenFinished = true);
    void notifyMovementFinished(double cooldownSeconds = 0.0);
    void deferRetry(double delaySeconds);
    void handleBlocked(double retryDelaySeconds, double skipCooldownSeconds = 0.0,
                       const QString& reason = QStringLiteral("blocked"));
    void fail(const QString& reason);

    const MoveRoutePathCache& pathCache() const { return m_pathCache; }
    MovePathSearchState& pathSearchState() { return m_pathSearch; }
    const MovePathSearchState& pathSearchState() const { return m_pathSearch; }
    void setPathPlan(const QPoint& targetHU, const QVector<QPoint>& nodesHU,
                     int expandedNodes, const QString& status);
    void beginPathSearch(const QPoint& targetHU);
    void updatePathSearchProgress(int expandedNodes, int expandedThisTick, const QString& status);
    void consumePathNode();
    void clearPathPlan(const QString& status = QString());

    bool pause();
    bool resume();
    bool cancelCurrent(bool startNext = true);
    void clearQueue();
    /// Cancela tickets ativos, preservando histórico terminal para waiters.
    void cancelAll();
    /// Nova linha do tempo (load/reset estrutural): limpa também histórico/falha.
    void reset();

    MoveRouteTicketState ticketState(MoveRouteTicket ticket) const;

    QJsonObject toJson() const;
    /// `allowAutonomousTicket` só deve ser true para a rota Custom da página.
    /// Player/rotas forçadas rejeitam ticket 0 para não criar um runtime sem
    /// identidade que nunca possa ser acompanhado pelo Interpreter.
    void fromJson(const QJsonObject& object, bool allowAutonomousTicket = false);
    /// Resumo leve para diagnóstico/debug; não duplica a lista inteira de comandos.
    QJsonObject diagnostics() const;
    MoveRouteTicket highestTicket() const;

private:
    void normalizeCurrent();
    void activateNext();
    void finishCurrent(MoveRouteTicketState terminalState, const QString& reason = QString(),
                       bool startNext = true);
    void remember(MoveRouteTicket ticket, MoveRouteTicketState state);
    static QJsonObject requestToJson(const MoveRouteRequest& request);
    static MoveRouteRequest requestFromJson(const QJsonObject& object);

    std::optional<MoveRouteRequest> m_current;
    QQueue<MoveRouteRequest> m_queue;
    int m_commandIndex = 0;
    double m_waitSeconds = 0.0;
    double m_cooldownSeconds = 0.0;
    double m_deferredCooldown = 0.0;
    bool m_advanceAfterWait = false;
    bool m_awaitingMovement = false;
    bool m_advanceAfterMovement = true;
    bool m_paused = false;
    int m_blockedAttempts = 0;
    MoveRoutePathCache m_pathCache;
    MovePathSearchState m_pathSearch;
    int m_pathExpandedThisTick = 0;
    QString m_lastFailure;
    QHash<MoveRouteTicket, MoveRouteTicketState> m_history;
    QQueue<MoveRouteTicket> m_historyOrder;
};

} // namespace game
