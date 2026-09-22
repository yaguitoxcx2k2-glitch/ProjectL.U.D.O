#pragma once

#include "core/Editor.h"

#include <QImage>
#include <QSize>

namespace ui {

/// Composição editorial compartilhada por comandos que precisam mostrar o
/// mapa real. Inclui panorama/parallax visível no editor e as camadas do mapa.
QImage renderMapPreview(const core::Editor& editor, const core::MapDoc& doc,
                        const QSize& target = QSize());

} // namespace ui
