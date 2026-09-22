#include "ModuleManagerDialog.h"

#include "modules/EditorModuleRegistry.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

namespace ui {

namespace {
constexpr int ModuleIdRole = Qt::UserRole + 730;
}

ModuleManagerDialog::ModuleManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Módulos do LUDO Map Editor"));
    resize(620, 430);
    setModal(true);

    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("O Editor é composto por módulos independentes. Os módulos essenciais do mapa e da integração com RPG Maker MV/MZ ficam sempre ativos; os demais podem ser ocultados para simplificar o workspace."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    const auto& registry = modules::EditorModuleRegistry::instance();
    for (const auto& descriptor : registry.descriptors()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(descriptor.name, descriptor.description), m_list);
        item->setData(ModuleIdRole, int(descriptor.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(registry.isEnabled(descriptor.id) ? Qt::Checked : Qt::Unchecked);
        if (descriptor.required) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Módulo essencial — permanece sempre ativo."));
        }
        item->setSizeHint(QSize(0, 58));
    }
    root->addWidget(m_list, 1);

    auto* note = new QLabel(
        tr("Alterações nos módulos opcionais são aplicadas ao reiniciar o Editor. Nenhum dado do mapa é apagado ao ocultar um módulo."), this);
    note->setWordWrap(true);
    note->setProperty("uiRole", QStringLiteral("hint"));
    root->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* defaults = buttons->addButton(tr("Restaurar padrões"), QDialogButtonBox::ResetRole);
    root->addWidget(buttons);

    connect(defaults, &QPushButton::clicked, this, [this] {
        const auto& descriptors = modules::EditorModuleRegistry::instance().descriptors();
        for (int row = 0; row < m_list->count(); ++row) {
            auto* item = m_list->item(row);
            const auto id = static_cast<modules::EditorModuleId>(item->data(ModuleIdRole).toInt());
            for (const auto& descriptor : descriptors) {
                if (descriptor.id == id) {
                    item->setCheckState((descriptor.required || descriptor.defaultEnabled) ? Qt::Checked : Qt::Unchecked);
                    break;
                }
            }
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveModules();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ModuleManagerDialog::saveModules()
{
    auto& registry = modules::EditorModuleRegistry::instance();
    for (int row = 0; row < m_list->count(); ++row) {
        auto* item = m_list->item(row);
        const auto id = static_cast<modules::EditorModuleId>(item->data(ModuleIdRole).toInt());
        const bool before = registry.isEnabled(id);
        const bool after = item->checkState() == Qt::Checked;
        if (before != after && registry.setEnabled(id, after)) m_changed = true;
    }
}

} // namespace ui
