#include "GameDebugDialog.h"

#include "GameSession.h"
#include "core/InputMap.h"
#include "game/debug/LiveEventInspector.h"
#include "RuntimeProfilerChart.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QInputDialog>
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace game {

GameDebugDialog::GameDebugDialog(GameSession& session, QWidget* parent)
    : QDialog(parent), m_session(session)
{
    setWindowTitle(tr("Depuração e desempenho — F8"));
    resize(1040, 700);
    auto* layout = new QVBoxLayout(this);
    m_overview = new QLabel(this); m_overview->setWordWrap(true); layout->addWidget(m_overview);
    m_execution = new QLabel(this); m_execution->setWordWrap(true); layout->addWidget(m_execution);

    auto* controls = new QHBoxLayout;
    auto* pause = new QPushButton(tr("Pausar"), this);
    auto* step = new QPushButton(tr("Entrar"), this);
    auto* stepOver = new QPushButton(tr("Avançar"), this);
    auto* stepOut = new QPushButton(tr("Sair da chamada"), this);
    auto* hotReload = new QPushButton(tr("Recarregar alterações"), this);
    auto* continueButton = new QPushButton(tr("Continuar"), this);
    m_breakpoint = new QPushButton(this);
    m_conditionalBreakpoint = new QPushButton(tr("Pausa condicional…"), this);
    controls->addWidget(pause); controls->addWidget(step); controls->addWidget(stepOver); controls->addWidget(stepOut);
    controls->addWidget(continueButton); controls->addWidget(hotReload);
    controls->addWidget(m_breakpoint); controls->addWidget(m_conditionalBreakpoint); controls->addStretch(1);
    layout->addLayout(controls);

    auto* tabs = new QTabWidget(this);
    m_state = new QTableWidget(tabs);
    m_state->setColumnCount(4);
    m_state->setHorizontalHeaderLabels({tr("Tipo"), tr("Nº"), tr("Nome"), tr("Valor")});
    m_state->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_state->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_state, tr("Interruptores, variáveis e textos"));

    m_customDatabases = new QTableWidget(tabs);
    m_customDatabases->setColumnCount(5);
    m_customDatabases->setHorizontalHeaderLabels({tr("Banco"), tr("Modo"), tr("Registro"), tr("Campo"), tr("Valor efetivo")});
    m_customDatabases->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_customDatabases->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_customDatabases->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_customDatabases->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_customDatabases->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_customDatabases, tr("Bancos de dados"));

    m_runtimeMaps = new QTableWidget(tabs);
    m_runtimeMaps->setColumnCount(6);
    m_runtimeMaps->setHorizontalHeaderLabels({tr("Mapa"), tr("Alteração"), tr("Camada / Origem"), tr("X"), tr("Y"), tr("Valor atual")});
    m_runtimeMaps->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_runtimeMaps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_runtimeMaps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_runtimeMaps->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_runtimeMaps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_runtimeMaps, tr("Alterações no mapa"));

    auto* schedulerInputPage = new QWidget(tabs);
    auto* schedulerInputLayout = new QVBoxLayout(schedulerInputPage);
    m_scheduler = new QTableWidget(schedulerInputPage);
    m_scheduler->setColumnCount(7);
    m_scheduler->setHorizontalHeaderLabels({tr("Evento comum"), tr("Gatilho"), tr("Política"), tr("Prioridade"), tr("Ativo"), tr("Pendente"), tr("Frames")});
    m_scheduler->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_scheduler->setEditTriggers(QAbstractItemView::NoEditTriggers);
    schedulerInputLayout->addWidget(new QLabel(tr("Eventos comuns agendados"), schedulerInputPage));
    schedulerInputLayout->addWidget(m_scheduler, 1);
    m_input = new QTableWidget(schedulerInputPage);
    m_input->setColumnCount(5);
    m_input->setHorizontalHeaderLabels({tr("Ação"), tr("Mantida"), tr("Pressionada"), tr("Solta"), tr("Frames")});
    m_input->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_input->setEditTriggers(QAbstractItemView::NoEditTriggers);
    schedulerInputLayout->addWidget(new QLabel(tr("Entradas do jogador"), schedulerInputPage));
    schedulerInputLayout->addWidget(m_input, 1);
    m_inputAnalog = new QLabel(schedulerInputPage); m_inputAnalog->setWordWrap(true); schedulerInputLayout->addWidget(m_inputAnalog);
    tabs->addTab(schedulerInputPage, tr("Agendamentos e controles"));

    m_trace = new QTableWidget(tabs);
    m_trace->setColumnCount(7);
    m_trace->setHorizontalHeaderLabels({tr("#"), tr("Fase"), tr("Fonte"), tr("Evento"), tr("Comando"), tr("Índice"), tr("Pilha")});
    m_trace->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_trace->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_trace, tr("Rastro de eventos"));

    m_callStack = new QTableWidget(tabs);
    m_callStack->setColumnCount(5);
    m_callStack->setHorizontalHeaderLabels({tr("Nível"), tr("Fonte estável"), tr("Evento"), tr("Comando"), tr("Parâmetros / Locais")});
    m_callStack->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_callStack->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_callStack->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(m_callStack, tr("Chamadas e valores locais"));

    auto* routePage = new QWidget(tabs);
    auto* routeLayout = new QVBoxLayout(routePage);
    m_routeOverlay = new QCheckBox(tr("Mostrar caminho A* e estado das rotas sobre o mapa"), routePage);
    m_routeOverlay->setChecked(m_session.moveRouteDebugVisible());
    m_routeOverlay->setToolTip(tr("Disponível somente durante os testes no Editor. O jogo exportado não mostra esta visualização."));
    routeLayout->addWidget(m_routeOverlay);
    m_routes = new QTableWidget(routePage);m_routes->setColumnCount(9);
    m_routes->setHorizontalHeaderLabels({tr("Alvo"),tr("Ticket"),tr("Estado"),tr("Comando"),tr("#"),tr("Fila"),tr("Bloqueios"),tr("Caminho / recálculos"),tr("Falha")});
    m_routes->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_routes->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    m_routes->horizontalHeader()->setSectionResizeMode(7,QHeaderView::Stretch);
    m_routes->setEditTriggers(QAbstractItemView::NoEditTriggers);routeLayout->addWidget(m_routes,1);
    tabs->addTab(routePage,tr("Rotas de Movimento"));

    auto* runtimePage = new QWidget(tabs);
    auto* runtimeLayout = new QVBoxLayout(runtimePage);
    m_uiState = new QLabel(runtimePage); m_uiState->setWordWrap(true); runtimeLayout->addWidget(m_uiState);
    m_commonEvents = new QListWidget(runtimePage); runtimeLayout->addWidget(m_commonEvents, 1);
    tabs->addTab(runtimePage, tr("Eventos comuns e interface"));

    auto* profilerPage = new QWidget(tabs);
    auto* profilerLayout = new QVBoxLayout(profilerPage);
    m_profiler = new QLabel(profilerPage); m_profiler->setWordWrap(true); m_profiler->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_profilerChart = new RuntimeProfilerChart(profilerPage);
    profilerLayout->addWidget(m_profiler);
    profilerLayout->addWidget(m_profilerChart, 1);
    auto* profilerTools = new QHBoxLayout;
    auto* resetProfiler = new QPushButton(tr("Limpar histórico"), profilerPage);
    auto* toggleTrace = new QPushButton(tr("Registrar desempenho"), profilerPage);
    toggleTrace->setCheckable(true);
    auto* exportTrace = new QPushButton(tr("Exportar análise para Perfetto/Chrome…"), profilerPage);
    profilerTools->addWidget(resetProfiler); profilerTools->addWidget(toggleTrace); profilerTools->addWidget(exportTrace); profilerTools->addStretch(1);
    profilerLayout->addLayout(profilerTools);
    connect(resetProfiler, &QPushButton::clicked, this, [this]{ m_profilerChart->clearHistory(); });
    connect(toggleTrace, &QPushButton::toggled, this, [this, toggleTrace](bool enabled){
        m_session.traceRecorder().setEnabled(enabled);
        toggleTrace->setText(enabled ? tr("Parar registro") : tr("Registrar desempenho"));
    });
    connect(exportTrace, &QPushButton::clicked, this, [this]{
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar análise de desempenho"), QStringLiteral("ludo-runtime-trace.json"), tr("Análise JSON (*.json)"));
        if(path.isEmpty()) return; QString error;
        if(!m_session.traceRecorder().writeChromeTrace(path,&error)) QMessageBox::warning(this,tr("Análise de desempenho"),error);
    });
    tabs->addTab(profilerPage, tr("Desempenho"));
    auto* workbench=new QMainWindow(this);workbench->setCentralWidget(tabs);
    m_liveInspector=new LiveEventInspector(m_session,workbench);workbench->addDockWidget(Qt::RightDockWidgetArea,m_liveInspector);
    layout->addWidget(workbench, 1);

    auto* row = new QHBoxLayout;
    auto* clear = new QPushButton(tr("Limpar rastro"), this);
    auto* diagnostic = new QPushButton(tr("Gerar pacote de diagnóstico…"), this);
    auto* update = new QPushButton(tr("Atualizar"), this);
    auto* close = new QPushButton(tr("Fechar"), this);
    row->addWidget(clear); row->addWidget(diagnostic); row->addStretch(1); row->addWidget(update); row->addWidget(close);
    layout->addLayout(row);

    connect(pause, &QPushButton::clicked, this, [this]{ m_session.debugPause(); refresh(); });
    connect(step, &QPushButton::clicked, this, [this]{ m_session.debugPumpOneCommand(); refresh(); });
    connect(stepOver, &QPushButton::clicked, this, [this]{ m_session.debugPumpStepOver(); refresh(); });
    connect(stepOut, &QPushButton::clicked, this, [this]{ m_session.debugPumpStepOut(); refresh(); });
    connect(hotReload, &QPushButton::clicked, this, [this]{
        QStringList diagnostics; const bool ok=m_session.hotReload(&diagnostics);
        refresh();
        if(!ok) QMessageBox::warning(this,tr("Recarregar alterações"),tr("Hot Reload aplicado parcialmente.\n%1").arg(diagnostics.join(QLatin1Char('\n'))));
        else if(!diagnostics.isEmpty()) QMessageBox::information(this,tr("Recarregar alterações"),tr("Hot Reload concluído.\n%1").arg(diagnostics.join(QLatin1Char('\n'))));
    });
    connect(continueButton, &QPushButton::clicked, this, [this]{ m_session.debugContinue(); accept(); });
    connect(m_routeOverlay,&QCheckBox::toggled,this,[this](bool on){m_session.setMoveRouteDebugVisible(on);refresh();});
    connect(m_breakpoint, &QPushButton::clicked, this, [this]{
        const DebugCommandLocation location = m_session.debugLocation();
        if (location.source.isEmpty() || location.commandIndex < 0) return;
        if (m_session.hasDebugBreakpoint(location.source, location.commandIndex))
            m_session.removeDebugBreakpoint(location.source, location.commandIndex);
        else m_session.addDebugBreakpoint(location.source, location.commandIndex);
        refresh();
    });
    connect(m_conditionalBreakpoint,&QPushButton::clicked,this,[this]{
        const DebugCommandLocation location=m_session.debugLocation();if(location.source.isEmpty()||location.commandIndex<0)return;
        const QVector<DebugWatch> watches=m_session.debugWatches();
        if(watches.isEmpty()){QMessageBox::information(this,tr("Pausa condicional"),tr("Adicione primeiro um valor para acompanhar no Inspetor de eventos."));return;}
        QStringList labels;for(const DebugWatch& watch:watches)labels<<QStringLiteral("%1 — %2").arg(debugWatchKindLabel(watch.kind),watch.label.isEmpty()?watch.reference:watch.label);
        bool ok=false;const QString selected=QInputDialog::getItem(this,tr("Pausa condicional"),tr("Valor acompanhado:"),labels,0,false,&ok);if(!ok)return;const int selectedIndex=labels.indexOf(selected);if(selectedIndex<0)return;
        const QStringList ops={QStringLiteral("=="),QStringLiteral("!="),QStringLiteral(">"),QStringLiteral(">="),QStringLiteral("<"),QStringLiteral("<="),QStringLiteral("contains")};
        const QString op=QInputDialog::getItem(this,tr("Pausa condicional"),tr("Operador:"),ops,0,false,&ok);if(!ok)return;
        const QString expected=QInputDialog::getText(this,tr("Pausa condicional"),tr("Valor esperado:"),QLineEdit::Normal,QString(),&ok);if(!ok)return;
        QVariant value=expected;bool numberOk=false;const qlonglong number=expected.toLongLong(&numberOk);if(numberOk)value=number;else if(expected.compare(QLatin1String("true"),Qt::CaseInsensitive)==0||expected.compare(QLatin1String("on"),Qt::CaseInsensitive)==0)value=true;else if(expected.compare(QLatin1String("false"),Qt::CaseInsensitive)==0||expected.compare(QLatin1String("off"),Qt::CaseInsensitive)==0)value=false;
        m_session.setDebugBreakpointCondition(location.source,location.commandIndex,{{QStringLiteral("watchSerial"),QVariant::fromValue(watches.at(selectedIndex).serial)},{QStringLiteral("op"),op},{QStringLiteral("value"),value}});refresh();
    });
    connect(clear, &QPushButton::clicked, this, [this]{ m_session.clearDebugTrace(); refresh(); });
    connect(diagnostic, &QPushButton::clicked, this, [this]{
        const QString path = QFileDialog::getSaveFileName(this, tr("Salvar pacote de diagnóstico"),
                                                          QStringLiteral("ludo-diagnostic.zip"), tr("ZIP (*.zip)"));
        if (path.isEmpty()) return;
        QString error;
        if (!m_session.writeDiagnosticBundle(path, &error)) QMessageBox::warning(this, tr("Diagnóstico"), error);
        else QMessageBox::information(this, tr("Diagnóstico"), tr("Pacote de diagnóstico criado com sucesso."));
    });
    connect(update, &QPushButton::clicked, this, &GameDebugDialog::refresh);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto* liveRefresh = new QTimer(this);
    liveRefresh->setInterval(250);
    connect(liveRefresh, &QTimer::timeout, this, &GameDebugDialog::refresh);
    liveRefresh->start();
    refresh();
}

void GameDebugDialog::updateBreakpointButton()
{
    const DebugCommandLocation location = m_session.debugLocation();
    const bool valid = !location.source.isEmpty() && location.commandIndex >= 0;
    m_breakpoint->setEnabled(valid);
    m_breakpoint->setText(valid && m_session.hasDebugBreakpoint(location.source, location.commandIndex)
                              ? tr("Remover ponto de pausa") : tr("Adicionar ponto de pausa"));
    m_conditionalBreakpoint->setEnabled(valid);
    m_conditionalBreakpoint->setText(valid&&!m_session.debugBreakpointCondition(location.source,location.commandIndex).isEmpty()?tr("Editar pausa condicional…"):tr("Pausa condicional…"));
}

void GameDebugDialog::refresh()
{
    const core::Editor& editor = m_session.editor();
    const QPoint cell = m_session.world().playerCell();
    const core::MapDoc* map = editor.doc();
    const auto& filters=m_session.filterSystem();
    auto scopeText=[](const core::FilterScopeConfig& scope){QStringList parts;if(scope.world)parts<<QStringLiteral("Mapa");if(scope.pictures)parts<<QStringLiteral("Imagens");if(scope.hud)parts<<QStringLiteral("HUD");return parts.join(QLatin1Char('+'));};
    QStringList activeFilters;
    const auto chromatic=filters.chromaticAberration();if(chromatic.active())activeFilters<<tr("Aberração %1 %2 px [%3]").arg(core::chromaticAberrationModeLabel(chromatic.mode)).arg(chromatic.intensityPixels,0,'f',1).arg(scopeText(chromatic.scope));
    const auto noise=filters.noise();if(noise.active())activeFilters<<tr("Ruído %1% / %2 px [%3]").arg(int(noise.intensity*100)).arg(noise.grainSizePixels,0,'f',1).arg(scopeText(noise.scope));
    const auto scanlines=filters.scanlines();if(scanlines.active())activeFilters<<tr("Linhas de tela %1% / %2 px%3 / pausa %4 quadros [%5]").arg(int(scanlines.intensity*100)).arg(scanlines.spacingPixels,0,'f',1).arg(scanlines.whiteSweep?tr(" + faixa branca"):QString()).arg(int(scanlines.sweepDelaySeconds*60.0)).arg(scopeText(scanlines.scope));
    const auto vignette=filters.vignette();if(vignette.active())activeFilters<<tr("Vinheta %1% [%2]").arg(int(vignette.intensity*100)).arg(scopeText(vignette.scope));
    const auto blur=filters.blur();if(blur.active())activeFilters<<tr("Desfoque %1 %2 px [%3]").arg(core::blurDirectionLabel(blur.direction)).arg(blur.radiusPixels,0,'f',1).arg(scopeText(blur.scope));
    const auto tilt=filters.tiltShift();if(tilt.active())activeFilters<<tr("Foco seletivo %1 px / faixa %2% [%3]").arg(tilt.blurPixels,0,'f',1).arg(int(tilt.focusWidth*100)).arg(scopeText(tilt.scope));
    if(!activeFilters.isEmpty())activeFilters<<tr("camadas de efeito: %1").arg(filters.activeSlotDescriptions().join(QStringLiteral(", ")));
    const QString filterText=activeFilters.isEmpty()?tr("desligados"):activeFilters.join(QStringLiteral(" · "));
    m_overview->setText(tr("Mapa: <b>%1</b> · Jogador: <b>(%2, %3)</b> · UI: <b>%4</b> · Foco: <b>%5</b> · Filtros: <b>%6</b>")
                            .arg(map ? map->name : tr("(nenhum)"))
                            .arg(cell.x()).arg(cell.y()).arg(m_session.debugUiScreen(),
                                                           m_session.debugUiFocus().isEmpty() ? tr("(nenhum)") : m_session.debugUiFocus(),filterText));
    const DebugCommandLocation loc = m_session.debugLocation();
    const EventRuntimeDebugState runtime=m_session.eventRuntimeDebugState();
    m_execution->setText(tr("Execução: <b>%1</b> · Origem: %2 · Comando: <b>%3</b> #%4 · Nível de chamada: %5 · Cutscene: %6 · Espera atual: %7")
                             .arg(m_session.debugPaused() ? (m_session.debugBlocked() ? tr("PAUSADO no comando") : tr("PAUSADO")) : tr("EXECUTANDO"),
                                  loc.source.isEmpty() ? tr("(nenhuma)") : loc.source,
                                  loc.commandType.isEmpty() ? tr("(nenhum)") : loc.commandType)
                             .arg(loc.commandIndex + 1).arg(loc.callDepth).arg(runtime.cutsceneDepth).arg(runtime.awaitableKind));
    updateBreakpointButton();
    if(m_liveInspector)m_liveInspector->refresh();

    const QVector<Interpreter::DebugFrameInfo> callStack=m_session.debugCallStack();
    m_callStack->setRowCount(callStack.size());
    for(int i=0;i<callStack.size();++i){
        const auto& frame=callStack.at(i);QStringList values;QStringList keys=frame.values.keys();keys.sort(Qt::CaseInsensitive);
        for(const QString& key:keys)values.push_back(QStringLiteral("%1=%2").arg(key,frame.values.value(key).toString()));
        m_callStack->setItem(i,0,new QTableWidgetItem(QString::number(i+1)));
        m_callStack->setItem(i,1,new QTableWidgetItem(frame.source));
        m_callStack->setItem(i,2,new QTableWidgetItem(frame.commonFrame?tr("Common %1").arg(frame.commonEventNumber):frame.eventId));
        m_callStack->setItem(i,3,new QTableWidgetItem(QString::number(frame.commandIndex+1)));
        m_callStack->setItem(i,4,new QTableWidgetItem(values.join(QStringLiteral(" · "))));
    }

    m_state->setRowCount(editor.switches.size() + editor.variables.size() + editor.strings.size());
    int row = 0;
    for (const core::SwitchDef& item : editor.switches) {
        m_state->setItem(row,0,new QTableWidgetItem(tr("Switch"))); m_state->setItem(row,1,new QTableWidgetItem(QString::number(item.id)));
        m_state->setItem(row,2,new QTableWidgetItem(item.name)); m_state->setItem(row,3,new QTableWidgetItem(m_session.state().switchOn(item.id)?tr("ON"):tr("OFF"))); ++row;
    }
    for (const core::VariableDef& item : editor.variables) {
        m_state->setItem(row,0,new QTableWidgetItem(tr("Variável"))); m_state->setItem(row,1,new QTableWidgetItem(QString::number(item.id)));
        m_state->setItem(row,2,new QTableWidgetItem(item.name)); m_state->setItem(row,3,new QTableWidgetItem(QString::number(m_session.state().variable(item.id)))); ++row;
    }
    for (const core::StringDef& item : editor.strings) {
        m_state->setItem(row,0,new QTableWidgetItem(tr("String"))); m_state->setItem(row,1,new QTableWidgetItem(QString::number(item.id)));
        m_state->setItem(row,2,new QTableWidgetItem(item.name)); m_state->setItem(row,3,new QTableWidgetItem(m_session.state().stringValue(item.id))); ++row;
    }

    int customRows = 0;
    for (const core::CustomDatabaseDefinition& database : editor.customDatabases)
        customRows += database.records.size() * database.fields.size();
    m_customDatabases->setRowCount(customRows);
    int customRow = 0;
    for (const core::CustomDatabaseDefinition& database : editor.customDatabases) {
        for (const core::CustomDatabaseRecord& record : database.records) {
            for (const core::CustomDatabaseField& field : database.fields) {
                QVariant value = m_session.state().customDatabaseValue(editor, database.id, record.id, field.id);
                QString rendered;
                if (field.type == core::CustomDatabaseFieldType::Boolean) rendered = value.toBool() ? tr("Ligado") : tr("Desligado");
                else if (field.type == core::CustomDatabaseFieldType::RecordReference) {
                    rendered = value.toString();
                    if (const core::CustomDatabaseDefinition* targetDb = editor.customDatabase(field.referenceDatabaseId))
                        if (const core::CustomDatabaseRecord* target = core::customDatabaseRecordById(*targetDb, rendered))
                            rendered = QStringLiteral("%1 [%2]").arg(target->name.isEmpty()?tr("Registro %1").arg(target->number):target->name, target->id);
                } else rendered = value.toString();
                m_customDatabases->setItem(customRow,0,new QTableWidgetItem(database.name.isEmpty()?database.id:database.name));
                m_customDatabases->setItem(customRow,1,new QTableWidgetItem(core::customDatabaseModeLabel(database.mode)));
                m_customDatabases->setItem(customRow,2,new QTableWidgetItem(record.name.isEmpty()?tr("Registro %1").arg(record.number):record.name));
                m_customDatabases->setItem(customRow,3,new QTableWidgetItem(field.name.isEmpty()?field.id:field.name));
                m_customDatabases->setItem(customRow,4,new QTableWidgetItem(rendered));
                ++customRow;
            }
        }
    }

    const QJsonArray runtimeMaps = m_session.state().toJson().value(QStringLiteral("runtimeMaps")).toArray();
    int runtimeMapRows = 0;
    for (const QJsonValue& mapValue : runtimeMaps) {
        const QJsonObject mapObject = mapValue.toObject();
        runtimeMapRows += mapObject.value(QStringLiteral("cells")).toArray().size();
        runtimeMapRows += mapObject.value(QStringLiteral("passage")).toArray().size();
        runtimeMapRows += mapObject.value(QStringLiteral("terrain")).toArray().size();
        runtimeMapRows += mapObject.value(QStringLiteral("tilesetRemap")).toArray().size();
    }
    m_runtimeMaps->setRowCount(runtimeMapRows);
    int runtimeMapRow = 0;
    const auto layerNameFor = [](const QVector<core::LayerPtr>& roots, const QString& id) {
        std::function<QString(const QVector<core::LayerPtr>&)> find = [&](const QVector<core::LayerPtr>& layers) -> QString {
            for (const core::LayerPtr& layer : layers) {
                if (!layer) continue;
                if (layer->id == id) return layer->name.isEmpty() ? layer->id : layer->name;
                const QString child = find(layer->children); if (!child.isEmpty()) return child;
            }
            return {};
        };
        return find(roots);
    };
    const auto tilesetNameFor = [&](const QString& id) {
        for (const core::Tileset& tileset : editor.tilesets)
            if (tileset.id == id) return tileset.name.isEmpty() ? tileset.id : tileset.name;
        return id;
    };
    for (const QJsonValue& mapValue : runtimeMaps) {
        const QJsonObject mapObject = mapValue.toObject();
        const QString mapId = mapObject.value(QStringLiteral("id")).toString();
        const core::MapDoc* runtimeMap = editor.mapById(mapId);
        const QString mapName = runtimeMap && !runtimeMap->name.isEmpty() ? runtimeMap->name : mapId;
        const QJsonArray cells = mapObject.value(QStringLiteral("cells")).toArray();
        for (const QJsonValue& cellValue : cells) {
            const QJsonObject cell = cellValue.toObject(); const QString layerId=cell.value(QStringLiteral("layerId")).toString();
            QStringList renderedTiles; for (const QJsonValue& tileValue : cell.value(QStringLiteral("tiles")).toArray()) { const QJsonObject tile=tileValue.toObject(); renderedTiles << tr("%1 [%2,%3]").arg(tilesetNameFor(tile.value(QStringLiteral("tilesetId")).toString())).arg(tile.value(QStringLiteral("tx")).toInt()).arg(tile.value(QStringLiteral("ty")).toInt()); }
            if (renderedTiles.isEmpty()) renderedTiles << tr("(célula vazia)");
            m_runtimeMaps->setItem(runtimeMapRow,0,new QTableWidgetItem(mapName)); m_runtimeMaps->setItem(runtimeMapRow,1,new QTableWidgetItem(tr("Tile")));
            m_runtimeMaps->setItem(runtimeMapRow,2,new QTableWidgetItem(runtimeMap?layerNameFor(runtimeMap->layers,layerId):layerId)); m_runtimeMaps->setItem(runtimeMapRow,3,new QTableWidgetItem(QString::number(cell.value(QStringLiteral("x")).toInt()))); m_runtimeMaps->setItem(runtimeMapRow,4,new QTableWidgetItem(QString::number(cell.value(QStringLiteral("y")).toInt()))); m_runtimeMaps->setItem(runtimeMapRow,5,new QTableWidgetItem(renderedTiles.join(QStringLiteral(" + ")))); ++runtimeMapRow;
        }
        for (const QJsonValue& passageValue : mapObject.value(QStringLiteral("passage")).toArray()) { const QJsonObject item=passageValue.toObject(); const int mask=item.value(QStringLiteral("mask")).toInt();
            m_runtimeMaps->setItem(runtimeMapRow,0,new QTableWidgetItem(mapName));m_runtimeMaps->setItem(runtimeMapRow,1,new QTableWidgetItem(tr("Passagem")));m_runtimeMaps->setItem(runtimeMapRow,2,new QTableWidgetItem(tr("Célula do mapa")));m_runtimeMaps->setItem(runtimeMapRow,3,new QTableWidgetItem(QString::number(item.value(QStringLiteral("x")).toInt())));m_runtimeMaps->setItem(runtimeMapRow,4,new QTableWidgetItem(QString::number(item.value(QStringLiteral("y")).toInt())));m_runtimeMaps->setItem(runtimeMapRow,5,new QTableWidgetItem(tr("Máscara %1 (C=%2 D=%3 B=%4 E=%5)").arg(mask).arg(bool(mask&core::Editor::SideTop)).arg(bool(mask&core::Editor::SideRight)).arg(bool(mask&core::Editor::SideBottom)).arg(bool(mask&core::Editor::SideLeft))));++runtimeMapRow; }
        for (const QJsonValue& terrainValue : mapObject.value(QStringLiteral("terrain")).toArray()) { const QJsonObject item=terrainValue.toObject();
            m_runtimeMaps->setItem(runtimeMapRow,0,new QTableWidgetItem(mapName));m_runtimeMaps->setItem(runtimeMapRow,1,new QTableWidgetItem(tr("Terrain/Tag")));m_runtimeMaps->setItem(runtimeMapRow,2,new QTableWidgetItem(tr("Célula do mapa")));m_runtimeMaps->setItem(runtimeMapRow,3,new QTableWidgetItem(QString::number(item.value(QStringLiteral("x")).toInt())));m_runtimeMaps->setItem(runtimeMapRow,4,new QTableWidgetItem(QString::number(item.value(QStringLiteral("y")).toInt())));m_runtimeMaps->setItem(runtimeMapRow,5,new QTableWidgetItem(QString::number(item.value(QStringLiteral("value")).toInt())));++runtimeMapRow; }
        for (const QJsonValue& remapValue : mapObject.value(QStringLiteral("tilesetRemap")).toArray()) { const QJsonObject item=remapValue.toObject(); const QString source=item.value(QStringLiteral("source")).toString(), target=item.value(QStringLiteral("target")).toString();
            m_runtimeMaps->setItem(runtimeMapRow,0,new QTableWidgetItem(mapName));m_runtimeMaps->setItem(runtimeMapRow,1,new QTableWidgetItem(tr("Tileset")));m_runtimeMaps->setItem(runtimeMapRow,2,new QTableWidgetItem(tilesetNameFor(source)));m_runtimeMaps->setItem(runtimeMapRow,3,new QTableWidgetItem(tr("—")));m_runtimeMaps->setItem(runtimeMapRow,4,new QTableWidgetItem(tr("—")));m_runtimeMaps->setItem(runtimeMapRow,5,new QTableWidgetItem(tr("→ %1").arg(tilesetNameFor(target))));++runtimeMapRow; }
    }

    const QJsonObject diagnostic = m_session.diagnosticSnapshot();
    const QJsonArray scheduler = diagnostic.value(QStringLiteral("commonScheduler")).toArray();
    m_scheduler->setRowCount(scheduler.size());
    for (int i=0; i<scheduler.size(); ++i) {
        const QJsonObject item=scheduler.at(i).toObject();
        const QString name=item.value(QStringLiteral("name")).toString();
        const int number=item.value(QStringLiteral("number")).toInt();
        m_scheduler->setItem(i,0,new QTableWidgetItem(name.isEmpty()?tr("Common Event %1").arg(number):QStringLiteral("%1 — %2").arg(number).arg(name)));
        m_scheduler->setItem(i,1,new QTableWidgetItem(item.value(QStringLiteral("trigger")).toString()));
        m_scheduler->setItem(i,2,new QTableWidgetItem(item.value(QStringLiteral("policy")).toString()));
        m_scheduler->setItem(i,3,new QTableWidgetItem(QString::number(item.value(QStringLiteral("priority")).toInt())));
        m_scheduler->setItem(i,4,new QTableWidgetItem(item.value(QStringLiteral("active")).toBool()?tr("Sim"):tr("Não")));
        m_scheduler->setItem(i,5,new QTableWidgetItem(item.value(QStringLiteral("pending")).toBool()?tr("Sim"):tr("Não")));
        m_scheduler->setItem(i,6,new QTableWidgetItem(QString::number(item.value(QStringLiteral("framesUntilRun")).toInt())));
    }
    const QJsonArray inputActions = diagnostic.value(QStringLiteral("inputActions")).toArray();
    m_input->setRowCount(inputActions.size());
    for (int i=0; i<inputActions.size(); ++i) {
        const QJsonObject item=inputActions.at(i).toObject();
        bool valid=false;const core::GameAction action=core::gameActionFromId(item.value(QStringLiteral("id")).toString(),&valid);
        m_input->setItem(i,0,new QTableWidgetItem(valid?core::gameActionLabel(action):item.value(QStringLiteral("id")).toString()));
        m_input->setItem(i,1,new QTableWidgetItem(item.value(QStringLiteral("held")).toBool()?tr("Sim"):tr("Não")));
        m_input->setItem(i,2,new QTableWidgetItem(item.value(QStringLiteral("pressed")).toBool()?tr("Sim"):tr("Não")));
        m_input->setItem(i,3,new QTableWidgetItem(item.value(QStringLiteral("released")).toBool()?tr("Sim"):tr("Não")));
        m_input->setItem(i,4,new QTableWidgetItem(QString::number(item.value(QStringLiteral("holdFrames")).toInt())));
    }
    const QJsonObject analog=diagnostic.value(QStringLiteral("inputAnalog")).toObject();
    m_inputAnalog->setText(tr("Controle: <b>%1</b> · X: %2 · Y: %3 · Magnitude: %4 · LT: %5 · RT: %6")
        .arg(analog.value(QStringLiteral("connected")).toBool()?tr("conectado"):tr("desconectado"))
        .arg(analog.value(QStringLiteral("x")).toDouble(),0,'f',3)
        .arg(analog.value(QStringLiteral("y")).toDouble(),0,'f',3)
        .arg(std::hypot(analog.value(QStringLiteral("x")).toDouble(),analog.value(QStringLiteral("y")).toDouble()),0,'f',3)
        .arg(analog.value(QStringLiteral("leftTrigger")).toDouble(),0,'f',3)
        .arg(analog.value(QStringLiteral("rightTrigger")).toDouble(),0,'f',3));

    const QVector<GameSession::DebugTraceEntry>& trace = m_session.debugTrace();
    m_trace->setRowCount(trace.size());
    for (int index=0; index<trace.size(); ++index) {
        const auto& item=trace[index]; const core::MapDoc* traceMap=editor.mapById(item.mapId); QString eventName=item.eventId;
        if(traceMap) for(const core::MapEvent& event:traceMap->events) if(event.id==item.eventId){eventName=event.name;break;}
        m_trace->setItem(index,0,new QTableWidgetItem(QString::number(item.sequence)));
        m_trace->setItem(index,1,new QTableWidgetItem(item.phase));
        m_trace->setItem(index,2,new QTableWidgetItem(item.source.isEmpty()?(traceMap?traceMap->name:item.mapId):item.source));
        m_trace->setItem(index,3,new QTableWidgetItem(eventName.isEmpty()?tr("Evento comum"):eventName));
        m_trace->setItem(index,4,new QTableWidgetItem(item.commandType));
        m_trace->setItem(index,5,new QTableWidgetItem(QString::number(item.commandIndex+1)));
        m_trace->setItem(index,6,new QTableWidgetItem(QString::number(item.callDepth)));
    }
    if(m_trace->rowCount()>0)m_trace->scrollToBottom();

    const QVector<MoveRouteDebugEntry> routes=m_session.world().moveRouteDebugEntries();
    m_routes->setRowCount(routes.size());
    for(int i=0;i<routes.size();++i){const auto& route=routes[i];
        const QString path=route.pathStatus.isEmpty()?tr("—"):tr("%1 · %2 replans · %3 nós · %4 neste tick").arg(route.pathStatus).arg(route.pathReplans).arg(route.pathExpandedNodes).arg(route.pathExpandedThisTick);
        m_routes->setItem(i,0,new QTableWidgetItem(route.target));m_routes->setItem(i,1,new QTableWidgetItem(QString::number(route.ticket)));
        m_routes->setItem(i,2,new QTableWidgetItem(route.state));m_routes->setItem(i,3,new QTableWidgetItem(route.commandType));m_routes->setItem(i,4,new QTableWidgetItem(QString::number(route.commandIndex+1)));
        m_routes->setItem(i,5,new QTableWidgetItem(QString::number(route.queuedCount)));m_routes->setItem(i,6,new QTableWidgetItem(QString::number(route.blockedAttempts)));
        m_routes->setItem(i,7,new QTableWidgetItem(path));m_routes->setItem(i,8,new QTableWidgetItem(route.lastFailure));
    }
    m_routeOverlay->setChecked(m_session.moveRouteDebugVisible());

    m_commonEvents->clear();
    const QStringList active = m_session.activeCommonEvents();
    if (active.isEmpty()) m_commonEvents->addItem(tr("Nenhum Common Event ativo."));
    else m_commonEvents->addItems(active);
    m_uiState->setText(tr("Tela atual: <b>%1</b><br>Foco atual: <b>%2</b><br>Common Events ativos: <b>%3</b>")
                           .arg(m_session.debugUiScreen(), m_session.debugUiFocus().isEmpty()?tr("(nenhum)"):m_session.debugUiFocus())
                           .arg(active.size()));

    const RuntimeProfilerSnapshot p=m_session.profilerSnapshot();
    if(m_profilerChart) m_profilerChart->addFrame(p.frameMs,p.p95FrameMs);
    const World::SpatialCacheDiagnostics spatial=m_session.world().spatialCacheDiagnostics();
    QStringList stageLines;
    QStringList stageNames=p.stageMs.keys(); stageNames.sort(Qt::CaseInsensitive);
    for(const QString& name:stageNames) stageLines << tr("%1: %2 ms").arg(name).arg(p.stageMs.value(name),0,'f',3);
    const QString stages=stageLines.isEmpty()?tr("(sem amostra ainda)"):stageLines.join(QStringLiteral("<br>"));
    m_profiler->setText(tr("Renderizador: <b>%1</b><br>FPS: <b>%2</b><br>Quadro atual: %3 ms<br>Média (%4 amostras): %5 ms<br>P50: %6 ms · P95: %7 ms · P99: %8 ms<br>Pior quadro: %9 ms<br>Chamadas de desenho/lotes: <b>%10</b> · elementos: <b>%11</b><br>Envios à GPU: %12 texturas / %13 bytes · vértices %14 bytes<br>Mapa na GPU: dados dinâmicos %15 bytes · mapa estático %16 bytes · malhas novas %17<br>Cache do mapa: %18 acertos / %19 falhas<br>Trocas de pipeline: %20 · trocas de recursos: %21<br>Eventos em execução: %22 · registros no rastro: %23<br>Índice espacial: %24 células de colisão · %25 eventos / %26 células · reconstruções %27/%28 · movimentos %29<br><br><b>Etapas do último quadro</b><br>%30")
                            .arg(p.renderer).arg(p.fps,0,'f',1).arg(p.frameMs,0,'f',2).arg(p.sampleCount)
                            .arg(p.averageFrameMs,0,'f',2).arg(p.p50FrameMs,0,'f',2).arg(p.p95FrameMs,0,'f',2)
                            .arg(p.p99FrameMs,0,'f',2).arg(p.maxFrameMs,0,'f',2).arg(p.drawCalls).arg(p.quads)
                            .arg(p.textureUploads).arg(p.textureUploadBytes).arg(p.vertexUploadBytes)
                            .arg(p.dynamicVertexUploadBytes).arg(p.staticMapVertexUploadBytes).arg(p.gpuMapMeshBuilds)
                            .arg(p.mapCacheHits).arg(p.mapCacheMisses).arg(p.pipelineChanges).arg(p.shaderResourceChanges)
                            .arg(p.activeInterpreters).arg(p.traceEntries)
                            .arg(spatial.collisionCells).arg(spatial.indexedEventEntries).arg(spatial.indexedEventCells)
                            .arg(spatial.collisionRebuilds).arg(spatial.eventIndexRebuilds).arg(spatial.eventIndexMoves).arg(stages));
    m_profiler->setText(m_profiler->text()+tr("<br><br><b>Regularidade dos quadros</b><br>Quadros atrasados: %1/%2 (%3%) · atraso mais recente: %4 ms · pior atraso: %5 ms")
                            .arg(p.lateFrames).arg(p.totalFrames).arg(p.lateFrameRatio*100.0,0,'f',1)
                            .arg(p.lastFrameOverrunMs,0,'f',2).arg(p.worstFrameOverrunMs,0,'f',2));
}

} // namespace game
