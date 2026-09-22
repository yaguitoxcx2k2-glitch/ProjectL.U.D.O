#include "ProjectHealthDialog.h"

#include "EditorUiPrimitives.h"
#include "ProjectRecoveryManager.h"
#include "core/Editor.h"
#include "core/ProjectHealth.h"
#include "core/ProductReadiness.h"
#include "ProjectOnboarding.h"
#include "core/ProjectValidator.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ui {
namespace {
int severityValue(core::ValidationSeverity severity)
{
    if (severity == core::ValidationSeverity::Error) return 1;
    if (severity == core::ValidationSeverity::Warning) return 2;
    return 3;
}

QString readableBytes(qint64 bytes)
{
    if (bytes < 1024) return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QObject::tr("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    return QObject::tr("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}
}

ProjectHealthDialog::ProjectHealthDialog(core::Editor& editor, ProjectRecoveryManager* recovery,
                                         QWidget* parent)
    : QDialog(parent), m_editor(editor), m_recovery(recovery)
{
    setWindowTitle(tr("Saúde do projeto"));
    resize(980, 650);
    setAccessibleName(tr("Saúde do projeto"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(primitives::sectionTitle(tr("Visão geral"), this));
    m_summary = primitives::hintLabel(QString(), this);
    root->addWidget(m_summary);

    m_onboardingSummary = primitives::infoBanner(QString(), this)->findChild<QLabel*>();
    if (m_onboardingSummary) root->addWidget(m_onboardingSummary->parentWidget());

    m_readinessSummary = primitives::infoBanner(QString(), this)->findChild<QLabel*>();
    if (m_readinessSummary) root->addWidget(m_readinessSummary->parentWidget());

    auto* recoveryBox = primitives::infoBanner(QString(), this);
    auto* recoveryLayout = static_cast<QVBoxLayout*>(recoveryBox->layout());
    m_recoverySummary = qobject_cast<QLabel*>(recoveryLayout->itemAt(0)->widget());
    auto* recoveryActions = new QHBoxLayout;
    m_writeRecovery = new QPushButton(tr("Criar ponto de recuperação agora"), recoveryBox);
    m_discardRecovery = new QPushButton(tr("Remover cópia de recuperação"), recoveryBox);
    recoveryActions->addWidget(m_writeRecovery);
    recoveryActions->addWidget(m_discardRecovery);
    recoveryActions->addStretch(1);
    recoveryLayout->addLayout(recoveryActions);
    root->addWidget(recoveryBox);

    auto* filters = new QHBoxLayout;
    filters->addWidget(new QLabel(tr("Mostrar:"), this));
    m_severityFilter = new QComboBox(this);
    m_severityFilter->addItem(tr("Todos os níveis"), 0);
    m_severityFilter->addItem(tr("Erros"), 1);
    m_severityFilter->addItem(tr("Avisos"), 2);
    m_severityFilter->addItem(tr("Informações"), 3);
    m_areaFilter = new QComboBox(this);
    m_areaFilter->addItem(tr("Todas as áreas"), -1);
    for (core::ProjectHealthArea area : {core::ProjectHealthArea::Project, core::ProjectHealthArea::Maps,
                                         core::ProjectHealthArea::Events, core::ProjectHealthArea::Data,
                                         core::ProjectHealthArea::Assets, core::ProjectHealthArea::Runtime})
        m_areaFilter->addItem(core::projectHealthAreaLabel(area), int(area));
    filters->addWidget(m_severityFilter);
    filters->addWidget(m_areaFilter);
    filters->addStretch(1);
    root->addLayout(filters);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Nível"), tr("Área"), tr("Local"), tr("Problema"), tr("Código")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout;
    m_repair = new QPushButton(tr("Aplicar correções seguras"), this);
    auto* close = new QPushButton(tr("Fechar"), this);
    buttons->addWidget(m_repair);
    buttons->addStretch(1);
    buttons->addWidget(close);
    root->addLayout(buttons);

    connect(m_severityFilter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_areaFilter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_repair, &QPushButton::clicked, this, [this] {
        QStringList changes;
        const int count = core::ProjectValidator::repairSafe(m_editor, &changes);
        refresh();
        QMessageBox::information(this, tr("Correções seguras"),
                                 count > 0 ? tr("Correções aplicadas: %1.\n\n%2").arg(count).arg(changes.join(QLatin1Char('\n')))
                                           : tr("Nenhuma correção automática foi necessária."));
    });
    connect(m_writeRecovery, &QPushButton::clicked, this, [this] {
        if (!m_recovery) return;
        QString error;
        if (!m_recovery->writeNow(&error)) QMessageBox::warning(this, tr("Recuperação"), error);
        refreshRecovery();
    });
    connect(m_discardRecovery, &QPushButton::clicked, this, [this] {
        if (m_editor.projectPath.isEmpty()) return;
        if (QMessageBox::question(this, tr("Remover recuperação"),
                                  tr("Remover a cópia automática de recuperação deste projeto?\n\nO arquivo principal não será alterado."),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!ProjectRecoveryManager::discardRecovery(m_editor.projectPath, &error))
            QMessageBox::warning(this, tr("Recuperação"), error);
        refreshRecovery();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

bool ProjectHealthDialog::acceptsCurrentFilter(int severity, int area) const
{
    const int selectedSeverity = m_severityFilter->currentData().toInt();
    const int selectedArea = m_areaFilter->currentData().toInt();
    return (selectedSeverity == 0 || selectedSeverity == severity) &&
           (selectedArea < 0 || selectedArea == area);
}

void ProjectHealthDialog::refreshRecovery()
{
    const ProjectRecoveryStatus status = ProjectRecoveryManager::statusFor(m_editor.projectPath);
    if (m_editor.projectPath.isEmpty()) {
        m_recoverySummary->setText(tr("Este projeto ainda não possui um caminho salvo. A recuperação automática ficará disponível após o primeiro salvamento."));
        m_writeRecovery->setEnabled(false);
        m_discardRecovery->setEnabled(false);
        return;
    }
    if (!status.recoveryExists) {
        m_recoverySummary->setText(m_editor.projectDirty
            ? tr("Há alterações não salvas. O LUDO cria pontos de recuperação automáticos durante a edição.")
            : tr("Não existe uma cópia de recuperação pendente. O projeto principal está salvo."));
    } else {
        const QString when = QLocale().toString(status.recoveryModified, QLocale::ShortFormat);
        m_recoverySummary->setText(status.newerThanProject
            ? tr("Existe uma recuperação mais recente que o arquivo principal: %1 · %2. Ela será oferecida na próxima abertura.")
                  .arg(when, readableBytes(status.recoveryBytes))
            : tr("Existe uma cópia de recuperação: %1 · %2. Ela não é mais recente que o projeto salvo.")
                  .arg(when, readableBytes(status.recoveryBytes)));
    }
    m_writeRecovery->setEnabled(m_recovery && m_editor.projectDirty);
    m_discardRecovery->setEnabled(status.recoveryExists);
}

void ProjectHealthDialog::refresh()
{
    const core::ProjectHealthSnapshot snapshot = core::ProjectHealth::inspect(m_editor);
    if (snapshot.isClean()) {
        m_summary->setText(tr("Tudo certo com o projeto. Nenhum problema foi encontrado e ele está pronto para testar e exportar."));
    } else if (snapshot.validation.hasErrors()) {
        m_summary->setText(tr("Erros: %1 • Avisos: %2 • Informações: %3. Corrija os erros antes do teste final ou da exportação.")
                               .arg(snapshot.validation.errorCount).arg(snapshot.validation.warningCount).arg(snapshot.validation.infoCount));
    } else {
        m_summary->setText(tr("Nenhum erro impede a publicação. Avisos: %1 • Informações: %2.")
                               .arg(snapshot.validation.warningCount).arg(snapshot.validation.infoCount));
    }

    if (m_onboardingSummary) {
        const OnboardingSnapshot onboarding = ProjectOnboarding::inspect(m_editor);
        QStringList lines;
        for (const OnboardingStep& step : onboarding.steps)
            lines << tr("%1 %2 — %3").arg(step.complete ? QStringLiteral("✓") : QStringLiteral("○"), step.title, step.description);
        m_onboardingSummary->setText(tr("<b>Primeiros passos</b><br>%1<br><small>%2</small>")
                                         .arg(ProjectOnboarding::summaryText(onboarding), lines.join(QStringLiteral("<br>"))));
    }

    if (m_readinessSummary) {
        const core::ProductReadinessSnapshot readiness = core::ProductReadiness::inspect(m_editor);
        QStringList blockers;
        for (const core::ProductReadinessCheck& check : readiness.checks) {
            if (check.level != core::ProductReadinessLevel::Ready)
                blockers << tr("%1: %2").arg(check.title, check.detail);
        }
        const QString headline = readiness.canPublish()
            ? tr("Pronto para publicar.")
            : tr("Ainda não está pronto para publicar. Problemas que impedem a publicação: %1.").arg(readiness.blockedCount);
        m_readinessSummary->setText(tr("<b>%1</b><br>%2")
                                        .arg(headline, blockers.isEmpty() ? tr("A pasta do projeto, o espaço em disco e a validação estão em ordem.")
                                                                         : blockers.join(QStringLiteral("<br>"))));
    }

    QVector<const core::ValidationIssue*> visible;
    for (const core::ValidationIssue& issue : snapshot.validation.issues) {
        const int severity = severityValue(issue.severity);
        const int area = int(core::ProjectHealth::areaForIssue(issue));
        if (acceptsCurrentFilter(severity, area)) visible.push_back(&issue);
    }
    m_table->setRowCount(visible.size());
    for (int row = 0; row < visible.size(); ++row) {
        const core::ValidationIssue& issue = *visible[row];
        const auto area = core::ProjectHealth::areaForIssue(issue);
        m_table->setItem(row, 0, new QTableWidgetItem(core::validationSeverityLabel(issue.severity)));
        m_table->setItem(row, 1, new QTableWidgetItem(core::projectHealthAreaLabel(area)));
        m_table->setItem(row, 2, new QTableWidgetItem(issue.location));
        m_table->setItem(row, 3, new QTableWidgetItem(issue.message));
        m_table->setItem(row, 4, new QTableWidgetItem(issue.code));
    }
    m_repair->setEnabled(snapshot.safelyFixableCount > 0);
    refreshRecovery();
}

} // namespace ui
