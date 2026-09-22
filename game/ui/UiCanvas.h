#pragma once

#include "UiDrawList.h"

#include <QSize>

namespace game::ui {

/// Canvas logico da interface. Ele sempre usa a resolucao interna do projeto;
/// letterbox/escala de janela ficam por conta do renderer final.
class UiCanvas {
public:
    explicit UiCanvas(const QSize& logicalSize = QSize()) : m_size(logicalSize) {}

    void beginFrame(const QSize& logicalSize)
    {
        m_size = logicalSize;
        m_drawList.clear();
    }

    const QSize& logicalSize() const { return m_size; }
    UiDrawList& drawList() { return m_drawList; }
    const UiDrawList& drawList() const { return m_drawList; }

private:
    QSize m_size;
    UiDrawList m_drawList;
};

} // namespace game::ui
