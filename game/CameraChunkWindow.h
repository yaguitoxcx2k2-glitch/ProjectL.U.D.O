#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QtGlobal>

#include <cmath>

namespace game {

/// Região estável de chunks usada pelo cache QRhi do mapa.
///
/// A janela antiga seguia exatamente a borda visível e mudava a chave assim
/// que a câmera cruzava um único chunk. A janela abaixo anda em páginas e
/// mantém uma guarda ao redor do viewport: pequenas oscilações, shake e o
/// movimento contínuo da câmera reutilizam a mesma mesh residente.
inline QRect cameraChunkWindow(const QRectF& visible, const QSize& chunkSize,
                               int stride = 2, int guard = 1)
{
    const int cw = qMax(1, chunkSize.width());
    const int ch = qMax(1, chunkSize.height());
    stride = qMax(1, stride);
    guard = qMax(0, guard);

    const auto floorDiv = [](int value, int divisor) {
        int q = value / divisor;
        const int r = value % divisor;
        if (r != 0 && ((r < 0) != (divisor < 0))) --q;
        return q;
    };
    const int vx0 = int(std::floor(visible.left() / cw));
    const int vy0 = int(std::floor(visible.top() / ch));
    // O tamanho também precisa ser estável: derivá-lo da borda direita faria
    // a chave alternar quando só a fração de alinhamento do viewport mudasse.
    const int spanX = qMax(1, int(std::ceil(visible.width() / cw)) + 1);
    const int spanY = qMax(1, int(std::ceil(visible.height() / ch)) + 1);
    const int anchorX = floorDiv(vx0, stride) * stride;
    const int anchorY = floorDiv(vy0, stride) * stride;
    return QRect(anchorX - guard, anchorY - guard,
                 spanX + stride - 1 + guard * 2,
                 spanY + stride - 1 + guard * 2);
}

} // namespace game
