#include "RuntimeClock.h"
#include <QtGlobal>

namespace game {
RuntimeClock::RuntimeClock(double fixedHz)
    : m_fixedDelta(1.0 / qMax(1.0, fixedHz)) {}
void RuntimeClock::reset() { m_accumulator = 0.0; }
RuntimeFrameTiming RuntimeClock::advance(double realDelta)
{
    RuntimeFrameTiming out;
    out.realDelta = qBound(0.0, realDelta, 0.1);
    if (!m_deterministic) {
        out.simulationDelta = out.realDelta;
        out.simulationSteps = out.realDelta > 0.0 ? 1 : 0;
        return out;
    }
    m_accumulator += out.realDelta;
    out.simulationSteps = qMin(m_maxCatchupSteps, int(m_accumulator / m_fixedDelta));
    if (out.simulationSteps > 0) {
        out.simulationDelta = m_fixedDelta;
        m_accumulator -= out.simulationSteps * m_fixedDelta;
    }
    out.interpolationAlpha = qBound(0.0, m_accumulator / m_fixedDelta, 1.0);
    return out;
}
}
