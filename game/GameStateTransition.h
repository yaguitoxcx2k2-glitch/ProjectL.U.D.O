#pragma once

#include "core/Editor.h"

namespace game {

/// Tipo de transicao de estado usado para escolher a politica de mapas invalidos.
enum class GameStateTransitionKind {
    NewGame,
    Save,
    Load,
    Restart,
    MapTransfer
};

enum class InvalidMapPolicy {
    Reject,
    FallbackToProjectStartThenFirst
};

struct GameTransitionTarget {
    QString mapId;
    QPoint cell;
    bool usedFallback = false;
};

/// Politica unica para boot, Load, Restart e transferencias. Novo Jogo/Restart
/// podem recuperar um projeto cujo startMapId ficou invalido; Load/Transfer/Save
/// nunca saltam silenciosamente para outro mapa.
InvalidMapPolicy invalidMapPolicyFor(GameStateTransitionKind kind);

bool resolveGameTransitionTarget(const core::Editor& editor,
                                 GameStateTransitionKind kind,
                                 const QString& requestedMapId,
                                 const QPoint& requestedCell,
                                 GameTransitionTarget* out,
                                 QString* error = nullptr);

/// Saves preservam coordenada de meio tile. A validacao/clamp usa a mesma
/// geometria do mapa aceita pelas demais transicoes.
QPoint clampHalfCellToMap(const core::MapDoc& map, const QPoint& halfCell);

} // namespace game
