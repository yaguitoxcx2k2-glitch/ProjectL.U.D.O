#pragma once

#include "UiDrawList.h"

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace game::ui {

/// Backend QPainter do canvas. O caminho CPU desenha direto no frame; o QRhi
/// usa a mesma lista para gerar a textura de UI que e composta pela GPU.
class UiPainterRenderer {
public:
    static void render(QPainter& painter, const UiDrawList& list);
    static void drawNineSlice(QPainter& painter, const UiNineSliceCommand& cmd);
};

} // namespace game::ui
