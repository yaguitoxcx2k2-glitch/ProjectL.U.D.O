#pragma once
#include <cstdint>

namespace game::pure {

struct FramePacingSnapshot {
    int lateFrames = 0;
    int totalFrames = 0;
    double lateRatio = 0.0;
    double lastOverrunMs = 0.0;
    double worstOverrunMs = 0.0;
};

class FramePacer {
public:
    static constexpr double Fps = 60.0;
    static constexpr std::int64_t StepNs = 16666666;

    void reset(std::int64_t nowNs = 0) noexcept { m_nextNs = nowNs; m_lateFrames = 0; m_totalFrames = 0; m_lastOverrunMs = 0.0; m_worstOverrunMs = 0.0; }
    std::int64_t nextDelayNs(std::int64_t frameEndNs) noexcept
    {
        m_nextNs += StepNs;
        std::int64_t wait = m_nextNs - frameEndNs;
        ++m_totalFrames;
        m_lastOverrunMs = wait < 0 ? double(-wait) / 1000000.0 : 0.0;
        if (m_lastOverrunMs > m_worstOverrunMs) m_worstOverrunMs = m_lastOverrunMs;
        if (wait < 0) { ++m_lateFrames; m_nextNs = frameEndNs + StepNs; wait = StepNs; }
        return wait;
    }
    int lateFrames() const noexcept { return m_lateFrames; }
    FramePacingSnapshot pacingSnapshot() const noexcept {
        return {m_lateFrames, m_totalFrames,
                m_totalFrames > 0 ? double(m_lateFrames) / m_totalFrames : 0.0,
                m_lastOverrunMs, m_worstOverrunMs};
    }
private:
    std::int64_t m_nextNs = 0;
    int m_lateFrames = 0;
    int m_totalFrames = 0;
    double m_lastOverrunMs = 0.0;
    double m_worstOverrunMs = 0.0;
};

} // namespace game::pure
