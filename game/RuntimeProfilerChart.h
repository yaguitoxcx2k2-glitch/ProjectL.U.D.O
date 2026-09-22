#pragma once
#include <QWidget>
#include <QVector>
namespace game {
class RuntimeProfilerChart : public QWidget {
public:
    explicit RuntimeProfilerChart(QWidget* parent=nullptr);
    void addFrame(double frameMs, double p95Ms);
    void clearHistory();
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<double> m_frames;
    double m_p95 = 0.0;
    int m_capacity = 240;
};
}
