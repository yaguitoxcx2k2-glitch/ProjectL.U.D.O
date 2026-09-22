#pragma once

#include "core/NoCodePlugin.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTextBrowser;
QT_END_NAMESPACE

namespace ui {

/// Compositor declarativo: campos + comandos nativos seguros + preview da
/// expansão. O resultado salvo é exatamente o contrato consumido pelo runtime.
class PluginCommandComposer final : public QDialog
{
public:
    explicit PluginCommandComposer(core::PluginCommand command = {}, QWidget* parent = nullptr);
    core::PluginCommand command() const { return m_command; }

protected:
    void accept() override;

private:
    void addField();
    void editCommandParams(int row);
    void syncFromUi();
    void refreshPreview();

    core::PluginCommand m_command;
    QLineEdit *m_id=nullptr,*m_name=nullptr,*m_category=nullptr,*m_description=nullptr;
    QLineEdit *m_icon=nullptr,*m_shortcut=nullptr,*m_tags=nullptr;
    QCheckBox* m_catalog=nullptr;
    QTableWidget* m_fields=nullptr;
    QListWidget *m_native=nullptr,*m_canvas=nullptr;
    QTextBrowser* m_preview=nullptr;
};

} // namespace ui
