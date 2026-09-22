// ============================================================================
// MovePathfinding.h — planejador determinístico compartilhado das rotas.
//
// RC2.21 / Rota B: A* único para Jogador/Eventos. O planejador não conhece
// sprites nem editor; recebe apenas coordenadas em meia-célula e um callback
// puro de passabilidade. Isso evita duplicação de pathfinding por ator.
//
// RC2.28 / O6: a mesma busca pode ser amortizada em vários ticks por um
// orçamento fixo de nós expandidos. O estado é efêmero e nunca é serializado.
// ============================================================================
#pragma once

#include "core/EventModel.h"

#include <QHash>
#include <QPoint>
#include <QVector>
#include <QString>
#include <functional>
#include <queue>
#include <vector>

namespace game {

enum class MovePathStatus { Searching, Reached, PathFound, NoPath, SearchLimit, Invalid };

struct MovePathQuery {
    QPoint startHU;
    QPoint targetHU;
    int stepHU = 2;
    core::MoveRoutePathBehavior behavior = core::MoveRoutePathBehavior::Reach;
    int minDistanceTiles = 0;
    int maxDistanceTiles = 0;
    bool diagonal = true;
    int maxExpandedNodes = 4096;
    std::function<bool(const QPoint&,int,int)> canMoveFrom;
};

struct MovePathResult {
    MovePathStatus status = MovePathStatus::Invalid;
    QVector<QPoint> nodesHU;  ///< nós absolutos, sem o start
    int expandedNodes = 0;   ///< acumulado da busca
    int expandedThisStep = 0;///< trabalho feito nesta chamada incremental
    QString reason;

    bool searching() const { return status == MovePathStatus::Searching; }
    bool reached() const { return status == MovePathStatus::Reached; }
    bool hasPath() const { return status == MovePathStatus::PathFound && !nodesHU.isEmpty(); }
};

/// Nó da heap do A*. É público apenas para que MovePathSearchState possa ser
/// armazenado por valor dentro de MoveRouteRuntime sem alocação/PIMPL extra.
struct MovePathOpenNode {
    QPoint pos;
    int g = 0;
    int f = 0;
    quint64 serial = 0;
};

struct MovePathOpenCompare {
    bool operator()(const MovePathOpenNode& a,const MovePathOpenNode& b) const
    {
        if(a.f!=b.f)return a.f>b.f;
        if(a.g!=b.g)return a.g>b.g;
        return a.serial>b.serial;
    }
};

/// Estado efêmero de uma busca incremental. O callback de colisão não é
/// armazenado aqui: a cada tick o executor fornece o callback atual do World.
/// Isso evita manter referências capturadas de um frame anterior.
struct MovePathSearchState {
    static constexpr int DefaultExpansionBudgetPerTick = 512;

    QPoint startHU;
    QPoint targetHU;
    int stepHU = 2;
    core::MoveRoutePathBehavior behavior = core::MoveRoutePathBehavior::Reach;
    int minDistanceTiles = 0;
    int maxDistanceTiles = 0;
    bool diagonal = true;
    int maxExpandedNodes = 4096;

    std::priority_queue<MovePathOpenNode,std::vector<MovePathOpenNode>,MovePathOpenCompare> open;
    QHash<QPoint,int> bestG;
    QHash<QPoint,QPoint> parent;
    QVector<QPoint> dirs;
    quint64 serial = 0;
    int expandedNodes = 0;
    MovePathStatus status = MovePathStatus::Invalid;
    QString reason;
    QPoint goalHU;
    bool initialized = false;
    bool finished = false;

    void reset();
};

QString movePathStatusId(MovePathStatus status);
int movePathDistanceHU(const QPoint& a, const QPoint& b, bool diagonal);
bool movePathGoalSatisfied(const MovePathQuery& query, const QPoint& positionHU);

/// Retorna true se o estado incremental ainda representa exatamente a mesma
/// busca lógica. Mudança de origem/alvo/opções reinicia a busca determinística.
bool movePathSearchMatches(const MovePathSearchState& state,const MovePathQuery& query);

/// Avança no máximo expansionBudget nós válidos. Retorna Searching se ainda
/// houver trabalho. O limite total maxExpandedNodes continua soberano.
MovePathResult advanceMovePathSearch(MovePathSearchState& state,const MovePathQuery& query,
                                     int expansionBudget = MovePathSearchState::DefaultExpansionBudgetPerTick);

/// Compatibilidade/oráculo síncrono: usa exatamente a mesma máquina incremental
/// até obter um resultado terminal. Testes existentes continuam podendo comparar
/// o resultado determinístico sem depender de ticks.
MovePathResult planMovePath(const MovePathQuery& query);

} // namespace game
