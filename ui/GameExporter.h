// ============================================================================
// GameExporter.h — empacotamento do jogo jogavel.
// ============================================================================
#pragma once

namespace core { class Editor; }
class QWidget;

namespace ui {

class GameExporter
{
public:
    /// Pressupoe que o projeto ja foi validado/salvo pela MainWindow.
    static void run(core::Editor& editor, QWidget* parent);
};

} // namespace ui
