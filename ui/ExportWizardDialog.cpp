#include "ExportWizardDialog.h"
#include "UniversalAssetPicker.h"

#include "EditorUiPrimitives.h"
#include "core/Editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace ui {

ExportWizardDialog::ExportWizardDialog(core::Editor& editor, const ExportPreflightResult& preflight,
                                       QWidget* parent)
    : QDialog(parent), m_editor(editor), m_preflight(preflight)
{
    setWindowTitle(tr("Exportar jogo"));
    resize(760, 680);
    setAccessibleName(tr("Assistente de exportação"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(primitives::sectionTitle(tr("1. Verificação do projeto"), this));
    if (preflight.ready()) {
        root->addWidget(primitives::infoBanner(
            preflight.health.validation.warningCount > 0
                ? tr("O projeto pode ser exportado. Existem avisos para revisão, mas nenhum bloqueio.")
                : tr("O projeto passou pela verificação e está pronto para exportação."), this));
    } else {
        root->addWidget(primitives::warningBanner(
            tr("A exportação está bloqueada. Corrija os itens abaixo e execute a exportação novamente."), this));
    }

    auto* checks = new QListWidget(this);
    checks->setMaximumHeight(155);
    for (const QString& blocker : preflight.blockers) checks->addItem(tr("Bloqueio: %1").arg(blocker));
    for (const QString& warning : preflight.warnings) checks->addItem(tr("Aviso: %1").arg(warning));
    for (const QString& missing : preflight.missingUsedAssets) checks->addItem(tr("Asset ausente: %1").arg(missing));
    if (checks->count() == 0) checks->addItem(tr("Nenhum problema de pré-exportação encontrado."));
    root->addWidget(checks);

    root->addWidget(primitives::sectionTitle(tr("2. Identidade e pacote"), this));
    auto* form = new QFormLayout;
    m_gameName = new QLineEdit(editor.projectName.trimmed(), this);
    m_gameName->setAccessibleName(tr("Nome do jogo exportado"));

    m_profile = new QComboBox(this);
    for (const ExportBuildProfileDefaults& profile : ExportBuildProfilePolicy::profiles())
        m_profile->addItem(profile.displayName, profile.id);

    m_platform = new QComboBox(this);
    const ExportPlatform host = ExportPlatformPolicy::hostPlatform();
    for (const ExportPlatform platform : {ExportPlatform::Windows, ExportPlatform::Linux, ExportPlatform::MacOS}) {
        const ExportPlatformProfile profile = ExportPlatformPolicy::profile(platform);
        m_platform->addItem(profile.displayName + (platform == host ? tr(" (host)") : tr(" — requer build nessa plataforma")),
                            static_cast<int>(platform));
    }
    m_platform->setCurrentIndex(qMax(0, m_platform->findData(static_cast<int>(host))));
    m_platform->setEnabled(false);

    auto* iconRow = new QWidget(this);
    auto* iconLayout = new QHBoxLayout(iconRow);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    m_icon = new QLineEdit(iconRow);
    m_icon->setReadOnly(true);
    auto* chooseIcon = new QPushButton(tr("Escolher…"), iconRow);
    iconLayout->addWidget(m_icon, 1);
    iconLayout->addWidget(chooseIcon);

    form->addRow(tr("Nome do jogo:"), m_gameName);
    form->addRow(tr("Perfil de build:"), m_profile);
    form->addRow(tr("Plataforma:"), m_platform);
    form->addRow(tr("Ícone do jogo:"), iconRow);
    root->addLayout(form);

    m_portable = new QCheckBox(tr("Criar também uma versão portátil (.zip)"), this);
    m_portable->setChecked(true);
    m_secure = new QCheckBox(tr("Proteger game.ludo e Assets na build"), this);
    m_cleanup = new QCheckBox(tr("Exportar somente assets usados pelo jogo (recomendado)"), this);
    m_cleanup->setChecked(true);
    root->addWidget(m_portable);
    root->addWidget(m_secure);
    root->addWidget(m_cleanup);
    m_assetSummary = primitives::hintLabel(QString(), this);
    root->addWidget(m_assetSummary);

    root->addWidget(primitives::sectionTitle(tr("3. Destino"), this));
    auto* targetRow = new QHBoxLayout;
    m_target = new QLineEdit(this);
    m_target->setReadOnly(true);
    m_target->setPlaceholderText(tr("Escolha a pasta onde a pasta do jogo será criada"));
    auto* chooseTarget = new QPushButton(tr("Escolher pasta…"), this);
    targetRow->addWidget(m_target, 1);
    targetRow->addWidget(chooseTarget);
    root->addLayout(targetRow);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_export = buttons->addButton(tr("Exportar"), QDialogButtonBox::AcceptRole);
    root->addStretch(1);
    root->addWidget(buttons);

    connect(chooseIcon, &QPushButton::clicked, this, [this] {
        const QString path = UniversalAssetPickerDialog::chooseOne(
            m_editor,this,QStringLiteral("game.icon"),QStringLiteral("image"),tr("Escolher ícone do jogo"));
        if (!path.isEmpty()) m_icon->setText(path);
    });
    connect(chooseTarget, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Pasta para exportar o jogo"));
        if (!path.isEmpty()) m_target->setText(path);
        updateExportEnabled();
    });
    connect(m_cleanup, &QCheckBox::toggled, this, [this] { updateSummary(); });
    connect(m_profile, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyBuildProfile(); });
    connect(m_gameName, &QLineEdit::textChanged, this, [this] { updateExportEnabled(); });
    connect(m_export, &QPushButton::clicked, this, [this] { persistPreferences(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    restorePreferences();
    updateSummary();
    updateExportEnabled();
}

void ExportWizardDialog::restorePreferences()
{
    QSettings settings;
    const QString profile = settings.value(QStringLiteral("exportWizard/buildProfile"), QStringLiteral("release")).toString();
    const int profileIndex = m_profile->findData(profile);
    if (profileIndex >= 0) {
        const bool blocked = m_profile->blockSignals(true);
        m_profile->setCurrentIndex(profileIndex);
        m_profile->blockSignals(blocked);
    }
    applyBuildProfile();
    const QString lastTarget = settings.value(QStringLiteral("exportWizard/parentDir")).toString();
    if (!lastTarget.isEmpty()) m_target->setText(lastTarget);
    m_portable->setChecked(settings.value(QStringLiteral("exportWizard/portable"), m_portable->isChecked()).toBool());
    m_secure->setChecked(settings.value(QStringLiteral("exportWizard/protectedAssets"), m_secure->isChecked()).toBool());
    m_cleanup->setChecked(settings.value(QStringLiteral("exportWizard/cleanupUnused"), m_cleanup->isChecked()).toBool());
}

void ExportWizardDialog::persistPreferences() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("exportWizard/buildProfile"), m_profile->currentData().toString());
    settings.setValue(QStringLiteral("exportWizard/parentDir"), m_target->text().trimmed());
    settings.setValue(QStringLiteral("exportWizard/portable"), m_portable->isChecked());
    settings.setValue(QStringLiteral("exportWizard/protectedAssets"), m_secure->isChecked());
    settings.setValue(QStringLiteral("exportWizard/cleanupUnused"), m_cleanup->isChecked());
}

void ExportWizardDialog::applyBuildProfile()
{
    const ExportBuildProfileDefaults defaults = ExportBuildProfilePolicy::profileFromId(
        m_profile ? m_profile->currentData().toString() : QStringLiteral("release"));
    m_portable->setChecked(defaults.portableZip);
    m_secure->setChecked(defaults.protectAssets);
    m_cleanup->setChecked(defaults.cleanupUnused);
    updateSummary();
}

void ExportWizardDialog::updateSummary()
{
    if (m_cleanup->isChecked()) {
        m_assetSummary->setText(tr("Perfil %1 • Arquivos usados pelo jogo: %2 • Arquivos sem uso que serão deixados de fora: %3.")
                                    .arg(m_profile ? m_profile->currentText() : QString())
                                    .arg(m_preflight.usedAssets.size()).arg(m_preflight.unusedAssetCount()));
    } else {
        m_assetSummary->setText(tr("Perfil %1 • Todos os %2 arquivos encontrados no projeto serão incluídos.")
                                    .arg(m_profile ? m_profile->currentText() : QString())
                                    .arg(m_preflight.allAssets.size()));
    }
}

void ExportWizardDialog::updateExportEnabled()
{
    m_export->setEnabled(m_preflight.ready() && !m_target->text().trimmed().isEmpty() &&
                         !m_gameName->text().trimmed().isEmpty());
}

ExportWizardOptions ExportWizardDialog::options() const
{
    ExportWizardOptions result;
    result.gameName = m_gameName->text().trimmed();
    result.iconPath = m_icon->text().trimmed();
    result.parentDir = m_target->text().trimmed();
    result.portableZip = m_portable->isChecked();
    result.secure = m_secure->isChecked();
    result.cleanupUnused = m_cleanup->isChecked();
    result.buildProfile = m_profile ? m_profile->currentData().toString() : QStringLiteral("release");
    result.targetPlatform = m_platform
        ? static_cast<ExportPlatform>(m_platform->currentData().toInt())
        : ExportPlatformPolicy::hostPlatform();
    return result;
}

} // namespace ui
