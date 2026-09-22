#include "MovePathfinding.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

MovePathQuery normalizedQuery(const MovePathQuery& source)
{
    MovePathQuery q=source;
    q.stepHU=qBound(1,q.stepHU,2);
    q.maxExpandedNodes=qBound(1,q.maxExpandedNodes,65536);
    q.minDistanceTiles=qMax(0,q.minDistanceTiles);
    q.maxDistanceTiles=qMax(0,q.maxDistanceTiles);
    if(q.behavior==core::MoveRoutePathBehavior::KeepDistance&&q.maxDistanceTiles<q.minDistanceTiles)
        q.maxDistanceTiles=q.minDistanceTiles;
    return q;
}

int heuristic(const MovePathQuery& q,const QPoint& p)
{
    if(q.behavior==core::MoveRoutePathBehavior::Flee)return 0;
    if(q.behavior==core::MoveRoutePathBehavior::KeepDistance &&
       movePathDistanceHU(p,q.targetHU,q.diagonal)<q.minDistanceTiles*2)return 0;
    const int d=movePathDistanceHU(p,q.targetHU,q.diagonal);
    const int tolerance=qMax(0,q.maxDistanceTiles)*2;
    return qMax(0,d-tolerance)*10/qMax(1,q.stepHU);
}

QVector<QPoint> reconstruct(const QHash<QPoint,QPoint>& parent,QPoint current,const QPoint& start)
{
    QVector<QPoint> reverse;
    while(current!=start){
        reverse.push_back(current);
        const auto it=parent.constFind(current);
        if(it==parent.constEnd())return {};
        current=it.value();
    }
    std::reverse(reverse.begin(),reverse.end());
    return reverse;
}

MovePathResult stateResult(const MovePathSearchState& state,int expandedThisStep)
{
    MovePathResult out;
    out.status=state.status;
    out.expandedNodes=state.expandedNodes;
    out.expandedThisStep=qMax(0,expandedThisStep);
    out.reason=state.reason;
    if(state.status==MovePathStatus::PathFound)
        out.nodesHU=reconstruct(state.parent,state.goalHU,state.startHU);
    return out;
}

void beginSearch(MovePathSearchState& state,const MovePathQuery& source)
{
    state.reset();
    if(source.stepHU<=0||!source.canMoveFrom||source.maxExpandedNodes<1){
        state.initialized=true;state.finished=true;state.status=MovePathStatus::Invalid;
        state.reason=QStringLiteral("invalid-query");return;
    }

    const MovePathQuery q=normalizedQuery(source);
    state.startHU=q.startHU;state.targetHU=q.targetHU;state.stepHU=q.stepHU;
    state.behavior=q.behavior;state.minDistanceTiles=q.minDistanceTiles;
    state.maxDistanceTiles=q.maxDistanceTiles;state.diagonal=q.diagonal;
    state.maxExpandedNodes=q.maxExpandedNodes;state.initialized=true;

    if(movePathGoalSatisfied(q,q.startHU)){
        state.finished=true;state.status=MovePathStatus::Reached;
        state.reason=QStringLiteral("goal-satisfied");return;
    }

    // Ordem estável: cardeais antes de diagonais. Em empates, o serial mantém
    // resultados determinísticos entre builds/plataformas e entre ticks.
    state.dirs={QPoint(0,1),QPoint(-1,0),QPoint(1,0),QPoint(0,-1)};
    if(q.diagonal)state.dirs += QVector<QPoint>{QPoint(-1,1),QPoint(1,1),QPoint(-1,-1),QPoint(1,-1)};

    state.bestG.insert(q.startHU,0);
    state.open.push(MovePathOpenNode{q.startHU,0,heuristic(q,q.startHU),state.serial++});
    state.status=MovePathStatus::Searching;
    state.reason=QStringLiteral("searching");
}

MovePathQuery queryForState(const MovePathSearchState& state,const MovePathQuery& source)
{
    MovePathQuery q=source;
    q.startHU=state.startHU;q.targetHU=state.targetHU;q.stepHU=state.stepHU;
    q.behavior=state.behavior;q.minDistanceTiles=state.minDistanceTiles;
    q.maxDistanceTiles=state.maxDistanceTiles;q.diagonal=state.diagonal;
    q.maxExpandedNodes=state.maxExpandedNodes;
    return q;
}

} // namespace

void MovePathSearchState::reset()
{
    startHU=QPoint();targetHU=QPoint();stepHU=2;
    behavior=core::MoveRoutePathBehavior::Reach;minDistanceTiles=0;maxDistanceTiles=0;
    diagonal=true;maxExpandedNodes=4096;
    open=decltype(open){};bestG.clear();parent.clear();dirs.clear();serial=0;expandedNodes=0;
    status=MovePathStatus::Invalid;reason.clear();goalHU=QPoint();initialized=false;finished=false;
}

QString movePathStatusId(MovePathStatus status)
{
    switch(status){
    case MovePathStatus::Searching:return QStringLiteral("searching");
    case MovePathStatus::Reached:return QStringLiteral("reached");
    case MovePathStatus::PathFound:return QStringLiteral("pathFound");
    case MovePathStatus::NoPath:return QStringLiteral("noPath");
    case MovePathStatus::SearchLimit:return QStringLiteral("searchLimit");
    case MovePathStatus::Invalid:break;
    }
    return QStringLiteral("invalid");
}

int movePathDistanceHU(const QPoint& a,const QPoint& b,bool diagonal)
{
    const int dx=qAbs(a.x()-b.x()),dy=qAbs(a.y()-b.y());
    return diagonal?qMax(dx,dy):dx+dy;
}

bool movePathGoalSatisfied(const MovePathQuery& q,const QPoint& positionHU)
{
    const int distance=movePathDistanceHU(positionHU,q.targetHU,q.diagonal);
    const int minHU=qMax(0,q.minDistanceTiles)*2;
    const int maxHU=qMax(0,q.maxDistanceTiles)*2;
    switch(q.behavior){
    case core::MoveRoutePathBehavior::Flee:
        return distance>=minHU;
    case core::MoveRoutePathBehavior::KeepDistance:
        return distance>=minHU&&distance<=qMax(minHU,maxHU);
    case core::MoveRoutePathBehavior::Follow:
    case core::MoveRoutePathBehavior::Reach:
        return distance<=maxHU;
    }
    return false;
}

bool movePathSearchMatches(const MovePathSearchState& state,const MovePathQuery& source)
{
    if(!state.initialized)return false;
    if(source.stepHU<=0||source.maxExpandedNodes<1)return false;
    const MovePathQuery q=normalizedQuery(source);
    return state.startHU==q.startHU && state.targetHU==q.targetHU && state.stepHU==q.stepHU &&
           state.behavior==q.behavior && state.minDistanceTiles==q.minDistanceTiles &&
           state.maxDistanceTiles==q.maxDistanceTiles && state.diagonal==q.diagonal &&
           state.maxExpandedNodes==q.maxExpandedNodes;
}

MovePathResult advanceMovePathSearch(MovePathSearchState& state,const MovePathQuery& source,int expansionBudget)
{
    if(!movePathSearchMatches(state,source))beginSearch(state,source);
    if(state.finished)return stateResult(state,0);
    if(!source.canMoveFrom){
        state.finished=true;state.status=MovePathStatus::Invalid;state.reason=QStringLiteral("invalid-query");
        return stateResult(state,0);
    }

    const int budget=qMax(0,expansionBudget);
    if(budget==0)return stateResult(state,0);
    const MovePathQuery q=queryForState(state,source);
    int expandedThisStep=0;

    while(!state.open.empty() && expandedThisStep<budget){
        const MovePathOpenNode current=state.open.top();state.open.pop();
        if(state.bestG.value(current.pos,std::numeric_limits<int>::max())!=current.g)continue;

        ++state.expandedNodes;++expandedThisStep;
        // Preserva o contrato histórico: SearchLimit acontece ao tentar
        // expandir o primeiro nó além de maxExpandedNodes.
        if(state.expandedNodes>state.maxExpandedNodes){
            state.finished=true;state.status=MovePathStatus::SearchLimit;
            state.reason=QStringLiteral("search-limit");return stateResult(state,expandedThisStep);
        }
        if(current.pos!=state.startHU&&movePathGoalSatisfied(q,current.pos)){
            state.goalHU=current.pos;
            const QVector<QPoint> nodes=reconstruct(state.parent,current.pos,state.startHU);
            if(nodes.isEmpty()){
                state.finished=true;state.status=MovePathStatus::Invalid;
                state.reason=QStringLiteral("broken-parent-chain");return stateResult(state,expandedThisStep);
            }
            state.finished=true;state.status=MovePathStatus::PathFound;
            state.reason=QStringLiteral("path-found");return stateResult(state,expandedThisStep);
        }
        for(const QPoint& d:state.dirs){
            if(!source.canMoveFrom(current.pos,d.x(),d.y()))continue;
            const QPoint next=current.pos+QPoint(d.x()*state.stepHU,d.y()*state.stepHU);
            const bool diag=d.x()!=0&&d.y()!=0;
            const int ng=current.g+(diag?14:10);
            if(ng>=state.bestG.value(next,std::numeric_limits<int>::max()))continue;
            state.bestG.insert(next,ng);state.parent.insert(next,current.pos);
            state.open.push(MovePathOpenNode{next,ng,ng+heuristic(q,next),state.serial++});
        }
    }

    if(state.open.empty()){
        state.finished=true;state.status=MovePathStatus::NoPath;state.reason=QStringLiteral("no-path");
    }else{
        state.status=MovePathStatus::Searching;state.reason=QStringLiteral("searching");
    }
    return stateResult(state,expandedThisStep);
}

MovePathResult planMovePath(const MovePathQuery& query)
{
    MovePathSearchState state;
    // +1 mantém exatamente o limite histórico (a falha ocorre ao nó N+1).
    const int synchronousBudget=qBound(1,query.maxExpandedNodes,65536)+1;
    MovePathResult out=advanceMovePathSearch(state,query,synchronousBudget);
    while(out.searching())out=advanceMovePathSearch(state,query,synchronousBudget);
    return out;
}

} // namespace game
