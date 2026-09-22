#pragma once

#include <QPointF>
#include <QString>

namespace core { class Editor; struct FootstepSurface; }
namespace game { class GameState; }

namespace game {

struct FootstepDecision {
    QString surfaceId;
    QString sourcePath;
    int volume = 0;
    int pitch = 100;
    int pan = 0;
    int variantIndex = -1;
    bool valid() const { return !surfaceId.isEmpty() && !sourcePath.isEmpty() && volume > 0; }
};

/// Fonte única de verdade para som de passos. Editor, runtime, Common Events e
/// testes consultam esta classe em vez de cada subsistema reinterpretar tiles.
class FootstepResolver final
{
public:
    /// Superfície do chão sob a posição do ator. `actorCell` usa a mesma
    /// coordenada lógica (células, podendo ser fracionária) do GameWorld.
    static QString groundSurfaceId(const core::Editor& ed, const GameState& state,
                                   const QString& mapId, const QPointF& actorCell);

    /// Aplica regras do ator (player/página ativa do evento) sobre o chão.
    /// eventId vazio = jogador. Retorna vazio quando footsteps estão desligados.
    static QString actorSurfaceId(const core::Editor& ed, const GameState& state,
                                  const QString& mapId, const QString& eventId,
                                  const QPointF& actorCell, int* actorVolume = nullptr);

    /// Resolve variante, volume e pitch de forma determinística por serial.
    static FootstepDecision resolve(const core::Editor& ed, const GameState& state,
                                    const QString& mapId, const QString& eventId,
                                    const QPointF& actorCell,
                                    const QString& forcedSurfaceId,
                                    int commandVolume, quint64 serial,
                                    int lastVariant = -1, int previousVariant = -1);
};

} // namespace game
