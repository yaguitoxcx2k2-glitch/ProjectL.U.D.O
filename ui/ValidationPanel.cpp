#include "ValidationPanel.h"

#include "core/Editor.h"
#include "core/ProjectValidator.h"

#include <QComboBox>
#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {
namespace {

QString severityMark(core::ValidationSeverity severity)
{
    if (severity == core::ValidationSeverity::Error) return QStringLiteral("⛔");
    if (severity == core::ValidationSeverity::Warning) return QStringLiteral("⚠");
    return QStringLiteral("ℹ");
}

QColor severityColor(core::ValidationSeverity severity)
{
    if (severity == core::ValidationSeverity::Error) return QColor(214, 73, 73);
    if (severity == core::ValidationSeverity::Warning) return QColor(219, 159, 48);
    return QColor(74, 148, 219);
}

} // namespace

ValidationPanel::ValidationPanel(core::Editor& editor, QWidget* parent)
    : QWidget(parent), m_editor(editor)
{
    setObjectName(QStringLiteral("validationPanel"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto* filters = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("validationSummary"));
    filters->addWidget(m_summary, 1);
    m_severityFilter = new QComboBox(this);
    m_severityFilter->setObjectName(QStringLiteral("validationSeverityFilter"));
    m_severityFilter->addItem(tr("Todas as severidades"), -1);
    m_severityFilter->addItem(tr("Erros"), int(core::ValidationSeverity::Error));
    m_severityFilter->addItem(tr("Avisos"), int(core::ValidationSeverity::Warning));
    m_severityFilter->addItem(tr("Informações"), int(core::ValidationSeverity::Info));
    filters->addWidget(m_severityFilter);
    m_codeFilter = new QComboBox(this);
    m_codeFilter->setObjectName(QStringLiteral("validationCodeFilter"));
    m_codeFilter->setMinimumContentsLength(18);
    filters->addWidget(m_codeFilter);
    m_contextFilter = new QLineEdit(this);
    m_contextFilter->setObjectName(QStringLiteral("validationContextFilter"));
    m_contextFilter->setPlaceholderText(tr("Filtrar local, mensagem ou sugestão…"));
    m_contextFilter->setClearButtonEnabled(true);
    filters->addWidget(m_contextFilter, 1);
    root->addLayout(filters);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("validationProblemsTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("Severidade"), tr("Código"), tr("Local"), tr("Problema"), tr("Sugestão")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    root->addWidget(m_tree, 1);

    auto* actions = new QHBoxLayout;
    m_fixButton = new QPushButton(tr("Corrigir itens seguros"), this);
    m_fixButton->setObjectName(QStringLiteral("validationFixButton"));
    actions->addWidget(m_fixButton);
    auto* exportButton = new QPushButton(tr("Exportar…"), this);
    exportButton->setObjectName(QStringLiteral("validationExportButton"));
    actions->addWidget(exportButton);
    actions->addStretch();
    auto* refreshButton = new QPushButton(tr("Atualizar"), this);
    refreshButton->setObjectName(QStringLiteral("validationRefreshButton"));
    actions->addWidget(refreshButton);
    root->addLayout(actions);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(250);
    const auto schedule = [this] { m_refreshTimer->start(); };
    connect(&m_editor, &core::Editor::mapChanged, this, schedule);
    connect(&m_editor, &core::Editor::layersChanged, this, schedule);
    connect(&m_editor, &core::Editor::docsChanged, this, schedule);
    connect(&m_editor, &core::Editor::projectChanged, this, schedule);
    connect(m_refreshTimer, &QTimer::timeout, this, &ValidationPanel::refresh);
    connect(m_severityFilter, &QComboBox::currentIndexChanged, this, &ValidationPanel::applyFilters);
    connect(m_codeFilter, &QComboBox::currentIndexChanged, this, &ValidationPanel::applyFilters);
    connect(m_contextFilter, &QLineEdit::textChanged, this, &ValidationPanel::applyFilters);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { navigateToItem(item); });
    connect(m_fixButton, &QPushButton::clicked, this, &ValidationPanel::repairSafeIssues);
    connect(exportButton, &QPushButton::clicked, this, &ValidationPanel::exportDiagnostics);
    connect(refreshButton, &QPushButton::clicked, this, &ValidationPanel::refresh);
    refresh();
}

void ValidationPanel::refresh()
{
    m_diagnostics = core::collectProjectDiagnostics(m_editor);
    m_summary->setText(core::summarizeDiagnostics(m_diagnostics));
    const QString selectedCode = m_codeFilter->currentData().toString();
    QSet<QString> codes;
    bool hasSafeFix = false;
    for (const core::EventDiagnostic& diagnostic : m_diagnostics) {
        codes.insert(diagnostic.code);
        hasSafeFix = hasSafeFix || diagnostic.safelyFixable;
    }
    QStringList sortedCodes = codes.values();
    sortedCodes.sort();
    m_codeFilter->blockSignals(true);
    m_codeFilter->clear();
    m_codeFilter->addItem(tr("Todos os códigos"), QString());
    for (const QString& code : sortedCodes) m_codeFilter->addItem(code, code);
    const int restored = m_codeFilter->findData(selectedCode);
    m_codeFilter->setCurrentIndex(restored >= 0 ? restored : 0);
    m_codeFilter->blockSignals(false);
    m_fixButton->setEnabled(hasSafeFix);
    applyFilters();
}

void ValidationPanel::applyFilters()
{
    m_tree->clear();
    const int severity = m_severityFilter->currentData().toInt();
    const QString code = m_codeFilter->currentData().toString();
    const QString context = m_contextFilter->text().trimmed();
    for (int index = 0; index < m_diagnostics.size(); ++index) {
        const core::EventDiagnostic& diagnostic = m_diagnostics.at(index);
        if (severity >= 0 && int(diagnostic.severity) != severity) continue;
        if (!code.isEmpty() && diagnostic.code != code) continue;
        const QString searchable = diagnostic.location + QLatin1Char(' ') + diagnostic.message + QLatin1Char(' ') + diagnostic.suggestion;
        if (!context.isEmpty() && !searchable.contains(context, Qt::CaseInsensitive)) continue;
        auto* item = new QTreeWidgetItem(m_tree, {
            severityMark(diagnostic.severity) + QLatin1Char(' ') + core::validationSeverityLabel(diagnostic.severity),
            diagnostic.code,
            diagnostic.location,
            diagnostic.message,
            diagnostic.suggestion
        });
        item->setData(0, Qt::UserRole, index);
        item->setForeground(0, severityColor(diagnostic.severity));
        QString tooltip = diagnostic.message + QStringLiteral("\n\n") + tr("Sugestão: %1").arg(diagnostic.suggestion);
        if (!diagnostic.relatedLocations.isEmpty()) {
            QStringList related;
            for (const QString& location : diagnostic.relatedLocations) related.push_back(location);
            tooltip += QStringLiteral("\n\n") + tr("Relacionados: %1").arg(related.join(QStringLiteral(", ")));
        }
        for (int column = 0; column < m_tree->columnCount(); ++column) item->setToolTip(column, tooltip);
    }
}

void ValidationPanel::navigateToItem(QTreeWidgetItem* item)
{
    if (!item) return;
    const int index = item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_diagnostics.size()) return;
    emit navigateRequested(m_diagnostics.at(index).target);
}

void ValidationPanel::repairSafeIssues()
{
    QStringList changes;
    const int changed = core::ProjectValidator::repairSafe(m_editor, &changes);
    if (changed > 0) {
        emit projectRepaired();
        refresh();
        QMessageBox::information(this, tr("Validação"), tr("Correções seguras aplicadas: %1.\n\n%2").arg(changed).arg(changes.join(QLatin1Char('\n'))));
    } else {
        QMessageBox::information(this, tr("Validação"), tr("Nenhuma correção automática segura estava disponível."));
    }
}

void ValidationPanel::exportDiagnostics()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar diagnósticos"), QStringLiteral("diagnosticos-ludo.txt"), tr("Texto (*.txt);;JSON (*.json)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Exportar diagnósticos"), tr("Não foi possível escrever o arquivo selecionado."));
        return;
    }
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        file.write(QJsonDocument(core::diagnosticsToJson(m_diagnostics)).toJson(QJsonDocument::Indented));
    else
        file.write(core::diagnosticsToText(m_diagnostics).toUtf8());
}

} // namespace ui
