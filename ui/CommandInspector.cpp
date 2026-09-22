#include "CommandInspector.h"

#include "CommandCatalog.h"
#include "core/CommandRegistry.h"
#include "core/Editor.h"
#include "core/EventCommandValidator.h"

#include <QApplication>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QShortcut>
#include <QTextBrowser>
#include <QToolBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {
namespace {
constexpr int KindRole=Qt::UserRole+1,IdRole=Qt::UserRole+2,ParentRole=Qt::UserRole+3;

QString commandDescription(const core::Editor& editor,const core::EventCommand& command)
{
    if(command.type==QLatin1String("plugin.call")){const QString pluginId=command.params.value(QStringLiteral("pluginId")).toString(),commandId=command.params.value(QStringLiteral("commandId")).toString();if(const core::NoCodePlugin* plugin=core::noCodePluginById(editor.plugins,pluginId))if(const core::PluginCommand* definition=core::pluginCommandById(*plugin,commandId))return definition->description.isEmpty()?definition->name:definition->description;}
    for(const CommandCatalogEntry& entry:commandCatalogForEditor(editor)){QString type=entry.type;if(type.startsWith(QLatin1String("plugin.call:")))type=QStringLiteral("plugin.call");if(type==command.type)return entry.path.join(QStringLiteral(" › "))+QStringLiteral(" — ")+entry.label;}
    return QObject::tr("Comando reconhecido pela LUDO.");
}
}

CommandInspectionData inspectEventCommand(const core::Editor& editor,const core::EventCommand& command,const core::ProjectReferenceLocation& location)
{
    CommandInspectionData data;data.type=command.type;const core::CommandDescriptor descriptor=core::CommandRegistry::describe(command.type);data.domain=core::commandDomainLabel(descriptor.domain);QStringList lifecycle;if(descriptor.pageActivation)lifecycle<<QObject::tr("Pode ser executado ao ativar a página");if(descriptor.sceneBoundary)lifecycle<<QObject::tr("Pode encerrar ou trocar a cena");if(descriptor.awaitable)lifecycle<<QObject::tr("Pode aguardar conclusão");if(descriptor.skipDuringCutscene)lifecycle<<QObject::tr("Segue as regras de avanço da cutscene");if(lifecycle.isEmpty())lifecycle<<QObject::tr("Executado na ordem normal do evento");data.lifecycle=lifecycle.join(QStringLiteral(" · "));data.description=commandDescription(editor,command);data.paramsJson=QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(command.params)).toJson(QJsonDocument::Indented));data.runtimeMetadata<<QObject::tr("Registrado: %1").arg(descriptor.registered?QObject::tr("sim"):QObject::tr("não"))<<QObject::tr("Pode ser usado por extensões: %1").arg(descriptor.pluginSafe?QObject::tr("sim"):QObject::tr("não"))<<QObject::tr("Altera o estado do jogo: %1").arg(descriptor.stateMutation?QObject::tr("sim"):QObject::tr("não"))<<QObject::tr("Pode aguardar conclusão: %1").arg(descriptor.awaitable?QObject::tr("sim"):QObject::tr("não"));core::ProjectValidationResult validation;core::validateEventCommands(editor,QVector<core::EventCommand>{command},QObject::tr("Inspetor de comando"),validation);data.diagnostics=validation.issues;data.references=core::eventCommandReferences(editor,command,location);return data;
}

CommandInspector::CommandInspector(core::Editor& editor,QWidget* parent):QWidget(parent),m_editor(editor)
{
    auto* root=new QVBoxLayout(this);root->setContentsMargins(4,4,4,4);m_summary=new QLabel(tr("Selecione um comando"),this);m_summary->setWordWrap(true);m_summary->setStyleSheet(QStringLiteral("font-weight:600;padding:4px"));root->addWidget(m_summary);
    auto* sections=new QToolBox(this);m_type=new QTextBrowser(sections);m_params=new QTextBrowser(sections);m_diagnostics=new QTextBrowser(sections);m_runtime=new QTextBrowser(sections);m_references=new QTreeWidget(sections);m_references->setColumnCount(3);m_references->setHeaderLabels({tr("Tipo"),tr("Definição"),tr("ID")});m_references->header()->setSectionResizeMode(1,QHeaderView::Stretch);sections->addItem(m_type,tr("Visão geral"));sections->addItem(m_params,tr("Parâmetros"));sections->addItem(m_diagnostics,tr("Diagnóstico"));sections->addItem(m_references,tr("Referências"));sections->addItem(m_runtime,tr("Informações avançadas"));root->addWidget(sections,1);
    connect(m_references,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem*,int){goToCurrentDefinition();});connect(m_references,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem*,int){if(QApplication::keyboardModifiers().testFlag(Qt::ControlModifier))goToCurrentDefinition();});auto* f12=new QShortcut(QKeySequence(Qt::Key_F12),this);f12->setContext(Qt::WidgetWithChildrenShortcut);connect(f12,&QShortcut::activated,this,[this]{goToCurrentDefinition();});
}

void CommandInspector::setCommand(const core::EventCommand* command,const core::ProjectReferenceLocation& location)
{
    m_references->clear();if(!command){m_summary->setText(tr("Selecione um comando"));m_type->clear();m_params->clear();m_diagnostics->clear();m_runtime->clear();return;}const CommandInspectionData data=inspectEventCommand(m_editor,*command,location);m_summary->setText(QStringLiteral("%1  ·  %2").arg(data.type,data.domain));m_type->setHtml(QStringLiteral("<p><b>Tipo:</b> %1</p><p><b>Área:</b> %2</p><p><b>Execução:</b> %3</p><p><b>Descrição:</b> %4</p>").arg(data.type.toHtmlEscaped(),data.domain.toHtmlEscaped(),data.lifecycle.toHtmlEscaped(),data.description.toHtmlEscaped()));m_params->setPlainText(data.paramsJson);QStringList diagnostics;for(const core::ValidationIssue& issue:data.diagnostics)diagnostics<<QStringLiteral("[%1] %2\n%3").arg(core::validationSeverityLabel(issue.severity),issue.code,issue.message);m_diagnostics->setPlainText(diagnostics.isEmpty()?tr("Nenhum problema encontrado neste comando."):diagnostics.join(QStringLiteral("\n\n")));m_runtime->setPlainText(data.runtimeMetadata.join(QLatin1Char('\n')));
    for(const core::ProjectReferenceUsage& reference:data.references){QString name=reference.symbolId;for(const core::ProjectReferenceSymbol& symbol:core::projectReferenceSymbols(m_editor))if(symbol.kind==reference.kind&&symbol.id==reference.symbolId){name=symbol.qualifiedName;break;}auto* item=new QTreeWidgetItem(m_references,{core::referenceSymbolKindLabel(reference.kind),name,reference.symbolId});item->setData(0,KindRole,core::referenceSymbolKindId(reference.kind));item->setData(0,IdRole,reference.symbolId);item->setData(0,ParentRole,reference.parentId);}
}

bool CommandInspector::goToCurrentDefinition()
{
    QTreeWidgetItem* item=m_references->currentItem();if(!item&&m_references->topLevelItemCount()>0)item=m_references->topLevelItem(0);if(!item)return false;core::ReferenceSymbolKind kind=core::ReferenceSymbolKind::CommonEvent;const QString kindId=item->data(0,KindRole).toString();for(core::ReferenceSymbolKind candidate:{core::ReferenceSymbolKind::CommonEvent,core::ReferenceSymbolKind::Switch,core::ReferenceSymbolKind::Variable,core::ReferenceSymbolKind::String,core::ReferenceSymbolKind::Map,core::ReferenceSymbolKind::MapEvent,core::ReferenceSymbolKind::Picture,core::ReferenceSymbolKind::Speaker,core::ReferenceSymbolKind::CutsceneRegion,core::ReferenceSymbolKind::SubtitleTrack,core::ReferenceSymbolKind::CommandTemplate,core::ReferenceSymbolKind::CustomDatabase,core::ReferenceSymbolKind::CustomField,core::ReferenceSymbolKind::CustomRecord,core::ReferenceSymbolKind::Plugin,core::ReferenceSymbolKind::DatabaseRecord,core::ReferenceSymbolKind::Tileset,core::ReferenceSymbolKind::FootstepSurface})if(core::referenceSymbolKindId(candidate)==kindId){kind=candidate;break;}emit navigateRequested(core::projectDefinitionLocation(m_editor,kind,item->data(0,IdRole).toString(),item->data(0,ParentRole).toString()));return true;
}

} // namespace ui
