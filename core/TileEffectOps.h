#pragma once

#include "Model.h"

#include <QQueue>
#include <QRect>
#include <QSet>
#include <QVector>

namespace core {

/// Componente 4-conectado de uma mascara de TileEffect. Tratar cada componente
/// como uma imagem continua evita que uma arvore composta por varios tiles seja
/// deformada celula por celula (o que criava "rasgos" nas emendas).
struct TileEffectComponent {
    QSet<quint64> cells;
    QRect bounds; // coordenadas de celula, inclusive via QRect normal
};

inline QVector<TileEffectComponent> tileEffectComponents(const TileEffect& effect)
{
    QVector<TileEffectComponent> out;
    QSet<quint64> remaining = effect.cells;
    QQueue<quint64> queue;

    while (!remaining.isEmpty()) {
        const quint64 first = *remaining.constBegin();
        remaining.remove(first);
        queue.enqueue(first);

        TileEffectComponent component;
        int minX = TileEffect::cellX(first), maxX = minX;
        int minY = TileEffect::cellY(first), maxY = minY;

        while (!queue.isEmpty()) {
            const quint64 key = queue.dequeue();
            component.cells.insert(key);
            const int x = TileEffect::cellX(key), y = TileEffect::cellY(key);
            minX = qMin(minX, x); maxX = qMax(maxX, x);
            minY = qMin(minY, y); maxY = qMax(maxY, y);

            const int nx[4] = {x - 1, x + 1, x, x};
            const int ny[4] = {y, y, y - 1, y + 1};
            for (int i = 0; i < 4; ++i) {
                if (nx[i] < 0 || ny[i] < 0) continue;
                const quint64 nkey = TileEffect::cellKey(nx[i], ny[i]);
                if (!remaining.contains(nkey)) continue;
                remaining.remove(nkey);
                queue.enqueue(nkey);
            }
        }
        component.bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        out.push_back(component);
    }

    return out;
}

} // namespace core
