#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QListWidget;
QT_END_NAMESPACE

namespace ui {

class ModuleManagerDialog final : public QDialog
{
public:
    explicit ModuleManagerDialog(QWidget* parent = nullptr);

    bool changed() const { return m_changed; }

private:
    void saveModules();
    QListWidget* m_list = nullptr;
    bool m_changed = false;
};

} // namespace ui
