#include "MoveRouteExecutor.h"
#include "MovePathfinding.h"

#include "core/ProjectIO.h"

#include <QRandomGenerator>
#include <QSet>
#include <QtGlobal>

namespace game {
namespace {

int directionFromDelta(int dx, int dy)
{
    dx = qBound(-1, dx, 1); dy = qBound(-1, dy, 1);
    if (dx < 0 && dy < 0) return 6;
    if (dx > 0 && dy < 0) return 7;
    if (dx < 0 && dy > 0) return 4;
    if (dx > 0 && dy > 0) return 5;
    if (dx < 0) return 1;
    if (dx > 0) return 2;
    if (dy < 0) return 3;
    return 0;
}

QPoint deltaFromDirection(int direction)
{
    switch (qBound(0, direction, 7)) {
    case 0: return QPoint(0,1);
    case 1: return QPoint(-1,0);
    case 2: return QPoint(1,0);
    case 3: return QPoint(0,-1);
    case 4: return QPoint(-1,1);
    case 5: return QPoint(1,1);
    case 6: return QPoint(-1,-1);
    case 7: return QPoint(1,-1);
    }
    return QPoint(0,1);
}

int right90(int d)
{
    static const int values[] = {1,3,0,2,6,4,7,5};
    return values[qBound(0,d,7)];
}
int left90(int d)
{
    static const int values[] = {2,0,3,1,5,7,4,6};
    return values[qBound(0,d,7)];
}
int opposite(int d)
{
    static const int values[] = {3,2,1,0,7,6,5,4};
    return values[qBound(0,d,7)];
}

double cooldown(const MoveRouteActorOps& actor)
{
    return actor.commandCooldown ? qMax(0.0, actor.commandCooldown()) : 0.0;
}

double retryDelay(const MoveRouteActorOps& actor)
{
    return actor.blockedRetryDelay ? qMax(0.0, actor.blockedRetryDelay()) : cooldown(actor);
}

void movementResult(MoveRouteRuntime& runtime, MoveRouteMoveResult result,
                    const MoveRouteActorOps& actor)
{
    if (result == MoveRouteMoveResult::Started) runtime.markMovementStarted();
    else if (result == MoveRouteMoveResult::NoOp) runtime.advance(cooldown(actor));
    else runtime.handleBlocked(retryDelay(actor), cooldown(actor));
}

std::optional<QPoint> cardinalReference(const MoveRouteActorOps& actor)
{
    if (!actor.referenceDelta) return std::nullopt;
    const std::optional<QPoint> value = actor.referenceDelta();
    if (!value) return std::nullopt;
    const QPoint delta = *value;
    if (delta.isNull()) return QPoint();
    return qAbs(delta.x()) > qAbs(delta.y())
        ? QPoint(delta.x() < 0 ? -1 : 1, 0)
        : QPoint(0, delta.y() < 0 ? -1 : 1);
}

core::EventGraphic graphicFromCommand(const core::MoveCommand& command)
{
    const QVariantMap& p = command.params;
    core::EventGraphic g;
    g.kind = core::EventGraphic::Charset;
    g.charset = core::io::dataUriToImage(p.value(QStringLiteral("image")).toString());
    g.sourcePath = p.value(QStringLiteral("source")).toString();
    g.charsetCols = qMax(1, p.value(QStringLiteral("cols"),3).toInt());
    g.charsetRows = qMax(1, p.value(QStringLiteral("rows"),4).toInt());
    g.characterCols = qMax(1, p.value(QStringLiteral("characterCols"),1).toInt());
    g.characterRows = qMax(1, p.value(QStringLiteral("characterRows"),1).toInt());
    g.characterIndex = qMax(0, p.value(QStringLiteral("characterIndex")).toInt());
    g.dir = qBound(0, p.value(QStringLiteral("dir")).toInt(), 7);
    g.frame = qMax(0, p.value(QStringLiteral("frame")).toInt());
    return g;
}

} // namespace

QStringList MoveRouteExecutor::supportedTypes()
{
    // Lista deliberadamente independente da lista canônica do core. O teste de
    // contrato compara ambas: se alguém adicionar um botão/comando no modelo e
    // esquecer o executor (ou o contrário), o gate RC falha antes do release.
    return {
        QStringLiteral("moveDown"), QStringLiteral("moveLeft"), QStringLiteral("moveRight"), QStringLiteral("moveUp"),
        QStringLiteral("moveDownLeft"), QStringLiteral("moveDownRight"), QStringLiteral("moveUpLeft"), QStringLiteral("moveUpRight"),
        QStringLiteral("moveRandom"), QStringLiteral("moveTowardPlayer"), QStringLiteral("moveAwayPlayer"),
        QStringLiteral("pathfind"),
        QStringLiteral("stepForward"), QStringLiteral("stepBackward"), QStringLiteral("jump"),
        QStringLiteral("turnDown"), QStringLiteral("turnLeft"), QStringLiteral("turnRight"), QStringLiteral("turnUp"),
        QStringLiteral("turnRight90"), QStringLiteral("turnLeft90"), QStringLiteral("turn180"), QStringLiteral("turn90Random"),
        QStringLiteral("turnRandom"), QStringLiteral("turnTowardPlayer"), QStringLiteral("turnAwayPlayer"),
        QStringLiteral("wait"), QStringLiteral("speed"), QStringLiteral("frequency"),
        QStringLiteral("walkAnimOn"), QStringLiteral("walkAnimOff"), QStringLiteral("stepAnimOn"), QStringLiteral("stepAnimOff"),
        QStringLiteral("dirFixOn"), QStringLiteral("dirFixOff"), QStringLiteral("throughOn"), QStringLiteral("throughOff"),
        QStringLiteral("transparentOn"), QStringLiteral("transparentOff"), QStringLiteral("opacity"), QStringLiteral("blend"),
        QStringLiteral("switchOn"), QStringLiteral("switchOff"), QStringLiteral("playSE"),
        QStringLiteral("changeGraphic"), QStringLiteral("shake"), QStringLiteral("rememberPosition")
    };
}

bool MoveRouteExecutor::supports(const QString& type)
{
    static const QSet<QString> supported=[] {
        QSet<QString> result;
        for(const QString& value:MoveRouteExecutor::supportedTypes())result.insert(value);
        return result;
    }();
    return supported.contains(type);
}

void MoveRouteExecutor::executeOne(MoveRouteRuntime& runtime, const MoveRouteActorOps& actor)
{
    if (!runtime.ready()) return;
    const core::MoveCommand* command = runtime.currentCommand();
    if (!command) { runtime.fail(QStringLiteral("invalid-command-index")); return; }
    const QString& t = command->type;
    const QVariantMap& p = command->params;
    if (!supports(t)) { runtime.fail(QStringLiteral("unknown-command:%1").arg(t)); return; }

    auto move = [&](int dx, int dy, bool keepDirection = false) {
        if (!actor.move) { runtime.fail(QStringLiteral("actor-move-unavailable")); return; }
        movementResult(runtime, actor.move(dx,dy,keepDirection), actor);
    };
    auto turn = [&](int direction) {
        if (!actor.setDirection || !actor.directionFixed) { runtime.fail(QStringLiteral("actor-direction-unavailable")); return; }
        if (!actor.directionFixed()) actor.setDirection(qBound(0,direction,7));
        runtime.advance(cooldown(actor));
    };
    auto reference = [&]() -> std::optional<QPoint> { return cardinalReference(actor); };

    if (t == QLatin1String("moveDown")) move(0,1);
    else if (t == QLatin1String("moveLeft")) move(-1,0);
    else if (t == QLatin1String("moveRight")) move(1,0);
    else if (t == QLatin1String("moveUp")) move(0,-1);
    else if (t == QLatin1String("moveDownLeft")) move(-1,1);
    else if (t == QLatin1String("moveDownRight")) move(1,1);
    else if (t == QLatin1String("moveUpLeft")) move(-1,-1);
    else if (t == QLatin1String("moveUpRight")) move(1,-1);
    else if (t == QLatin1String("moveRandom")) {
        const QPoint d = deltaFromDirection(QRandomGenerator::global()->bounded(4)); move(d.x(),d.y());
    }
    else if (t == QLatin1String("moveTowardPlayer") || t == QLatin1String("moveAwayPlayer")) {
        const auto d = reference();
        if (!d) { runtime.fail(QStringLiteral("reference-target-missing")); return; }
        QPoint delta = *d;
        if (t == QLatin1String("moveAwayPlayer")) delta = -delta;
        if (delta.isNull()) runtime.advance(cooldown(actor)); else move(delta.x(),delta.y());
    }
    else if (t == QLatin1String("pathfind")) {
        if(!actor.pathPositionHU||!actor.pathStepHU||!actor.pathCanMoveFrom||!actor.resolvePathTargetHU){
            runtime.fail(QStringLiteral("actor-pathfinding-unavailable"));return;
        }
        const core::MoveRoutePathOptions options=core::moveRoutePathOptionsFromCommand(*command);
        const QPoint current=actor.pathPositionHU();
        const int stepHU=qBound(1,actor.pathStepHU(),2);
        const std::optional<QPoint> target=actor.resolvePathTargetHU(options,runtime.sourceEventId());
        if(!target){runtime.fail(QStringLiteral("path-target-missing"));return;}

        MovePathQuery query;
        query.startHU=current;query.targetHU=*target;query.stepHU=stepHU;
        query.behavior=options.behavior;query.minDistanceTiles=options.minDistance;
        query.maxDistanceTiles=options.maxDistance;query.diagonal=options.diagonal;
        query.maxExpandedNodes=options.maxSearchNodes;query.canMoveFrom=actor.pathCanMoveFrom;

        if(movePathGoalSatisfied(query,current)){
            runtime.clearPathPlan(QStringLiteral("goal-satisfied"));
            if(options.continuous)runtime.deferRetry(qMax(0.04,cooldown(actor)));
            else runtime.advance(cooldown(actor));
            return;
        }

        const MoveRoutePathCache& cache=runtime.pathCache();
        bool needPlan=cache.commandIndex!=runtime.commandIndex()||cache.targetHU!=*target||cache.nodesHU.isEmpty();
        if(!needPlan){
            const QPoint next=cache.nodesHU.first();
            const QPoint delta=next-current;
            needPlan = (delta.x()%stepHU)!=0 || (delta.y()%stepHU)!=0 ||
                       qAbs(delta.x()/stepHU)>1 || qAbs(delta.y()/stepHU)>1 ||
                       (delta.isNull());
        }
        if(needPlan){
            MovePathSearchState& search=runtime.pathSearchState();
            if(!movePathSearchMatches(search,query)){
                search.reset();
                runtime.beginPathSearch(*target);
            }
            const MovePathResult plan=advanceMovePathSearch(
                search,query,MovePathSearchState::DefaultExpansionBudgetPerTick);
            if(plan.searching()){
                runtime.updatePathSearchProgress(plan.expandedNodes,plan.expandedThisStep,movePathStatusId(plan.status));
                // O comando permanece no mesmo índice e o ator fica parado. No
                // próximo tick a heap/bestG/parents continuam exatamente de
                // onde pararam, sem depender de relógio ou velocidade da CPU.
                return;
            }
            runtime.setPathPlan(*target,plan.nodesHU,plan.expandedNodes,movePathStatusId(plan.status));
            runtime.updatePathSearchProgress(plan.expandedNodes,plan.expandedThisStep,movePathStatusId(plan.status));
            search.reset();
            if(plan.reached()){
                if(options.continuous)runtime.deferRetry(qMax(0.04,cooldown(actor)));
                else runtime.advance(cooldown(actor));
                return;
            }
            if(!plan.hasPath()){
                // SearchLimit continua terminal mesmo com política Wait: agora o
                // custo até chegar ao limite é repartido em ticks, mas o teto
                // total maxSearchNodes mantém o mesmo contrato de segurança.
                if(plan.status==MovePathStatus::SearchLimit){runtime.fail(QStringLiteral("path-search-limit"));return;}
                if(plan.status==MovePathStatus::Invalid){runtime.fail(QStringLiteral("path-invalid-plan"));return;}
                runtime.handleBlocked(retryDelay(actor),cooldown(actor),QStringLiteral("path-not-found"));
                return;
            }
        }

        const MoveRoutePathCache& planned=runtime.pathCache();
        if(planned.nodesHU.isEmpty()){runtime.handleBlocked(retryDelay(actor),cooldown(actor),QStringLiteral("path-empty"));return;}
        const QPoint next=planned.nodesHU.first();
        const QPoint huDelta=next-current;
        if((huDelta.x()%stepHU)!=0||(huDelta.y()%stepHU)!=0){
            runtime.clearPathPlan(QStringLiteral("path-grid-mismatch"));
            runtime.handleBlocked(retryDelay(actor),cooldown(actor),QStringLiteral("path-grid-mismatch"));return;
        }
        const int dx=huDelta.x()/stepHU,dy=huDelta.y()/stepHU;
        if(qAbs(dx)>1||qAbs(dy)>1||(dx==0&&dy==0)){
            runtime.clearPathPlan(QStringLiteral("path-step-invalid"));
            runtime.handleBlocked(retryDelay(actor),cooldown(actor),QStringLiteral("path-step-invalid"));return;
        }
        if(!actor.move){runtime.fail(QStringLiteral("actor-move-unavailable"));return;}
        const MoveRouteMoveResult result=actor.move(dx,dy,false);
        if(result==MoveRouteMoveResult::Started){
            runtime.consumePathNode();
            // Um passo do A* não conclui o comando pathfind; após terminar o
            // movimento o mesmo comando reavalia alvo/obstáculos e continua.
            runtime.markMovementStarted(false);
        }else if(result==MoveRouteMoveResult::NoOp){
            runtime.clearPathPlan(QStringLiteral("path-noop"));
            runtime.deferRetry(qMax(0.04,cooldown(actor)));
        }else{
            runtime.clearPathPlan(QStringLiteral("path-step-blocked"));
            runtime.handleBlocked(retryDelay(actor),cooldown(actor),QStringLiteral("path-step-blocked"));
        }
    }
    else if (t == QLatin1String("stepForward") || t == QLatin1String("stepBackward")) {
        if (!actor.direction) { runtime.fail(QStringLiteral("actor-direction-unavailable")); return; }
        QPoint d = deltaFromDirection(actor.direction());
        if (t == QLatin1String("stepBackward")) d = -d;
        move(d.x(),d.y(),true);
    }
    else if (t == QLatin1String("jump")) {
        if (!actor.jump) { runtime.fail(QStringLiteral("actor-jump-unavailable")); return; }
        const int x=p.value(QStringLiteral("x")).toInt();
        const int y=p.value(QStringLiteral("y")).toInt();
        if(x==0&&y==0)runtime.advance(cooldown(actor));
        else movementResult(runtime, actor.jump(x,y), actor);
    }
    else if (t == QLatin1String("turnDown")) turn(0);
    else if (t == QLatin1String("turnLeft")) turn(1);
    else if (t == QLatin1String("turnRight")) turn(2);
    else if (t == QLatin1String("turnUp")) turn(3);
    else if (t == QLatin1String("turnRight90")) { if (!actor.direction) runtime.fail(QStringLiteral("actor-direction-unavailable")); else turn(right90(actor.direction())); }
    else if (t == QLatin1String("turnLeft90")) { if (!actor.direction) runtime.fail(QStringLiteral("actor-direction-unavailable")); else turn(left90(actor.direction())); }
    else if (t == QLatin1String("turn180")) { if (!actor.direction) runtime.fail(QStringLiteral("actor-direction-unavailable")); else turn(opposite(actor.direction())); }
    else if (t == QLatin1String("turn90Random")) { if (!actor.direction) runtime.fail(QStringLiteral("actor-direction-unavailable")); else turn(QRandomGenerator::global()->bounded(2) ? right90(actor.direction()) : left90(actor.direction())); }
    else if (t == QLatin1String("turnRandom")) turn(QRandomGenerator::global()->bounded(4));
    else if (t == QLatin1String("turnTowardPlayer") || t == QLatin1String("turnAwayPlayer")) {
        const auto d = reference();
        if (!d) { runtime.fail(QStringLiteral("reference-target-missing")); return; }
        QPoint delta = *d;
        if (t == QLatin1String("turnAwayPlayer")) delta = -delta;
        if (delta.isNull()) runtime.advance(cooldown(actor)); else turn(directionFromDelta(delta.x(),delta.y()));
    }
    else if (t == QLatin1String("wait")) {
        runtime.deferAdvance(qMax(1, p.value(QStringLiteral("frames"),30).toInt()) / 60.0, cooldown(actor));
    }
    else if (t == QLatin1String("speed")) { if(!actor.setSpeed){runtime.fail(QStringLiteral("actor-speed-unavailable"));return;} actor.setSpeed(qBound(.25,p.value(QStringLiteral("value"),3).toDouble(),20.0)); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("frequency")) { if(!actor.setFrequency){runtime.fail(QStringLiteral("actor-frequency-unavailable"));return;} actor.setFrequency(qBound(1,p.value(QStringLiteral("value"),3).toInt(),5)); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("walkAnimOn") || t == QLatin1String("walkAnimOff")) { if(!actor.setWalkingAnimation){runtime.fail(QStringLiteral("actor-walk-animation-unavailable"));return;} actor.setWalkingAnimation(t == QLatin1String("walkAnimOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("stepAnimOn") || t == QLatin1String("stepAnimOff")) { if(!actor.setSteppingAnimation){runtime.fail(QStringLiteral("actor-step-animation-unavailable"));return;} actor.setSteppingAnimation(t == QLatin1String("stepAnimOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("dirFixOn") || t == QLatin1String("dirFixOff")) { if(!actor.setDirectionFixed){runtime.fail(QStringLiteral("actor-direction-fix-unavailable"));return;} actor.setDirectionFixed(t == QLatin1String("dirFixOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("throughOn") || t == QLatin1String("throughOff")) { if(!actor.setThrough){runtime.fail(QStringLiteral("actor-through-unavailable"));return;} actor.setThrough(t == QLatin1String("throughOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("transparentOn") || t == QLatin1String("transparentOff")) { if(!actor.setTransparent){runtime.fail(QStringLiteral("actor-transparency-unavailable"));return;} actor.setTransparent(t == QLatin1String("transparentOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("opacity")) { if(!actor.setOpacity){runtime.fail(QStringLiteral("actor-opacity-unavailable"));return;} actor.setOpacity(qBound(0,p.value(QStringLiteral("value"),255).toInt(),255)); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("blend")) { if(!actor.setBlend){runtime.fail(QStringLiteral("actor-blend-unavailable"));return;} actor.setBlend(p.value(QStringLiteral("value"),QStringLiteral("normal")).toString()); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("switchOn") || t == QLatin1String("switchOff")) { if(!actor.setSwitch){runtime.fail(QStringLiteral("actor-switch-unavailable"));return;} actor.setSwitch(p.value(QStringLiteral("id")).toInt(), t == QLatin1String("switchOn")); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("playSE")) { if(!actor.playSound){runtime.fail(QStringLiteral("actor-sound-unavailable"));return;} actor.playSound(p.value(QStringLiteral("source")).toString(), qBound(0,p.value(QStringLiteral("volume"),90).toInt(),100)); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("changeGraphic")) { if(!actor.changeGraphic){runtime.fail(QStringLiteral("actor-graphic-unavailable"));return;} actor.changeGraphic(graphicFromCommand(*command)); runtime.advance(cooldown(actor)); }
    else if (t == QLatin1String("shake")) {
        const double seconds = p.value(QStringLiteral("unit"),QStringLiteral("frames")).toString() == QLatin1String("seconds")
            ? qMax(.01,p.value(QStringLiteral("duration"),30.0).toDouble())
            : qMax(.01,p.value(QStringLiteral("duration"),30.0).toDouble()) / 60.0;
        if(!actor.startShake){runtime.fail(QStringLiteral("actor-shake-unavailable"));return;}
        actor.startShake(qMax(0.0,p.value(QStringLiteral("x"),4.0).toDouble()),
                         qMax(0.0,p.value(QStringLiteral("y"),0.0).toDouble()), seconds);
        runtime.deferAdvance(seconds, cooldown(actor));
    } else if (t == QLatin1String("rememberPosition")) {
        if(!actor.rememberPosition){runtime.fail(QStringLiteral("remember-position-event-only"));return;}
        actor.rememberPosition(); runtime.advance(cooldown(actor));
    } else {
        // Última barreira contra um comando novo que foi listado como suportado
        // mas não ganhou semântica. Nunca deixe uma rota presa silenciosamente.
        runtime.fail(QStringLiteral("executor-command-not-implemented:%1").arg(t));
    }
}

} // namespace game
