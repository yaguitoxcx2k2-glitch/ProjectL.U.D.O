// ============================================================================
// PreviewClock.h — relógio comum para previews animados do editor.
// ============================================================================
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace ui {

class PreviewClock : public QObject
{
    Q_OBJECT
public:
    explicit PreviewClock(QObject* parent = nullptr);

    void start(int intervalMs = 33);
    void stop();
    void restart();
    bool running() const { return m_timer.isActive(); }
    double elapsedSeconds() const;

signals:
    void frame(double elapsedSeconds);

private:
    QTimer m_timer;
    QElapsedTimer m_elapsed;
};

} // namespace ui
