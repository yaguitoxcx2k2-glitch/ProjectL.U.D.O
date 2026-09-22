#include "RecoveryDecisionDialog.h"

#include "EditorUiPrimitives.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {
QString fileStamp(const QDateTime& dt)
{
    return dt.isValid() ? QLocale().toString(dt, QLocale::ShortFormat) : QObject::tr("indisponível");
}
}

RecoveryDecisionDialog::RecoveryDecisionDialog(const ProjectRecoveryStatus& status, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Recuperação do projeto"));
    resize(620, 330);
    setAccessibleName(tr("Recuperação do projeto"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(primitives::sectionTitle(tr("Encontramos trabalho mais recente"), this));
    root->addWidget(primitives::warningBanner(
        tr("Existe uma cópia automática mais recente que o arquivo principal. Ela pode conter alterações feitas antes de um fechamento inesperado."), this));

    root->addWidget(primitives::hintLabel(
        tr("Projeto salvo: %1\nRecuperação automática: %2\n\nRestaurar abre a recuperação sem sobrescrever o arquivo principal. Você decide depois se deseja salvá-la.")
            .arg(fileStamp(status.projectModified), fileStamp(status.recoveryModified)), this));

    auto* buttons = new QDialogButtonBox(this);
    auto* restore = buttons->addButton(tr("Restaurar recuperação"), QDialogButtonBox::AcceptRole);
    auto* saved = buttons->addButton(tr("Abrir versão salva"), QDialogButtonBox::ActionRole);
    auto* discard = buttons->addButton(tr("Descartar recuperação"), QDialogButtonBox::DestructiveRole);
    auto* cancel = buttons->addButton(QDialogButtonBox::Cancel);
    restore->setDefault(true);
    root->addStretch(1);
    root->addWidget(buttons);

    connect(restore, &QPushButton::clicked, this, [this] {
        m_choice = RecoveryChoice::RestoreRecovery;
        accept();
    });
    connect(saved, &QPushButton::clicked, this, [this] {
        m_choice = RecoveryChoice::OpenSavedProject;
        accept();
    });
    connect(discard, &QPushButton::clicked, this, [this] {
        m_choice = RecoveryChoice::DiscardRecovery;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

} // namespace ui
