// ============================================================================
// FrameClock.h — casca Qt do agendador puro de 60 fps.
// A matemática de pacing vive em game::pure::FramePacer e é testável sem Qt.
// ============================================================================
#pragma once

#include "game/pure/FramePacer.h"

#include <QElapsedTimer>
#include <QTimer>

#include <functional>

namespace game {

using FramePacingSnapshot = pure::FramePacingSnapshot;

class FrameClock
{
public:
    static constexpr double kFps = pure::FramePacer::Fps;
    static constexpr double kFrameSec = 1.0 / kFps;

    FrameClock()
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setSingleShot(true);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] { disparar(); });
    }

    void start(std::function<void()> quadro)
    {
        m_quadro = std::move(quadro);
        m_relogio.start();
        m_pacer.reset(0);
        m_timer.start(0);
    }
    void stop() { m_timer.stop(); }
    bool running() const { return m_timer.isActive(); }
    double lastFrameMs() const { return m_ultimoMs; }
    int lateFrames() const { return m_pacer.lateFrames(); }
    FramePacingSnapshot pacingSnapshot() const noexcept { return m_pacer.pacingSnapshot(); }

private:
    void disparar()
    {
        const qint64 inicio = m_relogio.nsecsElapsed();
        if (m_quadro) m_quadro();
        const qint64 fim = m_relogio.nsecsElapsed();
        m_ultimoMs = double(fim - inicio) / 1e6;
        const qint64 esperaNs = m_pacer.nextDelayNs(fim);
        m_timer.start(int(esperaNs / 1000000));
    }

    QTimer m_timer;
    QElapsedTimer m_relogio;
    std::function<void()> m_quadro;
    pure::FramePacer m_pacer;
    double m_ultimoMs = 0.0;
};

} // namespace game
