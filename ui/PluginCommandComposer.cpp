#include "PluginCommandComposer.h"

#include "CommandCatalog.h"
#include "core/CommandRegistry.h"

#include <QCheckBox>
#include <QAbstractItemModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace ui {
namespace {
constexpr int TypeRole=Qt::UserRole+1;
constexpr int ParamsRole=Qt::UserRole+2;

QString commandLabel(const QString& type)
{
    for(const CommandCatalogEntry& entry:builtInCommandCatalog())if(entry.type==type)return entry.label;
    return type;
}

QVariant parseDefault(const QString& text,const QString& type)
{
    if(type==QLatin1String("boolean"))return text.compare(QLatin1String("true"),Qt::CaseInsensitive)==0||text==QLatin1String("1");
    if(type==QLatin1String("number")||type==QLatin1String("integer")){bool ok=false;const double value=text.toDouble(&ok);return ok?QVariant(value):QVariant(0);}
    return text;
}
}

PluginCommandComposer::PluginCommandComposer(core::PluginCommand command,QWidget* parent)
    :QDialog(parent),m_command(std::move(command))
{
    setWindowTitle(tr("Compositor visual de comando No-Code"));resize(1180,760);
    auto* root=new QVBoxLayout(this);
    auto* hint=new QLabel(tr("Monte um comando reutilizável com os comandos disponíveis. Use ${campo} nos parâmetros JSON; a prévia abaixo mostra exatamente o que será executado no jogo."),this);hint->setWordWrap(true);root->addWidget(hint);
    auto* meta=new QFormLayout;m_id=new QLineEdit(m_command.id,this);m_name=new QLineEdit(m_command.name,this);m_category=new QLineEdit(m_command.category,this);m_description=new QLineEdit(m_command.description,this);m_icon=new QLineEdit(m_command.iconPath,this);m_shortcut=new QLineEdit(m_command.shortcutKey,this);m_tags=new QLineEdit(m_command.tags.join(QStringLiteral(", ")),this);m_catalog=new QCheckBox(tr("Mostrar no catálogo de comandos"),this);m_catalog->setChecked(m_command.showInCatalog);
    meta->addRow(tr("ID estável:"),m_id);meta->addRow(tr("Nome:"),m_name);meta->addRow(tr("Categoria:"),m_category);meta->addRow(tr("Descrição:"),m_description);meta->addRow(tr("Ícone/asset:"),m_icon);meta->addRow(tr("Atalho:"),m_shortcut);meta->addRow(tr("Tags:"),m_tags);meta->addRow(QString(),m_catalog);root->addLayout(meta);
    auto* split=new QSplitter(Qt::Horizontal,this);
    auto* fieldPane=new QWidget(split);auto* fv=new QVBoxLayout(fieldPane);fv->addWidget(new QLabel(tr("Campos do formulário"),fieldPane));m_fields=new QTableWidget(0,6,fieldPane);m_fields->setHorizontalHeaderLabels({tr("ID"),tr("Rótulo"),tr("Tipo"),tr("Obrigatório"),tr("Padrão"),tr("Opções")});m_fields->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);fv->addWidget(m_fields,1);auto* fieldButtons=new QHBoxLayout;auto* addFieldButton=new QPushButton(tr("Adicionar campo"),fieldPane);auto* removeFieldButton=new QPushButton(tr("Remover"),fieldPane);fieldButtons->addWidget(addFieldButton);fieldButtons->addWidget(removeFieldButton);fv->addLayout(fieldButtons);
    for(const core::PluginField& field:m_command.fields){const int row=m_fields->rowCount();m_fields->insertRow(row);const QStringList values={field.id,field.label,field.type,field.required?QStringLiteral("sim"):QStringLiteral("não"),field.defaultValue.toString(),field.options.join(QStringLiteral("|"))};for(int col=0;col<values.size();++col)m_fields->setItem(row,col,new QTableWidgetItem(values.at(col)));}
    auto* commandPane=new QWidget(split);auto* cv=new QVBoxLayout(commandPane);cv->addWidget(new QLabel(tr("Comandos nativos seguros — arraste para o canvas"),commandPane));m_native=new QListWidget(commandPane);m_native->setDragEnabled(true);m_native->setDragDropMode(QAbstractItemView::DragOnly);for(const core::CommandSchema& schema:core::CommandRegistry::schemas())if(schema.pluginSafe&&schema.catalogPolicy!=core::CommandCatalogPolicy::Legacy){auto* item=new QListWidgetItem(commandLabel(schema.type),m_native);item->setData(TypeRole,schema.type);item->setToolTip(schema.type);}cv->addWidget(m_native,1);cv->addWidget(new QLabel(tr("Canvas / ordem da expansão"),commandPane));m_canvas=new QListWidget(commandPane);m_canvas->setAcceptDrops(true);m_canvas->setDragEnabled(true);m_canvas->setDragDropMode(QAbstractItemView::DragDrop);m_canvas->setDefaultDropAction(Qt::CopyAction);m_canvas->setSelectionMode(QAbstractItemView::SingleSelection);for(const core::EventCommand& event:m_command.commands){auto* item=new QListWidgetItem(commandLabel(event.type),m_canvas);item->setData(TypeRole,event.type);item->setData(ParamsRole,event.params);item->setToolTip(event.type);}cv->addWidget(m_canvas,1);auto* cb=new QHBoxLayout;auto* add=new QPushButton(tr("Adicionar →"),commandPane);auto* remove=new QPushButton(tr("Remover"),commandPane);auto* up=new QPushButton(tr("Subir"),commandPane);auto* down=new QPushButton(tr("Descer"),commandPane);cb->addWidget(add);cb->addWidget(remove);cb->addWidget(up);cb->addWidget(down);cv->addLayout(cb);
    auto* previewPane=new QWidget(split);auto* pv=new QVBoxLayout(previewPane);pv->addWidget(new QLabel(tr("Prévia do comando"),previewPane));m_preview=new QTextBrowser(previewPane);pv->addWidget(m_preview,1);split->addWidget(fieldPane);split->addWidget(commandPane);split->addWidget(previewPane);split->setStretchFactor(1,2);split->setStretchFactor(2,2);root->addWidget(split,1);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&PluginCommandComposer::accept);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(addFieldButton,&QPushButton::clicked,this,&PluginCommandComposer::addField);connect(removeFieldButton,&QPushButton::clicked,this,[this]{const int row=m_fields->currentRow();if(row>=0)m_fields->removeRow(row);refreshPreview();});
    connect(add,&QPushButton::clicked,this,[this]{auto* source=m_native->currentItem();if(!source)return;auto* item=new QListWidgetItem(source->text(),m_canvas);item->setData(TypeRole,source->data(TypeRole));item->setData(ParamsRole,QVariantMap{});item->setToolTip(source->toolTip());refreshPreview();});
    connect(remove,&QPushButton::clicked,this,[this]{delete m_canvas->takeItem(m_canvas->currentRow());refreshPreview();});
    connect(up,&QPushButton::clicked,this,[this]{const int row=m_canvas->currentRow();if(row>0){auto* item=m_canvas->takeItem(row);m_canvas->insertItem(row-1,item);m_canvas->setCurrentRow(row-1);refreshPreview();}});connect(down,&QPushButton::clicked,this,[this]{const int row=m_canvas->currentRow();if(row>=0&&row+1<m_canvas->count()){auto* item=m_canvas->takeItem(row);m_canvas->insertItem(row+1,item);m_canvas->setCurrentRow(row+1);refreshPreview();}});
    connect(m_canvas,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){editCommandParams(m_canvas->row(item));});connect(m_canvas->model(),&QAbstractItemModel::rowsInserted,this,[this]{refreshPreview();});connect(m_fields,&QTableWidget::itemChanged,this,[this]{refreshPreview();});refreshPreview();
}

void PluginCommandComposer::addField()
{
    bool ok=false;const QString id=QInputDialog::getText(this,tr("Novo campo"),tr("ID do campo:"),QLineEdit::Normal,QString(),&ok).trimmed();if(!ok||id.isEmpty())return;
    const QStringList types={QStringLiteral("text"),QStringLiteral("number"),QStringLiteral("boolean"),QStringLiteral("choice"),QStringLiteral("asset"),QStringLiteral("color"),QStringLiteral("expression")};const QString type=QInputDialog::getItem(this,tr("Tipo do campo"),tr("Tipo:"),types,0,false,&ok);if(!ok)return;
    const int row=m_fields->rowCount();m_fields->insertRow(row);const QStringList values={id,id,type,QStringLiteral("sim"),type==QLatin1String("boolean")?QStringLiteral("false"):QString(),QString()};for(int col=0;col<values.size();++col)m_fields->setItem(row,col,new QTableWidgetItem(values.at(col)));m_fields->setCurrentCell(row,1);refreshPreview();
}

void PluginCommandComposer::editCommandParams(int row)
{
    if(row<0||row>=m_canvas->count())return;auto* item=m_canvas->item(row);const QByteArray current=QJsonDocument(QJsonObject::fromVariantMap(item->data(ParamsRole).toMap())).toJson(QJsonDocument::Indented);bool ok=false;const QString text=QInputDialog::getMultiLineText(this,tr("Parâmetros do comando"),tr("Objeto JSON. Placeholders: ${campo}"),QString::fromUtf8(current),&ok);if(!ok)return;QJsonParseError parse;const QJsonDocument doc=QJsonDocument::fromJson(text.toUtf8(),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject()){QMessageBox::warning(this,tr("JSON inválido"),parse.errorString());return;}item->setData(ParamsRole,doc.object().toVariantMap());refreshPreview();
}

void PluginCommandComposer::syncFromUi()
{
    m_command.id=m_id->text().trimmed();m_command.name=m_name->text().trimmed();m_command.category=m_category->text().trimmed();m_command.description=m_description->text().trimmed();m_command.iconPath=m_icon->text().trimmed();m_command.shortcutKey=m_shortcut->text().trimmed();m_command.tags=m_tags->text().split(QLatin1Char(','),Qt::SkipEmptyParts);for(QString& tag:m_command.tags)tag=tag.trimmed();m_command.tags.removeDuplicates();m_command.showInCatalog=m_catalog->isChecked();m_command.fields.clear();
    for(int row=0;row<m_fields->rowCount();++row){const auto value=[&](int col){auto* item=m_fields->item(row,col);return item?item->text().trimmed():QString();};core::PluginField field;field.id=value(0);field.label=value(1);field.type=value(2);field.required=value(3).compare(QStringLiteral("não"),Qt::CaseInsensitive)!=0&&value(3)!=QLatin1String("0");field.defaultValue=parseDefault(value(4),field.type);field.options=value(5).split(QLatin1Char('|'),Qt::SkipEmptyParts);m_command.fields.push_back(field);}
    m_command.commands.clear();for(int row=0;row<m_canvas->count();++row){auto* item=m_canvas->item(row);core::EventCommand event;event.type=item->data(TypeRole).toString();if(event.type.isEmpty())event.type=item->toolTip();event.params=item->data(ParamsRole).toMap();m_command.commands.push_back(event);}
}

void PluginCommandComposer::refreshPreview()
{
    syncFromUi();QVariantMap arguments;for(const core::PluginField& field:m_command.fields)arguments[field.id]=field.defaultValue;const QVector<core::EventCommand> expanded=core::expandPluginCommand(m_command,arguments);QString html;int index=1;for(const core::EventCommand& event:expanded){const QString json=QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(event.params)).toJson(QJsonDocument::Compact)).toHtmlEscaped();html+=QStringLiteral("<p><b>%1. %2</b><br><code>%3</code></p>").arg(index++).arg(event.type.toHtmlEscaped(),json);}QStringList errors;core::validatePluginCommand(m_command,&errors);if(!errors.isEmpty())html=QStringLiteral("<p style='color:#d66'><b>%1</b><br>%2</p>").arg(tr("Validação"),errors.join(QStringLiteral("<br>")).toHtmlEscaped())+html;m_preview->setHtml(html);
}

void PluginCommandComposer::accept()
{
    syncFromUi();QStringList errors;if(!core::validatePluginCommand(m_command,&errors)){QMessageBox::warning(this,tr("Comando incompleto"),errors.join(QLatin1Char('\n')));return;}QDialog::accept();
}

} // namespace ui
