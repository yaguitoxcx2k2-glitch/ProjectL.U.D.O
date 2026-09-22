#include "UiNavigation.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace game::ui {

int moveFocusIndex(int current, int count, int delta, bool wrap)
{
    if (count <= 0) return 0;
    current = qBound(0, current, count - 1);
    if (delta == 0) return current;
    if (!wrap) return qBound(0, current + delta, count - 1);
    int next = (current + delta) % count;
    if (next < 0) next += count;
    return next;
}

QString spatialFocusNeighbor(const QString& currentId,
                             const QStringList& candidates,
                             const QHash<QString, QRectF>& rects,
                             UiNavDirection direction,
                             bool wrap)
{
    if (currentId.isEmpty() || !rects.contains(currentId)) return QString();
    const QPointF origin = rects.value(currentId).center();
    QString best;
    double bestScore = std::numeric_limits<double>::max();
    const bool horizontal = direction == UiNavDirection::Left || direction == UiNavDirection::Right;

    auto evaluate = [&](const QString& id, bool wrapped) {
        if (id == currentId || !rects.contains(id)) return;
        const QPointF p = rects.value(id).center();
        const double dx = p.x() - origin.x(), dy = p.y() - origin.y();
        const double primary = horizontal ? dx : dy;
        const double secondary = horizontal ? std::abs(dy) : std::abs(dx);
        bool forward = false;
        switch (direction) {
        case UiNavDirection::Left:  forward = primary < -1e-9; break;
        case UiNavDirection::Right: forward = primary >  1e-9; break;
        case UiNavDirection::Up:    forward = primary < -1e-9; break;
        case UiNavDirection::Down:  forward = primary >  1e-9; break;
        }
        if (!wrapped && !forward) return;
        if (wrapped && forward) return;
        const double along = std::abs(primary);
        // Direção pesa mais que alinhamento, mas um elemento quase alinhado é
        // preferido a um diagonal muito distante.
        const double score = along + secondary * 2.25;
        if (score < bestScore) { bestScore = score; best = id; }
    };

    for (const QString& id : candidates) evaluate(id, false);
    if (!best.isEmpty() || !wrap) return best;

    // Wrap deve realmente atravessar para o extremo oposto (Right -> mais à
    // esquerda, Down -> mais acima, etc.), em vez de apenas escolher o vizinho
    // imediatamente atrás do foco atual. Entre elementos no mesmo extremo,
    // prefere o mais alinhado no eixo perpendicular.
    const bool seekMinimum = direction == UiNavDirection::Right || direction == UiNavDirection::Down;
    double edge = seekMinimum ? std::numeric_limits<double>::max()
                              : -std::numeric_limits<double>::max();
    for (const QString& id : candidates) {
        if (id == currentId || !rects.contains(id)) continue;
        const QPointF p = rects.value(id).center();
        const double axis = horizontal ? p.x() : p.y();
        edge = seekMinimum ? std::min(edge, axis) : std::max(edge, axis);
    }
    if (!std::isfinite(edge)) return QString();
    best.clear();
    bestScore = std::numeric_limits<double>::max();
    for (const QString& id : candidates) {
        if (id == currentId || !rects.contains(id)) continue;
        const QPointF p = rects.value(id).center();
        const double axis = horizontal ? p.x() : p.y();
        const double secondary = horizontal ? std::abs(p.y() - origin.y())
                                            : std::abs(p.x() - origin.x());
        // Prioridade lexicográfica aproximada: primeiro o extremo, depois o
        // alinhamento. As coordenadas são normalizadas, então o peso é seguro.
        const double score = std::abs(axis - edge) * 1000.0 + secondary;
        if (score < bestScore) { bestScore = score; best = id; }
    }
    return best;
}

void UiFocusController::setCount(int count)
{
    m_count = qMax(0, count);
    m_index = m_count > 0 ? qBound(0, m_index, m_count - 1) : 0;
}

void UiFocusController::setIndex(int index)
{
    m_index = m_count > 0 ? qBound(0, index, m_count - 1) : 0;
}

bool UiFocusController::move(int delta)
{
    if (m_count <= 0 || delta == 0) return false;
    const int before = m_index;
    m_index = moveFocusIndex(m_index, m_count, delta, m_wrap);
    return m_index != before;
}

} // namespace game::ui
