#include "GlobalSearchDialog.h"

#include "core/Editor.h"

#include <utility>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHash>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {
namespace {
constexpr int RoleMode = Qt::UserRole;
constexpr int RoleKind = Qt::UserRole + 1;
constexpr int RoleSymbolId = Qt::UserRole + 2;
constexpr int RoleOwnerType = Qt::UserRole + 3;
constexpr int RoleOwnerId = Qt::UserRole + 4;
constexpr int RoleMapId = Qt::UserRole + 5;
constexpr int RolePage = Qt::UserRole + 6;
constexpr int RoleCommand = Qt::UserRole + 7;
constexpr int RoleParentId = Qt::UserRole + 8;

core::ReferenceSymbolKind kindFromId(const QString& id)
{
    for (core::ReferenceSymbolKind kind : {
             core::ReferenceSymbolKind::CommonEvent, core::ReferenceSymbolKind::Switch,
             core::ReferenceSymbolKind::Variable, core::ReferenceSymbolKind::String,
             core::ReferenceSymbolKind::Map, core::ReferenceSymbolKind::MapEvent,
             core::ReferenceSymbolKind::Picture, core::ReferenceSymbolKind::Speaker,
             core::ReferenceSymbolKind::CutsceneRegion,
             core::ReferenceSymbolKind::SubtitleTrack, core::ReferenceSymbolKind::CommandTemplate,
             core::ReferenceSymbolKind::CustomDatabase, core::ReferenceSymbolKind::CustomField,
             core::ReferenceSymbolKind::CustomRecord, core::ReferenceSymbolKind::Plugin,
             core::ReferenceSymbolKind::DatabaseRecord, core::ReferenceSymbolKind::Tileset,
             core::ReferenceSymbolKind::FootstepSurface })
        if (core::referenceSymbolKindId(kind) == id) return kind;
    return core::ReferenceSymbolKind::CommonEvent;
}

QString locationLabel(const core::ProjectReferenceLocation& location)
{
    if (location.ownerType == QLatin1String("commonEvent"))
        return QObject::tr("Evento Comum: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("mapEvent")) {
        QString text = QObject::tr("Evento: %1").arg(location.ownerName);
        if (location.pageIndex >= 0) text += QObject::tr(" · Página %1").arg(location.pageIndex + 1);
        return text;
    }
    if (location.ownerType == QLatin1String("plugin"))
        return QObject::tr("Extensão: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("customDatabase"))
        return QObject::tr("Banco: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("map"))
        return QObject::tr("Mapa: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("cutsceneRegion"))
        return QObject::tr("Região de cutscene: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("databaseRecord"))
        return QObject::tr("Banco RPG: %1").arg(location.ownerName);
    if (location.ownerType == QLatin1String("project"))
        return QObject::tr("Projeto: %1").arg(location.ownerName);
    return location.ownerName;
}

bool containsCI(const QString& haystack, const QString& needle)
{
    return needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive);
}
}

GlobalSearchDialog::GlobalSearchDialog(core::Editor& editor, QWidget* parent,
                                       OpenCallback openCallback,
                                       const QString& initialSymbolKind,
                                       const QString& initialSymbolId)
    : QDialog(parent), ed(editor), m_open(std::move(openCallback)),
      m_exactKind(initialSymbolKind), m_exactId(initialSymbolId)
{
    setWindowTitle(tr("Busca Global / Encontrar Usos"));
    resize(1050, 650);
    auto* root = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Pesquise definições, textos de comandos e referências sem depender de números obscuros. "
                               "‘Encontrar usos’ é semântico: segue IDs estáveis e referências do projeto."), this);
    hint->setWordWrap(true); hint->setStyleSheet(QStringLiteral("color:#777;font-size:11px")); root->addWidget(hint);

    auto* top = new QHBoxLayout;
    m_query = new QLineEdit(this); m_query->setClearButtonEnabled(true);
    m_query->setPlaceholderText(tr("Buscar nome, texto, ID, comando ou contexto…"));
    m_scope = new QComboBox(this); m_scope->addItem(tr("Tudo"), QStringLiteral("all"));
    m_scope->addItem(tr("Definições"), QStringLiteral("definitions"));
    m_scope->addItem(tr("Usos"), QStringLiteral("uses"));
    m_scope->addItem(tr("Texto / conteúdo"), QStringLiteral("text"));
    m_scope->addItem(tr("Símbolos não usados"),QStringLiteral("unused"));
    m_scope->addItem(tr("Referências órfãs / ciclos"),QStringLiteral("problems"));
    m_kind = new QComboBox(this); m_kind->addItem(tr("Todos os tipos"), QString());
    for (core::ReferenceSymbolKind kind : {
             core::ReferenceSymbolKind::CommonEvent, core::ReferenceSymbolKind::Switch,
             core::ReferenceSymbolKind::Variable, core::ReferenceSymbolKind::String,
             core::ReferenceSymbolKind::Map, core::ReferenceSymbolKind::MapEvent,
             core::ReferenceSymbolKind::Picture, core::ReferenceSymbolKind::Speaker,
             core::ReferenceSymbolKind::CutsceneRegion,
             core::ReferenceSymbolKind::SubtitleTrack, core::ReferenceSymbolKind::CommandTemplate,
             core::ReferenceSymbolKind::CustomDatabase, core::ReferenceSymbolKind::CustomField,
             core::ReferenceSymbolKind::CustomRecord, core::ReferenceSymbolKind::Plugin,
             core::ReferenceSymbolKind::DatabaseRecord, core::ReferenceSymbolKind::Tileset,
             core::ReferenceSymbolKind::FootstepSurface })
        m_kind->addItem(core::referenceSymbolKindLabel(kind), core::referenceSymbolKindId(kind));
    top->addWidget(new QLabel(tr("Buscar:"), this)); top->addWidget(m_query, 1);
    top->addWidget(m_scope); top->addWidget(m_kind); root->addLayout(top);

    m_results = new QTreeWidget(this); m_results->setColumnCount(4);
    m_results->setHeaderLabels({tr("Tipo"), tr("Símbolo / resultado"), tr("Local"), tr("Detalhe")});
    m_results->setAlternatingRowColors(true); m_results->setRootIsDecorated(false);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_results->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    root->addWidget(m_results, 1);

    auto* actions = new QHBoxLayout;
    m_status = new QLabel(this); actions->addWidget(m_status, 1);
    m_findUses = new QPushButton(tr("Encontrar usos"), this);
    m_rename = new QPushButton(tr("Renomear com segurança…"), this);
    m_renumber = new QPushButton(tr("Renumerar Evento Comum…"), this);
    m_openButton = new QPushButton(tr("Abrir local"), this);
    m_definitionButton=new QPushButton(tr("Ir para definição"),this);
    actions->addWidget(m_findUses); actions->addWidget(m_rename); actions->addWidget(m_renumber); actions->addWidget(m_definitionButton);actions->addWidget(m_openButton);
    root->addLayout(actions);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this); root->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(m_query, &QLineEdit::textChanged, this, [this] { m_exactKind.clear(); m_exactId.clear(); refresh(); });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) { m_exactKind.clear(); m_exactId.clear(); refresh(); });
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int) { m_exactKind.clear(); m_exactId.clear(); refresh(); });
    connect(m_results, &QTreeWidget::itemSelectionChanged, this, &GlobalSearchDialog::updateButtons);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { if (m_openButton->isEnabled()) m_openButton->click(); });

    connect(m_findUses, &QPushButton::clicked, this, [this] {
        core::ReferenceSymbolKind kind; QString id; if (!currentSymbol(&kind, &id)) return; showUses(kind, id);
    });
    connect(m_rename, &QPushButton::clicked, this, [this] {
        core::ReferenceSymbolKind kind; QString id; core::ProjectReferenceSymbol symbol;
        if (!currentSymbol(&kind, &id, &symbol)) return;
        bool ok = false; const QString value = QInputDialog::getText(this, tr("Renomear com segurança"),
            tr("Novo nome para “%1”:\n\nAs referências usam IDs e continuarão válidas.").arg(symbol.qualifiedName),
            QLineEdit::Normal, symbol.name, &ok).trimmed();
        if (!ok || value.isEmpty() || value == symbol.name) return;
        QString error; if (!core::renameProjectSymbol(ed, kind, id, value, &error)) { QMessageBox::warning(this, tr("Renomear"), error); return; }
        refresh();
    });
    connect(m_renumber, &QPushButton::clicked, this, [this] {
        core::ReferenceSymbolKind kind; QString id; core::ProjectReferenceSymbol symbol;
        if (!currentSymbol(&kind, &id, &symbol) || kind != core::ReferenceSymbolKind::CommonEvent) return;
        bool ok=false; const int number=QInputDialog::getInt(this,tr("Renumerar Evento Comum"),
            tr("Novo número para “%1”:\nAs chamadas existentes serão atualizadas e manterão também o ID estável.").arg(symbol.name),
            symbol.number,1,999999,1,&ok); if(!ok||number==symbol.number)return;
        QString error; if(!core::renumberCommonEvent(ed,id,number,&error)){QMessageBox::warning(this,tr("Renumerar"),error);return;} refresh();
    });
    connect(m_openButton, &QPushButton::clicked, this, [this] {
        if (!m_open) return; const core::ProjectReferenceLocation location=currentLocation(); if(!location.ownerType.isEmpty()) m_open(location);
    });
    connect(m_definitionButton,&QPushButton::clicked,this,[this]{
        core::ReferenceSymbolKind kind;QString id;core::ProjectReferenceSymbol symbol;if(!currentSymbol(&kind,&id,&symbol)||!m_open)return;
        core::ProjectReferenceLocation location;location.ownerId=id;location.ownerName=symbol.qualifiedName;location.detail=core::referenceSymbolKindId(kind);
        if(kind==core::ReferenceSymbolKind::CommonEvent)location.ownerType=QStringLiteral("commonEvent");
        else if(kind==core::ReferenceSymbolKind::Map){location.ownerType=QStringLiteral("map");location.mapId=id;}
        else if(kind==core::ReferenceSymbolKind::MapEvent){location.ownerType=QStringLiteral("mapEvent");location.mapId=symbol.parentId;}
        else if(kind==core::ReferenceSymbolKind::CutsceneRegion){location.ownerType=QStringLiteral("cutsceneRegion");location.mapId=symbol.parentId;}
        else if(kind==core::ReferenceSymbolKind::Speaker)location.ownerType=QStringLiteral("speaker");
        else if(kind==core::ReferenceSymbolKind::SubtitleTrack)location.ownerType=QStringLiteral("subtitleTrack");
        else if(kind==core::ReferenceSymbolKind::CommandTemplate)location.ownerType=QStringLiteral("commandTemplate");
        else if(kind==core::ReferenceSymbolKind::Picture)location.ownerType=QStringLiteral("picture");
        else location=currentLocation();if(!location.ownerType.isEmpty())m_open(location);
    });

    if (!m_exactKind.isEmpty() && !m_exactId.isEmpty()) {
        const QSignalBlocker blocker(m_scope);
        m_scope->setCurrentIndex(m_scope->findData(QStringLiteral("uses")));
    }
    refresh();
}

void GlobalSearchDialog::showUses(core::ReferenceSymbolKind kind, const QString& id)
{
    const QString exactKind = core::referenceSymbolKindId(kind);
    {
        const QSignalBlocker queryBlocker(m_query), scopeBlocker(m_scope), kindBlocker(m_kind);
        m_query->clear();
        m_scope->setCurrentIndex(m_scope->findData(QStringLiteral("uses")));
        const int ki = m_kind->findData(exactKind); if (ki >= 0) m_kind->setCurrentIndex(ki);
    }
    m_exactKind = exactKind; m_exactId = id;
    refresh();
}

bool GlobalSearchDialog::currentSymbol(core::ReferenceSymbolKind* kind, QString* id,
                                       core::ProjectReferenceSymbol* definition) const
{
    QTreeWidgetItem* item=m_results->currentItem(); if(!item)return false;
    const QString kindId=item->data(0,RoleKind).toString(); const QString symbolId=item->data(0,RoleSymbolId).toString();
    if(kindId.isEmpty()||symbolId.isEmpty())return false; const core::ReferenceSymbolKind k=kindFromId(kindId);
    if(kind)*kind=k;if(id)*id=symbolId;
    if(definition){for(const auto& symbol:core::projectReferenceSymbols(ed))if(symbol.kind==k&&symbol.id==symbolId){*definition=symbol;return true;}return false;}
    return true;
}

core::ProjectReferenceLocation GlobalSearchDialog::currentLocation() const
{
    core::ProjectReferenceLocation location; QTreeWidgetItem* item=m_results->currentItem(); if(!item)return location;
    location.ownerType=item->data(0,RoleOwnerType).toString(); location.ownerId=item->data(0,RoleOwnerId).toString();
    location.mapId=item->data(0,RoleMapId).toString(); location.pageIndex=item->data(0,RolePage).toInt();
    location.commandIndex=item->data(0,RoleCommand).toInt(); location.ownerName=item->text(2); location.detail=item->text(3);
    if(location.ownerType.isEmpty()&&item->data(0,RoleMode).toString()==QLatin1String("definition")){
        const auto k=kindFromId(item->data(0,RoleKind).toString()); const QString id=item->data(0,RoleSymbolId).toString();
        if(k==core::ReferenceSymbolKind::CommonEvent){location.ownerType=QStringLiteral("commonEvent");location.ownerId=id;}
        else if(k==core::ReferenceSymbolKind::Map){location.ownerType=QStringLiteral("map");location.mapId=id;location.ownerId=id;}
        else if(k==core::ReferenceSymbolKind::MapEvent){location.ownerType=QStringLiteral("mapEvent");location.ownerId=id;location.mapId=item->data(0,RoleParentId).toString();}
        else if(k==core::ReferenceSymbolKind::CutsceneRegion){location.ownerType=QStringLiteral("cutsceneRegion");location.ownerId=id;location.mapId=item->data(0,RoleParentId).toString();}
        else if(k==core::ReferenceSymbolKind::CustomDatabase||k==core::ReferenceSymbolKind::CustomField||k==core::ReferenceSymbolKind::CustomRecord){location.ownerType=QStringLiteral("customDatabase");location.ownerId=(k==core::ReferenceSymbolKind::CustomDatabase?id:item->data(0,RoleParentId).toString());location.detail=core::referenceSymbolKindId(k);}
        else if(k==core::ReferenceSymbolKind::Plugin){location.ownerType=QStringLiteral("plugin");location.ownerId=id;}
        else if(k==core::ReferenceSymbolKind::DatabaseRecord){location.ownerType=QStringLiteral("databaseRecord");location.ownerId=id;location.detail=item->data(0,RoleParentId).toString();}
        else if(k==core::ReferenceSymbolKind::Tileset){location.ownerType=QStringLiteral("tileset");location.ownerId=id;}
        else if(k==core::ReferenceSymbolKind::FootstepSurface){location.ownerType=QStringLiteral("footstepSurface");location.ownerId=id;}
        else if(k==core::ReferenceSymbolKind::Switch||k==core::ReferenceSymbolKind::Variable||k==core::ReferenceSymbolKind::String){location.ownerType=QStringLiteral("gameData");location.ownerId=id;location.detail=core::referenceSymbolKindId(k);}
    }
    return location;
}

void GlobalSearchDialog::refresh()
{
    m_results->clear(); const QString query=m_query->text().trimmed(); const QString scope=m_scope->currentData().toString();
    const QString kindFilter=m_kind->currentData().toString(); const auto symbols=core::projectReferenceSymbols(ed);
    QHash<QString,core::ProjectReferenceSymbol> symbolByKey; for(const auto& s:symbols)symbolByKey.insert(core::referenceSymbolKindId(s.kind)+QLatin1Char('|')+s.id,s);
    int count=0;
    if(scope==QLatin1String("all")||scope==QLatin1String("definitions")){
        for(const auto& symbol:symbols){const QString kindId=core::referenceSymbolKindId(symbol.kind);if(!kindFilter.isEmpty()&&kindId!=kindFilter)continue;if(!m_exactKind.isEmpty()&&(kindId!=m_exactKind||symbol.id!=m_exactId))continue;
            const QString hay=QStringLiteral("%1 %2 %3 %4 %5").arg(core::referenceSymbolKindLabel(symbol.kind),symbol.name,symbol.qualifiedName,symbol.id).arg(symbol.number);
            if(!containsCI(hay,query))continue;auto* item=new QTreeWidgetItem(m_results, QStringList{core::referenceSymbolKindLabel(symbol.kind),symbol.qualifiedName,tr("Definição"),symbol.id});
            item->setData(0,RoleMode,QStringLiteral("definition"));item->setData(0,RoleKind,kindId);item->setData(0,RoleSymbolId,symbol.id);item->setData(0,RoleParentId,symbol.parentId);++count;
        }
    }
    if(scope==QLatin1String("all")||scope==QLatin1String("uses")){
        QSet<QString> seen; for(const auto& usage:core::projectReferenceUsages(ed)){const QString kindId=core::referenceSymbolKindId(usage.kind);if(!kindFilter.isEmpty()&&kindId!=kindFilter)continue;if(!m_exactKind.isEmpty()&&(kindId!=m_exactKind||usage.symbolId!=m_exactId))continue;
            const auto symbol=symbolByKey.value(kindId+QLatin1Char('|')+usage.symbolId);const QString name=symbol.qualifiedName.isEmpty()?usage.symbolId:symbol.qualifiedName;const QString local=locationLabel(usage.location);
            const QString hay=QStringLiteral("%1 %2 %3 %4 %5").arg(core::referenceSymbolKindLabel(usage.kind),name,local,usage.location.detail,usage.symbolId);if(!containsCI(hay,query))continue;
            const QString unique=kindId+QLatin1Char('|')+usage.symbolId+QLatin1Char('|')+usage.location.ownerType+QLatin1Char('|')+usage.location.ownerId+QLatin1Char('|')+QString::number(usage.location.pageIndex)+QLatin1Char('|')+QString::number(usage.location.commandIndex)+QLatin1Char('|')+usage.location.detail;if(seen.contains(unique))continue;seen.insert(unique);
            auto* item=new QTreeWidgetItem(m_results, QStringList{tr("Uso de %1").arg(core::referenceSymbolKindLabel(usage.kind)),name,local,usage.location.detail});item->setData(0,RoleMode,QStringLiteral("usage"));item->setData(0,RoleKind,kindId);item->setData(0,RoleSymbolId,usage.symbolId);item->setData(0,RoleParentId,usage.parentId);item->setData(0,RoleOwnerType,usage.location.ownerType);item->setData(0,RoleOwnerId,usage.location.ownerId);item->setData(0,RoleMapId,usage.location.mapId);item->setData(0,RolePage,usage.location.pageIndex);item->setData(0,RoleCommand,usage.location.commandIndex);++count;
        }
    }
    if((scope==QLatin1String("all")||scope==QLatin1String("text"))&&!query.isEmpty()&&m_exactKind.isEmpty()){
        for(const auto& occurrence:core::projectTextOccurrences(ed)){if(!containsCI(occurrence.text,query))continue;QString preview=occurrence.text;preview.replace(QLatin1Char('\n'),QLatin1Char(' '));if(preview.size()>240)preview=preview.left(237)+QStringLiteral("…");
            auto* item=new QTreeWidgetItem(m_results, QStringList{tr("Texto"),query,locationLabel(occurrence.location),preview});item->setData(0,RoleMode,QStringLiteral("text"));item->setData(0,RoleOwnerType,occurrence.location.ownerType);item->setData(0,RoleOwnerId,occurrence.location.ownerId);item->setData(0,RoleMapId,occurrence.location.mapId);item->setData(0,RolePage,occurrence.location.pageIndex);item->setData(0,RoleCommand,occurrence.location.commandIndex);++count;
        }
    }
    if(scope==QLatin1String("unused"))for(const auto& symbol:core::unusedSymbols(ed)){const QString kindId=core::referenceSymbolKindId(symbol.kind);if(!kindFilter.isEmpty()&&kindId!=kindFilter)continue;const QString hay=QStringLiteral("%1 %2 %3").arg(core::referenceSymbolKindLabel(symbol.kind),symbol.qualifiedName,symbol.id);if(!containsCI(hay,query))continue;auto* item=new QTreeWidgetItem(m_results,{tr("Não usado: %1").arg(core::referenceSymbolKindLabel(symbol.kind)),symbol.qualifiedName,tr("Definição"),symbol.id});item->setData(0,RoleMode,QStringLiteral("definition"));item->setData(0,RoleKind,kindId);item->setData(0,RoleSymbolId,symbol.id);item->setData(0,RoleParentId,symbol.parentId);++count;}
    if(scope==QLatin1String("problems")){for(const auto& usage:core::orphanedReferences(ed)){const QString kindId=core::referenceSymbolKindId(usage.kind);if(!kindFilter.isEmpty()&&kindId!=kindFilter)continue;auto* item=new QTreeWidgetItem(m_results,{tr("Referência órfã"),usage.symbolId,locationLabel(usage.location),usage.location.detail});item->setData(0,RoleMode,QStringLiteral("usage"));item->setData(0,RoleKind,kindId);item->setData(0,RoleSymbolId,usage.symbolId);item->setData(0,RoleOwnerType,usage.location.ownerType);item->setData(0,RoleOwnerId,usage.location.ownerId);item->setData(0,RoleMapId,usage.location.mapId);item->setData(0,RolePage,usage.location.pageIndex);item->setData(0,RoleCommand,usage.location.commandIndex);++count;}for(const QStringList& cycle:core::circularReferences(ed)){auto* item=new QTreeWidgetItem(m_results,{tr("Ciclo"),cycle.join(QStringLiteral(" → ")),tr("Índice semântico"),tr("Referência circular")});Q_UNUSED(item);++count;}}
    m_status->setText(m_exactKind.isEmpty()?tr("Resultados: %1").arg(count):tr("Usos encontrados: %1").arg(count));
    if(m_results->topLevelItemCount()>0)m_results->setCurrentItem(m_results->topLevelItem(0));updateButtons();
}

void GlobalSearchDialog::updateButtons()
{
    core::ReferenceSymbolKind kind;QString id;core::ProjectReferenceSymbol symbol;const bool hasSymbol=currentSymbol(&kind,&id,&symbol);
    m_findUses->setEnabled(hasSymbol);m_rename->setEnabled(hasSymbol);m_renumber->setEnabled(hasSymbol&&kind==core::ReferenceSymbolKind::CommonEvent);m_definitionButton->setEnabled(hasSymbol&&bool(m_open));
    const core::ProjectReferenceLocation location=currentLocation();m_openButton->setEnabled(bool(m_open)&&!location.ownerType.isEmpty());
}

} // namespace ui
