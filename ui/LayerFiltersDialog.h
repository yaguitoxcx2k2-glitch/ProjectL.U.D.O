#pragma once

#include "core/Editor.h"
#include <QWidget>

namespace ui {

/// Edita, com prévia ao vivo e Undo/Redo atômico, a pilha de filtros do
/// conteúdo (maskTarget=false) ou da máscara raster (maskTarget=true).
bool editLayerRasterFilters(core::Editor& editor, const core::LayerPtr& layer,
                            bool maskTarget, QWidget* parent = nullptr);

} // namespace ui
