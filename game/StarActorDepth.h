#pragma once

#include <algorithm>
#include <limits>

namespace game {

/// Profundidade de um tile com prioridade estilo formato 4×4.
///
/// Prioridade 0 nao participa desta ordenacao. Para 1..5, a borda de troca e
/// deslocada para baixo em (priority-1) alturas de tile. Isso permite montar
/// objetos altos com 4,3,2,1 de cima para baixo: todos recebem exatamente a
/// mesma profundidade na base do objeto e passam na frente/atras do ator como
/// uma unica estrutura visual.
inline double tilePriorityWorldDepth(double tileBottom, int priority, double tileHeight)
{
    const int p = std::max(1, std::min(5, priority));
    return tileBottom + double(p - 1) * std::max(0.0, tileHeight);
}

/// Compatibilidade com o antigo ★ booleano. ★ antigo == prioridade 1.
inline double starWorldDepth(double tileBottom)
{
    return tilePriorityWorldDepth(tileBottom, 1, 0.0);
}

/// Profundidade de um ator. A referência é o pé, nunca o topo do charset.
/// Prioridades Below/Above continuam vencendo a ordenação Y normal.
inline double actorWorldDepth(int priority, double footY)
{
    constexpr double forced = std::numeric_limits<double>::max() / 4.0;
    return priority < 0 ? -forced : priority > 0 ? forced : footY;
}

/// No empate o ator entra primeiro e o tile prioritário o cobre. O ator só
/// passa à frente depois que os pés cruzam a profundidade efetiva do tile.
inline bool priorityTileDrawsBeforeActor(double tileDepth, double actorDepth)
{
    return tileDepth < actorDepth;
}

/// Alias legado para testes/código ★ anteriores.
inline bool starDrawsBeforeActor(double starDepth, double actorDepth)
{
    return priorityTileDrawsBeforeActor(starDepth, actorDepth);
}

} // namespace game
