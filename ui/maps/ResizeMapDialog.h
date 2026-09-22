#pragma once

#include <QDialog>

namespace core { class Editor; }

namespace ui {

/// Feature dialog de Maps. A mutação passa por MapService/ResizeMapCommand em
/// vez de editar Editor diretamente; Dialogs.h apenas reexporta esta classe
/// durante a migração para módulos por feature.
class ResizeMapDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ResizeMapDialog(core::Editor& editor, QWidget* parent = nullptr);

private:
    core::Editor& m_editor;
};

} // namespace ui
