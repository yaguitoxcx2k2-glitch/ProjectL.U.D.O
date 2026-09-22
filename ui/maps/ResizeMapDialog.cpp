#include "ResizeMapDialog.h"

#include "core/Editor.h"
#include "core/commands/MapCommands.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ui {

ResizeMapDialog::ResizeMapDialog(core::Editor& editor, QWidget* parent)
    : QDialog(parent), m_editor(editor)
{
    setWindowTitle(tr("Redimensionar mapa"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* width = new QSpinBox(this);
    auto* height = new QSpinBox(this);
    width->setRange(1, 4096);
    height->setRange(1, 4096);
    width->setValue(m_editor.mapInfo().width);
    height->setValue(m_editor.mapInfo().height);
    auto* keep = new QCheckBox(tr("Manter o conteúdo existente (ancorado no canto superior-esquerdo)"), this);
    keep->setChecked(true);
    form->addRow(tr("Largura (tiles)"), width);
    form->addRow(tr("Altura (tiles)"), height);
    layout->addLayout(form);
    layout->addWidget(keep);

    auto* info = new QLabel(this);
    info->setStyleSheet(QStringLiteral("color:#999;font-size:11px"));
    const auto updateInfo = [this, width, height, info] {
        info->setText(tr("Novo tamanho em pixels: %1 × %2")
                          .arg(width->value() * m_editor.mapInfo().tileWidth)
                          .arg(height->value() * m_editor.mapInfo().tileHeight));
    };
    connect(width, &QSpinBox::valueChanged, this, [updateInfo] { updateInfo(); });
    connect(height, &QSpinBox::valueChanged, this, [updateInfo] { updateInfo(); });
    updateInfo();
    layout->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, width, height] {
        const core::MapDoc* map = m_editor.doc();
        if (!map) {
            reject();
            return;
        }
        core::ResizeMapCommand command(m_editor, core::MapId::fromLegacy(map->id),
                                       QSize(width->value(), height->value()));
        QString error;
        if (!command.execute(&error)) {
            QMessageBox::warning(this, tr("Redimensionar mapa"), error);
            return;
        }
        accept();
    });
}

} // namespace ui
