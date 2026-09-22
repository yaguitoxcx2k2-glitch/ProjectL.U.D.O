// ============================================================================
// MoveRouteExecutor.h — executor único de comandos de rota para qualquer ator.
//
// RC2.20 / Rota A: jogador e eventos fornecem apenas operações do ator; a
// semântica dos comandos fica centralizada aqui para impedir divergências.
// ============================================================================
#pragma once

#include "MoveRouteRuntime.h"
#include "core/EventModel.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace game {

enum class MoveRouteMoveResult { Started, Blocked, NoOp };

struct MoveRouteActorOps {
    std::function<MoveRouteMoveResult(int,int,bool)> move;
    std::function<MoveRouteMoveResult(int,int)> jump;
    /// Delta até o alvo de referência. nullopt = referência inexistente;
    /// QPoint(0,0) = já está sobre o alvo.
    std::function<std::optional<QPoint>()> referenceDelta;

    // Pathfinding estrutural. Coordenadas em meia-célula (HU) mantêm suporte
    // a movimento de 0,5 tile sem criar um segundo algoritmo.
    std::function<QPoint()> pathPositionHU;
    std::function<int()> pathStepHU;
    std::function<bool(const QPoint&,int,int)> pathCanMoveFrom;
    std::function<std::optional<QPoint>(const core::MoveRoutePathOptions&,const QString&)> resolvePathTargetHU;

    std::function<int()> direction;
    std::function<void(int)> setDirection;
    std::function<bool()> directionFixed;

    std::function<void(bool)> setWalkingAnimation;
    std::function<void(bool)> setSteppingAnimation;
    std::function<void(bool)> setDirectionFixed;
    std::function<void(bool)> setThrough;
    std::function<void(bool)> setTransparent;
    std::function<void(double)> setSpeed;
    std::function<void(int)> setFrequency;
    std::function<void(int)> setOpacity;
    std::function<void(const QString&)> setBlend;
    std::function<void(int,bool)> setSwitch;
    std::function<void(const QString&,int)> playSound;
    std::function<void(const core::EventGraphic&)> changeGraphic;
    std::function<void(double,double,double)> startShake;
    std::function<void()> rememberPosition;

    std::function<double()> commandCooldown;
    std::function<double()> blockedRetryDelay;
};

class MoveRouteExecutor
{
public:
    /// Executa no máximo um comando. A máquina de estado decide quando o ator
    /// está pronto; comandos instantâneos avançam o cursor, movimentos marcam
    /// espera física, e falhas seguem a política da própria rota.
    static void executeOne(MoveRouteRuntime& runtime, const MoveRouteActorOps& actor);

    /// Usado pelos testes/auditoria para garantir que a lista canônica do core
    /// e o executor evoluem juntos.
    static QStringList supportedTypes();
    static bool supports(const QString& type);
};

} // namespace game
