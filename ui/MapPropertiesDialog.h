#pragma once

#include "core/Editor.h"

#include <QDialog>

namespace ui {

/// Propriedades do mapa selecionado na árvore: ambiente, panorama e fogs.
class MapPropertiesDialog : public QDialog
{
public:
    explicit MapPropertiesDialog(core::Editor& ed, QWidget* parent = nullptr);
};

} // namespace ui
