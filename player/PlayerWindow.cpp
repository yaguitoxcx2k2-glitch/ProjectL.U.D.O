#include "PlayerWindow.h"

#include "game/GameSave.h"
#include "game/GameStateTransition.h"
#include "game/RuntimeGpuPolicy.h"
#include "game/RuntimePreloader.h"
#include "game/ui/UiDataBinding.h"
#include "game/ui/UiNavigation.h"
#include "game/ui/UiPainterRenderer.h"
#include "game/ui/UiWidgetRenderer.h"
#include "game/ui/UiWidgetBehavior.h"
#include "game/RhiGameWindow.h"
#include "game/RuntimeWindowUtils.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QEventLoop>
#include <QDebug>
#include <QFileInfo>
#include <QIcon>
#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTransform>
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QHBoxLayout>
#include <QListWidget>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace player {

namespace {

game::ui::UiTheme accessibleTitleTheme(const core::Editor& project)
{
    game::ui::UiTheme theme = game::ui::UiTheme::fromSettings(project.gameUi);
    if (!project.accessibility.enabled) return theme;
    QSettings settings;
    const int scalePercent = project.accessibility.allowUiScale
        ? qBound(100, settings.value(QStringLiteral("game/uiScalePercent"), project.accessibility.defaultUiScalePercent).toInt(), 150) : 100;
    const qreal factor = scalePercent / 100.0;
    theme.fontSize = qMax(6, qRound(theme.fontSize * factor));
    theme.paddingX = qMax(0, qRound(theme.paddingX * factor));
    theme.paddingY = qMax(0, qRound(theme.paddingY * factor));
    if (settings.value(QStringLiteral("game/strongFocus"), project.accessibility.strongFocusDefault).toBool()) {
        theme.selection.borderWidth = qMax<qreal>(4.0, theme.selection.borderWidth * 1.75);
        theme.selection.innerBorderWidth = qMax<qreal>(2.0, theme.selection.innerBorderWidth * 1.5);
        theme.selection.fill.setAlpha(qMax(210, theme.selection.fill.alpha()));
        theme.selection.border = Qt::white;
        theme.selectedText = Qt::white;
    }
    return theme;
}

const core::UiVisualLogicNodeSettings* titleLogicNodeById(const core::UiVisualLogicGraphSettings& graph,const QString&id)
{
    for(const auto&node:graph.nodes)if(node.id==id)return &node;
    return nullptr;
}

const core::UiVisualLogicGraphSettings* titleLogicGraphById(const core::UiLayoutElementSettings&meta,const QString&id)
{
    for(const auto&graph:meta.visualLogicGraphs)if(graph.id==id)return &graph;
    return nullptr;
}

QVector<const core::UiVisualLogicLinkSettings*> titleLogicOutgoing(const core::UiVisualLogicGraphSettings&graph,const QString&nodeId,const QString&port=QString())
{
    QVector<const core::UiVisualLogicLinkSettings*> out;
    for(const auto&link:graph.links){
        if(link.fromNodeId!=nodeId)continue;
        if(!port.isEmpty()&&link.fromPort!=port)continue;
        out.push_back(&link);
    }
    std::stable_sort(out.begin(),out.end(),[](const auto*a,const auto*b){return a->order<b->order;});
    return out;
}

QString initializePlayerLocale(const core::Editor& project)
{
    if (!project.localization.enabled) return {};
    QSettings settings;
    QString requested;
    if (project.localization.initialMode == QLatin1String("last"))
        requested = settings.value(QStringLiteral("game/locale")).toString();
    else if (project.localization.initialMode == QLatin1String("system"))
        requested = core::systemLocaleCode();
    else
        requested = project.localization.defaultLocale;
    return core::setPlayerLocale(project.localization, requested);
}

}

PlayerWindow::PlayerWindow(core::Editor& project, QWidget* parent, bool quitApplicationOnClose)
    : QWidget(parent), m_project(project), m_quitApplicationOnClose(quitApplicationOnClose)
{
    setWindowTitle(project.projectName);
    resize(qMax(640, project.gameResolution.width()), qMax(420, project.gameResolution.height()));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString& iconName : {QStringLiteral("game-icon.ico"),QStringLiteral("game-icon.png"),QStringLiteral("game-icon.jpg"),QStringLiteral("game-icon.jpeg"),QStringLiteral("game-icon.bmp")}) {
        const QString iconPath=appDir.filePath(iconName);
        if(QFileInfo::exists(iconPath)){setWindowIcon(QIcon(iconPath));break;}
    }
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    if (!project.titleScreen.backgroundPath.isEmpty()) {
        m_background = project.preloadedRuntimeImage(project.titleScreen.backgroundPath);
        if (m_background.isNull()) {
            const QString path = QFileInfo(project.titleScreen.backgroundPath).isAbsolute()
                ? project.titleScreen.backgroundPath
                : QDir(project.projectRoot()).filePath(project.titleScreen.backgroundPath);
            m_background.load(path);
        }
    }

    // Bloco F: resolve o idioma inicial antes de desenhar qualquer variante da
    // Tela de Título. "default" sempre começa no padrão, "system" detecta o
    // Windows e "last" reaproveita explicitamente a última escolha do jogador.
    initializePlayerLocale(project);
    m_designerTitle = hasDesignerTitle();
    m_hasSave = hasAnySave();
    m_titleState.resetFrom(project);
    m_titleTheme = accessibleTitleTheme(project);
    m_titleElapsed.start();
    m_titleLastLogicTickMs=m_titleElapsed.elapsed();

    if (!m_designerTitle) {
        const QString titleColor = project.titleScreen.titleColor.name();
        const QString accentColor = project.titleScreen.accentColor.name();
        QSettings accessibilitySettings;
        const int titleScale = project.accessibility.enabled && project.accessibility.allowUiScale
            ? qBound(100, accessibilitySettings.value(QStringLiteral("game/uiScalePercent"), project.accessibility.defaultUiScalePercent).toInt(), 150) : 100;
        const bool strongFocus = project.accessibility.enabled && accessibilitySettings.value(QStringLiteral("game/strongFocus"), project.accessibility.strongFocusDefault).toBool();
        const int buttonFont = qRound(16.0 * titleScale / 100.0); const int py=qRound(10.0*titleScale/100.0), px=qRound(28.0*titleScale/100.0);
        setStyleSheet(QStringLiteral("QLabel{color:%1;} QPushButton{font-size:%3px;padding:%4px %5px;min-width:240px;border:%6px solid %2;border-radius:5px;background:rgba(15,23,42,215);color:%1;} QPushButton:hover,QPushButton:focus{background:%2;color:#10141f;} QPushButton:disabled{color:#6b7280;border-color:#4b5563;}")
                          .arg(titleColor, accentColor).arg(buttonFont).arg(py).arg(px).arg(strongFocus ? 4 : 1));
        auto* root=new QVBoxLayout(this);root->setContentsMargins(70,60,70,60);root->addStretch();
        const QString configuredTitle = core::resolvePlayerText(project.localization,project.titleScreen.titleTextKey,project.titleScreen.titleText.trimmed());
        m_titleLabel=new QLabel(configuredTitle.isEmpty()?project.projectName:configuredTitle,this);m_titleLabel->setAlignment(Qt::AlignCenter);QFont font=m_titleLabel->font();font.setPointSize(30);font.setBold(true);m_titleLabel->setFont(font);root->addWidget(m_titleLabel);root->addSpacing(45);
        auto add=[&](const QString& text){auto* b=new QPushButton(text,this);root->addWidget(b,0,Qt::AlignHCenter);return b;};
        auto* start=add(core::resolvePlayerText(project.localization,QStringLiteral("system.title.new_game"),tr("Novo jogo")));m_continue=add(core::resolvePlayerText(project.localization,QStringLiteral("system.title.continue"),tr("Continuar")));m_continue->setVisible(project.titleScreen.showContinue);auto* quit=add(core::resolvePlayerText(project.localization,QStringLiteral("system.title.quit"),tr("Sair")));m_titleButtons={start,m_continue,quit};start->setFocus();root->addStretch();
        connect(start,&QPushButton::clicked,this,&PlayerWindow::newGame);connect(m_continue,&QPushButton::clicked,this,&PlayerWindow::continueGame);connect(quit,&QPushButton::clicked,this,[this]{if(m_quitApplicationOnClose)qApp->quit();else close();});
    } else {
        rebuildDesignerTitle();
    }

    if (m_directMapStartup) {
        // Nao deixe widgets legados piscarem por cima do framebuffer preto
        // durante o preload. Eles continuam compilados apenas para leitura de
        // projetos antigos e podem ser removidos numa migracao futura.
        if (m_titleLabel) m_titleLabel->hide();
        for (QPushButton* button : m_titleButtons) if (button) button->hide();
    }

    auto* gamepadTimer=new QTimer(this);
    connect(gamepadTimer,&QTimer::timeout,this,[this]{
        if (m_directMapStartup) return;
        pollTitleGamepad();
        if(m_designerTitle&&!m_game){
            const qint64 now=m_titleElapsed.elapsed();
            const int delta=int(qBound<qint64>(qint64(0),now-m_titleLastLogicTickMs,qint64(250)));
            m_titleLastLogicTickMs=now;
            updateTitleVisualLogic(delta);
            update();
        }
    });
    if (!m_directMapStartup) gamepadTimer->start(16);
}

PlayerWindow::~PlayerWindow()
{
    // No playtest o snapshot Editor e o QRhiWidget sao ambos filhos desta
    // janela. QObject pode destruir o snapshot antes do renderer, embora a
    // GameSession do renderer ainda o referencie. Encerre o jogo primeiro;
    // depois a destruicao normal pode liberar o snapshot com seguranca.
    if (m_game) {
        QWidget* game = m_game;
        m_game = nullptr;
        game->setParent(nullptr);
        delete game;
    }
}

void PlayerWindow::pollTitleGamepad()
{
    if(!isVisible()||m_game)return;
    const QSet<core::GameAction> current=m_gamepad.poll(m_project.inputSystem);
    if(!current.isEmpty()&&m_project.inputSystem.adaptivePrompts)m_project.inputSystem.forcedController=QStringLiteral("generic");
    auto pressed=[&](core::GameAction action){ return current.contains(action)&&!m_gamepadHeld.contains(action); };
    if(m_builtInLoadPanel&&m_builtInLoadPanel->isVisible()){
        if(m_builtInLoadList&&m_builtInLoadList->count()>0){int row=qMax(0,m_builtInLoadList->currentRow());if(pressed(core::GameAction::Up))m_builtInLoadList->setCurrentRow(qMax(0,row-1));if(pressed(core::GameAction::Down))m_builtInLoadList->setCurrentRow(qMin(m_builtInLoadList->count()-1,row+1));if(pressed(core::GameAction::Confirm)){auto*item=m_builtInLoadList->currentItem();if(item)startGame(item->data(Qt::UserRole).toInt());}}
        if(pressed(core::GameAction::Cancel)||pressed(core::GameAction::Quit))hideBuiltInLoadPanel();m_gamepadHeld=current;return;
    }
    if (m_designerTitle) {
        if(pressed(core::GameAction::Up)&&!handleTitleNativeDirection(core::GameAction::Up)) moveTitleFocus(core::GameAction::Up);
        if(pressed(core::GameAction::Down)&&!handleTitleNativeDirection(core::GameAction::Down)) moveTitleFocus(core::GameAction::Down);
        if(pressed(core::GameAction::Left)&&!handleTitleNativeDirection(core::GameAction::Left)) moveTitleFocus(core::GameAction::Left);
        if(pressed(core::GameAction::Right)&&!handleTitleNativeDirection(core::GameAction::Right)) moveTitleFocus(core::GameAction::Right);
        if(pressed(core::GameAction::Confirm)) activateTitleWidget(m_titleFocusId);
        if(pressed(core::GameAction::Cancel)){ if(m_quitApplicationOnClose)qApp->quit();else close(); }
        m_gamepadHeld=current;
        return;
    }
    auto moveFocus=[this](int delta){
        QVector<QPushButton*> available;for(QPushButton* button:m_titleButtons)if(button&&button->isVisible()&&button->isEnabled())available.push_back(button);
        if(available.isEmpty())return;int index=0;for(int i=0;i<available.size();++i)if(available[i]->hasFocus()){index=i;break;}
        index=(index+delta+available.size())%available.size();available[index]->setFocus();
    };
    if(pressed(core::GameAction::Up))moveFocus(-1);
    if(pressed(core::GameAction::Down))moveFocus(1);
    if(pressed(core::GameAction::Confirm))
        for(QPushButton* button:m_titleButtons)if(button&&button->hasFocus()&&button->isEnabled()){button->click();break;}
    if(pressed(core::GameAction::Cancel)){ if(m_quitApplicationOnClose)qApp->quit();else close(); }
    m_gamepadHeld=current;
}

bool PlayerWindow::hasAnySave() const
{
    for(int slot=1;slot<=99;++slot){game::GameSaveSummary summary;if(game::readGameSaveSummary(game::gameSavePath(m_project,slot),m_project,&summary))return true;}
    return false;
}

bool PlayerWindow::hasDesignerTitle() const
{
    for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)
        if(it.value().screen==QLatin1String("title"))return true;
    return false;
}

bool PlayerWindow::designerScreenUsesAction(const QString& screen,const QString&actionType) const
{
    for(auto wit=m_project.gameUi.widgets.cbegin();wit!=m_project.gameUi.widgets.cend();++wit){
        if(wit.value().screen!=screen)continue;
        const auto meta=m_project.gameUi.layoutElements.value(wit.key());
        for(const auto&binding:meta.eventBindings)for(const auto&action:binding.actions)if(action.type==actionType)return true;
        for(const auto&graph:meta.visualLogicGraphs)for(const auto&node:graph.nodes)
            if(node.type==QLatin1String("action")&&node.action.type==actionType)return true;
    }
    return false;
}

bool PlayerWindow::titleConditionPasses(const core::UiEventConditionSettings& condition) const
{
    int current=0;
    if(condition.source==QLatin1String("always"))return true;
    if(condition.source==QLatin1String("switch"))current=m_titleState.switchOn(condition.id)?1:0;
    else if(condition.source==QLatin1String("variable"))current=m_titleState.variable(condition.id);
    else if(condition.source==QLatin1String("gold"))current=m_titleState.gold();
    else return false;
    if(condition.op==QLatin1String("!="))return current!=condition.value;
    if(condition.op==QLatin1String(">"))return current>condition.value;
    if(condition.op==QLatin1String(">="))return current>=condition.value;
    if(condition.op==QLatin1String("<"))return current<condition.value;
    if(condition.op==QLatin1String("<="))return current<=condition.value;
    return current==condition.value;
}

bool PlayerWindow::titleWidgetVisible(const QString& id) const
{
    const auto& settings=m_project.gameUi;
    QString current=id;QSet<QString>visited;
    while(!current.isEmpty()){
        if(visited.contains(current))return false;visited.insert(current);
        const auto wit=settings.widgets.constFind(current);if(wit==settings.widgets.cend()||wit.value().screen!=m_titleUiScreenId)return false;
        const core::UiLayoutElementSettings meta=settings.layoutElements.value(current);
        const auto data=game::ui::UiDataBindingResolver::resolveRuntime(meta,m_project,m_titleState);
        bool visible=data.hasVisible?data.visible:meta.visible;
        if(m_titleRuntimeVisibility.contains(current))visible=m_titleRuntimeVisibility.value(current);
        if(!m_titleScreenStateId.isEmpty()){
            const auto st=settings.screenStates.constFind(m_titleScreenStateId);
            if(st!=settings.screenStates.cend()){
                const auto ov=st.value().elements.constFind(current);
                if(ov!=st.value().elements.cend()&&ov.value().hasVisible)visible=visible&&ov.value().visible;
            }
        }
        if(!visible)return false;
        const QString parent=meta.parentId;
        if(parent.isEmpty()||!settings.widgets.contains(parent))return true;
        current=parent;
    }
    return true;
}

bool PlayerWindow::titleWidgetEnabled(const QString& id) const
{
    if(!titleWidgetVisible(id))return false;
    const auto& settings=m_project.gameUi;
    const auto wit=settings.widgets.constFind(id);if(wit==settings.widgets.cend()||!wit.value().interactive)return false;
    const core::UiLayoutElementSettings meta=settings.layoutElements.value(id);
    const auto data=game::ui::UiDataBindingResolver::resolveRuntime(meta,m_project,m_titleState);
    bool enabled=data.hasEnabled?data.enabled:true;
    if(!m_titleScreenStateId.isEmpty()){
        const auto st=settings.screenStates.constFind(m_titleScreenStateId);
        if(st!=settings.screenStates.cend()){
            const auto ov=st.value().elements.constFind(id);
            if(ov!=st.value().elements.cend()&&ov.value().hasEnabled)enabled=enabled&&ov.value().enabled;
        }
    }
    if(enabled&&!m_hasSave){
        for(const auto&binding:meta.eventBindings)for(const auto&action:binding.actions)
            if(action.type==QLatin1String("title-continue"))return false;
    }
    if(enabled&&m_titleUiScreenId==QLatin1String("load")){
        const auto slotAvailable=[this](int slot){game::GameSaveSummary summary;return game::readGameSaveSummary(game::gameSavePath(m_project,qBound(1,slot,99)),m_project,&summary);};
        for(const auto&binding:meta.eventBindings)for(const auto&action:binding.actions)
            if(action.type==QLatin1String("load-slot")&&!slotAvailable(action.numberValue<=0?1:action.numberValue))return false;
        for(const auto&graph:meta.visualLogicGraphs)for(const auto&node:graph.nodes)
            if(node.type==QLatin1String("action")&&node.action.type==QLatin1String("load-slot")&&!slotAvailable(node.action.numberValue<=0?1:node.action.numberValue))return false;
    }
    return enabled;
}

QStringList PlayerWindow::titleFocusableWidgets() const
{
    QStringList ids;const auto& settings=m_project.gameUi;
    for(auto it=settings.widgets.cbegin();it!=settings.widgets.cend();++it){const auto&w=it.value();if(w.screen!=m_titleUiScreenId||!w.interactive||w.navigationMode==QLatin1String("none")||!titleWidgetEnabled(it.key()))continue;ids.push_back(it.key());}
    std::stable_sort(ids.begin(),ids.end(),[&](const QString&a,const QString&b){const int za=settings.layoutElements.value(a).zOrder,zb=settings.layoutElements.value(b).zOrder;return za==zb?a<b:za<zb;});
    return ids;
}

bool PlayerWindow::focusTitleWidget(const QString& id)
{
    QString next=id;if(!next.isEmpty()&&!titleFocusableWidgets().contains(next))return false;if(next==m_titleFocusId)return true;
    const QString old=m_titleFocusId;m_titleFocusId=next;if(!old.isEmpty())executeTitleEvent(old,QStringLiteral("unfocus"));if(!next.isEmpty())executeTitleEvent(next,QStringLiteral("focus"));update();return true;
}

bool PlayerWindow::moveTitleFocus(core::GameAction action)
{
    const QStringList candidates=titleFocusableWidgets();if(candidates.isEmpty())return false;
    if(m_titleFocusId.isEmpty()||!candidates.contains(m_titleFocusId))return focusTitleWidget(candidates.first());
    const auto& settings=m_project.gameUi;const auto current=settings.widgets.value(m_titleFocusId);
    QString manual;game::ui::UiNavDirection dir=game::ui::UiNavDirection::Down;
    if(action==core::GameAction::Up){manual=current.navUp;dir=game::ui::UiNavDirection::Up;}
    else if(action==core::GameAction::Down){manual=current.navDown;dir=game::ui::UiNavDirection::Down;}
    else if(action==core::GameAction::Left){manual=current.navLeft;dir=game::ui::UiNavDirection::Left;}
    else if(action==core::GameAction::Right){manual=current.navRight;dir=game::ui::UiNavDirection::Right;}else return false;
    if(current.navigationMode==QLatin1String("manual"))return !manual.isEmpty()&&candidates.contains(manual)?focusTitleWidget(manual):false;
    QHash<QString,QRectF> rects;for(const QString&id:candidates)rects.insert(id,settings.widgets.value(id).rect);
    const QString next=game::ui::spatialFocusNeighbor(m_titleFocusId,candidates,rects,dir,current.navigationWrap);
    return next.isEmpty()?false:focusTitleWidget(next);
}

core::UiWidgetSettings PlayerWindow::titleRuntimeWidget(const QString& id) const
{
    core::UiWidgetSettings w=m_project.gameUi.widgets.value(id);
    if(m_titleWidgetValues.contains(id))w.value=m_titleWidgetValues.value(id);
    if(m_titleWidgetSelection.contains(id))w.selectedIndex=m_titleWidgetSelection.value(id);
    if(m_titleWidgetChecked.contains(id))w.checked=m_titleWidgetChecked.value(id);
    if(m_titleWidgetText.contains(id))w.text=m_titleWidgetText.value(id);
    return w;
}

bool PlayerWindow::handleTitleNativeDirection(core::GameAction action)
{
    if(m_titleFocusId.isEmpty()||!m_project.gameUi.widgets.contains(m_titleFocusId))return false;
    core::UiWidgetSettings w=titleRuntimeWidget(m_titleFocusId);
    if(game::ui::behavior::directionAdjustsValue(w,action)){
        int direction=(action==core::GameAction::Left||action==core::GameAction::Up)?-1:1;if(game::ui::behavior::isSlider(w.type)&&w.orientation==QLatin1String("vertical"))direction=(action==core::GameAction::Up)?1:-1;
        const double next=game::ui::behavior::steppedValue(w,w.value,direction);if(!qFuzzyCompare(next+1.0,w.value+1.0)){m_titleWidgetValues[m_titleFocusId]=next;executeTitleEvent(m_titleFocusId,QStringLiteral("value-changed"));update();}return true;
    }
    if(game::ui::behavior::directionAdjustsSelection(w,action,m_titleExpandedDropdowns.contains(m_titleFocusId))){
        const int next=game::ui::behavior::movedIndex(w,w.selectedIndex,action);if(next!=w.selectedIndex){m_titleWidgetSelection[m_titleFocusId]=next;executeTitleEvent(m_titleFocusId,QStringLiteral("value-changed"));if(game::ui::behavior::isTabs(w.type)&&next<w.items.size()){const QString target=w.items.value(next).trimmed();for(auto it=m_project.gameUi.screenStates.cbegin();it!=m_project.gameUi.screenStates.cend();++it)if(it.value().screen==m_titleUiScreenId&&(it.key().compare(target,Qt::CaseInsensitive)==0||it.value().name.compare(target,Qt::CaseInsensitive)==0)){m_titleScreenStateId=it.key();break;}}update();}return true;
    }
    return false;
}

bool PlayerWindow::handleTitleTextKey(int key,const QString& text)
{
    if(m_titleEditingTextId.isEmpty())return false;const QString id=m_titleEditingTextId;if(!m_project.gameUi.widgets.contains(id)){m_titleEditingTextId.clear();return false;}
    QString value=titleRuntimeWidget(id).text;
    if(key==Qt::Key_Escape){m_titleEditingTextId.clear();update();return true;}
    if(key==Qt::Key_Return||key==Qt::Key_Enter){m_titleEditingTextId.clear();executeTitleEvent(id,QStringLiteral("value-changed"));update();return true;}
    if(key==Qt::Key_Backspace){if(!value.isEmpty())value.chop(1);m_titleWidgetText[id]=value;executeTitleEvent(id,QStringLiteral("value-changed"));update();return true;}
    if(key==Qt::Key_Delete){m_titleWidgetText[id].clear();executeTitleEvent(id,QStringLiteral("value-changed"));update();return true;}
    QString accepted;for(const QChar ch:text)if(!ch.isNull()&&ch.isPrint())accepted.append(ch);if(!accepted.isEmpty()){m_titleWidgetText[id]=(value+accepted).left(256);executeTitleEvent(id,QStringLiteral("value-changed"));update();return true;}return true;
}

bool PlayerWindow::applyTitleNativeActivation(const QString& id,const QPointF* logicalPoint)
{
    if(!m_project.gameUi.widgets.contains(id))return false;core::UiWidgetSettings w=titleRuntimeWidget(id);const QString type=w.type.trimmed().toLower();
    const QSize logical=m_project.gameResolution.isValid()?m_project.gameResolution:QSize(800,600);const QRectF r(w.rect.x()*logical.width(),w.rect.y()*logical.height(),w.rect.width()*logical.width(),w.rect.height()*logical.height());
    if(game::ui::behavior::isToggle(type)){m_titleWidgetChecked[id]=!w.checked;executeTitleEvent(id,QStringLiteral("value-changed"));return true;}
    if(game::ui::behavior::isRadio(type)){if(w.checked)return true;const QString parent=m_project.gameUi.layoutElements.value(id).parentId;for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.key()!=id&&it.value().screen==m_titleUiScreenId&&game::ui::behavior::isRadio(it.value().type)&&m_project.gameUi.layoutElements.value(it.key()).parentId==parent&&titleRuntimeWidget(it.key()).checked){m_titleWidgetChecked[it.key()]=false;executeTitleEvent(it.key(),QStringLiteral("value-changed"));}m_titleWidgetChecked[id]=true;executeTitleEvent(id,QStringLiteral("value-changed"));return true;}
    if(game::ui::behavior::isSlider(type)&&logicalPoint){m_titleWidgetValues[id]=game::ui::behavior::pointerValue(w,*logicalPoint,r);executeTitleEvent(id,QStringLiteral("value-changed"));return true;}
    if(game::ui::behavior::isStepper(type)&&logicalPoint){m_titleWidgetValues[id]=game::ui::behavior::steppedValue(w,w.value,logicalPoint->x()<r.center().x()?-1:1);executeTitleEvent(id,QStringLiteral("value-changed"));return true;}
    if(game::ui::behavior::isTabs(type)&&logicalPoint){const int index=game::ui::behavior::pointerItemIndex(w,*logicalPoint,r);if(index>=0){m_titleWidgetSelection[id]=index;executeTitleEvent(id,QStringLiteral("value-changed"));if(index<w.items.size()){const QString target=w.items[index].trimmed();for(auto it=m_project.gameUi.screenStates.cbegin();it!=m_project.gameUi.screenStates.cend();++it)if(it.value().screen==m_titleUiScreenId&&(it.key().compare(target,Qt::CaseInsensitive)==0||it.value().name.compare(target,Qt::CaseInsensitive)==0)){m_titleScreenStateId=it.key();break;}}}return true;}
    if(game::ui::behavior::isGrid(type)&&logicalPoint){const int index=game::ui::behavior::pointerGridIndex(w,*logicalPoint,r);if(index>=0){m_titleWidgetSelection[id]=index;executeTitleEvent(id,QStringLiteral("value-changed"));}return true;}
    if(game::ui::behavior::isListLike(type)&&logicalPoint){const int index=game::ui::behavior::pointerItemIndex(w,*logicalPoint,r);if(index>=0){m_titleWidgetSelection[id]=index;executeTitleEvent(id,QStringLiteral("value-changed"));}return true;}
    if(game::ui::behavior::isDropdown(type)){if(m_titleExpandedDropdowns.contains(id))m_titleExpandedDropdowns.remove(id);else{m_titleExpandedDropdowns.clear();m_titleExpandedDropdowns.insert(id);}update();return true;}
    if(game::ui::behavior::isTextInput(type)){m_titleEditingTextId=id;update();return true;}return false;
}

void PlayerWindow::executeTitleAction(const QString& elementId, const core::UiEventActionSettings& action)
{
    const QString target=action.targetElementId.isEmpty()||action.targetElementId==QLatin1String("self")?elementId:action.targetElementId;
    if(action.type==QLatin1String("title-new-game")){newGame();return;}
    if(action.type==QLatin1String("title-continue")){
        if(m_hasSave){
            if(designerScreenUsesAction(QStringLiteral("load"),QStringLiteral("load-slot")))setTitleUiScreen(QStringLiteral("load"));
            else continueGame();
        }
        return;
    }
    if(action.type==QLatin1String("title-quit")){if(m_quitApplicationOnClose)qApp->quit();else close();return;}
    if(action.type==QLatin1String("load-slot")){startGame(qBound(1,action.numberValue<=0?1:action.numberValue,99));return;}
    if(action.type==QLatin1String("back-ui")){if(m_titleUiScreenId!=QLatin1String("title"))setTitleUiScreen(QStringLiteral("title"));return;}
    if(action.type==QLatin1String("show"))m_titleRuntimeVisibility[target]=true;
    else if(action.type==QLatin1String("hide"))m_titleRuntimeVisibility[target]=false;
    else if(action.type==QLatin1String("set-state"))m_titleRuntimeStates[target]=action.textValue.trimmed().isEmpty()?QStringLiteral("normal"):action.textValue.trimmed();
    else if(action.type==QLatin1String("play-animation")&&!action.textValue.trimmed().isEmpty())m_titleRuntimeClips[target]=TitleClipState{action.textValue.trimmed(),m_titleElapsed.elapsed()};
    else if(action.type==QLatin1String("stop-animation"))m_titleRuntimeClips.remove(target);
    else if(action.type==QLatin1String("set-switch")&&action.intValue>0)m_titleState.setSwitch(action.intValue,action.boolValue);
    else if(action.type==QLatin1String("set-variable")&&action.intValue>0)m_titleState.setVariable(action.intValue,action.numberValue);
    else if(action.type==QLatin1String("add-variable")&&action.intValue>0)m_titleState.setVariable(action.intValue,m_titleState.variable(action.intValue)+action.numberValue);
    else if(action.type==QLatin1String("add-gold"))m_titleState.addGold(action.numberValue);
    else if(action.type==QLatin1String("set-screen-state")){
        const QString stateId=action.textValue.trimmed();const auto it=m_project.gameUi.screenStates.constFind(stateId);
        if(stateId.isEmpty()||(it!=m_project.gameUi.screenStates.cend()&&it.value().screen==m_titleUiScreenId))m_titleScreenStateId=stateId;
    }
    update();
}

void PlayerWindow::executeTitleEvent(const QString& id, const QString& trigger)
{
    const core::UiLayoutElementSettings meta=m_project.gameUi.layoutElements.value(id);
    for(const auto&binding:meta.eventBindings){
        if(!binding.enabled||binding.trigger!=trigger)continue;
        bool pass=binding.conditions.isEmpty()||binding.conditionMode!=QLatin1String("any");
        if(binding.conditionMode==QLatin1String("any")&&!binding.conditions.isEmpty()){pass=false;for(const auto&c:binding.conditions)if(titleConditionPasses(c)){pass=true;break;}}
        else for(const auto&c:binding.conditions)if(!titleConditionPasses(c)){pass=false;break;}
        if(!pass)continue;for(const auto&action:binding.actions){
            executeTitleAction(id,action);
            if(m_game||action.type==QLatin1String("title-new-game")||action.type==QLatin1String("title-continue")||action.type==QLatin1String("title-quit")||action.type==QLatin1String("load-slot")||action.type==QLatin1String("back-ui"))return;
        }
    }
    startTitleVisualLogic(id,trigger);
}

void PlayerWindow::startTitleVisualLogic(const QString& elementId,const QString&trigger)
{
    const core::UiLayoutElementSettings meta=m_project.gameUi.layoutElements.value(elementId);
    bool added=false;
    for(const auto&graph:meta.visualLogicGraphs){
        if(!graph.enabled||graph.id.isEmpty())continue;
        for(const auto&node:graph.nodes){
            if(node.type!=QLatin1String("event")||node.trigger!=trigger)continue;
            const auto links=titleLogicOutgoing(graph,node.id,QStringLiteral("next"));
            for(const auto*link:links){
                if(!titleLogicNodeById(graph,link->toNodeId))continue;
                const TitleLogicRunner runner{elementId,graph.id,link->toNodeId,-1,0};
                if(m_titleUpdatingLogic)m_titlePendingLogicRunners.push_back(runner);
                else m_titleLogicRunners.push_back(runner);
                added=true;
            }
        }
    }
    if(added&&!m_titleUpdatingLogic)updateTitleVisualLogic(0);
}

void PlayerWindow::updateTitleVisualLogic(int deltaMs)
{
    if(m_titleLogicRunners.isEmpty())return;
    m_titleUpdatingLogic=true;deltaMs=qMax(0,deltaMs);
    QVector<TitleLogicRunner> next;next.reserve(m_titleLogicRunners.size()+8);
    for(TitleLogicRunner runner:std::as_const(m_titleLogicRunners)){
        if(runner.waitRemainingMs>0){
            runner.waitRemainingMs=qMax(0,runner.waitRemainingMs-deltaMs);
            if(runner.waitRemainingMs>0){next.push_back(runner);continue;}
        }
        bool alive=true;int localSteps=0;
        while(alive&&localSteps++<256){
            const core::UiLayoutElementSettings meta=m_project.gameUi.layoutElements.value(runner.elementId);
            const auto*graph=titleLogicGraphById(meta,runner.graphId);
            if(!graph||!graph->enabled){alive=false;break;}
            const auto*node=titleLogicNodeById(*graph,runner.nodeId);
            if(!node){alive=false;break;}
            if(++runner.safetySteps>2048){alive=false;break;}

            if(node->type==QLatin1String("delay")){
                if(runner.waitRemainingMs<0){runner.waitRemainingMs=qBound(0,node->delayMs,600000);if(runner.waitRemainingMs>0)break;}
                runner.waitRemainingMs=-1;const auto out=titleLogicOutgoing(*graph,node->id,QStringLiteral("next"));
                if(out.isEmpty()){alive=false;break;}runner.nodeId=out.first()->toNodeId;continue;
            }
            if(node->type==QLatin1String("condition")){
                const QString port=titleConditionPasses(node->condition)?QStringLiteral("true"):QStringLiteral("false");
                const auto out=titleLogicOutgoing(*graph,node->id,port);if(out.isEmpty()){alive=false;break;}runner.nodeId=out.first()->toNodeId;continue;
            }
            if(node->type==QLatin1String("action")){
                executeTitleAction(runner.elementId,node->action);
                if(m_game||node->action.type==QLatin1String("title-new-game")||node->action.type==QLatin1String("title-continue")||node->action.type==QLatin1String("title-quit")||node->action.type==QLatin1String("load-slot")||node->action.type==QLatin1String("back-ui")){alive=false;break;}
                const auto out=titleLogicOutgoing(*graph,node->id,QStringLiteral("next"));if(out.isEmpty()){alive=false;break;}runner.nodeId=out.first()->toNodeId;continue;
            }
            if(node->type==QLatin1String("sequence")){
                const auto out=titleLogicOutgoing(*graph,node->id,QStringLiteral("next"));if(out.isEmpty()){alive=false;break;}
                for(int i=1;i<out.size();++i)next.push_back(TitleLogicRunner{runner.elementId,runner.graphId,out.at(i)->toNodeId,-1,runner.safetySteps});
                runner.nodeId=out.first()->toNodeId;continue;
            }
            if(node->type==QLatin1String("event")){
                const auto out=titleLogicOutgoing(*graph,node->id,QStringLiteral("next"));if(out.isEmpty()){alive=false;break;}runner.nodeId=out.first()->toNodeId;continue;
            }
            alive=false;
        }
        if(alive)next.push_back(runner);
    }
    m_titleLogicRunners=next;
    if(!m_titlePendingLogicRunners.isEmpty()){m_titleLogicRunners+=m_titlePendingLogicRunners;m_titlePendingLogicRunners.clear();}
    m_titleUpdatingLogic=false;
}

bool PlayerWindow::activateTitleWidget(const QString& id,const QPointF* logicalPoint)
{
    if(id.isEmpty()||!titleWidgetEnabled(id))return false;focusTitleWidget(id);executeTitleEvent(id,QStringLiteral("press"));if(m_game||!isVisible())return true;applyTitleNativeActivation(id,logicalPoint);executeTitleEvent(id,QStringLiteral("click"));if(m_game||!isVisible())return true;executeTitleEvent(id,QStringLiteral("release"));update();return true;
}

void PlayerWindow::rebuildDesignerTitle()
{
    m_hasSave=hasAnySave();m_titleTheme=game::ui::UiTheme::fromSettings(m_project.gameUi);
    m_titleUiScreenId=QStringLiteral("title");
    m_titleRuntimeStates.clear();m_titleRuntimeVisibility.clear();m_titleRuntimeClips.clear();
    m_titleWidgetValues.clear();m_titleWidgetSelection.clear();m_titleWidgetChecked.clear();m_titleWidgetText.clear();m_titleExpandedDropdowns.clear();m_titleEditingTextId.clear();
    m_titleLogicRunners.clear();m_titlePendingLogicRunners.clear();m_titleUpdatingLogic=false;m_titleLastLogicTickMs=m_titleElapsed.elapsed();
    m_titleScreenStateId.clear();for(auto it=m_project.gameUi.screenStates.cbegin();it!=m_project.gameUi.screenStates.cend();++it)if(it.value().screen==m_titleUiScreenId&&it.value().initial){m_titleScreenStateId=it.key();break;}
    const QStringList ids=titleFocusableWidgets();QString initial;for(const QString&id:ids)if(m_project.gameUi.widgets.value(id).initialFocus){initial=id;break;}if(initial.isEmpty()&&!ids.isEmpty())initial=ids.first();m_titleFocusId=initial;
    for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.value().screen==m_titleUiScreenId)executeTitleEvent(it.key(),QStringLiteral("open"));
    update();
}

void PlayerWindow::setTitleUiScreen(const QString& screen)
{
    if(screen.isEmpty()||m_titleUiScreenId==screen)return;
    for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)
        if(it.value().screen==m_titleUiScreenId)executeTitleEvent(it.key(),QStringLiteral("close"));
    m_titleUiScreenId=screen;m_titleRuntimeStates.clear();m_titleRuntimeVisibility.clear();m_titleRuntimeClips.clear();
    m_titleWidgetValues.clear();m_titleWidgetSelection.clear();m_titleWidgetChecked.clear();m_titleWidgetText.clear();m_titleExpandedDropdowns.clear();m_titleEditingTextId.clear();
    m_titleLogicRunners.clear();m_titlePendingLogicRunners.clear();m_titleUpdatingLogic=false;
    m_titleScreenStateId.clear();for(auto it=m_project.gameUi.screenStates.cbegin();it!=m_project.gameUi.screenStates.cend();++it)
        if(it.value().screen==m_titleUiScreenId&&it.value().initial){m_titleScreenStateId=it.key();break;}
    const QStringList ids=titleFocusableWidgets();QString initial;for(const QString&id:ids)if(m_project.gameUi.widgets.value(id).initialFocus){initial=id;break;}
    if(initial.isEmpty()&&!ids.isEmpty())initial=ids.first();m_titleFocusId=initial;m_titleHoverId.clear();m_titleLastLogicTickMs=m_titleElapsed.elapsed();
    for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.value().screen==m_titleUiScreenId)executeTitleEvent(it.key(),QStringLiteral("open"));
    update();
}

QPointF PlayerWindow::titleLogicalPoint(const QPointF& widgetPoint, bool* inside) const
{
    const QSize logical=m_project.gameResolution.isValid()?m_project.gameResolution:QSize(800,600);
    const qreal scale=qMin(width()/qreal(qMax(1,logical.width())),height()/qreal(qMax(1,logical.height())));
    const QSizeF drawn(logical.width()*scale,logical.height()*scale);const QPointF origin((width()-drawn.width())*.5,(height()-drawn.height())*.5);
    const QRectF target(origin,drawn);if(inside)*inside=target.contains(widgetPoint);return QPointF((widgetPoint.x()-origin.x())/qMax<qreal>(.0001,scale),(widgetPoint.y()-origin.y())/qMax<qreal>(.0001,scale));
}

bool PlayerWindow::titleWidgetContainsPoint(const QString& id, const QPointF& logicalPoint) const
{
    const auto& settings=m_project.gameUi;
    const auto it=settings.widgets.constFind(id);
    if(it==settings.widgets.cend())return false;
    const QSize viewport=m_project.gameResolution.isValid()?m_project.gameResolution:QSize(800,600);
    const auto&w=it.value();
    const QRectF rect(w.rect.x()*viewport.width(),w.rect.y()*viewport.height(),
                      w.rect.width()*viewport.width(),w.rect.height()*viewport.height());

    // O hit-test usa a mesma cadeia de transforms do renderer. Assim um botão
    // rotacionado/escalado no UI Designer continua clicável exatamente onde é
    // desenhado na Tela de Título do LudoPlayer.
    QStringList chain;QSet<QString>guard;QString current=id;
    while(!current.isEmpty()&&!guard.contains(current)){
        guard.insert(current);chain.prepend(current);
        current=settings.layoutElements.value(current).parentId;
    }
    QTransform transform;
    for(const QString&elementId:chain){
        const auto wit=settings.widgets.constFind(elementId);if(wit==settings.widgets.cend())continue;
        const auto&widget=wit.value();const auto meta=settings.layoutElements.value(elementId);
        const QRectF r(widget.rect.x()*viewport.width(),widget.rect.y()*viewport.height(),
                       widget.rect.width()*viewport.width(),widget.rect.height()*viewport.height());
        const QPointF pivot(r.left()+meta.pivot.x()*r.width(),r.top()+meta.pivot.y()*r.height());
        QTransform local;local.translate(pivot.x(),pivot.y());local.rotate(widget.rotationDegrees);
        local.shear(std::tan(qDegreesToRadians(qBound(-80.0,widget.skewXDegrees,80.0))),
                    std::tan(qDegreesToRadians(qBound(-80.0,widget.skewYDegrees,80.0))));
        local.scale(qBound(0.05,widget.transformScaleX,10.0),qBound(0.05,widget.transformScaleY,10.0));
        local.translate(-pivot.x(),-pivot.y());transform=transform*local;
    }
    bool ok=false;const QTransform inverse=transform.inverted(&ok);
    return ok&&rect.contains(inverse.map(logicalPoint));
}

void PlayerWindow::showEvent(QShowEvent* event)
{
    if (!m_directMapStartup) {
        m_hasSave=hasAnySave();
        if(m_continue)m_continue->setEnabled(m_hasSave);
        if(m_designerTitle&&!m_game)rebuildDesignerTitle();
    }
    QWidget::showEvent(event);
    if (QSettings().value(QStringLiteral("game/fullscreen"), false).toBool() &&
        !game::runtimeBorderlessWindow(this)) {
        QTimer::singleShot(0, this, [this] {
            if (QSettings().value(QStringLiteral("game/fullscreen"), false).toBool())
                game::setRuntimeBorderlessWindow(this, true, false);
        });
    }
    if (m_titleRuleApplied || m_game) return;
    m_titleRuleApplied = true;
    if (m_directMapStartup) {
        // RC2.85: a antiga Title Screen nao participa mais do startup do
        // runtime. O wrapper fica preto apenas durante preload/creacao do QRhi
        // e o primeiro mapa aparece por um fade de entrada.
        QTimer::singleShot(0,this,[this]{ if(!m_game) newGame(); });
        return;
    }
    const QString mode=m_project.titleScreen.showMode;
    const bool skip = mode==QLatin1String("skip") ||
                      (mode==QLatin1String("when-save-exists") && !m_hasSave) ||
                      (mode==QLatin1String("when-no-save") && m_hasSave);
    if(skip) QTimer::singleShot(0,this,[this]{ if(!m_game) newGame(); });
}

void PlayerWindow::closeEvent(QCloseEvent* event){QWidget::closeEvent(event);if(m_quitApplicationOnClose)qApp->quit();}

void PlayerWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    if (m_directMapStartup) {
        // Evita qualquer flash da Title Screen antiga enquanto o primeiro
        // backend QRhi/preload ainda esta sendo preparado.
        painter.fillRect(rect(), Qt::black);
        return;
    }
    // Com uma Tela de Título feita no UI Designer, nenhum fundo legado é
    // imposto por cima/por baixo da composição. O usuário controla 100% do
    // visual com os widgets (Image/Panel/Particles/etc.). Preto é apenas o
    // clear do framebuffer caso a própria UI deixe áreas transparentes.
    painter.fillRect(rect(), m_designerTitle ? Qt::black : m_project.titleScreen.backgroundColor);
    if (!m_designerTitle && !m_background.isNull()) {
        const double scale = qMax(width() / double(m_background.width()), height() / double(m_background.height()));
        const QSizeF size(m_background.width() * scale, m_background.height() * scale);
        const QRectF target((width() - size.width()) / 2.0, (height() - size.height()) / 2.0,size.width(), size.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);painter.drawImage(target, m_background);painter.fillRect(rect(), QColor(0, 0, 0, 45));
    }
    if(!m_designerTitle)return;
    const QSize logical=m_project.gameResolution.isValid()?m_project.gameResolution:QSize(800,600);
    m_titleCanvas.beginFrame(logical);
    QFont font=this->font();if(!m_titleTheme.fontFamily.isEmpty())font.setFamily(m_titleTheme.fontFamily);font.setPixelSize(qMax(6,m_titleTheme.fontSize));
    auto stateFn=[this](const QString&id){
        if(!titleWidgetEnabled(id))return QStringLiteral("disabled");
        if(m_titleExpandedDropdowns.contains(id))return QStringLiteral("expanded");
        if(id==m_titleEditingTextId)return QStringLiteral("selected");
        if(id==m_titleFocusId)return QStringLiteral("focused");
        if(id==m_titleHoverId)return QStringLiteral("hover");
        if(m_titleRuntimeStates.contains(id))return m_titleRuntimeStates.value(id);
        if(!m_titleScreenStateId.isEmpty()){
            const auto st=m_project.gameUi.screenStates.constFind(m_titleScreenStateId);if(st!=m_project.gameUi.screenStates.cend()){const auto ov=st.value().elements.constFind(id);if(ov!=st.value().elements.cend()&&!ov.value().visualState.isEmpty())return ov.value().visualState;}
        }
        return QStringLiteral("normal");
    };
    auto clipFn=[this](const QString&id){
        if(m_titleRuntimeClips.contains(id))return m_titleRuntimeClips.value(id).name;
        if(!m_titleScreenStateId.isEmpty()){const auto st=m_project.gameUi.screenStates.constFind(m_titleScreenStateId);if(st!=m_project.gameUi.screenStates.cend()){const auto ov=st.value().elements.constFind(id);if(ov!=st.value().elements.cend())return ov.value().animationClip;}}
        return QString();
    };
    auto clipTimeFn=[this](const QString&id){
        const auto runtime=m_titleRuntimeClips.constFind(id);if(runtime==m_titleRuntimeClips.cend())return 0;const auto meta=m_project.gameUi.layoutElements.value(id);for(const auto&clip:meta.animationClips)if(clip.name==runtime.value().name){const int duration=qMax(1,clip.durationMs);const qint64 elapsed=qMax<qint64>(0,m_titleElapsed.elapsed()-runtime.value().startedMs);return clip.loop?int(elapsed%duration):qMin<int>(duration,int(qMin<qint64>(elapsed,std::numeric_limits<int>::max())));}return 0;
    };
    const auto widgetFn=[this](const QString&id,const core::UiWidgetSettings&base){Q_UNUSED(base);return titleRuntimeWidget(id);};
    game::ui::UiWidgetRenderer::appendScreen(m_titleCanvas.drawList(),m_titleTheme,m_project,m_titleState,m_titleUiScreenId,logical,font,QStringLiteral("open"),1.0,stateFn,clipFn,clipTimeFn,[this](const QString&id,bool fallback){return fallback&&titleWidgetVisible(id);},m_titleElapsed.elapsed(),widgetFn);
    const qreal scale=qMin(width()/qreal(qMax(1,logical.width())),height()/qreal(qMax(1,logical.height())));const QSizeF drawn(logical.width()*scale,logical.height()*scale);const QPointF origin((width()-drawn.width())*.5,(height()-drawn.height())*.5);
    painter.save();painter.translate(origin);painter.scale(scale,scale);game::ui::UiPainterRenderer::render(painter,m_titleCanvas.drawList());painter.restore();
}

void PlayerWindow::mouseMoveEvent(QMouseEvent* event)
{
    if(!m_designerTitle||m_game){QWidget::mouseMoveEvent(event);return;}bool inside=false;const QPointF logical=titleLogicalPoint(event->position(),&inside);QString hover;
    if(inside && (event->buttons() & Qt::LeftButton) && !m_titleFocusId.isEmpty()) {
        const core::UiWidgetSettings focused = titleRuntimeWidget(m_titleFocusId);
        if (game::ui::behavior::isSlider(focused.type)) applyTitleNativeActivation(m_titleFocusId, &logical);
    }
    if(inside){QStringList ids;for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.value().screen==m_titleUiScreenId&&it.value().interactive&&titleWidgetEnabled(it.key()))ids.push_back(it.key());std::stable_sort(ids.begin(),ids.end(),[this](const QString&a,const QString&b){return m_project.gameUi.layoutElements.value(a).zOrder>m_project.gameUi.layoutElements.value(b).zOrder;});for(const QString&id:ids)if(titleWidgetContainsPoint(id,logical)){hover=id;break;}}
    if(hover!=m_titleHoverId){const QString old=m_titleHoverId;m_titleHoverId=hover;if(!old.isEmpty())executeTitleEvent(old,QStringLiteral("leave"));if(!hover.isEmpty())executeTitleEvent(hover,QStringLiteral("hover"));update();}
}

void PlayerWindow::mousePressEvent(QMouseEvent* event)
{
    if(!m_designerTitle||m_game||event->button()!=Qt::LeftButton){QWidget::mousePressEvent(event);return;}bool inside=false;const QPointF logical=titleLogicalPoint(event->position(),&inside);if(!inside)return;
    const QSize size=m_project.gameResolution.isValid()?m_project.gameResolution:QSize(800,600);for(const QString&id:std::as_const(m_titleExpandedDropdowns)){if(!m_project.gameUi.widgets.contains(id))continue;core::UiWidgetSettings w=titleRuntimeWidget(id);QRectF base(w.rect.x()*size.width(),w.rect.y()*size.height(),w.rect.width()*size.width(),w.rect.height()*size.height());const int n=qMin(8,w.items.size());const qreal rowH=qMax<qreal>(24,base.height());QRectF popup(base.left(),base.bottom()+2,base.width(),rowH*n);if(n>0&&popup.contains(logical)){w.orientation=QStringLiteral("vertical");const int index=game::ui::behavior::pointerItemIndex(w,logical,popup);if(index>=0){m_titleWidgetSelection[id]=index;executeTitleEvent(id,QStringLiteral("value-changed"));}m_titleExpandedDropdowns.remove(id);focusTitleWidget(id);executeTitleEvent(id,QStringLiteral("click"));update();return;}}
    QStringList ids;for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.value().screen==m_titleUiScreenId&&it.value().interactive&&titleWidgetEnabled(it.key()))ids.push_back(it.key());std::stable_sort(ids.begin(),ids.end(),[this](const QString&a,const QString&b){return m_project.gameUi.layoutElements.value(a).zOrder>m_project.gameUi.layoutElements.value(b).zOrder;});for(const QString&id:ids)if(titleWidgetContainsPoint(id,logical)){activateTitleWidget(id,&logical);return;}
}

void PlayerWindow::keyPressEvent(QKeyEvent* event)
{
    if (game::isRuntimeBorderlessToggleShortcut(event->key(), event->modifiers(), event->isAutoRepeat())) {
        game::toggleRuntimeBorderlessWindow(this, true);
        event->accept();
        return;
    }
    if(!m_designerTitle||m_game){QWidget::keyPressEvent(event);return;}if(handleTitleTextKey(event->key(),event->text())){event->accept();return;}switch(event->key()){case Qt::Key_Up:if(!handleTitleNativeDirection(core::GameAction::Up))moveTitleFocus(core::GameAction::Up);break;case Qt::Key_Down:if(!handleTitleNativeDirection(core::GameAction::Down))moveTitleFocus(core::GameAction::Down);break;case Qt::Key_Left:if(!handleTitleNativeDirection(core::GameAction::Left))moveTitleFocus(core::GameAction::Left);break;case Qt::Key_Right:if(!handleTitleNativeDirection(core::GameAction::Right))moveTitleFocus(core::GameAction::Right);break;case Qt::Key_Return:case Qt::Key_Enter:case Qt::Key_Space:activateTitleWidget(m_titleFocusId);break;case Qt::Key_Escape:if(!m_titleExpandedDropdowns.isEmpty()){m_titleExpandedDropdowns.clear();update();break;}if(m_quitApplicationOnClose)qApp->quit();else close();break;default:QWidget::keyPressEvent(event);return;}event->accept();
}

void PlayerWindow::wheelEvent(QWheelEvent* event)
{
    if(!m_designerTitle||m_game){QWidget::wheelEvent(event);return;}bool inside=false;const QPointF logical=titleLogicalPoint(event->position(),&inside);if(!inside){QWidget::wheelEvent(event);return;}QString hit;QStringList ids;for(auto it=m_project.gameUi.widgets.cbegin();it!=m_project.gameUi.widgets.cend();++it)if(it.value().screen==m_titleUiScreenId&&it.value().interactive&&titleWidgetEnabled(it.key()))ids.push_back(it.key());std::stable_sort(ids.begin(),ids.end(),[this](const QString&a,const QString&b){return m_project.gameUi.layoutElements.value(a).zOrder>m_project.gameUi.layoutElements.value(b).zOrder;});for(const QString&id:ids)if(titleWidgetContainsPoint(id,logical)){hit=id;break;}if(hit.isEmpty())hit=m_titleFocusId;if(hit.isEmpty()){QWidget::wheelEvent(event);return;}core::UiWidgetSettings w=titleRuntimeWidget(hit);if(!game::ui::behavior::isScrollable(w.type)&&!game::ui::behavior::isSlider(w.type)){QWidget::wheelEvent(event);return;}focusTitleWidget(hit);const int count=qMax(1,std::abs(event->angleDelta().y()/120));const int direction=event->angleDelta().y()<0?1:-1;if(w.type==QLatin1String("scroll-area")||game::ui::behavior::isSlider(w.type)){double value=w.value;for(int i=0;i<count;++i)value=game::ui::behavior::steppedValue(w,value,direction);m_titleWidgetValues[hit]=value;}else{int index=w.selectedIndex;const core::GameAction action=direction>0?core::GameAction::Down:core::GameAction::Up;for(int i=0;i<count;++i)index=game::ui::behavior::movedIndex(w,index,action);m_titleWidgetSelection[hit]=index;}executeTitleEvent(hit,QStringLiteral("value-changed"));event->accept();update();
}

void PlayerWindow::newGame(){startGame();}

void PlayerWindow::showBuiltInLoadPanel()
{
    if(!m_builtInLoadPanel){
        m_builtInLoadPanel=new QWidget(this);m_builtInLoadPanel->setObjectName(QStringLiteral("ludoBuiltInLoadPanel"));m_builtInLoadPanel->setStyleSheet(QStringLiteral("#ludoBuiltInLoadPanel{background:rgba(8,12,24,245);border:1px solid #64748b;border-radius:10px;} QListWidget{background:#0f172a;color:#e2e8f0;border:1px solid #475569;font-size:15px;} QPushButton{min-width:120px;padding:8px;}") );
        auto* layout=new QVBoxLayout(m_builtInLoadPanel);auto* title=new QLabel(tr("Carregar partida"),m_builtInLoadPanel);QFont f=title->font();f.setPointSize(18);f.setBold(true);title->setFont(f);layout->addWidget(title);m_builtInLoadList=new QListWidget(m_builtInLoadPanel);layout->addWidget(m_builtInLoadList,1);auto* buttons=new QHBoxLayout;buttons->addStretch();auto* cancel=new QPushButton(tr("Voltar"),m_builtInLoadPanel);m_builtInLoadButton=new QPushButton(tr("Carregar"),m_builtInLoadPanel);buttons->addWidget(cancel);buttons->addWidget(m_builtInLoadButton);layout->addLayout(buttons);
        connect(cancel,&QPushButton::clicked,this,&PlayerWindow::hideBuiltInLoadPanel);connect(m_builtInLoadButton,&QPushButton::clicked,this,[this]{auto*item=m_builtInLoadList?m_builtInLoadList->currentItem():nullptr;if(item)startGame(item->data(Qt::UserRole).toInt());});connect(m_builtInLoadList,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*item){if(item)startGame(item->data(Qt::UserRole).toInt());});
    }
    m_builtInLoadList->clear();for(int slot=1;slot<=99;++slot){game::GameSaveSummary summary;if(!game::readGameSaveSummary(game::gameSavePath(m_project,slot),m_project,&summary))continue;const QString when=summary.savedAt.isValid()?summary.savedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")):tr("data desconhecida");auto*item=new QListWidgetItem(tr("Slot %1 — %2 — %3").arg(slot).arg(summary.mapName.isEmpty()?summary.mapId:summary.mapName,when),m_builtInLoadList);item->setData(Qt::UserRole,slot);}
    if(m_builtInLoadList->count()==0){hideBuiltInLoadPanel();return;}m_builtInLoadList->setCurrentRow(0);const int w=qMin(width()-40,620),h=qMin(height()-40,460);m_builtInLoadPanel->setGeometry((width()-w)/2,(height()-h)/2,w,h);m_builtInLoadPanel->show();m_builtInLoadPanel->raise();m_builtInLoadList->setFocus();
}

void PlayerWindow::hideBuiltInLoadPanel()
{
    if(m_builtInLoadPanel)m_builtInLoadPanel->hide();if(m_designerTitle)setFocus();else for(QPushButton*button:m_titleButtons)if(button&&button->isVisible()&&button->isEnabled()){button->setFocus();break;}
}

void PlayerWindow::continueGame(){showBuiltInLoadPanel();}

void PlayerWindow::startGame(int loadSlot)
{
    hideBuiltInLoadPanel();
    game::GameTransitionTarget target;
    QString transitionError;
    if (!game::resolveGameTransitionTarget(m_project, game::GameStateTransitionKind::NewGame,
                                           m_project.startMapId, m_project.startPosition,
                                           &target, &transitionError)) {
        QMessageBox::warning(this, tr("Iniciar jogo"), transitionError);
        if (m_directMapStartup) { QTimer::singleShot(0, this, &QWidget::close); return; }
        show();
        return;
    }
    const int startIndex = m_project.mapIndexById(target.mapId);
    if (startIndex >= 0) m_project.switchDoc(startIndex);
    const core::MapInfo& info=m_project.mapInfo();
    const QPointF startPixel(target.cell.x()*qMax(1,info.tileWidth),
                             target.cell.y()*qMax(1,info.tileHeight));

    // RC2.63: antes de liberar gameplay, o Player aquece/cacheia BGM/BGS/ME/
    // SE/Voice referenciados usando o MESMO RuntimePreloader do F5/F6.
    game::RuntimePreloadOptions audioOptions;
    audioOptions.scope = game::RuntimePreloadScope::FullGame;
    game::RuntimePreloadReport audioReport;
    QString audioPreloadError;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool audioReady = game::prepareRuntimeAudioAssets(
        m_project, audioOptions, &audioReport, &audioPreloadError,
        [](const game::RuntimePreloadProgress&) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            return true;
        });
    QApplication::restoreOverrideCursor();
    if (!audioReady && !audioPreloadError.isEmpty()) {
        // Preload é otimização, não requisito. RuntimeAudio ainda pode abrir o
        // arquivo normalmente; apenas registramos o diagnóstico no console.
        qWarning().noquote() << "[LUDO] Audio preload:" << audioPreloadError;
    }

    startGpuGame(startPixel, loadSlot,
                 game::runtimeGpuBackendFallbackChain(m_project.runtimeGpuBackend));
}

void PlayerWindow::startGpuGame(const QPointF& startPixel, int loadSlot,
                                const QStringList& backends, int backendIndex)
{
    if (backendIndex < 0 || backendIndex >= backends.size()) {
        QMessageBox::critical(this, tr("Renderizador GPU"),
            tr("Não foi possível iniciar o jogo com os renderizadores gráficos disponíveis. Consulte os detalhes abaixo para identificar a causa.\n\nDetalhes: %1")
                .arg(game::RhiGameWindow::caminhoDoLog()));
        if (m_directMapStartup) { QTimer::singleShot(0, this, &QWidget::close); return; }
        show();
        return;
    }
    const QString backend = backends.at(backendIndex);
    QString runtimeBoundaryError;
    auto runtimeProject = core::RuntimeProject::fromEditor(m_project, &runtimeBoundaryError);
    if (!runtimeProject) {
        QMessageBox::warning(this, tr("Não foi possível iniciar o jogo"), runtimeBoundaryError);
        if (m_directMapStartup) QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    auto* gpu = new game::RhiGameWindow(std::move(runtimeProject), startPixel, this, true,
                                        !m_quitApplicationOnClose, backend);
    if (m_hotReloadSource) gpu->setHotReloadSource(m_hotReloadSource.data());
    if (loadSlot > 0) {
        QString error;
        if (!gpu->loadGame(loadSlot, &error)) {
            QMessageBox::warning(this, tr("Carregar partida"), error);
            gpu->deleteLater();
            if (m_directMapStartup) { QTimer::singleShot(0, this, &QWidget::close); return; }
            show(); return;
        }
    }
    // Depois do eventual Load: o snapshot nao pode substituir o fade desta
    // abertura. O mesmo caminho vale para Player exportado e playtest.
    gpu->startEntryFade(24);
    attachGame(gpu);
    QPointer<game::RhiGameWindow> watched(gpu);
    QTimer::singleShot(2500, this, [this, watched, startPixel, loadSlot, backends, backendIndex] {
        if (!watched || watched->primeiroQuadroOk()) return;
        m_backendRetryClosing.insert(watched.data());
        watched->close();
        if (backendIndex + 1 < backends.size()) {
            QMessageBox::warning(this, tr("Renderizador gráfico"),
                tr("%1 não conseguiu desenhar o jogo. A LUDO tentará %2.\n\nRelatório: %3")
                    .arg(game::runtimeGpuBackendDisplayName(backends.at(backendIndex)),
                         game::runtimeGpuBackendDisplayName(backends.at(backendIndex + 1)),
                         game::RhiGameWindow::caminhoDoLog()));
        }
        startGpuGame(startPixel, loadSlot, backends, backendIndex + 1);
    });
}

void PlayerWindow::attachGame(QWidget* game)
{
    // 3.20.3: o LudoPlayer passa a ser uma casca única. Title e jogo vivem
    // dentro da MESMA janela nativa; iniciar/continuar apenas troca o conteúdo.
    // Isso evita o antigo fluxo "janela de título -> segunda janela do jogo".
    m_game=game;
    game->setAttribute(Qt::WA_DeleteOnClose,true);
    game->setGeometry(rect());
    connect(game,&QObject::destroyed,this,[this,game]{
        const bool backendRetry = m_backendRetryClosing.remove(game) > 0;
        if(m_game!=game)return;
        m_game=nullptr;
        if (m_directMapStartup) {
            update();
            // Fechar o jogo encerra esta execucao. A unica excecao e a troca
            // automatica de backend GPU feita pelo watchdog.
            if (!backendRetry) QTimer::singleShot(0, this, [this]{ if(!m_game) close(); });
            return;
        }
        if(m_designerTitle)rebuildDesignerTitle();
        else { refreshBuiltInTitleLocalization(); for(QPushButton* button:m_titleButtons)if(button&&button->isVisible()&&button->isEnabled()){button->setFocus();break;} }
        update();
        if(m_designerTitle)setFocus();
        raise();
        activateWindow();
    });
    if (QSettings().value(QStringLiteral("game/fullscreen"), false).toBool())
        game::setRuntimeBorderlessWindow(this, true, false);
    game->show();
    game->raise();
    game->setFocus(Qt::OtherFocusReason);
}

void PlayerWindow::refreshBuiltInTitleLocalization()
{
    if (m_designerTitle) return;
    const QString configuredTitle = core::resolvePlayerText(m_project.localization,
        m_project.titleScreen.titleTextKey, m_project.titleScreen.titleText.trimmed());
    if (m_titleLabel) m_titleLabel->setText(configuredTitle.isEmpty() ? m_project.projectName : configuredTitle);
    if (m_titleButtons.size() > 0 && m_titleButtons[0])
        m_titleButtons[0]->setText(core::resolvePlayerText(m_project.localization, QStringLiteral("system.title.new_game"), tr("Novo jogo")));
    if (m_titleButtons.size() > 1 && m_titleButtons[1])
        m_titleButtons[1]->setText(core::resolvePlayerText(m_project.localization, QStringLiteral("system.title.continue"), tr("Continuar")));
    if (m_titleButtons.size() > 2 && m_titleButtons[2])
        m_titleButtons[2]->setText(core::resolvePlayerText(m_project.localization, QStringLiteral("system.title.quit"), tr("Sair")));
}

void PlayerWindow::resizeEvent(QResizeEvent* event)
{
    if(m_game)m_game->setGeometry(rect());
    if(m_builtInLoadPanel&&m_builtInLoadPanel->isVisible()){const int w=qMin(width()-40,620),h=qMin(height()-40,460);m_builtInLoadPanel->setGeometry((width()-w)/2,(height()-h)/2,w,h);}
    QWidget::resizeEvent(event);
}

} // namespace player
