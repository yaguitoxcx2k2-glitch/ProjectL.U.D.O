#pragma once
namespace core { class Editor; }
class QWidget;
namespace ui {
class CollaborationClient;
void runRpgMakerPublication(core::Editor& editor,QWidget* parent,CollaborationClient* team);
}
