#pragma once
#include <QtGlobal>

namespace game {
struct RuntimeFrameTiming {
    double realDelta = 0.0;
    double simulationDelta = 0.0;
    int simulationSteps = 0;
    double interpolationAlpha = 0.0;
};

/// Fixed-step capable runtime clock. Legacy mode preserves one variable step;
/// deterministic/headless mode consumes exact simulation quanta.
class RuntimeClock {
public:
    explicit RuntimeClock(double fixedHz = 60.0);
    void setDeterministic(bool enabled) { m_deterministic = enabled; }
    bool deterministic() const { return m_deterministic; }
    void reset();
    RuntimeFrameTiming advance(double realDelta);
    double fixedDelta() const { return m_fixedDelta; }
private:
    double m_fixedDelta = 1.0 / 60.0;
    double m_accumulator = 0.0;
    bool m_deterministic = false;
    int m_maxCatchupSteps = 5;
};
}
