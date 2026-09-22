#include "ProjectCreationDialog.h"

#include "Icons.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

ProjectCreationDialog::ProjectCreationDialog(QWidget* parent, const QString& initialParentFolder)
    : QDialog(parent)
{
    setWindowTitle(tr("Criar novo projeto de mapas"));
    resize(640, 310);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Crie um projeto de mapas e escolha a versão do RPG Maker que será o destino deste projeto."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    m_name = new QLineEdit(tr("MeuMapa"), this);
    m_name->setAccessibleName(tr("Nome do projeto"));

    m_engine = new QComboBox(this);
    m_engine->setAccessibleName(tr("Engine do projeto"));
    m_engine->addItem(tr("RPG Maker MZ"), QStringLiteral("mz"));
    m_engine->addItem(tr("RPG Maker MV"), QStringLiteral("mv"));
    m_engine->setToolTip(tr("A engine escolhida define validação, sincronização e o LudoMapSystem usado na exportação."));

    auto* folderRow = new QWidget(this);
    auto* folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    m_folder = new QLineEdit(initialParentFolder.isEmpty() ? QDir::homePath() : initialParentFolder,
                             folderRow);
    m_folder->setAccessibleName(tr("Pasta dos projetos"));
    auto* browse = new QPushButton(icons::get(QStringLiteral("folder")), tr("Escolher…"), folderRow);
    folderLayout->addWidget(m_folder, 1);
    folderLayout->addWidget(browse);

    form->addRow(tr("Nome do projeto:"), m_name);
    form->addRow(tr("Engine:"), m_engine);
    form->addRow(tr("Criar dentro de:"), folderRow);
    layout->addLayout(form);

    auto* note = new QLabel(
        tr("O projeto guarda a engine escolhida. No RPG Maker MV a grade base é 48×48 px. Recursos do LUDO que exigem plugins extras não fazem parte do modo MV; a exceção é o LudoMapSystem, usado para executar os mapas exportados."),
        this);
    note->setWordWrap(true);
    note->setProperty("uiRole", QStringLiteral("hint"));
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Pasta dos projetos"), m_folder->text());
        if (!dir.isEmpty()) m_folder->setText(dir);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_name->selectAll();
    m_name->setFocus();
}

core::ProjectCreationRequest ProjectCreationDialog::request() const
{
    core::ProjectCreationRequest out;
    out.projectName = m_name ? m_name->text().trimmed() : QString();
    out.parentFolder = m_folder ? m_folder->text().trimmed() : QString();
    const QString engine = m_engine ? m_engine->currentData().toString() : QStringLiteral("mz");
    out.engine = core::rpgMakerEngineFromId(engine, core::RpgMakerEngine::MZ);
    out.templateId = out.engine == core::RpgMakerEngine::MV
                         ? QStringLiteral("rpg_maker_mv_map")
                         : QStringLiteral("rpg_maker_mz_map");
    return out;
}

} // namespace ui
