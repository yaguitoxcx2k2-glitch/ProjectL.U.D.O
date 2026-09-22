#include "PreviewClock.h"

namespace ui {

PreviewClock::PreviewClock(QObject* parent) : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        emit frame(elapsedSeconds());
    });
}

void PreviewClock::start(int intervalMs)
{
    if (!m_elapsed.isValid()) m_elapsed.start();
    if (!m_timer.isActive()) m_timer.start(qMax(1, intervalMs));
}

void PreviewClock::stop()
{
    m_timer.stop();
}

void PreviewClock::restart()
{
    m_elapsed.restart();
}

double PreviewClock::elapsedSeconds() const
{
    return m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0.0;
}

} // namespace ui
