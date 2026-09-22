#include "game/debug/LiveEventInspector.h"

#include "game/GameSession.h"
#include "core/GameValueRegistry.h"

#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace game {
namespace {

QVariant typedInput(const QString& text)
{
    const QString value=text.trimmed();
    if(value.compare(QLatin1String("true"),Qt::CaseInsensitive)==0||value.compare(QLatin1String("on"),Qt::CaseInsensitive)==0)return true;
    if(value.compare(QLatin1String("false"),Qt::CaseInsensitive)==0||value.compare(QLatin1String("off"),Qt::CaseInsensitive)==0)return false;
    bool integer=false;const qlonglong number=value.toLongLong(&integer);if(integer)return number;
    bool real=false;const double decimal=value.toDouble(&real);if(real)return decimal;
    return value;
}

QString displayValue(const QVariant& value)
{
    if(!value.isValid()||value.isNull())return QObject::tr("(indisponível)");
    if(value.metaType().id()==QMetaType::Bool)return value.toBool()?QObject::tr("ON"):QObject::tr("OFF");
    return value.toString();
}

}

LiveEventInspector::LiveEventInspector(GameSession& session, QWidget* parent)
    : QDockWidget(tr("Inspetor de eventos ao vivo"), parent), m_session(session)
{
    setObjectName(QStringLiteral("LiveEventInspector"));
    setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea|Qt::BottomDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable|QDockWidget::DockWidgetClosable);
    auto* page=new QWidget(this);auto* root=new QVBoxLayout(page);root->setContentsMargins(6,6,6,6);
    m_current=new QLabel(page);m_current->setWordWrap(true);root->addWidget(m_current);
    m_runtime=new QLabel(page);m_runtime->setWordWrap(true);m_runtime->setTextInteractionFlags(Qt::TextSelectableByMouse);root->addWidget(m_runtime);
    auto* executionButtons=new QHBoxLayout;auto* pause=new QPushButton(tr("Pausar"),page);auto* continueRun=new QPushButton(tr("Continuar"),page);auto* stepInto=new QPushButton(tr("Entrar"),page);auto* stepOver=new QPushButton(tr("Avançar"),page);auto* stepOut=new QPushButton(tr("Sair da chamada"),page);
    executionButtons->addWidget(pause);executionButtons->addWidget(continueRun);executionButtons->addWidget(stepInto);executionButtons->addWidget(stepOver);executionButtons->addWidget(stepOut);executionButtons->addStretch(1);root->addLayout(executionButtons);

    m_stack=new QTableWidget(page);m_stack->setColumnCount(4);m_stack->setHorizontalHeaderLabels({tr("Nível"),tr("Origem"),tr("Evento"),tr("Comando")});
    m_stack->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);m_stack->setMaximumHeight(150);m_stack->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(m_stack);

    auto* watchButtons=new QHBoxLayout;auto* add=new QPushButton(tr("+ Acompanhar valor"),page);auto* remove=new QPushButton(tr("Remover"),page);
    auto* changed=new QPushButton(tr("Pausar ao mudar"),page);auto* condition=new QPushButton(tr("Condição…"),page);
    watchButtons->addWidget(add);watchButtons->addWidget(remove);watchButtons->addWidget(changed);watchButtons->addWidget(condition);watchButtons->addStretch(1);root->addLayout(watchButtons);
    m_watches=new QTableWidget(page);m_watches->setColumnCount(6);m_watches->setHorizontalHeaderLabels({tr("Tipo"),tr("Nome / referência"),tr("Valor"),tr("Alterado"),tr("Pausa"),tr("Condição")});
    m_watches->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);m_watches->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    m_watches->setSelectionBehavior(QAbstractItemView::SelectRows);m_watches->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(m_watches,1);

    auto* filterRow=new QHBoxLayout;m_filter=new QLineEdit(page);m_filter->setClearButtonEnabled(true);m_filter->setPlaceholderText(tr("Filtrar rastro por fase, origem, evento ou comando…"));
    auto* exportText=new QPushButton(tr("Salvar TXT"),page);auto* exportJson=new QPushButton(tr("Salvar JSON"),page);filterRow->addWidget(m_filter,1);filterRow->addWidget(exportText);filterRow->addWidget(exportJson);root->addLayout(filterRow);
    m_trace=new QTableWidget(page);m_trace->setColumnCount(6);m_trace->setHorizontalHeaderLabels({tr("#"),tr("Fase"),tr("Origem"),tr("Evento"),tr("Comando"),tr("Detalhes")});
    m_trace->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);m_trace->horizontalHeader()->setSectionResizeMode(5,QHeaderView::Stretch);m_trace->setEditTriggers(QAbstractItemView::NoEditTriggers);root->addWidget(m_trace,2);
    auto* playbackRow=new QHBoxLayout;m_playbackLabel=new QLabel(tr("Navegação: nenhum registro"),page);m_playback=new QSlider(Qt::Horizontal,page);m_playback->setRange(0,0);playbackRow->addWidget(m_playbackLabel);playbackRow->addWidget(m_playback,1);root->addLayout(playbackRow);
    setWidget(page);

    connect(add,&QPushButton::clicked,this,&LiveEventInspector::addWatch);connect(remove,&QPushButton::clicked,this,&LiveEventInspector::removeWatch);
    connect(pause,&QPushButton::clicked,this,[this]{m_session.debugPause();refresh();});connect(continueRun,&QPushButton::clicked,this,[this]{m_session.debugContinue();refresh();});
    connect(stepInto,&QPushButton::clicked,this,[this]{m_session.debugPumpOneCommand();refresh();});connect(stepOver,&QPushButton::clicked,this,[this]{m_session.debugPumpStepOver();refresh();});connect(stepOut,&QPushButton::clicked,this,[this]{m_session.debugPumpStepOut();refresh();});
    connect(changed,&QPushButton::clicked,this,&LiveEventInspector::toggleBreakOnChange);connect(condition,&QPushButton::clicked,this,&LiveEventInspector::configureWatchCondition);
    connect(exportText,&QPushButton::clicked,this,[this]{exportTrace(false);});connect(exportJson,&QPushButton::clicked,this,[this]{exportTrace(true);});
    connect(m_filter,&QLineEdit::textChanged,this,&LiveEventInspector::refresh);
    connect(m_playback,&QSlider::valueChanged,this,[this](int value){if(value>=0&&value<m_trace->rowCount()){m_trace->selectRow(value);m_trace->scrollToItem(m_trace->item(value,0));m_playbackLabel->setText(tr("Navegação: %1 / %2").arg(value+1).arg(m_trace->rowCount()));}});
    m_timer=new QTimer(this);m_timer->setInterval(180);connect(m_timer,&QTimer::timeout,this,&LiveEventInspector::refresh);m_timer->start();refresh();
}

quint64 LiveEventInspector::selectedWatchSerial() const
{
    const int row=m_watches->currentRow();if(row<0||!m_watches->item(row,0))return 0;return m_watches->item(row,0)->data(Qt::UserRole).toULongLong();
}

void LiveEventInspector::addWatch()
{
    bool ok=false;const QStringList kinds={tr("Variável"),tr("Switch"),tr("String"),tr("Valor do jogo")};
    const QString chosen=QInputDialog::getItem(this,tr("Acompanhar valor"),tr("Tipo:"),kinds,0,false,&ok);if(!ok)return;
    game::DebugWatchKind kind=game::DebugWatchKind::Variable;QStringList labels,refs;
    const core::Editor& editor=m_session.editor();
    if(chosen==kinds.at(0)){for(const auto& item:editor.variables){labels<<QStringLiteral("%1 — %2").arg(item.id).arg(item.name);refs<<QString::number(item.id);}}
    else if(chosen==kinds.at(1)){kind=game::DebugWatchKind::Switch;for(const auto& item:editor.switches){labels<<QStringLiteral("%1 — %2").arg(item.id).arg(item.name);refs<<QString::number(item.id);}}
    else if(chosen==kinds.at(2)){kind=game::DebugWatchKind::String;for(const auto& item:editor.strings){labels<<QStringLiteral("%1 — %2").arg(item.id).arg(item.name);refs<<QString::number(item.id);}}
    else {kind=game::DebugWatchKind::GameValue;for(const auto& item:core::gameValueDescriptors()){labels<<QStringLiteral("%1 — %2").arg(item.label,item.key);refs<<item.key;}}
    if(labels.isEmpty())return;const QString selected=QInputDialog::getItem(this,tr("Acompanhar valor"),tr("Valor a acompanhar:"),labels,0,false,&ok);if(!ok)return;
    const int index=labels.indexOf(selected);if(index<0)return;m_session.addDebugWatch(kind,refs.at(index),selected);refresh();
}

void LiveEventInspector::removeWatch(){const quint64 serial=selectedWatchSerial();if(serial)m_session.removeDebugWatch(serial);refresh();}

void LiveEventInspector::toggleBreakOnChange()
{
    const quint64 serial=selectedWatchSerial();if(!serial)return;for(const auto& watch:m_session.debugWatches())if(watch.serial==serial){m_session.setDebugWatchBreakOnChange(serial,!watch.breakOnChange);break;}refresh();
}

void LiveEventInspector::configureWatchCondition()
{
    const quint64 serial=selectedWatchSerial();if(!serial)return;bool ok=false;const QStringList ops={QStringLiteral("=="),QStringLiteral("!="),QStringLiteral(">"),QStringLiteral(">="),QStringLiteral("<"),QStringLiteral("<="),QStringLiteral("contains")};
    const QString op=QInputDialog::getItem(this,tr("Condição de pausa"),tr("Pausar quando:"),ops,0,false,&ok);if(!ok)return;
    const QString value=QInputDialog::getText(this,tr("Condição de pausa"),tr("Valor de comparação:"),QLineEdit::Normal,QString(),&ok);if(!ok)return;
    m_session.setDebugWatchCondition(serial,op,typedInput(value));refresh();
}

void LiveEventInspector::exportTrace(bool json)
{
    const QString path=QFileDialog::getSaveFileName(this,json?tr("Salvar rastro em JSON"):tr("Salvar rastro em texto"),json?QStringLiteral("ludo-event-trace.json"):QStringLiteral("ludo-event-trace.txt"),json?tr("JSON (*.json)"):tr("Texto (*.txt)"));if(path.isEmpty())return;
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return;
    file.write(json?QJsonDocument(m_session.debugTraceJson(m_filter->text())).toJson(QJsonDocument::Indented):m_session.debugTraceText(m_filter->text()).toUtf8());
}

void LiveEventInspector::refresh()
{
    const game::EventRuntimeDebugState runtime=m_session.eventRuntimeDebugState();const auto& loc=runtime.current;
    m_current->setText(tr("<b>%1</b> · %2 · comando <b>%3</b> #%4 · pilha %5").arg(m_session.debugPaused()?tr("PAUSADO"):tr("EXECUTANDO"),loc.source.isEmpty()?tr("sem fonte"):loc.source,loc.commandType.isEmpty()?tr("nenhum"):loc.commandType).arg(loc.commandIndex+1).arg(loc.callDepth));
    QStringList tickets;for(quint64 ticket:runtime.parallelTickets)tickets<<QString::number(ticket);
    m_runtime->setText(tr("Nível da cutscene: <b>%1</b> · Espera atual: <b>%2</b> · Motivo de cancelamento: <b>%3</b><br>Execuções paralelas: %4<br>Último alvo resolvido: %5<br>Última condição avaliada: %6")
        .arg(runtime.cutsceneDepth).arg(runtime.awaitableKind,runtime.cancellationReason,tickets.isEmpty()?tr("nenhum"):tickets.join(QStringLiteral(", ")),runtime.targetResolution.isEmpty()?tr("—"):runtime.targetResolution,runtime.conditionEvaluation.isEmpty()?tr("—"):runtime.conditionEvaluation));
    const auto stack=m_session.debugCallStack();m_stack->setRowCount(stack.size());for(int i=0;i<stack.size();++i){const auto& frame=stack.at(i);m_stack->setItem(i,0,new QTableWidgetItem(QString::number(i+1)));m_stack->setItem(i,1,new QTableWidgetItem(frame.source));m_stack->setItem(i,2,new QTableWidgetItem(frame.eventId));m_stack->setItem(i,3,new QTableWidgetItem(QString::number(frame.commandIndex+1)));}
    const auto watches=m_session.debugWatchSamples();m_watches->setRowCount(watches.size());for(int i=0;i<watches.size();++i){const auto& sample=watches.at(i);auto* kind=new QTableWidgetItem(game::debugWatchKindLabel(sample.watch.kind));kind->setData(Qt::UserRole,QVariant::fromValue(sample.watch.serial));m_watches->setItem(i,0,kind);m_watches->setItem(i,1,new QTableWidgetItem(sample.watch.label.isEmpty()?sample.watch.reference:sample.watch.label));auto* value=new QTableWidgetItem(displayValue(sample.value));if(sample.changed)value->setBackground(QColor(QStringLiteral("#f6d365")));m_watches->setItem(i,2,value);m_watches->setItem(i,3,new QTableWidgetItem(sample.changed?tr("SIM"):tr("não")));m_watches->setItem(i,4,new QTableWidgetItem(sample.watch.breakOnChange?tr("ao mudar"):tr("—")));m_watches->setItem(i,5,new QTableWidgetItem(sample.watch.breakOperator.isEmpty()?tr("—"):QStringLiteral("%1 %2").arg(sample.watch.breakOperator,displayValue(sample.watch.breakValue))));}
    const QJsonArray trace=m_session.debugTraceJson(m_filter->text());m_trace->setRowCount(trace.size());for(int i=0;i<trace.size();++i){const QJsonObject item=trace.at(i).toObject();m_trace->setItem(i,0,new QTableWidgetItem(QString::number(item.value(QStringLiteral("sequence")).toInt())));m_trace->setItem(i,1,new QTableWidgetItem(item.value(QStringLiteral("phase")).toString()));m_trace->setItem(i,2,new QTableWidgetItem(item.value(QStringLiteral("source")).toString()));m_trace->setItem(i,3,new QTableWidgetItem(item.value(QStringLiteral("eventId")).toString()));m_trace->setItem(i,4,new QTableWidgetItem(item.value(QStringLiteral("commandType")).toString()));m_trace->setItem(i,5,new QTableWidgetItem(QString::fromUtf8(QJsonDocument(item.value(QStringLiteral("details")).toObject()).toJson(QJsonDocument::Compact))));}
    const QSignalBlocker blocker(m_playback);const int old=m_playback->value();m_playback->setRange(0,qMax(0,m_trace->rowCount()-1));m_playback->setValue(qBound(0,old,m_playback->maximum()));m_playbackLabel->setText(m_trace->rowCount()?tr("Navegação: %1 / %2").arg(m_playback->value()+1).arg(m_trace->rowCount()):tr("Navegação: nenhum registro"));
}

} // namespace game
