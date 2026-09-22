#include "MoveRouteDialog.h"

#include "AudioPicker.h"
#include "UniversalAssetPicker.h"
#include "CommandPreviewDialog.h"
#include "Icons.h"
#include "MapPreviewRenderer.h"
#include "core/ProjectIO.h"
#include "core/Renderer.h"
#include "core/MoveRoutePresetStore.h"
#include "game/GameWorld.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QAbstractItemView>
#include <QMenu>
#include <QSizePolicy>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <optional>
#include <algorithm>
#include <memory>
#include <functional>
#include <cmath>

namespace ui {

namespace {

QString labelOf(const QString& t)
{
    static const QHash<QString, QString> m = {
        {"moveDown", QObject::tr("Mover para baixo")}, {"moveLeft", QObject::tr("Mover para a esquerda")},
        {"moveRight", QObject::tr("Mover para a direita")}, {"moveUp", QObject::tr("Mover para cima")},
        {"moveDownLeft", QObject::tr("Mover para Inferior-esq.")}, {"moveDownRight", QObject::tr("Mover para Inferior-dir.")},
        {"moveUpLeft", QObject::tr("Mover para Superior-esq.")}, {"moveUpRight", QObject::tr("Mover para Superior-dir.")},
        {"moveRandom", QObject::tr("Mover aleatoriamente")}, {"moveTowardPlayer", QObject::tr("Ir em direção ao jogador")},
        {"moveAwayPlayer", QObject::tr("Afastar-se do jogador")}, {"pathfind", QObject::tr("Encontrar caminho…")},
        {"stepForward", QObject::tr("Dar um passo para frente")},
        {"stepBackward", QObject::tr("Dar um passo para trás")}, {"jump", QObject::tr("Saltar…")},
        {"wait", QObject::tr("Esperar…")}, {"turnDown", QObject::tr("Virar para baixo")},
        {"turnLeft", QObject::tr("Virar para a esquerda")}, {"turnRight", QObject::tr("Virar para a direita")},
        {"turnUp", QObject::tr("Virar para cima")}, {"turnRight90", QObject::tr("Girar 90° para a direita")},
        {"turnLeft90", QObject::tr("Girar 90° para a esquerda")}, {"turn180", QObject::tr("Girar 180°")},
        {"turn90Random", QObject::tr("Girar 90° aleatoriamente")}, {"turnRandom", QObject::tr("Virar aleatoriamente")},
        {"turnTowardPlayer", QObject::tr("Virar para o jogador")}, {"turnAwayPlayer", QObject::tr("Virar de costas para o jogador")},
        {"walkAnimOn", QObject::tr("Ativar animação ao andar")}, {"walkAnimOff", QObject::tr("Desativar animação ao andar")},
        {"stepAnimOn", QObject::tr("Ativar animação parado")}, {"stepAnimOff", QObject::tr("Desativar animação parado")},
        {"dirFixOn", QObject::tr("Fixar direção")}, {"dirFixOff", QObject::tr("Liberar direção")},
        {"throughOn", QObject::tr("Permitir atravessar")}, {"throughOff", QObject::tr("Bloquear atravessar")},
        {"transparentOn", QObject::tr("Ficar invisível")}, {"transparentOff", QObject::tr("Ficar visível")},
        {"switchOn", QObject::tr("Ligar interruptor…")}, {"switchOff", QObject::tr("Desligar interruptor…")},
        {"changeGraphic", QObject::tr("Mudar aparência…")}, {"opacity", QObject::tr("Alterar Opacidade…")},
        {"shake", QObject::tr("Tremer sprite…")},
        {"blend", QObject::tr("Alterar mistura…")}, {"speed", QObject::tr("Mudar Velocidade…")},
        {"frequency", QObject::tr("Mudar Frequência…")}, {"playSE", QObject::tr("Tocar efeito sonoro…")},
        {"rememberPosition", QObject::tr("Guardar posição atual")}
    };
    return m.value(t, t);
}

class RouteMapCellPicker : public QWidget
{
public:
    RouteMapCellPicker(core::Editor& editor,const QPoint& initial,QWidget* parent=nullptr)
        : QWidget(parent),ed(editor),m_selection(initial)
    {
        setMinimumSize(460,260);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        setCursor(Qt::CrossCursor);setToolTip(QObject::tr("Clique ou arraste para escolher o tile do destino."));
    }
    QPoint selection() const{return m_selection;}
    void setSelection(const QPoint& p){m_selection=p;clamp();update();}
    std::function<void(const QPoint&)> onPicked;
protected:
    void resizeEvent(QResizeEvent*) override{m_cache=QImage();}
    void mousePressEvent(QMouseEvent* e) override{if(e->button()==Qt::LeftButton)pick(e->position());}
    void mouseMoveEvent(QMouseEvent* e) override{if(e->buttons()&Qt::LeftButton)pick(e->position());}
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);p.fillRect(rect(),QColor("#15171b"));
        const core::MapDoc* doc=ed.doc();
        if(!doc||doc->map.width<=0||doc->map.height<=0){p.setPen(QColor("#999"));p.drawText(rect(),Qt::AlignCenter,QObject::tr("Mapa atual indisponível"));return;}
        const QRectF available=QRectF(rect()).adjusted(8,8,-8,-28);
        const double scale=qMin(available.width()/qMax(1,doc->map.pixelWidth()),available.height()/qMax(1,doc->map.pixelHeight()));
        const QSizeF size(doc->map.pixelWidth()*scale,doc->map.pixelHeight()*scale);
        m_area=QRectF(available.center()-QPointF(size.width()/2,size.height()/2),size);
        if(m_cache.isNull()){
            m_cache=renderMapPreview(ed,*doc,m_area.size().toSize());
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform,false);p.drawImage(m_area,m_cache);
        const double cw=m_area.width()/doc->map.width,ch=m_area.height()/doc->map.height;
        const QRectF mark(m_area.left()+m_selection.x()*cw,m_area.top()+m_selection.y()*ch,cw,ch);
        p.setPen(QPen(QColor("#75c7ff"),2));p.setBrush(QColor(69,169,255,95));p.drawRect(mark.adjusted(1,1,-1,-1));
        p.setPen(QColor("#e6e9ee"));p.drawText(QRectF(8,height()-22,width()-16,18),Qt::AlignCenter,
            QObject::tr("Destino: tile (%1, %2)").arg(m_selection.x()).arg(m_selection.y()));
    }
private:
    void clamp(){
        if(const core::MapDoc* doc=ed.doc()){
            m_selection.setX(qBound(0,m_selection.x(),qMax(0,doc->map.width-1)));
            m_selection.setY(qBound(0,m_selection.y(),qMax(0,doc->map.height-1)));
        }
    }
    void pick(const QPointF& pos){
        const core::MapDoc* doc=ed.doc();if(!doc||!m_area.contains(pos))return;
        const double fx=(pos.x()-m_area.left())/m_area.width(),fy=(pos.y()-m_area.top())/m_area.height();
        const QPoint cell(qBound(0,int(fx*doc->map.width),doc->map.width-1),
                          qBound(0,int(fy*doc->map.height),doc->map.height-1));
        if(cell==m_selection)return;m_selection=cell;update();if(onPicked)onPicked(cell);
    }
    core::Editor& ed;QPoint m_selection;QRectF m_area;QImage m_cache;
};

QString pathTargetSummary(const core::MoveRoutePathOptions& o,const core::Editor* ed=nullptr)
{
    switch(o.targetKind){
    case core::MoveRoutePathTargetKind::Cell:return QObject::tr("tile %1,%2").arg(o.cell.x()).arg(o.cell.y());
    case core::MoveRoutePathTargetKind::Player:return QObject::tr("Jogador");
    case core::MoveRoutePathTargetKind::SourceEvent:return QObject::tr("Evento chamador");
    case core::MoveRoutePathTargetKind::Event:
        if(ed)if(const core::MapEvent* ev=ed->findEvent(o.eventId))return QObject::tr("Evento %1").arg(ev->name);
        return o.eventId.isEmpty()?QObject::tr("Evento ausente"):QObject::tr("Evento %1").arg(o.eventId.left(8));
    }
    return QObject::tr("destino");
}

bool editPathfindCommand(core::Editor& ed,QWidget* parent,core::MoveCommand& command,bool allowSourceEvent,
                         std::optional<core::MoveRoutePathBehavior> forcedBehavior=std::nullopt)
{
    core::MoveRoutePathOptions o=core::moveRoutePathOptionsFromCommand(command);
    if(forcedBehavior)o.behavior=*forcedBehavior;
    QDialog d(parent);d.setWindowTitle(QObject::tr("Encontrar caminho"));d.resize(760,720);
    auto* outer=new QVBoxLayout(&d);auto* form=new QFormLayout;

    auto* behavior=new QComboBox(&d);
    behavior->addItem(QObject::tr("Ir até o destino"),"reach");
    behavior->addItem(QObject::tr("Seguir continuamente"),"follow");
    behavior->addItem(QObject::tr("Fugir até uma distância"),"flee");
    behavior->addItem(QObject::tr("Manter distância continuamente"),"keepDistance");
    behavior->setCurrentIndex(qMax(0,behavior->findData(core::moveRoutePathBehaviorId(o.behavior))));
    behavior->setEnabled(!forcedBehavior.has_value());

    auto* targetKind=new QComboBox(&d);
    targetKind->addItem(QObject::tr("Posição no mapa"),"cell");
    targetKind->addItem(QObject::tr("Jogador"),"player");
    if(allowSourceEvent)targetKind->addItem(QObject::tr("Evento que iniciou esta rota"),"sourceEvent");
    targetKind->addItem(QObject::tr("Outro evento"),"event");
    int tki=targetKind->findData(core::moveRoutePathTargetKindId(o.targetKind));
    if(tki<0)tki=targetKind->findData(QStringLiteral("player"));
    targetKind->setCurrentIndex(qMax(0,tki));

    auto* event=new QComboBox(&d);
    if(const core::MapDoc* doc=ed.doc())for(const core::MapEvent& ev:doc->events)
        event->addItem(ev.name.isEmpty()?QObject::tr("Evento %1").arg(ev.id):ev.name,ev.id);
    event->setCurrentIndex(qMax(0,event->findData(o.eventId)));

    auto* x=new QSpinBox(&d);auto* y=new QSpinBox(&d);
    int maxX=9999,maxY=9999;if(const core::MapDoc* doc=ed.doc()){maxX=qMax(0,doc->map.width-1);maxY=qMax(0,doc->map.height-1);}
    x->setRange(0,maxX);y->setRange(0,maxY);x->setValue(qBound(0,o.cell.x(),maxX));y->setValue(qBound(0,o.cell.y(),maxY));
    auto* coords=new QWidget(&d);auto* coordsL=new QHBoxLayout(coords);coordsL->setContentsMargins(0,0,0,0);coordsL->addWidget(x);coordsL->addWidget(new QLabel(",",coords));coordsL->addWidget(y);

    auto* minD=new QSpinBox(&d);minD->setRange(0,99);minD->setSuffix(QObject::tr(" tiles"));minD->setValue(o.minDistance);
    auto* maxD=new QSpinBox(&d);maxD->setRange(0,99);maxD->setSuffix(QObject::tr(" tiles"));maxD->setValue(o.maxDistance);
    auto* diagonal=new QCheckBox(QObject::tr("Permitir diagonais"),&d);diagonal->setChecked(o.diagonal);
    auto* continuous=new QCheckBox(QObject::tr("Continuar acompanhando o alvo"),&d);continuous->setChecked(o.continuous);
    auto* maxNodes=new QSpinBox(&d);maxNodes->setRange(64,65536);maxNodes->setSingleStep(256);maxNodes->setValue(o.maxSearchNodes);
    maxNodes->setToolTip(QObject::tr("Trava de segurança contra buscas excessivas. 4096 é recomendado para a maioria dos mapas."));

    form->addRow(QObject::tr("Comportamento:"),behavior);form->addRow(QObject::tr("Alvo:"),targetKind);
    form->addRow(QObject::tr("Evento:"),event);form->addRow(QObject::tr("Tile X,Y:"),coords);
    form->addRow(QObject::tr("Distância mínima:"),minD);form->addRow(QObject::tr("Distância/tolerância máxima:"),maxD);
    form->addRow(QString(),diagonal);form->addRow(QString(),continuous);form->addRow(QObject::tr("Limite de busca:"),maxNodes);
    outer->addLayout(form);

    auto* picker=new RouteMapCellPicker(ed,QPoint(x->value(),y->value()),&d);outer->addWidget(picker,1);
    picker->onPicked=[x,y](const QPoint& cell){x->setValue(cell.x());y->setValue(cell.y());};
    QObject::connect(x,&QSpinBox::valueChanged,picker,[picker,y](int value){picker->setSelection(QPoint(value,y->value()));});
    QObject::connect(y,&QSpinBox::valueChanged,picker,[picker,x](int value){picker->setSelection(QPoint(x->value(),value));});

    auto* hint=new QLabel(&d);hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#9ea7b3;font-size:11px"));outer->addWidget(hint);
    auto refresh=[=]{
        const QString kind=targetKind->currentData().toString();
        const QString b=behavior->currentData().toString();
        const bool cell=kind==QLatin1String("cell"),ev=kind==QLatin1String("event");
        coords->setVisible(cell);picker->setVisible(cell);event->setVisible(ev);
        minD->setEnabled(b==QLatin1String("flee")||b==QLatin1String("keepDistance"));
        maxD->setEnabled(b!=QLatin1String("flee"));
        const bool continuousBehavior=b==QLatin1String("follow")||b==QLatin1String("keepDistance");
        continuous->setChecked(continuousBehavior);
        continuous->setEnabled(false);
        continuous->setToolTip(QObject::tr("Seguir e Manter distância são contínuos por definição; Ir até e Fugir concluem quando atingem o objetivo."));
        if(b==QLatin1String("follow"))hint->setText(QObject::tr("Segue o alvo e permanece neste comando; use Cancelar Rota para encerrar. A distância máxima é o espaço que ele tenta manter."));
        else if(b==QLatin1String("keepDistance"))hint->setText(QObject::tr("Aproxima quando está longe e recua quando está perto. Permanece ativo até a rota ser cancelada."));
        else if(b==QLatin1String("flee"))hint->setText(QObject::tr("Procura o caminho livre mais curto para atingir pelo menos a distância mínima e então conclui."));
        else hint->setText(QObject::tr("Calcula um caminho até o destino e conclui quando estiver dentro da tolerância máxima."));
    };
    QObject::connect(targetKind,&QComboBox::currentIndexChanged,&d,[=](int){
        const QString kind=targetKind->currentData().toString(),b=behavior->currentData().toString();
        if(kind!=QLatin1String("cell")&&(b==QLatin1String("reach")||b==QLatin1String("follow"))&&maxD->value()==0)
            maxD->setValue(1);
        refresh();
    });
    QObject::connect(behavior,&QComboBox::currentIndexChanged,&d,[refresh](int){refresh();});
    refresh();

    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);outer->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&d,[&]{
        const QString kind=targetKind->currentData().toString();
        if(kind==QLatin1String("event")&&event->currentData().toString().trimmed().isEmpty()){
            QMessageBox::warning(&d,QObject::tr("Pathfinding"),QObject::tr("Escolha um Evento válido como alvo."));return;
        }
        if(behavior->currentData().toString()==QLatin1String("keepDistance")&&maxD->value()<minD->value()){
            maxD->setValue(minD->value());
        }
        d.accept();
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
    if(d.exec()!=QDialog::Accepted)return false;

    o.behavior=core::moveRoutePathBehaviorFromId(behavior->currentData().toString());
    o.targetKind=core::moveRoutePathTargetKindFromId(targetKind->currentData().toString());
    o.eventId=event->currentData().toString();o.cell=QPoint(x->value(),y->value());
    o.minDistance=minD->value();o.maxDistance=maxD->value();o.diagonal=diagonal->isChecked();
    o.continuous=o.behavior==core::MoveRoutePathBehavior::Follow||o.behavior==core::MoveRoutePathBehavior::KeepDistance;
    o.maxSearchNodes=maxNodes->value();
    if(o.behavior==core::MoveRoutePathBehavior::KeepDistance&&o.maxDistance<o.minDistance)o.maxDistance=o.minDistance;
    command.type=QStringLiteral("pathfind");command.params=core::moveRoutePathOptionsToParams(o);return true;
}

QString commandDescription(const core::MoveCommand& command)
{
    const QString& t=command.type;
    if(t==QLatin1String("pathfind"))return QObject::tr("Encontra automaticamente um caminho até o destino e recalcula a rota se o alvo ou os obstáculos mudarem.");
    if(t.startsWith(QLatin1String("move"))||t==QLatin1String("stepForward")||t==QLatin1String("stepBackward"))
        return QObject::tr("Move uma etapa respeitando as colisões do mapa.");
    if(t==QLatin1String("jump"))return QObject::tr("Salta até a posição indicada, respeitando os limites válidos do mapa.");
    if(t.startsWith(QLatin1String("turn")))return QObject::tr("Altera apenas a direção visual, respeitando Direção Fixa.");
    if(t==QLatin1String("wait"))return QObject::tr("Pausa esta rota pelo número de frames configurado.");
    if(t==QLatin1String("speed")||t==QLatin1String("frequency"))return QObject::tr("Altera a temporização dos próximos passos da rota.");
    if(t==QLatin1String("throughOn")||t==QLatin1String("throughOff"))return QObject::tr("Liga/desliga Atravessar; também afeta o pathfinding subsequente.");
    if(t==QLatin1String("changeGraphic"))return QObject::tr("Troca o gráfico do ator sem reiniciar a rota.");
    if(t==QLatin1String("switchOn")||t==QLatin1String("switchOff"))return QObject::tr("Liga ou desliga o interruptor escolhido quando esta etapa da rota for executada.");
    if(t==QLatin1String("playSE"))return QObject::tr("Toca o efeito sonoro escolhido durante a rota.");
    if(t==QLatin1String("shake"))return QObject::tr("Aplica tremor visual ao ator sem alterar sua colisão.");
    if(t==QLatin1String("rememberPosition"))return QObject::tr("Memoriza a posição atual do evento. Ao voltar a este mapa, ele reaparece nessa posição; o estado também acompanha Save/Load.");
    return QObject::tr("Comando executado pelo MoveRouteExecutor compartilhado de Jogador e Eventos.");
}

class RouteCommandListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;
    std::function<void()> onInternalDrop;
protected:
    void dropEvent(QDropEvent* event) override
    {
        QListWidget::dropEvent(event);
        if(event->isAccepted()&&onInternalDrop)onInternalDrop();
    }
};

class RoutePreviewWidget : public QWidget
{
public:
    RoutePreviewWidget(core::Editor& editor, QWidget* parent = nullptr)
        : QWidget(parent), m_ed(editor)
    {
        setMinimumSize(360, 360);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        m_timer.setInterval(33);
        QObject::connect(&m_timer,&QTimer::timeout,this,[this]{ tick(.033*m_speedMultiplier); });
    }

    void setRoute(const core::MoveRoute* route){m_route=route;reset();}
    void setTarget(const QString& target){m_requestedTarget=target;reset();}
    void setSpeedMultiplier(double value){m_speedMultiplier=qBound(.25,value,4.0);}
    void play(){if(m_world&&m_route&&!m_route->commands.isEmpty())m_timer.start();}
    void pause(){m_timer.stop();update();}
    void step(){m_timer.stop();tick(1.0/60.0);}
    bool running() const{return m_timer.isActive();}

    std::function<void(int)> onCommandChanged;

    void reset()
    {
        m_timer.stop();m_cache=QImage();m_lastCommand=-1;m_status.clear();m_runtimeTarget.clear();
        m_world=std::make_unique<game::World>(m_ed);
        m_world->config.from(m_ed.player);
        QPoint start=m_ed.startPosition;
        if(const core::MapDoc* doc=m_ed.doc()){
            if(m_ed.startMapId!=doc->id||start.x()<0||start.y()<0||start.x()>=doc->map.width||start.y()>=doc->map.height)
                start=QPoint(qMax(0,doc->map.width/2),qMax(0,doc->map.height/2));
        }
        m_world->reset(start);
        if(!m_route||m_route->commands.isEmpty()){m_status=QObject::tr("Rota vazia");update();return;}
        m_runtimeTarget=m_requestedTarget.trimmed();
        if(m_runtimeTarget.isEmpty()||m_runtimeTarget==QLatin1String("self")){
            if(!m_ed.selectedEventId.isEmpty())m_runtimeTarget=QStringLiteral("event:")+m_ed.selectedEventId;
            else m_runtimeTarget=QStringLiteral("player");
        }
        core::MoveRoute route=*m_route;
        route.target=m_runtimeTarget;route.startMode=core::MoveRouteStartMode::Replace;route.waitForCompletion=false;
        const game::MoveRouteTicket ticket=m_world->startMoveRoute(m_runtimeTarget,route,m_ed.selectedEventId);
        if(ticket==0){m_status=QObject::tr("Alvo indisponível para o preview");}
        else m_status=QObject::tr("Runtime real da rota — ticket %1").arg(ticket);
        updateCommandTracking();update();
    }

protected:
    void resizeEvent(QResizeEvent*) override{m_cache=QImage();}
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);p.fillRect(rect(),QColor("#11151b"));
        const core::MapDoc* doc=m_ed.doc();
        if(!doc||!m_world){p.setPen(QColor("#9aa4b2"));p.drawText(rect(),Qt::AlignCenter,QObject::tr("Mapa indisponível"));return;}
        QRectF available=QRectF(rect()).adjusted(8,8,-8,-58);
        const double scale=qMin(available.width()/qMax(1,doc->map.pixelWidth()),available.height()/qMax(1,doc->map.pixelHeight()));
        const QSizeF mapSize(doc->map.pixelWidth()*scale,doc->map.pixelHeight()*scale);
        const QRectF mapRect(available.center()-QPointF(mapSize.width()/2,mapSize.height()/2),mapSize);
        if(m_cache.isNull()){
            m_cache=renderMapPreview(m_ed,*doc,mapRect.size().toSize());
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform,false);p.drawImage(mapRect,m_cache);
        const double sx=mapRect.width()/qMax(1,doc->map.pixelWidth());
        const double sy=mapRect.height()/qMax(1,doc->map.pixelHeight());
        auto worldToWidget=[&](const QPointF& px){return QPointF(mapRect.left()+px.x()*sx,mapRect.top()+px.y()*sy);};
        auto huToWidget=[&](const QPoint& hu){return worldToWidget(QPointF(
            hu.x()*m_ed.mapInfo().tileWidth/2.0 + m_ed.mapInfo().tileWidth/2.0,
            hu.y()*m_ed.mapInfo().tileHeight/2.0 + m_ed.mapInfo().tileHeight/2.0));};

        // Colisões ficam visíveis sem criar uma segunda regra: a consulta vem
        // do próprio World usado pelo runtime do preview.
        p.save();p.setClipRect(mapRect);p.setPen(Qt::NoPen);p.setBrush(QColor(230,74,74,38));
        const double cw=m_ed.mapInfo().tileWidth*sx,ch=m_ed.mapInfo().tileHeight*sy;
        for(int y=0;y<doc->map.height;++y)for(int x=0;x<doc->map.width;++x)
            if(m_world->isBlocked(x,y))p.drawRect(QRectF(mapRect.left()+x*cw,mapRect.top()+y*ch,cw,ch));

        const QVector<game::MoveRouteDebugEntry> entries=m_world->moveRouteDebugEntries();
        for(const game::MoveRouteDebugEntry& entry:entries){
            if(entry.target!=m_runtimeTarget&&!(m_runtimeTarget.startsWith(QLatin1String("event:"))&&entry.target==m_runtimeTarget+QStringLiteral(":custom")))continue;
            if(entry.hasPathTarget){
                QPainterPath path;path.moveTo(huToWidget(entry.actorPositionHU));
                for(const QPoint& node:entry.pathNodesHU)path.lineTo(huToWidget(node));
                p.setPen(QPen(QColor("#69d0ff"),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);p.drawPath(path);
                const QPointF target=huToWidget(entry.pathTargetHU);
                p.setPen(QPen(QColor("#ffe16a"),2));p.setBrush(QColor(255,225,106,70));p.drawEllipse(target,7,7);
                p.setPen(QColor("#ffe16a"));p.drawText(target+QPointF(9,-8),QObject::tr("destino"));
            }
        }

        auto drawGraphic=[&](const core::EventGraphic& graphic,const QPointF& feet,double opacity,const QString& blend){
            if(graphic.kind!=core::EventGraphic::Charset||graphic.charset.isNull())return false;
            const QRect src=graphic.charsetFrameRect();if(src.isEmpty())return false;
            const QSizeF size(src.width()*sx,src.height()*sy);
            const QRectF dst(feet.x()-size.width()/2.0,feet.y()-size.height(),size.width(),size.height());
            p.save();p.setOpacity(qBound(.04,opacity,1.0));
            if(blend==QLatin1String("add"))p.setCompositionMode(QPainter::CompositionMode_Plus);
            else if(blend==QLatin1String("multiply"))p.setCompositionMode(QPainter::CompositionMode_Multiply);
            else if(blend==QLatin1String("screen"))p.setCompositionMode(QPainter::CompositionMode_Screen);
            p.drawImage(dst,graphic.charset,src);p.restore();return true;
        };
        // Eventos de referência. O ator controlado usa a aparência/opacity/blend
        // do World real para que Change Graphic/Opacity/Transparent também sejam previewáveis.
        for(const core::MapEvent& event:doc->events){
            game::World::EventActorView view;if(!m_world->eventView(event,&view))continue;
            const QPointF pos=worldToWidget(view.pixel+QPointF(m_ed.mapInfo().tileWidth/2.0,m_ed.mapInfo().tileHeight));
            const bool controlled=m_runtimeTarget==QStringLiteral("event:")+event.id;
            const double opacity=view.transparent?0.10:view.opacity/255.0;
            if(controlled&&!drawGraphic(view.graphic,pos,opacity,view.blend)){
                p.setPen(QPen(QColor("#ffd45e"),3));p.setBrush(QColor(255,212,94,120));p.drawEllipse(pos,8,8);
            }else if(!controlled){p.setPen(QPen(QColor(255,255,255,90),1));p.setBrush(QColor(255,255,255,35));p.drawEllipse(pos,4,4);}
            if(controlled){p.setPen(QColor("#ffd45e"));p.drawText(pos+QPointF(10,4),event.name.isEmpty()?QObject::tr("Evento"):event.name);}
        }
        const QPointF playerPos=worldToWidget(m_world->playerVisualPixel()+QPointF(m_ed.mapInfo().tileWidth/2.0,m_ed.mapInfo().tileHeight));
        const bool playerControlled=m_runtimeTarget==QLatin1String("player");
        if(playerControlled){
            core::EventGraphic g;
            if(const core::EventGraphic* overrideGraphic=m_world->playerGraphicOverride())g=*overrideGraphic;
            else if(m_ed.player.hasCharset()){g.kind=core::EventGraphic::Charset;g.charset=m_ed.player.charset;g.charsetCols=m_ed.player.frameCols;g.charsetRows=m_ed.player.frameRows;g.dir=game::charsetRow(m_world->facing(),m_ed.player.spriteDirs);g.frame=m_world->animFrame();}
            const double opacity=m_world->playerTransparent()?0.10:m_world->playerOpacity()/255.0;
            if(!drawGraphic(g,playerPos,opacity,m_world->playerBlend())){p.setPen(QPen(QColor("#67d6ff"),3));p.setBrush(QColor(103,214,255,140));p.drawEllipse(playerPos,8,8);}
            p.setPen(QColor("#bfeeff"));p.drawText(playerPos+QPointF(10,4),QObject::tr("Jogador"));
        }else{p.setPen(QPen(QColor(103,214,255,140),1));p.setBrush(QColor(103,214,255,55));p.drawEllipse(playerPos,5,5);}
        p.restore();

        p.setPen(Qt::white);p.drawText(QRectF(10,height()-48,width()-20,20),Qt::AlignLeft|Qt::AlignVCenter,m_status);
        const auto entry=currentEntry();
        QString detail=QObject::tr("Play/Pause/Passo usam game::World + MoveRouteRuntime + MoveRouteExecutor reais.");
        if(entry){
            detail=QObject::tr("%1 · comando #%2 %3 · fila %4 · bloqueios %5")
                .arg(entry->state).arg(entry->commandIndex+1).arg(entry->commandType)
                .arg(entry->queuedCount).arg(entry->blockedAttempts);
            if(!entry->pathStatus.isEmpty())detail+=QObject::tr(" · A*: %1 / replans %2 / nós %3 / tick %4")
                .arg(entry->pathStatus).arg(entry->pathReplans).arg(entry->pathExpandedNodes).arg(entry->pathExpandedThisTick);
        }
        p.setPen(QColor("#aeb8c8"));p.drawText(QRectF(10,height()-27,width()-20,20),Qt::AlignLeft|Qt::AlignVCenter,detail);
    }

private:
    std::optional<game::MoveRouteDebugEntry> currentEntry() const
    {
        if(!m_world)return std::nullopt;
        for(const game::MoveRouteDebugEntry& entry:m_world->moveRouteDebugEntries())
            if(entry.target==m_runtimeTarget)return entry;
        return std::nullopt;
    }
    void updateCommandTracking()
    {
        const auto entry=currentEntry();
        if(!entry)return;
        if(entry->commandIndex!=m_lastCommand){m_lastCommand=entry->commandIndex;if(onCommandChanged)onCommandChanged(m_lastCommand);}
    }
    void tick(double dt)
    {
        if(!m_world||!m_route)return;
        m_world->update(qBound(0.0,dt,.2),0,0);updateCommandTracking();
        const auto entry=currentEntry();
        if(!entry){m_timer.stop();m_status=QObject::tr("Rota concluída");}
        else if(!entry->lastFailure.isEmpty()){m_status=QObject::tr("Falha: %1").arg(entry->lastFailure);m_timer.stop();}
        update();
    }

    core::Editor& m_ed;
    const core::MoveRoute* m_route=nullptr;
    std::unique_ptr<game::World> m_world;
    QTimer m_timer;
    QImage m_cache;
    QString m_requestedTarget=QStringLiteral("self"),m_runtimeTarget,m_status;
    double m_speedMultiplier=1.0;
    int m_lastCommand=-1;
};
} // namespace

QString MoveRouteDialog::commandLabel(const core::MoveCommand& c)
{
    QString s = labelOf(c.type);
    if(c.type==QLatin1String("pathfind")){
        const core::MoveRoutePathOptions o=core::moveRoutePathOptionsFromCommand(c);
        switch(o.behavior){
        case core::MoveRoutePathBehavior::Follow:s=QObject::tr("Seguir %1").arg(pathTargetSummary(o));break;
        case core::MoveRoutePathBehavior::Flee:s=QObject::tr("Fugir de %1 até %2 tiles").arg(pathTargetSummary(o)).arg(o.minDistance);break;
        case core::MoveRoutePathBehavior::KeepDistance:s=QObject::tr("Manter %1–%2 tiles de %3").arg(o.minDistance).arg(o.maxDistance).arg(pathTargetSummary(o));break;
        case core::MoveRoutePathBehavior::Reach:s=QObject::tr("Ir até %1").arg(pathTargetSummary(o));break;
        }
        if(o.diagonal)s+=QObject::tr(" · 8-dir");
    }
    else if (c.type == QLatin1String("wait")) s += QObject::tr(" (%1 quadros)").arg(c.params.value("frames",30).toInt());
    else if (c.type == QLatin1String("jump")) s += QObject::tr(" (%1,%2)").arg(c.params.value("x").toInt()).arg(c.params.value("y").toInt());
    else if (c.type == QLatin1String("switchOn") || c.type == QLatin1String("switchOff")) s += QObject::tr(" #%1").arg(c.params.value("id").toInt());
    else if (c.type == QLatin1String("speed")){const QVector<double>v={.375,.75,1.5,3,6,12};const QStringList l={QObject::tr("8× mais lento"),QObject::tr("4× mais lento"),QObject::tr("2× mais lento"),QObject::tr("Normal"),QObject::tr("2× mais rápido"),QObject::tr("4× mais rápido")};double best=1e9;int bi=3;for(int i=0;i<v.size();++i)if(qAbs(v[i]-c.params.value("value").toDouble())<best){best=qAbs(v[i]-c.params.value("value").toDouble());bi=i;}s+=QStringLiteral(" (%1: %2)").arg(bi+1).arg(l[bi]);}
    else if (c.type == QLatin1String("frequency")) s += QObject::tr(" (%1: %2)").arg(c.params.value("value").toInt()).arg(QStringList{QObject::tr("Mínima"),QObject::tr("Baixa"),QObject::tr("Normal"),QObject::tr("Alta"),QObject::tr("Máxima")}.value(c.params.value("value",3).toInt()-1));
    else if (c.type == QLatin1String("opacity")) s += QObject::tr(" (%1)").arg(c.params.value("value").toInt());
    else if (c.type == QLatin1String("shake"))
        s += QObject::tr(" (X %1, Y %2, %3 %4)")
                 .arg(c.params.value("x",4).toDouble()).arg(c.params.value("y",0).toDouble())
                 .arg(c.params.value("duration",30).toDouble())
                 .arg(c.params.value("unit","frames").toString()==QLatin1String("seconds") ? QObject::tr("s") : QObject::tr("frames"));
    else if (c.type == QLatin1String("changeGraphic")) s += QObject::tr(" (personagem %1, dir %2, frame %3)")
        .arg(c.params.value("characterIndex").toInt()+1).arg(c.params.value("dir").toInt()+1).arg(c.params.value("frame").toInt()+1);
    else if (c.type == QLatin1String("playSE")) s += QObject::tr(" (%1 · %2%)")
        .arg(QFileInfo(c.params.value("source").toString()).fileName())
        .arg(c.params.value("volume",90).toInt());
    return s;
}

MoveRouteDialog::MoveRouteDialog(core::Editor& editor, const core::MoveRoute& route,
                                 QWidget* parent, bool allowTargets)
    : QDialog(parent), m_ed(editor), m_route(route), m_allowTargets(allowTargets)
{
    setWindowTitle(tr("Criar rota de movimento"));
    resize(1420, 780);
    auto* outer=new QVBoxLayout(this);
    auto* body=new QHBoxLayout;outer->addLayout(body,1);

    // ---------------------------------------------------------------- lista
    auto* left=new QWidget(this);auto* lv=new QVBoxLayout(left);
    m_target=new QComboBox(left);m_target->addItem(tr("Este evento"),QStringLiteral("self"));
    if(allowTargets){
        m_target->addItem(tr("Jogador"),QStringLiteral("player"));
        if(const core::MapDoc* doc=m_ed.doc())for(const core::MapEvent& ev:doc->events)
            m_target->addItem(tr("Evento: %1").arg(ev.name.isEmpty()?ev.id:ev.name),QStringLiteral("event:")+ev.id);
    }
    m_target->setCurrentIndex(qMax(0,m_target->findData(m_route.target)));m_target->setEnabled(allowTargets);
    if(!allowTargets){m_target->setToolTip(tr("Rotas autônomas da página sempre controlam o próprio evento."));m_route.target=QStringLiteral("self");}
    lv->addWidget(m_target);

    m_list=new RouteCommandListWidget(left);m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);m_list->setAcceptDrops(true);m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);m_list->setDefaultDropAction(Qt::MoveAction);
    lv->addWidget(m_list,1);
    auto* description=new QLabel(tr("Selecione um comando para ver como ele funciona."),left);
    description->setWordWrap(true);description->setMinimumHeight(42);description->setStyleSheet(QStringLiteral("color:#9ba7b4"));lv->addWidget(description);

    auto* order=new QHBoxLayout;
    auto* remove=new QPushButton(icons::get(QStringLiteral("remove")),tr("Remover"),left);
    auto* up=new QToolButton(left);up->setIcon(icons::get(QStringLiteral("up")));up->setToolTip(tr("Mover seleção para cima"));
    auto* down=new QToolButton(left);down->setIcon(icons::get(QStringLiteral("down")));down->setToolTip(tr("Mover seleção para baixo"));
    order->addWidget(remove);order->addStretch(1);order->addWidget(up);order->addWidget(down);lv->addLayout(order);

    // --------------------------------------------------------------- presets
    auto* presetBox=new QGroupBox(tr("Rotas prontas"),left);auto* presetLayout=new QVBoxLayout(presetBox);
    auto* presetCombo=new QComboBox(presetBox);presetLayout->addWidget(presetCombo);
    auto* presetButtons=new QHBoxLayout;auto* applyPreset=new QPushButton(tr("Aplicar"),presetBox);
    auto* savePreset=new QPushButton(tr("Salvar como…"),presetBox);auto* updatePreset=new QPushButton(tr("Atualizar"),presetBox);
    presetButtons->addWidget(applyPreset);presetButtons->addWidget(savePreset);presetButtons->addWidget(updatePreset);presetLayout->addLayout(presetButtons);
    auto* presetManage=new QHBoxLayout;auto* duplicatePreset=new QPushButton(tr("Duplicar"),presetBox);
    auto* renamePreset=new QPushButton(tr("Renomear…"),presetBox);auto* deletePreset=new QPushButton(tr("Excluir"),presetBox);
    presetManage->addWidget(duplicatePreset);presetManage->addWidget(renamePreset);presetManage->addWidget(deletePreset);presetLayout->addLayout(presetManage);lv->addWidget(presetBox);
    auto presets=std::make_shared<QVector<core::MoveRoutePreset>>();
    auto presetLoadError=std::make_shared<QString>();
    auto updatePresetButtons=[presetCombo,savePreset,updatePreset,duplicatePreset,renamePreset,deletePreset,presetLoadError]{
        const bool has=presetCombo->count()>0;const bool builtIn=has&&presetCombo->currentData(Qt::UserRole+1).toBool();const bool writable=presetLoadError->isEmpty();
        savePreset->setEnabled(writable);duplicatePreset->setEnabled(has&&writable);updatePreset->setEnabled(has&&!builtIn&&writable);
        renamePreset->setEnabled(has&&!builtIn&&writable);deletePreset->setEnabled(has&&!builtIn&&writable);
    };
    auto refreshPresets=[this,presets,presetCombo,presetLoadError,updatePresetButtons]{
        const QString wanted=presetCombo->currentData().toString();presetCombo->clear();presets->clear();
        *presets=core::builtInMoveRoutePresets();QString error;
        const QVector<core::MoveRoutePreset> user=core::loadUserMoveRoutePresets(m_ed.projectRoot(),&error);*presetLoadError=error;
        for(const auto& item:user)presets->push_back(item);
        for(const auto& item:*presets){presetCombo->addItem(item.builtIn?tr("%1 (LUDO)").arg(item.name):item.name,item.id);presetCombo->setItemData(presetCombo->count()-1,item.builtIn,Qt::UserRole+1);}
        int idx=presetCombo->findData(wanted);presetCombo->setCurrentIndex(idx>=0?idx:0);
        updatePresetButtons();
        if(!error.isEmpty())presetCombo->setToolTip(tr("Não foi possível salvar suas rotas prontas: %1").arg(error));else presetCombo->setToolTip(tr("Suas rotas prontas ficam salvas no Editor e podem ser reutilizadas em outros momentos."));
    };
    refreshPresets();
    connect(presetCombo,&QComboBox::currentIndexChanged,this,[updatePresetButtons](int){updatePresetButtons();});

    // --------------------------------------------------------------- opções
    auto* options=new QGroupBox(tr("Opções"),left);auto* ov=new QVBoxLayout(options);
    m_repeat=new QCheckBox(tr("Repetir movimentos"),options);m_repeat->setChecked(m_route.repeat);
    m_wait=new QCheckBox(tr("Esperar terminar esta rota"),options);m_wait->setChecked(m_route.waitForCompletion);
    m_blockedPolicy=new QComboBox(options);
    m_blockedPolicy->addItem(tr("Se o caminho estiver bloqueado: esperar e tentar de novo"),QStringLiteral("wait"));
    m_blockedPolicy->addItem(tr("Se o caminho estiver bloqueado: pular esta etapa"),QStringLiteral("skip"));
    m_blockedPolicy->addItem(tr("Se o caminho estiver bloqueado: cancelar a rota"),QStringLiteral("cancel"));
    m_blockedPolicy->setCurrentIndex(qMax(0,m_blockedPolicy->findData(core::moveRouteBlockedPolicyId(m_route.blockedPolicy))));
    m_startMode=new QComboBox(options);m_startMode->addItem(tr("Ao iniciar: substituir a rota atual"),QStringLiteral("replace"));m_startMode->addItem(tr("Ao iniciar: esperar a rota atual terminar"),QStringLiteral("queue"));
    m_startMode->setCurrentIndex(qMax(0,m_startMode->findData(core::moveRouteStartModeId(m_route.startMode))));
    m_blockedPolicy->setToolTip(tr("Escolha o que deve acontecer quando o personagem não conseguir avançar."));
    m_startMode->setToolTip(tr("A nova rota pode esperar automaticamente a rota atual terminar."));
    if(!allowTargets){m_wait->setChecked(false);m_wait->hide();m_startMode->setCurrentIndex(qMax(0,m_startMode->findData(QStringLiteral("replace"))));m_startMode->hide();}
    ov->addWidget(m_repeat);ov->addWidget(m_wait);ov->addWidget(m_blockedPolicy);ov->addWidget(m_startMode);
    auto* previewButton = new QPushButton(tr("Ver prévia"), options);
    previewButton->setToolTip(tr("Mostra uma prévia da rota em uma janela separada."));
    ov->addWidget(previewButton);
    lv->addWidget(options);
    body->addWidget(left,2);

    // ---------------------------------------------------------- comandos/busca
    auto* commands=new QGroupBox(tr("Etapas da rota"),this);auto* commandsLayout=new QVBoxLayout(commands);
    auto* search=new QLineEdit(commands);search->setPlaceholderText(tr("Buscar comando…"));commandsLayout->addWidget(search);
    auto* commandGridHost=new QWidget(commands);auto* grid=new QGridLayout(commandGridHost);commandsLayout->addWidget(commandGridHost,1);
    auto commandButtons=std::make_shared<QVector<QPushButton*>>();
    auto registerButton=[commandButtons](QPushButton* button,const QString& type){button->setProperty("routeCommandType",type);commandButtons->push_back(button);};
    auto* pathTitle=new QLabel(tr("Caminho automático"),commandGridHost);pathTitle->setStyleSheet(QStringLiteral("font-weight:600;color:#9ed7ff"));grid->addWidget(pathTitle,0,0,1,3);
    auto addPath=[this,grid,commandGridHost,registerButton](const QString& text,core::MoveRoutePathBehavior behavior,int row,int column){
        auto* b=new QPushButton(text,commandGridHost);registerButton(b,QStringLiteral("pathfind ")+text);
        connect(b,&QPushButton::clicked,this,[this,behavior]{core::MoveCommand c;c.type=QStringLiteral("pathfind");core::MoveRoutePathOptions o;o.behavior=behavior;
            if(behavior==core::MoveRoutePathBehavior::Reach){o.targetKind=core::MoveRoutePathTargetKind::Cell;o.maxDistance=0;}
            else if(behavior==core::MoveRoutePathBehavior::Follow){o.targetKind=core::MoveRoutePathTargetKind::Player;o.maxDistance=1;}
            else if(behavior==core::MoveRoutePathBehavior::Flee){o.targetKind=core::MoveRoutePathTargetKind::Player;o.minDistance=o.maxDistance=4;}
            else{o.targetKind=core::MoveRoutePathTargetKind::Player;o.minDistance=2;o.maxDistance=4;}
            c.params=core::moveRoutePathOptionsToParams(o);if(!editPathfindCommand(m_ed,this,c,m_allowTargets,behavior))return;m_route.commands.push_back(c);reload(m_route.commands.size()-1);});
        grid->addWidget(b,row,column);
    };
    addPath(tr("Ir até…"),core::MoveRoutePathBehavior::Reach,1,0);addPath(tr("Seguir alvo…"),core::MoveRoutePathBehavior::Follow,1,1);
    addPath(tr("Fugir do alvo…"),core::MoveRoutePathBehavior::Flee,1,2);addPath(tr("Manter distância…"),core::MoveRoutePathBehavior::KeepDistance,2,0);
    auto* pathHint=new QLabel(tr("A prévia usa as mesmas regras de colisão e caminho do jogo, então o resultado deve corresponder ao playtest."),commandGridHost);
    pathHint->setWordWrap(true);pathHint->setStyleSheet(QStringLiteral("color:#8f98a3;font-size:10px"));grid->addWidget(pathHint,2,1,1,2);
    struct Def{const char*type;bool param;};const Def defs[]={
        {"moveDown",0},{"turnDown",0},{"walkAnimOn",0},{"moveLeft",0},{"turnLeft",0},{"walkAnimOff",0},
        {"moveRight",0},{"turnRight",0},{"stepAnimOn",0},{"moveUp",0},{"turnUp",0},{"stepAnimOff",0},
        {"moveDownLeft",0},{"turnRight90",0},{"dirFixOn",0},{"moveDownRight",0},{"turnLeft90",0},{"dirFixOff",0},
        {"moveUpLeft",0},{"turn180",0},{"throughOn",0},{"moveUpRight",0},{"turn90Random",0},{"throughOff",0},
        {"moveRandom",0},{"turnRandom",0},{"transparentOn",0},{"moveTowardPlayer",0},{"turnTowardPlayer",0},{"transparentOff",0},
        {"moveAwayPlayer",0},{"turnAwayPlayer",0},{"changeGraphic",1},{"stepForward",0},{"switchOn",1},{"opacity",1},
        {"stepBackward",0},{"switchOff",1},{"blend",1},{"jump",1},{"speed",1},{"playSE",1},{"wait",1},{"frequency",1},{"shake",1},{"rememberPosition",0}};
    int bi=0;for(const Def& def:defs){const QString type=QString::fromLatin1(def.type);auto* b=new QPushButton(labelOf(type),commandGridHost);registerButton(b,type);
        b->setToolTip(commandDescription(core::MoveCommand{type,{}}));connect(b,&QPushButton::clicked,this,[this,type,param=def.param]{if(param)addParameterized(type);else addSimple(type);});grid->addWidget(b,3+bi/3,bi%3);++bi;}
    connect(search,&QLineEdit::textChanged,this,[commandButtons](const QString& query){const QString q=query.trimmed();for(QPushButton* b:*commandButtons){const QString hay=(b->text()+QLatin1Char(' ')+b->property("routeCommandType").toString()).toLower();b->setVisible(q.isEmpty()||hay.contains(q.toLower()));}});
    body->addWidget(commands,3);

    // -------------------------------------------------------------- preview
    auto* previewBox=new QGroupBox(tr("Prévia da rota"),this);auto* pv=new QVBoxLayout(previewBox);
    auto* preview=new RoutePreviewWidget(m_ed,previewBox);preview->setObjectName(QStringLiteral("routePreviewWidget"));preview->setTarget(m_target->currentData().toString());preview->setRoute(&m_route);pv->addWidget(preview,1);
    auto* controls=new QHBoxLayout;auto* play=new QPushButton(tr("▶"),previewBox);auto* pause=new QPushButton(tr("Ⅱ"),previewBox);auto* reset=new QPushButton(tr("↺"),previewBox);auto* step=new QPushButton(tr("Passo"),previewBox);
    auto* speed=new QComboBox(previewBox);speed->addItem(QStringLiteral("0.5×"),.5);speed->addItem(QStringLiteral("1×"),1.0);speed->addItem(QStringLiteral("2×"),2.0);speed->addItem(QStringLiteral("4×"),4.0);speed->setCurrentIndex(1);
    controls->addWidget(play);controls->addWidget(pause);controls->addWidget(reset);controls->addWidget(step);controls->addStretch(1);controls->addWidget(new QLabel(tr("Velocidade:"),previewBox));controls->addWidget(speed);pv->addLayout(controls);
    auto* exactHint=new QLabel(tr("Esta prévia usa as mesmas regras de movimento do jogo. Interruptores e sons são simulados sem alterar o projeto."),previewBox);exactHint->setWordWrap(true);exactHint->setStyleSheet(QStringLiteral("color:#8f98a3;font-size:10px"));pv->addWidget(exactHint);
    connect(play,&QPushButton::clicked,preview,[preview]{preview->play();});connect(pause,&QPushButton::clicked,preview,[preview]{preview->pause();});connect(reset,&QPushButton::clicked,preview,[preview]{preview->reset();});connect(step,&QPushButton::clicked,preview,[preview]{preview->step();});
    connect(speed,&QComboBox::currentIndexChanged,preview,[speed,preview](int){preview->setSpeedMultiplier(speed->currentData().toDouble());});
    connect(m_target,&QComboBox::currentIndexChanged,preview,[this,preview](int){preview->setTarget(m_target->currentData().toString());});
    connect(m_repeat,&QCheckBox::toggled,preview,[this,preview](bool on){m_route.repeat=on;preview->reset();});
    connect(m_blockedPolicy,&QComboBox::currentIndexChanged,preview,[this,preview](int){m_route.blockedPolicy=core::moveRouteBlockedPolicyFromId(m_blockedPolicy->currentData().toString());preview->reset();});
    preview->onCommandChanged=[this](int row){if(row>=0&&row<m_list->count()&&!m_list->hasFocus())m_list->setCurrentRow(row);};
    auto* previewDialog = new CommandPreviewDialog(tr("Prévia da rota"), previewBox, this);
    previewDialog->resize(900, 680);
    connect(previewButton,&QPushButton::clicked,previewDialog,&CommandPreviewDialog::present);

    // ---------------------------------------------------------- edição em bloco
    auto clip=std::make_shared<QVector<core::MoveCommand>>();
    auto selectedRows=[this]{QVector<int> rows;for(QListWidgetItem* item:m_list->selectedItems())rows.push_back(m_list->row(item));std::sort(rows.begin(),rows.end());return rows;};
    auto copy=[this,clip,selectedRows]{clip->clear();for(int row:selectedRows())if(row>=0&&row<m_route.commands.size())clip->push_back(m_route.commands[row]);};
    auto eraseRows=[this](QVector<int> rows){std::sort(rows.begin(),rows.end(),std::greater<int>());for(int row:rows)if(row>=0&&row<m_route.commands.size())m_route.commands.remove(row);};
    auto cut=[this,copy,selectedRows,eraseRows]{const QVector<int> rows=selectedRows();if(rows.isEmpty())return;copy();eraseRows(rows);reload(qMin(rows.first(),m_route.commands.size()-1));};
    auto paste=[this,clip]{if(clip->isEmpty())return;int at=m_list->currentRow()>=0?m_list->currentRow()+1:m_route.commands.size();for(const auto& c:*clip)m_route.commands.insert(at++,c);reload(at-1);};
    auto duplicate=[this,selectedRows]{const QVector<int> rows=selectedRows();if(rows.isEmpty())return;QVector<core::MoveCommand> copies;for(int row:rows)copies.push_back(m_route.commands[row]);int at=rows.last()+1;for(const auto& c:copies)m_route.commands.insert(at++,c);reload(at-1);};
    auto removeSelected=[this,selectedRows,eraseRows]{const QVector<int> rows=selectedRows();if(rows.isEmpty())return;eraseRows(rows);reload(qMin(rows.first(),m_route.commands.size()-1));};
    auto moveSelection=[this,selectedRows](int delta){QVector<int> rows=selectedRows();if(rows.isEmpty())return;if(delta<0&&rows.first()==0)return;if(delta>0&&rows.last()==m_route.commands.size()-1)return;if(delta<0){for(int row:rows)m_route.commands.swapItemsAt(row,row-1);}else{for(auto it=rows.crbegin();it!=rows.crend();++it)m_route.commands.swapItemsAt(*it,*it+1);}reload(rows.first()+delta);for(int row:rows)m_list->item(row+delta)->setSelected(true);};
    connect(remove,&QPushButton::clicked,this,removeSelected);connect(up,&QToolButton::clicked,this,[moveSelection]{moveSelection(-1);});connect(down,&QToolButton::clicked,this,[moveSelection]{moveSelection(1);});
    auto shortcut=[this](const QKeySequence& key,std::function<void()> fn){auto* s=new QShortcut(key,m_list);connect(s,&QShortcut::activated,this,[fn]{fn();});};
    shortcut(QKeySequence::Copy,copy);shortcut(QKeySequence::Cut,cut);shortcut(QKeySequence::Paste,paste);shortcut(QKeySequence(Qt::CTRL|Qt::Key_D),duplicate);shortcut(QKeySequence::Delete,removeSelected);shortcut(QKeySequence(Qt::Key_Return),[this]{editCommand(m_list->currentRow());});
    connect(m_list,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){editCommand(m_list->currentRow());});
    connect(m_list,&QListWidget::currentRowChanged,this,[this,description](int row){if(row<0||row>=m_route.commands.size())description->setText(tr("Selecione um comando para ver como ele funciona."));else description->setText(QStringLiteral("<b>%1</b><br>%2").arg(commandLabel(m_route.commands[row]).toHtmlEscaped(),commandDescription(m_route.commands[row]).toHtmlEscaped()));});
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);connect(m_list,&QListWidget::customContextMenuRequested,this,[=](const QPoint& pos){QMenu menu(this);menu.addAction(tr("Editar"),this,[this]{editCommand(m_list->currentRow());});menu.addSeparator();menu.addAction(tr("Recortar"),cut);menu.addAction(tr("Copiar"),copy);QAction* pa=menu.addAction(tr("Colar"),paste);pa->setEnabled(!clip->isEmpty());menu.addAction(tr("Duplicar"),duplicate);menu.addSeparator();menu.addAction(tr("Excluir"),removeSelected);menu.exec(m_list->mapToGlobal(pos));});
    if(auto* routeList=dynamic_cast<RouteCommandListWidget*>(m_list))routeList->onInternalDrop=[this,preview]{
        const QVector<core::MoveCommand> old=m_route.commands;QVector<core::MoveCommand> reordered;reordered.reserve(old.size());
        for(int row=0;row<m_list->count();++row){const int source=m_list->item(row)->data(Qt::UserRole).toInt();if(source>=0&&source<old.size())reordered.push_back(old[source]);}
        if(reordered.size()==old.size()){m_route.commands=reordered;reload(m_list->currentRow());preview->reset();}
    };

    // --------------------------------------------------------------- presets callbacks
    auto userPresets=[presets]{QVector<core::MoveRoutePreset> users;for(const auto& item:*presets)if(!item.builtIn)users.push_back(item);return users;};
    auto selectedPreset=[presets,presetCombo]()->std::optional<core::MoveRoutePreset>{const QString id=presetCombo->currentData().toString();for(const auto& item:*presets)if(item.id==id)return item;return std::nullopt;};
    connect(applyPreset,&QPushButton::clicked,this,[=]{const auto chosen=selectedPreset();if(!chosen)return;
        const QString target=m_route.target;const auto startMode=m_route.startMode;const bool wait=m_route.waitForCompletion;m_route=chosen->route;m_route.target=allowTargets?target:QStringLiteral("self");m_route.startMode=allowTargets?startMode:core::MoveRouteStartMode::Replace;m_route.waitForCompletion=allowTargets?wait:false;
        m_repeat->setChecked(m_route.repeat);m_blockedPolicy->setCurrentIndex(qMax(0,m_blockedPolicy->findData(core::moveRouteBlockedPolicyId(m_route.blockedPolicy))));reload();preview->reset();});
    connect(savePreset,&QPushButton::clicked,this,[=]{if(!presetLoadError->isEmpty())return;bool ok=false;const QString name=QInputDialog::getText(this,tr("Salvar preset de rota"),tr("Nome:"),QLineEdit::Normal,QString(),&ok).simplified();if(!ok||name.isEmpty())return;core::MoveRoutePreset preset;preset.name=name;preset.route=m_route;QString error;if(!core::normalizeMoveRoutePreset(&preset,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}auto users=userPresets();users.push_back(preset);if(!core::saveUserMoveRoutePresets(m_ed.projectRoot(),users,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}refreshPresets();presetCombo->setCurrentIndex(presetCombo->findData(preset.id));});
    connect(updatePreset,&QPushButton::clicked,this,[=]{if(!presetLoadError->isEmpty())return;const auto chosen=selectedPreset();if(!chosen||chosen->builtIn)return;auto users=userPresets();QString error;for(auto& item:users)if(item.id==chosen->id){item.route=m_route;if(!core::normalizeMoveRoutePreset(&item,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}break;}if(!core::saveUserMoveRoutePresets(m_ed.projectRoot(),users,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}refreshPresets();});
    connect(duplicatePreset,&QPushButton::clicked,this,[=]{if(!presetLoadError->isEmpty())return;const auto chosen=selectedPreset();if(!chosen)return;bool ok=false;const QString name=QInputDialog::getText(this,tr("Duplicar preset"),tr("Nome da cópia:"),QLineEdit::Normal,tr("%1 — Cópia").arg(chosen->name),&ok).simplified();if(!ok||name.isEmpty())return;core::MoveRoutePreset copy=*chosen;copy.builtIn=false;copy.id.clear();copy.name=name;QString error;if(!core::normalizeMoveRoutePreset(&copy,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}auto users=userPresets();users.push_back(copy);if(!core::saveUserMoveRoutePresets(m_ed.projectRoot(),users,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}refreshPresets();presetCombo->setCurrentIndex(presetCombo->findData(copy.id));});
    connect(renamePreset,&QPushButton::clicked,this,[=]{if(!presetLoadError->isEmpty())return;const auto chosen=selectedPreset();if(!chosen||chosen->builtIn)return;bool ok=false;const QString name=QInputDialog::getText(this,tr("Renomear preset"),tr("Nome:"),QLineEdit::Normal,chosen->name,&ok).simplified();if(!ok||name.isEmpty())return;auto users=userPresets();QString error;for(auto& item:users)if(item.id==chosen->id){item.name=name;if(!core::normalizeMoveRoutePreset(&item,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}break;}if(!core::saveUserMoveRoutePresets(m_ed.projectRoot(),users,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}refreshPresets();});
    connect(deletePreset,&QPushButton::clicked,this,[=]{if(!presetLoadError->isEmpty())return;const auto chosen=selectedPreset();if(!chosen||chosen->builtIn)return;if(QMessageBox::question(this,tr("Excluir preset"),tr("Excluir o preset ‘%1’?" ).arg(chosen->name))!=QMessageBox::Yes)return;auto users=userPresets();users.erase(std::remove_if(users.begin(),users.end(),[&](const auto& item){return item.id==chosen->id;}),users.end());QString error;if(!core::saveUserMoveRoutePresets(m_ed.projectRoot(),users,&error)){QMessageBox::warning(this,tr("Preset de rota"),error);return;}refreshPresets();});

    auto* box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);outer->addWidget(box);
    connect(box,&QDialogButtonBox::accepted,this,[this,allowTargets]{m_route.repeat=m_repeat->isChecked();m_route.blockedPolicy=core::moveRouteBlockedPolicyFromId(m_blockedPolicy->currentData().toString());if(allowTargets){m_route.startMode=core::moveRouteStartModeFromId(m_startMode->currentData().toString());m_route.waitForCompletion=m_wait->isChecked();m_route.target=m_target->currentData().toString();}else{m_route.target=QStringLiteral("self");m_route.startMode=core::MoveRouteStartMode::Replace;m_route.waitForCompletion=false;}accept();});
    connect(box,&QDialogButtonBox::rejected,this,&QDialog::reject);
    reload();
}

void MoveRouteDialog::addSimple(const QString& type)
{
    m_route.commands.push_back(core::MoveCommand{type,{}});
    reload(m_route.commands.size()-1);
}

void MoveRouteDialog::addParameterized(const QString& type)
{
    core::MoveCommand c; c.type=type;
    bool ok=false;
    if(type==QLatin1String("pathfind")) {
        core::MoveRoutePathOptions o;c.params=core::moveRoutePathOptionsToParams(o);
        if(!editPathfindCommand(m_ed,this,c,m_allowTargets))return;
        m_route.commands.push_back(c);reload(m_route.commands.size()-1);return;
    }
    if(type==QLatin1String("wait")) {
        int v=QInputDialog::getInt(this,tr("Esperar"),tr("Quadros:"),30,1,9999,1,&ok); if(!ok)return; c.params["frames"]=v;
    } else if(type==QLatin1String("jump")) {
        QDialog d(this); d.setWindowTitle(tr("Saltar")); auto* v=new QVBoxLayout(&d); auto* f=new QFormLayout;
        auto* x=new QSpinBox(&d); x->setRange(-99,99); auto* y=new QSpinBox(&d); y->setRange(-99,99); f->addRow(tr("X:"),x);f->addRow(tr("Y:"),y);v->addLayout(f);
        auto* bb=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(bb);connect(bb,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(bb,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;c.params["x"]=x->value();c.params["y"]=y->value();
    } else if(type==QLatin1String("switchOn")||type==QLatin1String("switchOff")) {
        int id=QInputDialog::getInt(this,tr("Interruptor"),tr("Número:"),1,1,9999,1,&ok);if(!ok)return;c.params["id"]=id;
    } else if(type==QLatin1String("speed")) {
        const QStringList labels={tr("1: 8× mais lento"),tr("2: 4× mais lento"),tr("3: 2× mais lento"),tr("4: Normal"),tr("5: 2× mais rápido"),tr("6: 4× mais rápido")};const QVector<double> vals={.375,.75,1.5,3,6,12};QString s=QInputDialog::getItem(this,tr("Velocidade"),tr("Velocidade:"),labels,3,false,&ok);if(!ok)return;c.params["value"]=vals[labels.indexOf(s)];
    } else if(type==QLatin1String("frequency")) {
        const QStringList labels={tr("1: Mínima"),tr("2: Baixa"),tr("3: Normal"),tr("4: Alta"),tr("5: Máxima")};QString s=QInputDialog::getItem(this,tr("Frequência"),tr("Frequência:"),labels,2,false,&ok);if(!ok)return;c.params["value"]=labels.indexOf(s)+1;
    } else if(type==QLatin1String("opacity")) {
        int n=QInputDialog::getInt(this,tr("Opacidade"),tr("0 a 255:"),255,0,255,1,&ok);if(!ok)return;c.params["value"]=n;
    } else if(type==QLatin1String("blend")) {
        QStringList ids={"normal","add","multiply","screen"};
        QStringList labels={tr("Normal"),tr("Somar"),tr("Multiplicar"),tr("Clarear")};
        QString s=QInputDialog::getItem(this,tr("Mistura"),tr("Modo:"),labels,0,false,&ok);if(!ok)return;c.params["value"]=ids[labels.indexOf(s)];
    } else if(type==QLatin1String("shake")) {
        QDialog d(this); d.setWindowTitle(tr("Tremer sprite"));
        auto* v=new QVBoxLayout(&d); auto* f=new QFormLayout;
        auto makeAmp=[&](double value){auto* s=new QDoubleSpinBox(&d);s->setRange(0,128);s->setDecimals(1);s->setValue(value);s->setSuffix(tr(" px"));return s;};
        auto* x=makeAmp(4); auto* y=makeAmp(0);
        auto* dur=new QDoubleSpinBox(&d);dur->setRange(.01,9999);dur->setDecimals(2);dur->setValue(30);
        auto* unit=new QComboBox(&d);unit->addItem(tr("Quadros"),"frames");unit->addItem(tr("Segundos"),"seconds");
        f->addRow(tr("Tremor horizontal:"),x);f->addRow(tr("Tremor vertical:"),y);
        f->addRow(tr("Duração:"),dur);f->addRow(tr("Unidade:"),unit);v->addLayout(f);
        auto* hint=new QLabel(tr("Use 0 px para desativar um dos eixos. O tremor é apenas visual e não altera colisões."),&d);hint->setWordWrap(true);v->addWidget(hint);
        auto* bb=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(bb);connect(bb,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(bb,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        if(d.exec()!=QDialog::Accepted)return;
        c.params["x"]=x->value();c.params["y"]=y->value();c.params["duration"]=dur->value();c.params["unit"]=unit->currentData().toString();
    } else if(type==QLatin1String("playSE")) {
        QString source;
        int volume = 90;
        if (!chooseGameAudio(m_ed, this, tr("Escolher efeito sonoro"), source, volume, QStringLiteral("SE"))) return;
        c.params["source"] = source;
        c.params["volume"] = volume;
    } else if(type==QLatin1String("changeGraphic")) {
        core::EventGraphic g; g.kind=core::EventGraphic::Charset;
        if(!UniversalAssetPickerDialog::chooseCharacterGraphic(m_ed,this,g,tr("Selecionar personagem")))return;
        if(g.charset.isNull())return;
        c.params["image"]=core::io::imageToDataUri(g.charset);
        c.params["source"]=g.sourcePath;c.params["cols"]=g.charsetCols;c.params["rows"]=g.charsetRows;
        c.params["characterCols"]=g.characterCols;c.params["characterRows"]=g.characterRows;
        c.params["characterIndex"]=g.characterIndex;c.params["dir"]=g.dir;c.params["frame"]=g.frame;
    }
    m_route.commands.push_back(c); reload(m_route.commands.size()-1);
}

bool MoveRouteDialog::editCommand(int row)
{
    if(row<0||row>=m_route.commands.size())return false;core::MoveCommand c=m_route.commands[row];bool ok=false;
    if(c.type=="pathfind"){ok=editPathfindCommand(m_ed,this,c,m_allowTargets);}
    else if(c.type=="changeGraphic"){core::EventGraphic g;g.kind=core::EventGraphic::Charset;g.charset=core::io::dataUriToImage(c.params.value("image").toString());g.sourcePath=c.params.value("source").toString();g.charsetCols=c.params.value("cols",3).toInt();g.charsetRows=c.params.value("rows",4).toInt();g.characterCols=c.params.value("characterCols",1).toInt();g.characterRows=c.params.value("characterRows",1).toInt();g.characterIndex=c.params.value("characterIndex").toInt();g.dir=c.params.value("dir").toInt();g.frame=c.params.value("frame").toInt();if(!UniversalAssetPickerDialog::chooseCharacterGraphic(m_ed,this,g,tr("Selecionar personagem")))return false;c.params["image"]=core::io::imageToDataUri(g.charset);c.params["source"]=g.sourcePath;c.params["cols"]=g.charsetCols;c.params["rows"]=g.charsetRows;c.params["characterCols"]=g.characterCols;c.params["characterRows"]=g.characterRows;c.params["characterIndex"]=g.characterIndex;c.params["dir"]=g.dir;c.params["frame"]=g.frame;ok=true;}
    else if(c.type=="wait"){int n=QInputDialog::getInt(this,tr("Esperar"),tr("Quadros:"),c.params.value("frames",30).toInt(),1,9999,1,&ok);if(ok)c.params["frames"]=n;}
    else if(c.type=="opacity"){int n=QInputDialog::getInt(this,tr("Opacidade"),tr("0 a 255:"),c.params.value("value",255).toInt(),0,255,1,&ok);if(ok)c.params["value"]=n;}
    else if(c.type=="speed"||c.type=="frequency"){const bool speed=c.type=="speed";QStringList labels=speed?QStringList{tr("1: 8× mais lento"),tr("2: 4× mais lento"),tr("3: 2× mais lento"),tr("4: Normal"),tr("5: 2× mais rápido"),tr("6: 4× mais rápido")}:QStringList{tr("1: Mínima"),tr("2: Baixa"),tr("3: Normal"),tr("4: Alta"),tr("5: Máxima")};QVector<double> values={.375,.75,1.5,3,6,12};int cur=qBound(0,c.params.value("value",3).toInt()-1,4);if(speed){const double current=c.params.value("value",3.0).toDouble();cur=0;for(int i=1;i<values.size();++i)if(qAbs(values[i]-current)<qAbs(values[cur]-current))cur=i;}QString s=QInputDialog::getItem(this,speed?tr("Velocidade"):tr("Frequência"),tr("Valor:"),labels,cur,false,&ok);if(ok){if(speed)c.params["value"]=values[labels.indexOf(s)];else c.params["value"]=labels.indexOf(s)+1;}}
    else if(c.type=="jump"){QDialog d(this);auto*v=new QVBoxLayout(&d);auto*f=new QFormLayout;auto*x=new QSpinBox(&d);auto*y=new QSpinBox(&d);x->setRange(-99,99);y->setRange(-99,99);x->setValue(c.params.value("x").toInt());y->setValue(c.params.value("y").toInt());f->addRow("X",x);f->addRow("Y",y);v->addLayout(f);auto*b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(b);connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);ok=d.exec()==QDialog::Accepted;if(ok){c.params["x"]=x->value();c.params["y"]=y->value();}}
    else if(c.type=="switchOn"||c.type=="switchOff"){int n=QInputDialog::getInt(this,tr("Interruptor"),tr("Número:"),c.params.value("id",1).toInt(),1,9999,1,&ok);if(ok)c.params["id"]=n;}
    else if(c.type=="blend"){QStringList ids={"normal","add","multiply","screen"},labels={tr("Normal"),tr("Somar"),tr("Multiplicar"),tr("Clarear")};int cur=qMax(0,ids.indexOf(c.params.value("value","normal").toString()));QString s=QInputDialog::getItem(this,tr("Mistura"),tr("Modo:"),labels,cur,false,&ok);if(ok)c.params["value"]=ids[labels.indexOf(s)];}
    else if(c.type=="playSE"){
        QString source=c.params.value("source").toString();
        int volume=qBound(0,c.params.value("volume",90).toInt(),100);
        ok=chooseGameAudio(m_ed,this,tr("Escolher efeito sonoro"),source,volume,QStringLiteral("SE"));
        if(ok){c.params["source"]=source;c.params["volume"]=volume;}
    }
    else if(c.type=="shake"){
        QDialog d(this);d.setWindowTitle(tr("Tremer sprite"));auto*v=new QVBoxLayout(&d);auto*f=new QFormLayout;
        auto makeAmp=[&](double value){auto*s=new QDoubleSpinBox(&d);s->setRange(0,128);s->setDecimals(1);s->setValue(value);s->setSuffix(tr(" px"));return s;};
        auto*x=makeAmp(c.params.value("x",4.0).toDouble());auto*y=makeAmp(c.params.value("y",0.0).toDouble());
        auto*dur=new QDoubleSpinBox(&d);dur->setRange(.01,9999);dur->setDecimals(2);dur->setValue(c.params.value("duration",30.0).toDouble());
        auto*unit=new QComboBox(&d);unit->addItem(tr("Quadros"),"frames");unit->addItem(tr("Segundos"),"seconds");unit->setCurrentIndex(qMax(0,unit->findData(c.params.value("unit","frames"))));
        f->addRow(tr("Tremor horizontal:"),x);f->addRow(tr("Tremor vertical:"),y);f->addRow(tr("Duração:"),dur);f->addRow(tr("Unidade:"),unit);v->addLayout(f);
        auto*b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&d);v->addWidget(b);connect(b,&QDialogButtonBox::accepted,&d,&QDialog::accept);connect(b,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        ok=d.exec()==QDialog::Accepted;if(ok){c.params["x"]=x->value();c.params["y"]=y->value();c.params["duration"]=dur->value();c.params["unit"]=unit->currentData().toString();}
    }
    else return false;
    if(ok){m_route.commands[row]=c;reload(row);}return ok;
}

void MoveRouteDialog::reload(int row)
{
    if(!m_list)return;
    m_list->clear();
    for(int i=0;i<m_route.commands.size();++i){
        auto* item=new QListWidgetItem(commandLabel(m_route.commands[i]),m_list);
        item->setData(Qt::UserRole,i); // identidade efêmera para drag-and-drop interno
        item->setToolTip(commandDescription(m_route.commands[i]));
    }
    if(row<0&&m_list->count())row=m_list->count()-1;
    row=qBound(-1,row,m_list->count()-1);
    if(row>=0)m_list->setCurrentRow(row);
    if(QWidget* widget=findChild<QWidget*>(QStringLiteral("routePreviewWidget")))
        if(auto* preview=dynamic_cast<RoutePreviewWidget*>(widget))preview->reset();
}

} // namespace ui
