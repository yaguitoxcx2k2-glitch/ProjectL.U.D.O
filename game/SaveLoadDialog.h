#pragma once

#include "core/Editor.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

namespace game {

class SaveLoadDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { LoadOnly, SaveAndLoad };
    enum class Action { None, Save, Load };

    SaveLoadDialog(const core::Editor& editor,Mode mode,QWidget* parent=nullptr);
    int selectedSlot() const;
    Action selectedAction() const { return m_action; }

private:
    void refresh();
    void choose(Action action);
    const core::Editor& m_editor;
    Mode m_mode;
    Action m_action=Action::None;
    QListWidget* m_slots=nullptr;
    QPushButton* m_save=nullptr;
    QPushButton* m_load=nullptr;
};

} // namespace game
