#pragma once

#include "core/Editor.h"
#include "core/EventModel.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QListWidget;
class QCheckBox;
class QComboBox;
QT_END_NAMESPACE

namespace ui {

class MoveRouteDialog : public QDialog
{
public:
    MoveRouteDialog(core::Editor& ed, const core::MoveRoute& route,
                    QWidget* parent = nullptr, bool allowTargets = false);

    core::MoveRoute route() const { return m_route; }
    static QString commandLabel(const core::MoveCommand& c);

private:
    void addSimple(const QString& type);
    void addParameterized(const QString& type);
    bool editCommand(int row);
    void reload(int row = -1);

    core::Editor& m_ed;
    core::MoveRoute m_route;
    QListWidget* m_list = nullptr;
    QCheckBox *m_repeat = nullptr, *m_wait = nullptr;
    QComboBox *m_target = nullptr, *m_blockedPolicy = nullptr, *m_startMode = nullptr;
    bool m_allowTargets = false;
};

} // namespace ui
