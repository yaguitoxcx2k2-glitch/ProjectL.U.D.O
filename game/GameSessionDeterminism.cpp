#include "GameSession.h"
#include "core/RuntimeProject.h"
#include "game/RuntimeStateHash.h"
#include <QRandomGenerator>

namespace game {
GameSession::GameSession(core::RuntimeProject& project, const QPointF& startPixel, const QFont& font)
    : GameSession(project.legacyModel(), startPixel, font)
{}

void GameSession::setDeterministicMode(bool enabled, quint64 seed)
{
    m_runtimeClock.setDeterministic(enabled);
    m_runtimeClock.reset();
    if (enabled) {
        m_runtimeRandom.reseed(seed);
        m_replay.setSeed(seed);
    }
}

void GameSession::tickFixedStep()
{
    const double previous = m_forcedDeltaSeconds;
    m_forcedDeltaSeconds = m_runtimeClock.fixedDelta();
    tick();
    m_forcedDeltaSeconds = previous;
}

QByteArray GameSession::deterministicStateHash() const
{
    return RuntimeStateHash::calculate(m_state, currentMapId(), m_world.playerPixel(), m_runtimeRandom.state());
}

int GameSession::randomBounded(int upperExclusive)
{
    return m_runtimeClock.deterministic()
        ? m_runtimeRandom.bounded(upperExclusive)
        : QRandomGenerator::global()->bounded(upperExclusive);
}
}
