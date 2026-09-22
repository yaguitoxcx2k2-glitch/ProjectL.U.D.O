#include "UiMenuController.h"

#include "game/GameSave.h"
#include "game/GameSession.h"
#include "game/RpgSystem.h"
#include "game/ui/UiDataBinding.h"
#include "game/ui/UiNavigation.h"
#include "game/ui/UiWidgetBehavior.h"

#include <QCoreApplication>
#include <QMetaType>
#include <QSettings>
#include <QRandomGenerator>
#include <QVariant>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <utility>

namespace game::ui {
namespace {

QString trUi(const char* text)
{
    return QCoreApplication::translate("UiMenuController", text);
}

QString projectTr(const GameSession& session, const QString& key, const QString& fallback)
{
    const core::Editor& ed = session.editor();
    return core::resolvePlayerText(ed.localization, key, fallback);
}

QString menuUiScreenId(UiMenuScreen screen)
{
    if (screen == UiMenuScreen::Save || screen == UiMenuScreen::SaveOverwrite) return QStringLiteral("save");
    if (screen == UiMenuScreen::Load) return QStringLiteral("load");
    return QStringLiteral("menu");
}

bool screenUsesAction(const core::GameUiSettings& settings,const QString&screen,const QString&actionType)
{
    for(auto wit=settings.widgets.cbegin();wit!=settings.widgets.cend();++wit){
        if(wit.value().screen!=screen)continue;
        const auto meta=settings.layoutElements.value(wit.key());
        for(const auto&binding:meta.eventBindings)for(const auto&action:binding.actions)if(action.type==actionType)return true;
        for(const auto&graph:meta.visualLogicGraphs)for(const auto&node:graph.nodes)
            if(node.type==QLatin1String("action")&&node.action.type==actionType)return true;
    }
    return false;
}

QTransform widgetHierarchyTransform(const core::GameUiSettings& settings,const QString& id,const QSize& viewport)
{
    QStringList chain;QSet<QString>guard;QString current=id;
    while(!current.isEmpty()&&!guard.contains(current)){guard.insert(current);chain.prepend(current);current=settings.layoutElements.value(current).parentId;}
    QTransform out;
    for(const QString&elementId:chain){const auto it=settings.widgets.constFind(elementId);if(it==settings.widgets.cend())continue;const auto&w=it.value();const auto meta=settings.layoutElements.value(elementId);const QRectF r(w.rect.x()*viewport.width(),w.rect.y()*viewport.height(),w.rect.width()*viewport.width(),w.rect.height()*viewport.height());const QPointF pivot(r.left()+meta.pivot.x()*r.width(),r.top()+meta.pivot.y()*r.height());QTransform local;local.translate(pivot.x(),pivot.y());local.rotate(w.rotationDegrees);local.shear(std::tan(qDegreesToRadians(qBound(-80.0,w.skewXDegrees,80.0))),std::tan(qDegreesToRadians(qBound(-80.0,w.skewYDegrees,80.0))));local.scale(qBound(0.05,w.transformScaleX,10.0),qBound(0.05,w.transformScaleY,10.0));local.translate(-pivot.x(),-pivot.y());out=out*local;}
    return out;
}

bool widgetContainsPoint(const core::GameUiSettings&settings,const QString&id,const QPointF&point,const QSize&viewport)
{
    const auto it=settings.widgets.constFind(id);if(it==settings.widgets.cend())return false;const auto&w=it.value();const QRectF r(w.rect.x()*viewport.width(),w.rect.y()*viewport.height(),w.rect.width()*viewport.width(),w.rect.height()*viewport.height());bool ok=false;const QTransform inv=widgetHierarchyTransform(settings,id,viewport).inverted(&ok);return ok&&r.contains(inv.map(point));
}

QString formattedTime(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

int menuClipDurationMs(const core::GameUiSettings& settings, const QString& screenId, const QString& trigger)
{
    int duration = 0;
    QStringList ids{QStringLiteral("menu.list"), QStringLiteral("menu.detail")};
    for (auto it = settings.widgets.cbegin(); it != settings.widgets.cend(); ++it)
        if (it.value().screen == screenId) ids.push_back(it.key());
    for (const QString& id : ids) {
        const core::UiLayoutElementSettings meta = settings.layoutElements.value(id);
        for (const core::UiAnimationClipSettings& clip : meta.animationClips)
            if (clip.trigger == trigger && !clip.keyframes.isEmpty()) duration = qMax(duration, clip.durationMs);
    }
    return qBound(0, duration, 10000);
}


bool compareUiValue(int left, const QString& op, int right)
{
    if (op == QLatin1String("!=")) return left != right;
    if (op == QLatin1String(">")) return left > right;
    if (op == QLatin1String(">=")) return left >= right;
    if (op == QLatin1String("<")) return left < right;
    if (op == QLatin1String("<=")) return left <= right;
    return left == right;
}

bool uiConditionPasses(const GameState& state, const core::UiEventConditionSettings& condition)
{
    if (condition.source == QLatin1String("always")) return true;
    if (condition.source == QLatin1String("switch"))
        return compareUiValue(state.switchOn(condition.id) ? 1 : 0, condition.op, condition.value);
    if (condition.source == QLatin1String("variable"))
        return compareUiValue(state.variable(condition.id), condition.op, condition.value);
    if (condition.source == QLatin1String("gold"))
        return compareUiValue(state.gold(), condition.op, condition.value);
    return false;
}

bool uiBindingPasses(const GameState& state, const core::UiEventBindingSettings& binding)
{
    if (binding.conditions.isEmpty()) return true;
    if (binding.conditionMode == QLatin1String("any")) {
        for (const auto& condition : binding.conditions)
            if (uiConditionPasses(state, condition)) return true;
        return false;
    }
    for (const auto& condition : binding.conditions)
        if (!uiConditionPasses(state, condition)) return false;
    return true;
}

const core::UiVisualLogicNodeSettings* logicNodeById(const core::UiVisualLogicGraphSettings& graph, const QString& id)
{
    for (const auto& node : graph.nodes) if (node.id == id) return &node;
    return nullptr;
}

const core::UiVisualLogicGraphSettings* logicGraphById(const core::UiLayoutElementSettings& meta, const QString& id)
{
    for (const auto& graph : meta.visualLogicGraphs) if (graph.id == id) return &graph;
    return nullptr;
}

QVector<const core::UiVisualLogicLinkSettings*> logicOutgoing(const core::UiVisualLogicGraphSettings& graph,
                                                               const QString& nodeId, const QString& port = QString())
{
    QVector<const core::UiVisualLogicLinkSettings*> out;
    for (const auto& link : graph.links) {
        if (link.fromNodeId != nodeId) continue;
        if (!port.isEmpty() && link.fromPort != port) continue;
        out.push_back(&link);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto* a, const auto* b) {
        if (a->order != b->order) return a->order < b->order;
        return a->toNodeId < b->toNodeId;
    });
    return out;
}

QStringList variantIds(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
    QStringList result;
    const QVariantList list = value.toList();
    if (!list.isEmpty()) {
        for (const QVariant& item : list) {
            const QString id = item.toString().trimmed();
            if (!id.isEmpty()) result.push_back(id);
        }
        return result;
    }
    const QString raw = value.toString();
    for (const QString& part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString id = part.trimmed();
        if (!id.isEmpty()) result.push_back(id);
    }
    return result;
}

const core::DatabaseRecord* inventoryRecord(const core::Editor& ed, const QString& id,
                                            QString* category = nullptr)
{
    for (const QString& cat : {QStringLiteral("items"), QStringLiteral("weapons"),
                               QStringLiteral("armors")}) {
        if (const core::DatabaseRecord* record = databaseRecord(ed, cat, id)) {
            if (category) *category = cat;
            return record;
        }
    }
    return nullptr;
}

QString actorName(const core::Editor& ed, const QString& actorId)
{
    return databaseRecordName(ed, QStringLiteral("actors"), actorId, trUi("Personagem"));
}

QStringList actorSkillIds(const core::Editor& ed, const PartyMemberState& member)
{
    QStringList ids;
    if (const core::DatabaseRecord* actor = databaseRecord(ed, QStringLiteral("actors"), member.actorId)) {
        ids += variantIds(actor->data.value(QStringLiteral("skillIds")));
        if (const core::DatabaseRecord* klass = databaseRecord(
                ed, QStringLiteral("classes"), actor->data.value(QStringLiteral("classId")).toString()))
            ids += variantIds(klass->data.value(QStringLiteral("skillIds")));
    }
    ids.removeDuplicates();
    return ids;
}

QString signedValue(int value)
{
    if (value > 0) return QStringLiteral("+%1").arg(value);
    return QString::number(value);
}

void applyStateEffect(const core::Editor& ed, PartyMemberState& target,
                      const core::DatabaseRecord& effect)
{
    const QString addId = effect.data.value(QStringLiteral("stateAddId")).toString();
    const int chance = qBound(0, effect.data.value(QStringLiteral("stateChance"), 100).toInt(), 100);
    if (!addId.isEmpty() && (chance >= 100 || QRandomGenerator::global()->bounded(100) < chance)) {
        if (!target.states.contains(addId)) target.states.push_back(addId);
        if (const core::DatabaseRecord* state = databaseRecord(ed, QStringLiteral("states"), addId))
            target.stateTurns[addId] = qMax(0, state->data.value(QStringLiteral("duration"), 0).toInt());
    }
    const QString removeId = effect.data.value(QStringLiteral("stateRemoveId")).toString();
    if (!removeId.isEmpty()) {
        target.states.removeAll(removeId);
        target.stateTurns.remove(removeId);
    }
}

} // namespace

UiMenuController::UiMenuController(GameSession& session)
    : m_session(session)
{
}

QString UiMenuController::runtimeVisualState(const QString& elementId) const
{
    if (const core::UiScreenStateElementSettings* screenOverride = screenStateOverride(elementId)) {
        if (screenOverride->hasEnabled && !screenOverride->enabled) return QStringLiteral("disabled");
    }
    if (m_expandedDropdowns.contains(elementId)) return QStringLiteral("expanded");
    if (m_editingTextElementId == elementId) return QStringLiteral("selected");
    if (m_runtimeStates.contains(elementId)) return m_runtimeStates.value(elementId);
    if (!m_focusedElementId.isEmpty() && m_focusedElementId == elementId) return QStringLiteral("focused");
    if (const core::UiScreenStateElementSettings* screenOverride = screenStateOverride(elementId))
        if (!screenOverride->visualState.isEmpty()) return screenOverride->visualState;
    return QStringLiteral("normal");
}

QString UiMenuController::runtimeClipName(const QString& elementId) const
{
    return m_runtimeClips.value(elementId).name;
}

int UiMenuController::runtimeClipTimeMs(const QString& elementId) const
{
    return m_runtimeClips.value(elementId).timeMs;
}

bool UiMenuController::runtimeVisibility(const QString& elementId, bool fallback) const
{
    if (m_runtimeVisibility.contains(elementId)) return m_runtimeVisibility.value(elementId);
    if (const core::UiScreenStateElementSettings* screenOverride = screenStateOverride(elementId))
        if (screenOverride->hasVisible) return screenOverride->visible;
    return fallback;
}

const core::Editor& UiMenuController::editor() const
{
    return m_session.editor();
}

const GameState& UiMenuController::state() const
{
    return m_session.state();
}

double UiMenuController::runtimeValue(const QString& id) const
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);
    if(it==m_session.editor().gameUi.widgets.cend())return 0.0;
    return m_runtimeWidgetValues.value(id,it.value().value);
}

int UiMenuController::runtimeSelectedIndex(const QString& id) const
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);
    if(it==m_session.editor().gameUi.widgets.cend())return 0;
    return m_runtimeWidgetSelection.value(id,it.value().selectedIndex);
}

bool UiMenuController::runtimeChecked(const QString& id) const
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);
    if(it==m_session.editor().gameUi.widgets.cend())return false;
    return m_runtimeWidgetChecked.value(id,it.value().checked);
}

QString UiMenuController::runtimeText(const QString& id) const
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);
    if(it==m_session.editor().gameUi.widgets.cend())return QString();
    return m_runtimeWidgetText.value(id,it.value().text);
}

void UiMenuController::applyRuntimeWidget(const QString& elementId, core::UiWidgetSettings& widget) const
{
    if(m_runtimeWidgetValues.contains(elementId))widget.value=m_runtimeWidgetValues.value(elementId);
    if(m_runtimeWidgetSelection.contains(elementId))widget.selectedIndex=m_runtimeWidgetSelection.value(elementId);
    if(m_runtimeWidgetChecked.contains(elementId))widget.checked=m_runtimeWidgetChecked.value(elementId);
    if(m_runtimeWidgetText.contains(elementId))widget.text=m_runtimeWidgetText.value(elementId);
}

void UiMenuController::setRuntimeSelectedIndex(const QString& id,int index,bool fireEvent)
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);if(it==m_session.editor().gameUi.widgets.cend())return;
    const int next=behavior::boundedIndex(it.value(),index);const int old=runtimeSelectedIndex(id);
    m_runtimeWidgetSelection[id]=next;
    if(next!=old&&fireEvent)executeUiEvent(id,QStringLiteral("value-changed"));
}

void UiMenuController::setRuntimeValue(const QString& id,double value,bool fireEvent)
{
    const auto it=m_session.editor().gameUi.widgets.constFind(id);if(it==m_session.editor().gameUi.widgets.cend())return;
    const double lo=qMin(it.value().minimum,it.value().maximum),hi=qMax(it.value().minimum,it.value().maximum);
    const double next=qBound(lo,value,hi);const double old=runtimeValue(id);m_runtimeWidgetValues[id]=next;
    if(!qFuzzyCompare(old+1.0,next+1.0)&&fireEvent)executeUiEvent(id,QStringLiteral("value-changed"));
}

void UiMenuController::applyTabScreenState(const QString& id,int index)
{
    const auto wit=m_session.editor().gameUi.widgets.constFind(id);if(wit==m_session.editor().gameUi.widgets.cend())return;
    const QString target=wit.value().items.value(index).trimmed();if(target.isEmpty())return;
    for(auto it=m_session.editor().gameUi.screenStates.cbegin();it!=m_session.editor().gameUi.screenStates.cend();++it){
        if(it.value().screen!=currentUiScreenId())continue;
        if(it.key().compare(target,Qt::CaseInsensitive)==0||it.value().name.compare(target,Qt::CaseInsensitive)==0){setActiveScreenState(it.key());return;}
    }
}

bool UiMenuController::handleFocusedNativeDirection(core::GameAction action)
{
    if(m_focusedElementId.isEmpty())return false;
    const auto wit=m_session.editor().gameUi.widgets.constFind(m_focusedElementId);if(wit==m_session.editor().gameUi.widgets.cend())return false;
    const core::UiWidgetSettings& w=wit.value();
    if(behavior::directionAdjustsValue(w,action)){
        int direction=(action==core::GameAction::Left||action==core::GameAction::Up)?-1:1;
        if(w.type==QLatin1String("scroll-area"))direction=(action==core::GameAction::Up)?-1:1;
        else if(behavior::isSlider(w.type)&&w.orientation==QLatin1String("vertical"))direction=(action==core::GameAction::Up)?1:-1;
        setRuntimeValue(m_focusedElementId,behavior::steppedValue(w,runtimeValue(m_focusedElementId),direction));return true;
    }
    const bool expanded=m_expandedDropdowns.contains(m_focusedElementId);
    if(behavior::directionAdjustsSelection(w,action,expanded)){
        const int next=behavior::movedIndex(w,runtimeSelectedIndex(m_focusedElementId),action);
        setRuntimeSelectedIndex(m_focusedElementId,next);if(behavior::isTabs(w.type))applyTabScreenState(m_focusedElementId,next);return true;
    }
    return false;
}

bool UiMenuController::applyNativeActivation(const QString& id,const QPointF* point,const QSize* viewport)
{
    const auto wit=m_session.editor().gameUi.widgets.constFind(id);if(wit==m_session.editor().gameUi.widgets.cend())return false;
    const core::UiWidgetSettings& w=wit.value();const QString type=w.type.trimmed().toLower();
    QRectF r;if(viewport)r=QRectF(w.rect.x()*viewport->width(),w.rect.y()*viewport->height(),w.rect.width()*viewport->width(),w.rect.height()*viewport->height());
    if(behavior::isToggle(type)){
        m_runtimeWidgetChecked[id]=!runtimeChecked(id);executeUiEvent(id,QStringLiteral("value-changed"));return true;
    }
    if(behavior::isRadio(type)){
        if(runtimeChecked(id))return true;
        const QString parent=m_session.editor().gameUi.layoutElements.value(id).parentId;
        for(auto it=m_session.editor().gameUi.widgets.cbegin();it!=m_session.editor().gameUi.widgets.cend();++it){
            if(it.key()==id||it.value().screen!=currentUiScreenId()||!behavior::isRadio(it.value().type))continue;
            if(m_session.editor().gameUi.layoutElements.value(it.key()).parentId==parent&&runtimeChecked(it.key())){m_runtimeWidgetChecked[it.key()]=false;executeUiEvent(it.key(),QStringLiteral("value-changed"));}
        }
        m_runtimeWidgetChecked[id]=true;executeUiEvent(id,QStringLiteral("value-changed"));return true;
    }
    if(behavior::isSlider(type)&&point&&viewport){setRuntimeValue(id,behavior::pointerValue(w,*point,r));return true;}
    if(behavior::isStepper(type)&&point&&viewport){const int direction=point->x()<r.center().x()?-1:1;setRuntimeValue(id,behavior::steppedValue(w,runtimeValue(id),direction));return true;}
    if(behavior::isTabs(type)&&point&&viewport){const int index=behavior::pointerItemIndex(w,*point,r);if(index>=0){setRuntimeSelectedIndex(id,index);applyTabScreenState(id,index);}return true;}
    if(behavior::isGrid(type)&&point&&viewport){const int index=behavior::pointerGridIndex(w,*point,r);if(index>=0)setRuntimeSelectedIndex(id,index);return true;}
    if(behavior::isListLike(type)&&point&&viewport){const int index=behavior::pointerItemIndex(w,*point,r);if(index>=0)setRuntimeSelectedIndex(id,index);return true;}
    if(behavior::isDropdown(type)){
        if(m_expandedDropdowns.contains(id)){m_expandedDropdowns.remove(id);}else{m_expandedDropdowns.clear();m_expandedDropdowns.insert(id);}return true;
    }
    if(behavior::isTextInput(type)){m_editingTextElementId=id;return true;}
    return false;
}

bool UiMenuController::handleTextKey(int key,const QString& text)
{
    if(m_editingTextElementId.isEmpty())return false;
    const QString id=m_editingTextElementId;const auto wit=m_session.editor().gameUi.widgets.constFind(id);
    if(wit==m_session.editor().gameUi.widgets.cend()||!behavior::isTextInput(wit.value().type)){m_editingTextElementId.clear();return false;}
    if(key==Qt::Key_Escape){m_editingTextElementId.clear();return true;}
    if(key==Qt::Key_Return||key==Qt::Key_Enter){m_editingTextElementId.clear();executeUiEvent(id,QStringLiteral("value-changed"));return true;}
    QString value=runtimeText(id);
    if(key==Qt::Key_Backspace){if(!value.isEmpty())value.chop(1);m_runtimeWidgetText[id]=value;executeUiEvent(id,QStringLiteral("value-changed"));return true;}
    if(key==Qt::Key_Delete){value.clear();m_runtimeWidgetText[id]=value;executeUiEvent(id,QStringLiteral("value-changed"));return true;}
    QString accepted;for(const QChar ch:text)if(!ch.isNull()&&ch.isPrint())accepted.append(ch);
    if(!accepted.isEmpty()){value=(value+accepted).left(256);m_runtimeWidgetText[id]=value;executeUiEvent(id,QStringLiteral("value-changed"));return true;}
    return true;
}

const core::UiScreenStateSettings* UiMenuController::activeScreenState() const
{
    if (m_activeScreenStateId.isEmpty()) return nullptr;
    const auto it = m_session.editor().gameUi.screenStates.constFind(m_activeScreenStateId);
    return it == m_session.editor().gameUi.screenStates.cend() ? nullptr : &it.value();
}

QString UiMenuController::currentUiScreenId() const
{
    return m_customScreenId.isEmpty() ? menuUiScreenId(m_screen) : m_customScreenId;
}

const core::UiScreenStateElementSettings* UiMenuController::screenStateOverride(const QString& elementId) const
{
    const core::UiScreenStateSettings* state = activeScreenState();
    if (!state) return nullptr;
    const auto it = state->elements.constFind(elementId);
    return it == state->elements.cend() ? nullptr : &it.value();
}

QString UiMenuController::initialScreenStateId() const
{
    for (auto it = m_session.editor().gameUi.screenStates.cbegin(); it != m_session.editor().gameUi.screenStates.cend(); ++it) {
        if (it.value().screen == currentUiScreenId() && it.value().initial) return it.key();
    }
    return QString(); // sem estado inicial = comportamento legado 3.10
}

bool UiMenuController::setActiveScreenState(const QString& stateId, bool playAnimations)
{
    QString next = stateId;
    if (!next.isEmpty()) {
        const auto it = m_session.editor().gameUi.screenStates.constFind(next);
        if (it == m_session.editor().gameUi.screenStates.cend() || it.value().screen != currentUiScreenId()) return false;
    }
    const bool sameState = next == m_activeScreenStateId;
    if (!sameState) m_activeScreenStateId = next;
    if (playAnimations) {
        if (const core::UiScreenStateSettings* state = activeScreenState()) {
            for (auto it = state->elements.cbegin(); it != state->elements.cend(); ++it)
                if (!it.value().animationClip.isEmpty())
                    m_runtimeClips[it.key()] = RuntimeClipState{it.value().animationClip, 0};
        }
    }
    // Se o estado tornou o foco atual invisível/desabilitado, solta o foco.
    if (!m_focusedElementId.isEmpty() && !focusableWidgetIds().contains(m_focusedElementId)) setFocusedElement(QString());
    return true;
}

bool UiMenuController::dataBindingEnabled(const QString& elementId, bool fallback) const
{
    const core::UiLayoutElementSettings meta = m_session.editor().gameUi.layoutElements.value(elementId);
    const UiResolvedDataBindings bindings = UiDataBindingResolver::resolveRuntime(meta, m_session.editor(), m_session.state());
    bool enabled = bindings.hasEnabled ? bindings.enabled : fallback;
    if (const core::UiScreenStateElementSettings* screenOverride = screenStateOverride(elementId))
        if (screenOverride->hasEnabled) enabled = enabled && screenOverride->enabled;
    return enabled;
}

QStringList UiMenuController::focusableWidgetIds() const
{
    const auto& settings = m_session.editor().gameUi;
    const QString screenId = currentUiScreenId();
    if (screenId.isEmpty()) return {};
    auto effectiveVisible = [&](const QString& id) {
        // A render tree propaga visibilidade do parent. Input/foco deve obedecer
        // à mesma regra para filhos de containers ocultos não ficarem ativos.
        QString current=id; QSet<QString> visited;
        while(!current.isEmpty()){
            if(visited.contains(current))return false;
            visited.insert(current);
            const core::UiLayoutElementSettings meta=settings.layoutElements.value(current);
            const UiResolvedDataBindings data=UiDataBindingResolver::resolveRuntime(meta,m_session.editor(),m_session.state());
            const bool base=data.hasVisible?data.visible:meta.visible;
            if(!runtimeVisibility(current,base))return false;
            const QString parent=meta.parentId;
            const auto parentIt=settings.widgets.constFind(parent);
            if(parent.isEmpty()||parentIt==settings.widgets.cend()||parentIt.value().screen!=screenId)return true;
            current=parent;
        }
        return true;
    };
    QString activeModal; int modalZ = std::numeric_limits<int>::min();
    for (auto it = settings.widgets.cbegin(); it != settings.widgets.cend(); ++it) {
        if (it.value().screen != screenId || !(it.value().modal || it.value().type == QLatin1String("modal")) || !effectiveVisible(it.key())) continue;
        const int z = settings.layoutElements.value(it.key()).zOrder;
        if (activeModal.isEmpty() || z >= modalZ) { activeModal = it.key(); modalZ = z; }
    }
    auto belongsToModal = [&](const QString& id) {
        if (activeModal.isEmpty()) return true;
        QString current=id; QSet<QString> visited;
        while (!current.isEmpty() && !visited.contains(current)) { if (current==activeModal) return true; visited.insert(current); current=settings.layoutElements.value(current).parentId; }
        return false;
    };
    QStringList ids;
    for (auto it = settings.widgets.cbegin(); it != settings.widgets.cend(); ++it) {
        const core::UiWidgetSettings& w = it.value();
        if (w.screen != screenId || !w.interactive || w.navigationMode == QLatin1String("none") || !belongsToModal(it.key())) continue;
        if (!effectiveVisible(it.key()) || !dataBindingEnabled(it.key(), true)) continue;
        ids.push_back(it.key());
    }
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a,const QString& b){
        const int za=settings.layoutElements.value(a).zOrder, zb=settings.layoutElements.value(b).zOrder;
        return za==zb?a<b:za<zb;
    });
    return ids;
}

QString UiMenuController::initialFocusWidgetId() const
{
    const QStringList ids = focusableWidgetIds();
    for (const QString& id : ids) if (m_session.editor().gameUi.widgets.value(id).initialFocus) return id;
    return QString();
}

bool UiMenuController::setFocusedElement(const QString& id)
{
    QString next=id;
    if (!next.isEmpty() && !focusableWidgetIds().contains(next)) return false;
    if (next == m_focusedElementId) return true;
    const QString old=m_focusedElementId;
    m_focusedElementId=next;
    if (!old.isEmpty()) executeUiEvent(old, QStringLiteral("unfocus"));
    if (!next.isEmpty()) executeUiEvent(next, QStringLiteral("focus"));
    return true;
}

bool UiMenuController::moveWidgetFocus(core::GameAction action)
{
    if (m_focusedElementId.isEmpty()) return false;
    const auto& settings=m_session.editor().gameUi;
    const QStringList candidates=focusableWidgetIds();
    if (!candidates.contains(m_focusedElementId)) { setFocusedElement(QString()); return false; }
    const core::UiWidgetSettings current=settings.widgets.value(m_focusedElementId);
    QString manual;
    UiNavDirection dir=UiNavDirection::Down;
    if(action==core::GameAction::Up){manual=current.navUp;dir=UiNavDirection::Up;}
    else if(action==core::GameAction::Down){manual=current.navDown;dir=UiNavDirection::Down;}
    else if(action==core::GameAction::Left){manual=current.navLeft;dir=UiNavDirection::Left;}
    else if(action==core::GameAction::Right){manual=current.navRight;dir=UiNavDirection::Right;}
    else return false;
    if (current.navigationMode == QLatin1String("manual")) {
        // No modo Manual, uma direção sem alvo configurado não cai
        // silenciosamente para o algoritmo automático.
        return !manual.isEmpty() && candidates.contains(manual) ? setFocusedElement(manual) : false;
    }
    if(current.navigationMode==QLatin1String("none"))return false;
    QHash<QString,QRectF> rects; for(const QString&id:candidates)rects.insert(id,settings.widgets.value(id).rect);
    const QString next=spatialFocusNeighbor(m_focusedElementId,candidates,rects,dir,current.navigationWrap);
    return next.isEmpty()?false:setFocusedElement(next);
}

bool UiMenuController::activateFocusedWidget()
{
    if (m_focusedElementId.isEmpty() || !focusableWidgetIds().contains(m_focusedElementId)) return false;
    const QString id=m_focusedElementId;
    const QString screenId=currentUiScreenId();
    executeUiEvent(id,QStringLiteral("press"));
    if(currentUiScreenId()!=screenId)return true;
    applyNativeActivation(id);
    if(currentUiScreenId()!=screenId)return true;
    executeUiEvent(id,QStringLiteral("click"));
    if(currentUiScreenId()!=screenId)return true;
    if((active()&&!m_closing)||customActive())executeUiEvent(id,QStringLiteral("release"));
    return true;
}

bool UiMenuController::activateWidgetAt(const QPointF& logicalPosition, const QSize& viewport)
{
    if ((!active() && !customActive()) || m_closing || viewport.width() <= 0 || viewport.height() <= 0) return false;
    const auto& settings = m_session.editor().gameUi;
    const QString screenId = currentUiScreenId();
    if (screenId.isEmpty()) return false;

    // Dropdown expandido recebe clique no popup mesmo fora do retângulo base.
    for(const QString& dropdownId:std::as_const(m_expandedDropdowns)){
        const auto wit=settings.widgets.constFind(dropdownId);if(wit==settings.widgets.cend()||wit.value().screen!=screenId)continue;
        const core::UiWidgetSettings& w=wit.value();
        const QRectF base(w.rect.x()*viewport.width(),w.rect.y()*viewport.height(),w.rect.width()*viewport.width(),w.rect.height()*viewport.height());
        const int n=qMin(8,w.items.size());const qreal rowH=qMax<qreal>(24,base.height());
        const QRectF popup(base.left(),base.bottom()+2,base.width(),rowH*n);
        if(n>0&&popup.contains(logicalPosition)){
            core::UiWidgetSettings popupWidget=w;popupWidget.orientation=QStringLiteral("vertical");
            const int index=behavior::pointerItemIndex(popupWidget,logicalPosition,popup);
            if(index>=0)setRuntimeSelectedIndex(dropdownId,index);m_expandedDropdowns.remove(dropdownId);setFocusedElement(dropdownId);
            executeUiEvent(dropdownId,QStringLiteral("click"));return true;
        }
    }

    const auto effectiveVisible = [&](const QString& id) {
        QString current=id; QSet<QString> visited;
        while(!current.isEmpty()){
            if(visited.contains(current))return false;
            visited.insert(current);
            const core::UiLayoutElementSettings meta=settings.layoutElements.value(current);
            const UiResolvedDataBindings data=UiDataBindingResolver::resolveRuntime(meta,m_session.editor(),m_session.state());
            const bool base=data.hasVisible?data.visible:meta.visible;
            if(!runtimeVisibility(current,base))return false;
            const QString parent=meta.parentId;
            const auto parentIt=settings.widgets.constFind(parent);
            if(parent.isEmpty()||parentIt==settings.widgets.cend()||parentIt.value().screen!=screenId)return true;
            current=parent;
        }
        return true;
    };

    // Se existir um Modal customizado visível, apenas ele e seus descendentes
    // recebem clique. Isso já garante o comportamento essencial de Overlay /
    // Modal sem criar um sistema paralelo de input; Escape continua no fluxo
    // normal do Menu; o Bloco C usa o mesmo foco também para teclado/gamepad.
    QString activeModal;
    int modalZ = std::numeric_limits<int>::min();
    for (auto it = settings.widgets.cbegin(); it != settings.widgets.cend(); ++it) {
        if (it.value().screen != screenId ||
            !(it.value().modal || it.value().type == QLatin1String("modal")) ||
            !effectiveVisible(it.key())) continue;
        const int z = settings.layoutElements.value(it.key()).zOrder;
        if (activeModal.isEmpty() || z >= modalZ) { activeModal = it.key(); modalZ = z; }
    }
    const auto belongsToModal = [&](const QString& id) {
        if (activeModal.isEmpty()) return true;
        QString current = id;
        QSet<QString> visited;
        while (!current.isEmpty() && !visited.contains(current)) {
            if (current == activeModal) return true;
            visited.insert(current);
            current = settings.layoutElements.value(current).parentId;
        }
        return false;
    };

    QStringList ids;
    for (auto it = settings.widgets.cbegin(); it != settings.widgets.cend(); ++it) {
        if (it.value().screen == screenId && it.value().interactive && belongsToModal(it.key()))
            ids.push_back(it.key());
    }
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        return settings.layoutElements.value(a).zOrder > settings.layoutElements.value(b).zOrder;
    });
    for (const QString& id : ids) {
        const core::UiWidgetSettings widget = settings.widgets.value(id);
        if (!effectiveVisible(id) || !dataBindingEnabled(id, true)) continue;
        if (!widgetContainsPoint(settings,id,logicalPosition,viewport)) continue;
        setFocusedElement(id);
        if(behavior::isSlider(widget.type))m_pointerDragElementId=id;else m_pointerDragElementId.clear();
        if(currentUiScreenId()!=screenId)return true;
        executeUiEvent(id, QStringLiteral("press"));
        if(currentUiScreenId()!=screenId)return true;
        applyNativeActivation(id,&logicalPosition,&viewport);
        if(currentUiScreenId()!=screenId)return true;
        executeUiEvent(id, QStringLiteral("click"));
        if(currentUiScreenId()!=screenId)return true;
        executeUiEvent(id, QStringLiteral("release"));
        return true;
    }
    return !activeModal.isEmpty(); // modal visível engole clique no conteúdo de fundo.
}

bool UiMenuController::wheelWidgetAt(const QPointF& logicalPosition,const QSize& viewport,int steps)
{
    if((!active()&&!customActive())||steps==0)return false;
    const auto& settings=m_session.editor().gameUi;const QString screenId=currentUiScreenId();
    QStringList ids;for(auto it=settings.widgets.cbegin();it!=settings.widgets.cend();++it)if(it.value().screen==screenId&&it.value().interactive)ids.push_back(it.key());
    std::stable_sort(ids.begin(),ids.end(),[&](const QString&a,const QString&b){return settings.layoutElements.value(a).zOrder>settings.layoutElements.value(b).zOrder;});
    for(const QString&id:ids){const auto&w=settings.widgets.value(id);if(!behavior::isScrollable(w.type)||!widgetContainsPoint(settings,id,logicalPosition,viewport))continue;setFocusedElement(id);
        if(w.type==QLatin1String("scroll-area")||behavior::isSlider(w.type)){double value=runtimeValue(id);for(int i=0;i<std::abs(steps);++i)value=behavior::steppedValue(w,value,steps>0?-1:1);setRuntimeValue(id,value);}
        else {int index=runtimeSelectedIndex(id);const core::GameAction action=steps>0?core::GameAction::Up:core::GameAction::Down;for(int i=0;i<std::abs(steps);++i)index=behavior::movedIndex(w,index,action);setRuntimeSelectedIndex(id,index);if(behavior::isTabs(w.type))applyTabScreenState(id,index);}
        return true;}
    return false;
}

bool UiMenuController::dragWidgetAt(const QPointF& logicalPosition,const QSize& viewport)
{
    if(m_pointerDragElementId.isEmpty()||viewport.width()<=0||viewport.height()<=0)return false;
    const auto it=m_session.editor().gameUi.widgets.constFind(m_pointerDragElementId);if(it==m_session.editor().gameUi.widgets.cend()||it.value().screen!=currentUiScreenId()||!behavior::isSlider(it.value().type)){m_pointerDragElementId.clear();return false;}
    const core::UiWidgetSettings& w=it.value();const QRectF r(w.rect.x()*viewport.width(),w.rect.y()*viewport.height(),w.rect.width()*viewport.width(),w.rect.height()*viewport.height());setRuntimeValue(m_pointerDragElementId,behavior::pointerValue(w,logicalPosition,r));return true;
}

void UiMenuController::releasePointerWidget(){m_pointerDragElementId.clear();}

void UiMenuController::executeUiAction(const QString& elementId, const QString& trigger, const core::UiEventActionSettings& action)
{
    const QString target = action.targetElementId.isEmpty() || action.targetElementId == QLatin1String("self")
        ? elementId : action.targetElementId;
    if (action.type == QLatin1String("play-animation")) {
        if (!action.textValue.trimmed().isEmpty()) m_runtimeClips[target] = RuntimeClipState{action.textValue.trimmed(), 0};
    } else if (action.type == QLatin1String("stop-animation")) {
        if (action.textValue.trimmed().isEmpty() || m_runtimeClips.value(target).name == action.textValue.trimmed())
            m_runtimeClips.remove(target);
    } else if (action.type == QLatin1String("set-state")) {
        m_runtimeStates[target] = action.textValue.trimmed().isEmpty() ? QStringLiteral("normal") : action.textValue.trimmed();
    } else if (action.type == QLatin1String("show")) {
        m_runtimeVisibility[target] = true;
    } else if (action.type == QLatin1String("hide")) {
        m_runtimeVisibility[target] = false;
    } else if (action.type == QLatin1String("set-switch")) {
        if (action.intValue > 0) m_session.state().setSwitch(action.intValue, action.boolValue);
    } else if (action.type == QLatin1String("set-variable")) {
        if (action.intValue > 0) m_session.state().setVariable(action.intValue, action.numberValue);
    } else if (action.type == QLatin1String("add-variable")) {
        if (action.intValue > 0) m_session.state().setVariable(action.intValue, m_session.state().variable(action.intValue) + action.numberValue);
    } else if (action.type == QLatin1String("add-gold")) {
        m_session.state().addGold(action.numberValue);
    } else if (action.type == QLatin1String("common-event")) {
        if (action.intValue > 0) m_session.startUiCommonEvent(action.intValue);
    } else if (action.type == QLatin1String("set-screen-state")) {
        setActiveScreenState(action.textValue.trimmed(), true);
    } else if (action.type == QLatin1String("close-ui")) {
        if (trigger != QLatin1String("close")) {
            if (customActive()) closeCustom(); else close();
        }
    } else if (action.type == QLatin1String("back-ui")) {
        if (trigger != QLatin1String("close")) {
            if (customActive()) closeCustom(); else back();
        }
    } else if (action.type == QLatin1String("save-slot")) {
        const int slot = qBound(1, action.numberValue <= 0 ? 1 : action.numberValue, 99);
        performSave(slot);
        if (m_screen == UiMenuScreen::Save || m_screen == UiMenuScreen::SaveOverwrite) rebuildSaves(false);
    } else if (action.type == QLatin1String("load-slot")) {
        const int slot = qBound(1, action.numberValue <= 0 ? 1 : action.numberValue, 99);
        performLoad(slot);
    }
}

void UiMenuController::executeUiEvent(const QString& elementId, const QString& trigger)
{
    const core::UiLayoutElementSettings meta = m_session.editor().gameUi.layoutElements.value(elementId);
    for (const core::UiEventBindingSettings& binding : meta.eventBindings) {
        if (!binding.enabled || binding.trigger != trigger || !uiBindingPasses(m_session.state(), binding)) continue;
        for (const core::UiEventActionSettings& action : binding.actions) executeUiAction(elementId, trigger, action);
    }
    startVisualLogic(elementId, trigger);
}

void UiMenuController::startVisualLogic(const QString& elementId, const QString& trigger)
{
    const core::UiLayoutElementSettings meta = m_session.editor().gameUi.layoutElements.value(elementId);
    bool added = false;
    for (const auto& graph : meta.visualLogicGraphs) {
        if (!graph.enabled || graph.id.isEmpty()) continue;
        for (const auto& node : graph.nodes) {
            if (node.type != QLatin1String("event") || node.trigger != trigger) continue;
            const auto links = logicOutgoing(graph, node.id, QStringLiteral("next"));
            for (const auto* link : links) {
                if (!logicNodeById(graph, link->toNodeId)) continue;
                LogicRunner runner{elementId, graph.id, link->toNodeId, -1, 0};
                if (m_updatingLogic) m_pendingLogicRunners.push_back(runner);
                else m_logicRunners.push_back(runner);
                added = true;
            }
        }
    }
    // Executa imediatamente até o primeiro Delay. Isso mantém o mesmo
    // comportamento instantâneo dos Eventos Simples quando o grafo não espera.
    if (added && !m_updatingLogic) updateVisualLogic(0);
}

void UiMenuController::updateVisualLogic(int deltaMs)
{
    if (m_logicRunners.isEmpty()) return;
    m_updatingLogic = true;
    deltaMs = qMax(0, deltaMs);
    QVector<LogicRunner> nextRunners;
    nextRunners.reserve(m_logicRunners.size() + 8);

    for (LogicRunner runner : std::as_const(m_logicRunners)) {
        if (runner.waitRemainingMs > 0) {
            runner.waitRemainingMs = qMax(0, runner.waitRemainingMs - deltaMs);
            if (runner.waitRemainingMs > 0) { nextRunners.push_back(runner); continue; }
        }

        bool alive = true;
        int localSteps = 0;
        while (alive && localSteps++ < 256) {
            const core::UiLayoutElementSettings meta = m_session.editor().gameUi.layoutElements.value(runner.elementId);
            const core::UiVisualLogicGraphSettings* graph = logicGraphById(meta, runner.graphId);
            if (!graph || !graph->enabled) { alive = false; break; }
            const core::UiVisualLogicNodeSettings* node = logicNodeById(*graph, runner.nodeId);
            if (!node) { alive = false; break; }
            runner.safetySteps++;
            if (runner.safetySteps > 2048) { alive = false; break; } // protege ciclos sem Delay.

            if (node->type == QLatin1String("delay")) {
                if (runner.waitRemainingMs < 0) {
                    runner.waitRemainingMs = qBound(0, node->delayMs, 600000);
                    if (runner.waitRemainingMs > 0) break;
                }
                runner.waitRemainingMs = -1;
                const auto out = logicOutgoing(*graph, node->id, QStringLiteral("next"));
                if (out.isEmpty()) { alive = false; break; }
                runner.nodeId = out.first()->toNodeId;
                continue;
            }

            if (node->type == QLatin1String("condition")) {
                const QString port = uiConditionPasses(m_session.state(), node->condition) ? QStringLiteral("true") : QStringLiteral("false");
                const auto out = logicOutgoing(*graph, node->id, port);
                if (out.isEmpty()) { alive = false; break; }
                runner.nodeId = out.first()->toNodeId;
                continue;
            }

            if (node->type == QLatin1String("action")) {
                executeUiAction(runner.elementId, QStringLiteral("visual-logic"), node->action);
                const auto out = logicOutgoing(*graph, node->id, QStringLiteral("next"));
                if (out.isEmpty()) { alive = false; break; }
                runner.nodeId = out.first()->toNodeId;
                continue;
            }

            if (node->type == QLatin1String("sequence")) {
                const auto out = logicOutgoing(*graph, node->id, QStringLiteral("next"));
                if (out.isEmpty()) { alive = false; break; }
                // A primeira saída continua neste runner; as demais criam ramos
                // independentes que podem inclusive esperar em Delays diferentes.
                for (int i = 1; i < out.size(); ++i)
                    nextRunners.push_back(LogicRunner{runner.elementId, runner.graphId, out.at(i)->toNodeId, -1, runner.safetySteps});
                runner.nodeId = out.first()->toNodeId;
                continue;
            }

            if (node->type == QLatin1String("event")) {
                const auto out = logicOutgoing(*graph, node->id, QStringLiteral("next"));
                if (out.isEmpty()) { alive = false; break; }
                runner.nodeId = out.first()->toNodeId;
                continue;
            }

            alive = false; // tipo desconhecido não trava o jogo.
        }
        if (alive) nextRunners.push_back(runner);
    }
    m_logicRunners = nextRunners;
    if (!m_pendingLogicRunners.isEmpty()) {
        m_logicRunners += m_pendingLogicRunners;
        m_pendingLogicRunners.clear();
    }
    m_updatingLogic = false;
}

void UiMenuController::updateRuntimeAnimations(double dt)
{
    if (m_runtimeClips.isEmpty()) return;
    const int deltaMs = qMax(0, qRound(dt * 1000.0));
    QStringList finished;
    for (auto it = m_runtimeClips.begin(); it != m_runtimeClips.end(); ++it) {
        const core::UiLayoutElementSettings meta = m_session.editor().gameUi.layoutElements.value(it.key());
        const core::UiAnimationClipSettings* clip = nullptr;
        for (const auto& candidate : meta.animationClips) {
            if (candidate.name == it.value().name) { clip = &candidate; break; }
        }
        if (!clip || clip->keyframes.isEmpty()) { finished.push_back(it.key()); continue; }
        it.value().timeMs += deltaMs;
        if (it.value().timeMs > clip->durationMs) {
            if (clip->loop) it.value().timeMs %= qMax(1, clip->durationMs);
            else { it.value().timeMs = clip->durationMs; finished.push_back(it.key()); }
        }
    }
    for (const QString& id : finished) {
        const QString clipName = m_runtimeClips.value(id).name;
        m_runtimeClips.remove(id);
        executeUiEvent(id, QStringLiteral("animation-end"));
        Q_UNUSED(clipName);
    }
}

void UiMenuController::open(bool standalone)
{
    if (active()) return;
    if (customActive()) closeCustom();
    m_standalone = standalone;
    m_returnToTitleRequested = false;
    m_fullscreenToggleRequested = false;
    m_audioSettingsChanged = false;
    m_notice.clear();
    m_runtimeStates.clear();
    m_runtimeClips.clear();
    m_runtimeVisibility.clear();
    m_logicRunners.clear();
    m_pendingLogicRunners.clear();
    m_updatingLogic = false;
    m_focusedElementId.clear();
    m_activeScreenStateId.clear();
    m_uiElapsedMs = 0;
    m_parent = UiMenuScreen::Main;
    setActiveScreenState(initialScreenStateId(), false);
    setScreen(UiMenuScreen::Main, false);
    // O estado precisa estar ativo antes de setScreen() para influenciar
    // visibilidade/foco; depois reproduzimos seus clips de entrada.
    setActiveScreenState(m_activeScreenStateId, true);
}

void UiMenuController::resetRuntimeUiState()
{
    m_runtimeStates.clear();
    m_runtimeClips.clear();
    m_runtimeVisibility.clear();
    m_logicRunners.clear();
    m_pendingLogicRunners.clear();
    m_updatingLogic = false;
    m_focusedElementId.clear();
    m_activeScreenStateId.clear();
    m_pointerDragElementId.clear();
    m_uiElapsedMs = 0;
}

bool UiMenuController::openBuiltIn(const QString& screenId, bool standalone)
{
    open(standalone);
    if (screenId.isEmpty() || screenId == QLatin1String("main") || screenId == QLatin1String("menu")) return true;
    if (screenId == QLatin1String("status")) setScreen(UiMenuScreen::Status, false);
    else if (screenId == QLatin1String("inventory") || screenId == QLatin1String("items")) setScreen(UiMenuScreen::Inventory, false);
    else if (screenId == QLatin1String("equipment")) setScreen(UiMenuScreen::EquipmentActor, false);
    else if (screenId == QLatin1String("skills")) setScreen(UiMenuScreen::SkillsActor, false);
    else if (screenId == QLatin1String("quests")) setScreen(UiMenuScreen::Quests, false);
    else if (screenId == QLatin1String("dialogueLog") || screenId == QLatin1String("dialogue-log")) setScreen(UiMenuScreen::DialogueLog, false);
    else if (screenId == QLatin1String("save")) setScreen(UiMenuScreen::Save, false);
    else if (screenId == QLatin1String("load")) setScreen(UiMenuScreen::Load, false);
    else if (screenId == QLatin1String("settings")) setScreen(UiMenuScreen::Settings, false);
    else { close(); return false; }
    return true;
}

void UiMenuController::openCustom(const QString& screenId)
{
    const QString requested = screenId.trimmed();
    if (requested.isEmpty() || active()) return;
    if (m_customScreenId == requested) return;
    if (customActive()) closeCustom();

    resetRuntimeUiState();
    m_customScreenId = requested;
    setActiveScreenState(initialScreenStateId(), false);
    setActiveScreenState(m_activeScreenStateId, true);

    const auto& ui = m_session.editor().gameUi;
    QStringList ids;
    for (auto it = ui.widgets.cbegin(); it != ui.widgets.cend(); ++it)
        if (it.value().screen == m_customScreenId) ids.push_back(it.key());
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        const int za = ui.layoutElements.value(a).zOrder;
        const int zb = ui.layoutElements.value(b).zOrder;
        return za == zb ? a < b : za < zb;
    });
    for (const QString& id : ids) {
        if (!customActive()) break; // Open pode disparar Close UI.
        executeUiEvent(id, QStringLiteral("open"));
    }
    if (!customActive()) return;

    QString initial = initialFocusWidgetId();
    if (initial.isEmpty()) {
        const QStringList candidates = focusableWidgetIds();
        if (!candidates.isEmpty()) initial = candidates.first();
    }
    if (!initial.isEmpty()) setFocusedElement(initial);
}

void UiMenuController::closeCustom()
{
    if (!customActive()) return;
    const QString closingScreen = m_customScreenId;
    // Marca como fechada antes de disparar Unfocus/Close. Assim uma ação
    // "Close UI" ligada ao próprio Unfocus não reentra recursivamente aqui.
    m_customScreenId.clear();
    if (!m_focusedElementId.isEmpty()) setFocusedElement(QString());

    const auto& ui = m_session.editor().gameUi;
    QStringList ids;
    for (auto it = ui.widgets.cbegin(); it != ui.widgets.cend(); ++it)
        if (it.value().screen == closingScreen) ids.push_back(it.key());
    std::stable_sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        const int za = ui.layoutElements.value(a).zOrder;
        const int zb = ui.layoutElements.value(b).zOrder;
        return za == zb ? a > b : za > zb;
    });
    for (const QString& id : ids) executeUiEvent(id, QStringLiteral("close"));

    resetRuntimeUiState();
}

void UiMenuController::resetForGameStateTransition()
{
    m_customScreenId.clear();
    m_returnToTitleRequested = false;
    m_fullscreenToggleRequested = false;
    m_audioSettingsChanged = false;
    m_standalone = false;
    m_closing = false;
    m_transitionElapsed = 0.0;
    m_pendingItemId.clear();
    m_pendingActorId.clear();
    m_pendingSkillId.clear();
    m_pendingEquipSlot.clear();
    m_pendingSaveSlot = 1;
    finishClose();
    resetRuntimeUiState();
}

void UiMenuController::close()
{
    if (!active() || m_closing) return;
    if (!m_focusedElementId.isEmpty()) setFocusedElement(QString());
    else executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("unfocus"));
    executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("close"));
    executeUiEvent(QStringLiteral("menu.detail"), QStringLiteral("close"));
    for (auto it = m_session.editor().gameUi.widgets.cbegin(); it != m_session.editor().gameUi.widgets.cend(); ++it)
        if (it.value().screen == menuUiScreenId(m_screen)) executeUiEvent(it.key(), QStringLiteral("close"));
    const auto& ui = m_session.editor().gameUi;
    const int customMs = menuClipDurationMs(ui, menuUiScreenId(m_screen), QStringLiteral("close"));
    const int durationMs = customMs > 0 ? customMs
        : (ui.closeAnimation == QLatin1String("none") ? 0 : qBound(0, ui.animationMs, 2000));
    m_transitionDuration = durationMs / 1000.0;
    if (m_transitionDuration <= 0.0) { finishClose(); return; }
    m_closing = true;
    m_transitionElapsed = 0.0;
}

void UiMenuController::finishClose()
{
    m_closing = false;
    m_screen = UiMenuScreen::Closed;
    m_entries.clear();
    m_title.clear();
    m_detail.clear();
    m_notice.clear();
    m_selected = 0;
    m_focusedElementId.clear();
    m_activeScreenStateId.clear();
    m_runtimeWidgetValues.clear();m_runtimeWidgetSelection.clear();m_runtimeWidgetChecked.clear();m_runtimeWidgetText.clear();
    m_expandedDropdowns.clear();m_editingTextElementId.clear();m_pointerDragElementId.clear();
    m_logicRunners.clear();
    m_pendingLogicRunners.clear();
    m_updatingLogic = false;
}

void UiMenuController::update(double dt)
{
    if (!active() && !customActive()) return;
    const int deltaMs = qMax(0, qRound(dt * 1000.0));
    m_uiElapsedMs = qMin<qint64>(std::numeric_limits<qint64>::max()-deltaMs, m_uiElapsedMs) + deltaMs;
    updateRuntimeAnimations(dt);
    updateVisualLogic(deltaMs);
    m_transitionElapsed = qMin(m_transitionDuration, m_transitionElapsed + qMax(0.0, dt));
    if (m_closing && m_transitionElapsed >= m_transitionDuration) finishClose();
}

bool UiMenuController::handleCustomAction(core::GameAction action)
{
    if (!customActive()) return false;
    if (m_focusedElementId.isEmpty()) {
        QString initial = initialFocusWidgetId();
        if (initial.isEmpty()) {
            const QStringList candidates = focusableWidgetIds();
            if (!candidates.isEmpty()) initial = candidates.first();
        }
        if (!initial.isEmpty()) setFocusedElement(initial);
    }

    if (action == core::GameAction::Up || action == core::GameAction::Down ||
        action == core::GameAction::Left || action == core::GameAction::Right) {
        if(!handleFocusedNativeDirection(action))moveWidgetFocus(action);
        return true;
    }
    if (action == core::GameAction::Confirm) {
        activateFocusedWidget();
        return true;
    }
    if (action == core::GameAction::Cancel || action == core::GameAction::Quit) {
        if(!m_editingTextElementId.isEmpty()){m_editingTextElementId.clear();return true;}
        if(!m_expandedDropdowns.isEmpty()){m_expandedDropdowns.clear();return true;}
        closeCustom();
        return true;
    }
    return true;
}

double UiMenuController::transitionProgress() const
{
    if (!active() || m_transitionDuration <= 0.0) return m_closing ? 0.0 : 1.0;
    const double p = qBound(0.0, m_transitionElapsed / m_transitionDuration, 1.0);
    return m_closing ? 1.0 - p : p;
}

int UiMenuController::firstVisible(int maxRows) const
{
    maxRows = qMax(1, maxRows);
    const int count = m_entries.size();
    if (count <= maxRows) return 0;
    return qBound(0, m_selected - maxRows / 2, count - maxRows);
}

QString UiMenuController::summary() const
{
    const QString mapName = m_session.editor().doc() ? m_session.editor().doc()->name : QString();
    return QStringLiteral("%1 G   ·   %2   ·   %3")
        .arg(m_session.state().gold())
        .arg(formattedTime(m_session.playTimeSeconds()), mapName);
}

QString UiMenuController::footerHint() const
{
    if (m_screen == UiMenuScreen::Settings)
        return trUi("← → alterar   ·   Confirmar selecionar   ·   Esc voltar");
    if (m_screen == UiMenuScreen::DialogueLog)
        return trUi("← → todas/não lidas   ·   Confirmar marcar lida   ·   Esc voltar");
    return trUi("↑ ↓ selecionar   ·   Confirmar   ·   Esc voltar");
}

bool UiMenuController::takeReturnToTitleRequest()
{
    const bool result = m_returnToTitleRequested;
    m_returnToTitleRequested = false;
    return result;
}

bool UiMenuController::takeFullscreenToggleRequest()
{
    const bool result = m_fullscreenToggleRequested;
    m_fullscreenToggleRequested = false;
    return result;
}

bool UiMenuController::takeAudioSettingsChangedRequest()
{
    const bool result = m_audioSettingsChanged;
    m_audioSettingsChanged = false;
    return result;
}

void UiMenuController::setScreen(UiMenuScreen screen, bool rememberParent)
{
    if (active()) {
        if (!m_focusedElementId.isEmpty()) setFocusedElement(QString());
        else executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("unfocus"));
    }
    if (rememberParent && active()) m_parent = m_screen;
    m_closing = false;
    m_screen = screen;
    m_selected = 0;
    const auto& ui = m_session.editor().gameUi;
    const int customMs = menuClipDurationMs(ui, menuUiScreenId(m_screen), QStringLiteral("open"));
    const int durationMs = customMs > 0 ? customMs
        : (ui.openAnimation == QLatin1String("none") ? 0 : qBound(0, ui.animationMs, 2000));
    m_transitionDuration = durationMs / 1000.0;
    m_transitionElapsed = 0.0;
    m_notice.clear();
    rebuild();
    executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("open"));
    executeUiEvent(QStringLiteral("menu.detail"), QStringLiteral("open"));
    for (auto it = m_session.editor().gameUi.widgets.cbegin(); it != m_session.editor().gameUi.widgets.cend(); ++it)
        if (it.value().screen == menuUiScreenId(m_screen)) executeUiEvent(it.key(), QStringLiteral("open"));
    const QString initialWidget = initialFocusWidgetId();
    if (!initialWidget.isEmpty()) setFocusedElement(initialWidget);
    else executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("focus"));
}

void UiMenuController::back()
{
    switch (m_screen) {
    case UiMenuScreen::Main: close(); break;
    case UiMenuScreen::InventoryTarget: setScreen(UiMenuScreen::Inventory, false); break;
    case UiMenuScreen::EquipmentSlot: setScreen(UiMenuScreen::EquipmentActor, false); break;
    case UiMenuScreen::EquipmentItem: setScreen(UiMenuScreen::EquipmentSlot, false); break;
    case UiMenuScreen::Skills: setScreen(UiMenuScreen::SkillsActor, false); break;
    case UiMenuScreen::SkillTarget: setScreen(UiMenuScreen::Skills, false); break;
    case UiMenuScreen::SaveOverwrite: setScreen(UiMenuScreen::Save, false); break;
    default: setScreen(UiMenuScreen::Main, false); break;
    }
}

void UiMenuController::move(int delta)
{
    if (m_entries.isEmpty() || delta == 0) return;
    const int old = m_selected;
    const int count = m_entries.size();
    for (int tries = 0; tries < count; ++tries) {
        m_selected = (m_selected + delta + count) % count;
        if (m_entries.at(m_selected).enabled) break;
    }
    if (old != m_selected) {
        refreshDetail();
        executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("value-changed"));
    }
}

void UiMenuController::selectIndex(int index)
{
    if (index < 0 || index >= m_entries.size() || !m_entries.at(index).enabled) return;
    const int old = m_selected;
    m_selected = index;
    refreshDetail();
    if (old != m_selected) executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("value-changed"));
}

bool UiMenuController::handleAction(core::GameAction action)
{
    if (!active()) return false;
    if (m_closing) return true;

    const auto& uiSettings=m_session.editor().gameUi;
    const QString uiScreen=menuUiScreenId(m_screen);
    const bool fullyDesignedSave=uiScreen==QLatin1String("save")&&screenUsesAction(uiSettings,uiScreen,QStringLiteral("save-slot"));
    const bool fullyDesignedLoad=uiScreen==QLatin1String("load")&&screenUsesAction(uiSettings,uiScreen,QStringLiteral("load-slot"));
    if(fullyDesignedSave||fullyDesignedLoad){
        if(m_focusedElementId.isEmpty()){
            QString initial=initialFocusWidgetId();
            if(initial.isEmpty()){
                const QStringList candidates=focusableWidgetIds();
                if(!candidates.isEmpty())initial=candidates.first();
            }
            if(!initial.isEmpty())setFocusedElement(initial);
        }
        if(action==core::GameAction::Up||action==core::GameAction::Down||action==core::GameAction::Left||action==core::GameAction::Right){if(!handleFocusedNativeDirection(action))moveWidgetFocus(action);return true;}
        if(action==core::GameAction::Confirm){activateFocusedWidget();return true;}
        if(action==core::GameAction::Cancel||action==core::GameAction::Quit){back();return true;}
        return true;
    }

    // Quando um Widget customizado possui foco, o mesmo GameAction vindo do
    // teclado ou do gamepad navega por ele. Cancel solta primeiro o foco para
    // que menus legados continuem sempre acessíveis.
    if (!m_focusedElementId.isEmpty()) {
        if (action == core::GameAction::Up || action == core::GameAction::Down ||
            action == core::GameAction::Left || action == core::GameAction::Right) {
            if(!handleFocusedNativeDirection(action)) moveWidgetFocus(action); return true;
        }
        if (action == core::GameAction::Confirm) { activateFocusedWidget(); return true; }
        if (action == core::GameAction::Cancel || action == core::GameAction::Quit) { setFocusedElement(QString()); return true; }
    }

    const bool listEnabled = dataBindingEnabled(QStringLiteral("menu.list"), true);
    if (!listEnabled && action != core::GameAction::Cancel && action != core::GameAction::Quit)
        return true;
    if (action == core::GameAction::Up) move(-1);
    else if (action == core::GameAction::Down) move(1);
    else if (action == core::GameAction::Left) adjust(-1);
    else if (action == core::GameAction::Right) adjust(1);
    else if (action == core::GameAction::Confirm) activateSelected();
    else if (action == core::GameAction::Cancel || action == core::GameAction::Quit) back();
    else return false;
    return true;
}

void UiMenuController::activateSelected()
{
    if (!active() || m_selected < 0 || m_selected >= m_entries.size() ||
        !m_entries.at(m_selected).enabled) return;
    const QString value = m_entries.at(m_selected).value;
    executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("press"));
    executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("click"));
    if (!active() || m_closing) return;

    switch (m_screen) {
    case UiMenuScreen::Main:
        if (value == QLatin1String("status")) setScreen(UiMenuScreen::Status);
        else if (value == QLatin1String("inventory")) setScreen(UiMenuScreen::Inventory);
        else if (value == QLatin1String("equipment")) setScreen(UiMenuScreen::EquipmentActor);
        else if (value == QLatin1String("skills")) setScreen(UiMenuScreen::SkillsActor);
        else if (value == QLatin1String("quests")) setScreen(UiMenuScreen::Quests);
        else if (value == QLatin1String("dialogueLog")) setScreen(UiMenuScreen::DialogueLog);
        else if (value == QLatin1String("save")) setScreen(UiMenuScreen::Save);
        else if (value == QLatin1String("load")) setScreen(UiMenuScreen::Load);
        else if (value == QLatin1String("settings")) setScreen(UiMenuScreen::Settings);
        else if (value == QLatin1String("return")) { m_returnToTitleRequested = true; close(); }
        break;
    case UiMenuScreen::Status:
        break;
    case UiMenuScreen::Inventory: {
        QString category;
        const core::DatabaseRecord* item = inventoryRecord(m_session.editor(), value, &category);
        if (!item || category != QLatin1String("items")) {
            m_notice = trUi("Este registro é equipamento e deve ser usado na tela Equipamento.");
            break;
        }
        const QString scope = item->data.value(QStringLiteral("scope"), QStringLiteral("allyOne")).toString();
        if (scope == QLatin1String("allyOne") && !m_session.state().party().isEmpty()) {
            m_pendingItemId = value;
            setScreen(UiMenuScreen::InventoryTarget);
        } else {
            useInventoryItem(value);
            rebuildInventory();
        }
        break;
    }
    case UiMenuScreen::InventoryTarget:
        useInventoryItem(m_pendingItemId, value);
        { const QString notice = m_notice; setScreen(UiMenuScreen::Inventory, false); m_notice = notice; }
        break;
    case UiMenuScreen::EquipmentActor:
        m_pendingActorId = value;
        setScreen(UiMenuScreen::EquipmentSlot);
        break;
    case UiMenuScreen::EquipmentSlot:
        m_pendingEquipSlot = value;
        setScreen(UiMenuScreen::EquipmentItem);
        break;
    case UiMenuScreen::EquipmentItem:
        applyEquipment(value);
        { const QString notice = m_notice; setScreen(UiMenuScreen::EquipmentSlot, false); m_notice = notice; }
        break;
    case UiMenuScreen::SkillsActor:
        m_pendingActorId = value;
        setScreen(UiMenuScreen::Skills);
        break;
    case UiMenuScreen::Skills: {
        m_pendingSkillId = value;
        const core::DatabaseRecord* skill = databaseRecord(m_session.editor(), QStringLiteral("skills"), value);
        if (!skill) break;
        const QString scope = skill->data.value(QStringLiteral("scope"), QStringLiteral("enemyOne")).toString();
        if (scope == QLatin1String("allyOne")) setScreen(UiMenuScreen::SkillTarget);
        else if (scope == QLatin1String("self") || scope == QLatin1String("allyAll")) {
            useSkill(m_pendingActorId, value, scope == QLatin1String("self") ? m_pendingActorId : QString());
            rebuildSkills();
        } else {
            m_notice = trUi("Esta habilidade só pode ser usada durante a batalha.");
        }
        break;
    }
    case UiMenuScreen::SkillTarget:
        useSkill(m_pendingActorId, m_pendingSkillId, value);
        { const QString notice = m_notice; setScreen(UiMenuScreen::Skills, false); m_notice = notice; }
        break;
    case UiMenuScreen::Quests:
        break;
    case UiMenuScreen::DialogueLog:
        m_session.dialogueHistory().markRead(value.toInt());
        rebuildDialogueLog();
        break;
    case UiMenuScreen::Save: {
        const int slot = value.toInt();
        GameSaveSummary previous;
        if (readGameSaveSummary(gameSavePath(m_session.editor(), slot), m_session.editor(), &previous)) {
            m_pendingSaveSlot = slot;
            setScreen(UiMenuScreen::SaveOverwrite);
        } else {
            performSave(slot);
            rebuildSaves(false);
        }
        break;
    }
    case UiMenuScreen::Load:
        performLoad(value.toInt());
        break;
    case UiMenuScreen::SaveOverwrite:
        if (value == QLatin1String("yes")) {
            performSave(m_pendingSaveSlot);
            { const QString notice = m_notice; setScreen(UiMenuScreen::Save, false); m_notice = notice; }
        } else setScreen(UiMenuScreen::Save, false);
        break;
    case UiMenuScreen::Settings:
        if (value == QLatin1String("fullscreen")) {
            QSettings settings;
            settings.setValue(QStringLiteral("game/fullscreen"), !settings.value(QStringLiteral("game/fullscreen"), false).toBool());
            m_fullscreenToggleRequested = true;
            rebuildSettings();
        } else adjust(1);
        break;
    case UiMenuScreen::Closed:
        break;
    }
    refreshDetail();
    if (active()) executeUiEvent(QStringLiteral("menu.list"), QStringLiteral("release"));
}

void UiMenuController::adjust(int delta)
{
    if (m_screen == UiMenuScreen::DialogueLog) {
        m_dialogueUnreadOnly = !m_dialogueUnreadOnly;
        rebuildDialogueLog(); refreshDetail(); return;
    }
    if (m_screen != UiMenuScreen::Settings || m_selected < 0 || m_selected >= m_entries.size()) return;
    core::Editor& ed = m_session.editor();
    const QString key = m_entries.at(m_selected).value;
    if (key == QLatin1String("masterVolume") || key == QLatin1String("bgmVolume") ||
               key == QLatin1String("bgsVolume") || key == QLatin1String("meVolume") ||
               key == QLatin1String("seVolume") ||
               key == QLatin1String("voiceVolume")) {
        QSettings settings;
        const QString settingsKey = key == QLatin1String("masterVolume") ? QStringLiteral("audio/master")
            : key == QLatin1String("bgmVolume") ? QStringLiteral("audio/bgm")
            : key == QLatin1String("bgsVolume") ? QStringLiteral("audio/bgs")
            : key == QLatin1String("meVolume") ? QStringLiteral("audio/me")
            : key == QLatin1String("voiceVolume") ? QStringLiteral("audio/voice")
            : QStringLiteral("audio/se");
        const int current = qBound(0, settings.value(settingsKey, 100).toInt(), 100);
        settings.setValue(settingsKey, qBound(0, current + delta * 5, 100));
        m_audioSettingsChanged = true;
    } else if (key == QLatin1String("textSpeed")) {
        QSettings settings;
        const int current = qBound(50, settings.value(QStringLiteral("game/textSpeedPercent"), ed.accessibility.defaultTextSpeedPercent).toInt(), 200);
        settings.setValue(QStringLiteral("game/textSpeedPercent"), qBound(50, current + delta * 10, 200));
        m_session.refreshPlayerPreferences();
    } else if (key == QLatin1String("language")) {
        QStringList locales = ed.localization.enabledLocaleCodes();
        if (!locales.isEmpty()) {
            QString current = m_session.playerLocale();
            int index = locales.indexOf(current); if (index < 0) index = 0;
            index = (index + (delta >= 0 ? 1 : -1) + locales.size()) % locales.size();
            const QString resolved = m_session.setPlayerLocale(locales.at(index));
            m_notice = ed.localization.resolve(QStringLiteral("system.settings.language_changed"), trUi("Idioma alterado."), resolved);
        }
    } else if (key == QLatin1String("uiScale")) {
        if (ed.accessibility.allowUiScale) { QSettings settings; const int current=qBound(100,settings.value(QStringLiteral("game/uiScalePercent"),ed.accessibility.defaultUiScalePercent).toInt(),150); settings.setValue(QStringLiteral("game/uiScalePercent"),qBound(100,current+delta*10,150)); m_session.refreshPlayerPreferences(); }
    } else if (key == QLatin1String("reduceShake")) {
        if (ed.accessibility.allowReducedMotion) { QSettings settings; const bool current=settings.value(QStringLiteral("game/reduceShake"),ed.accessibility.reduceShakeDefault).toBool(); settings.setValue(QStringLiteral("game/reduceShake"),!current); m_session.refreshPlayerPreferences(); }
    } else if (key == QLatin1String("reduceFlash")) {
        if (ed.accessibility.allowReducedMotion) { QSettings settings; const bool current=settings.value(QStringLiteral("game/reduceFlash"),ed.accessibility.reduceFlashDefault).toBool(); settings.setValue(QStringLiteral("game/reduceFlash"),!current); m_session.refreshPlayerPreferences(); }
    } else if (key == QLatin1String("strongFocus")) {
        QSettings settings; const bool current=settings.value(QStringLiteral("game/strongFocus"),ed.accessibility.strongFocusDefault).toBool(); settings.setValue(QStringLiteral("game/strongFocus"),!current); m_session.refreshPlayerPreferences();
    } else if (key == QLatin1String("uiVolume")) {
        ed.gameUi.soundVolume = qBound(0, ed.gameUi.soundVolume + delta * 5, 100);
    } else if (key == QLatin1String("opacity")) {
        ed.gameUi.windowOpacity = qBound(20, ed.gameUi.windowOpacity + delta * 5, 100);
    } else if (key == QLatin1String("animation")) {
        ed.gameUi.animationMs = qBound(0, ed.gameUi.animationMs + delta * 20, 1000);
    } else if (key == QLatin1String("filter")) {
        const QString current = ed.runtimeScaleFilter.trimmed().toLower();
        ed.runtimeScaleFilter = current == QLatin1String("nearest")
            ? QStringLiteral("bilinear") : QStringLiteral("nearest");
        m_session.refreshPlayerPreferences();
    } else if (key == QLatin1String("fullscreen")) {
        QSettings settings;
        settings.setValue(QStringLiteral("game/fullscreen"), !settings.value(QStringLiteral("game/fullscreen"), false).toBool());
        m_fullscreenToggleRequested = true;
    }
    rebuildSettings();
    refreshDetail();
}

void UiMenuController::refreshLocalization()
{
    if (active()) rebuild();
    // Telas customizadas usam UiWidgetRenderer, que resolve Text Keys a cada
    // draw. Ainda assim, o detalhe/notice legado deve acompanhar o idioma.
    refreshDetail();
}

void UiMenuController::rebuild()
{
    switch (m_screen) {
    case UiMenuScreen::Main: rebuildMain(); break;
    case UiMenuScreen::Status: rebuildStatus(); break;
    case UiMenuScreen::Inventory: rebuildInventory(); break;
    case UiMenuScreen::InventoryTarget: rebuildInventoryTargets(); break;
    case UiMenuScreen::EquipmentActor: rebuildEquipmentActors(); break;
    case UiMenuScreen::EquipmentSlot: rebuildEquipmentSlots(); break;
    case UiMenuScreen::EquipmentItem: rebuildEquipmentItems(); break;
    case UiMenuScreen::SkillsActor: rebuildSkillsActors(); break;
    case UiMenuScreen::Skills: rebuildSkills(); break;
    case UiMenuScreen::SkillTarget: rebuildSkillTargets(); break;
    case UiMenuScreen::Quests: rebuildQuests(); break;
    case UiMenuScreen::DialogueLog: rebuildDialogueLog(); break;
    case UiMenuScreen::Save: rebuildSaves(false); break;
    case UiMenuScreen::Load: rebuildSaves(true); break;
    case UiMenuScreen::SaveOverwrite: rebuildOverwriteConfirm(); break;
    case UiMenuScreen::Settings: rebuildSettings(); break;
    case UiMenuScreen::Closed: break;
    }
    if (!m_entries.isEmpty()) {
        m_selected = qBound(0, m_selected, m_entries.size() - 1);
        if (!m_entries.at(m_selected).enabled) move(1);
    } else m_selected = 0;
    refreshDetail();
}

void UiMenuController::rebuildMain()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.title"), trUi("Menu"));
    m_entries = {
        {projectTr(m_session, QStringLiteral("system.menu.status"), trUi("Status")), QStringLiteral("status"), !m_session.state().party().isEmpty()},
        {projectTr(m_session, QStringLiteral("system.menu.items"), trUi("Itens")), QStringLiteral("inventory"), true},
        {projectTr(m_session, QStringLiteral("system.menu.equipment"), trUi("Equipamento")), QStringLiteral("equipment"), !m_session.state().party().isEmpty()},
        {projectTr(m_session, QStringLiteral("system.menu.skills"), trUi("Habilidades")), QStringLiteral("skills"), !m_session.state().party().isEmpty()},
        {projectTr(m_session, QStringLiteral("system.menu.quests"), trUi("Missões")), QStringLiteral("quests"), true},
        {QStringLiteral("%1%2").arg(projectTr(m_session, QStringLiteral("system.menu.dialogue_history"), trUi("Histórico de diálogos")),
             m_session.dialogueHistory().unreadCount() > 0 ? QStringLiteral("  (%1)").arg(m_session.dialogueHistory().unreadCount()) : QString()), QStringLiteral("dialogueLog"), !m_session.dialogueHistory().entries().isEmpty()},
        {projectTr(m_session, QStringLiteral("system.menu.save"), trUi("Salvar")), QStringLiteral("save"), true},
        {projectTr(m_session, QStringLiteral("system.menu.load"), trUi("Carregar")), QStringLiteral("load"), true},
        {projectTr(m_session, QStringLiteral("system.menu.settings"), trUi("Configurações")), QStringLiteral("settings"), true},
        {m_standalone ? projectTr(m_session, QStringLiteral("system.menu.return_title"), trUi("Voltar ao título")) : trUi("Encerrar teste"), QStringLiteral("return"), true}
    };
}

void UiMenuController::rebuildStatus()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.status"), trUi("Status"));
    m_entries.clear();
    for (const PartyMemberState& member : m_session.state().party()) {
        const CombatStats stats = memberStats(m_session.editor(), member);
        m_entries.push_back({QStringLiteral("%1   Nv %2   HP %3/%4")
                                 .arg(actorName(m_session.editor(), member.actorId))
                                 .arg(member.level).arg(member.hp).arg(stats.maxHp),
                             member.actorId, true});
    }
}

void UiMenuController::rebuildInventory()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.items"), trUi("Inventário"));
    m_entries.clear();
    for (const QString& id : m_session.state().inventoryIds()) {
        QString category;
        const core::DatabaseRecord* record = inventoryRecord(m_session.editor(), id, &category);
        const QString name = record ? databaseRecordName(m_session.editor(), category, id, id) : id;
        m_entries.push_back({QStringLiteral("%1  ×%2").arg(name).arg(m_session.state().itemCount(id)), id, true});
    }
    if (m_entries.isEmpty()) m_notice = trUi("O inventário está vazio.");
}

void UiMenuController::rebuildInventoryTargets()
{
    m_title = trUi("Usar item — Alvo");
    m_entries.clear();
    for (const PartyMemberState& member : m_session.state().party())
        m_entries.push_back({actorName(m_session.editor(), member.actorId), member.actorId, true});
}

void UiMenuController::rebuildEquipmentActors()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.equipment"), trUi("Equipamento"));
    m_entries.clear();
    for (const PartyMemberState& member : m_session.state().party())
        m_entries.push_back({actorName(m_session.editor(), member.actorId), member.actorId, true});
}

void UiMenuController::rebuildEquipmentSlots()
{
    m_title = trUi("Equipamento — Slot");
    m_entries.clear();
    const PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
    if (!member) return;
    m_entries = {
        {trUi("Arma"), QStringLiteral("weapon"), true},
        {trUi("Armadura"), QStringLiteral("armor"), true},
        {trUi("Acessório"), QStringLiteral("accessory"), true}
    };
}

void UiMenuController::rebuildEquipmentItems()
{
    m_title = trUi("Equipamento — Escolher");
    m_entries.clear();
    const PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
    if (!member) return;
    QString equipped;
    QString category = m_pendingEquipSlot == QLatin1String("weapon") ? QStringLiteral("weapons") : QStringLiteral("armors");
    if (m_pendingEquipSlot == QLatin1String("weapon")) equipped = member->weaponId;
    else if (m_pendingEquipSlot == QLatin1String("armor")) equipped = member->armorId;
    else equipped = member->accessoryId;
    m_entries.push_back({trUi("(nenhum)"), QString(), true});
    for (const core::DatabaseRecord& record : m_session.editor().database.value(category)) {
        const QString itemSlot = record.data.value(QStringLiteral("slot")).toString();
        if (!itemSlot.isEmpty() && itemSlot != m_pendingEquipSlot) continue;
        if (m_session.state().itemCount(record.id) <= 0 && record.id != equipped) continue;
        m_entries.push_back({QStringLiteral("%1   [%2]").arg(databaseRecordName(m_session.editor(), category, record.id, record.name)).arg(m_session.state().itemCount(record.id)),
                             record.id, true});
    }
}

void UiMenuController::rebuildSkillsActors()
{
    m_title = trUi("Habilidades — Personagem");
    m_entries.clear();
    for (const PartyMemberState& member : m_session.state().party())
        m_entries.push_back({actorName(m_session.editor(), member.actorId), member.actorId, true});
}

void UiMenuController::rebuildSkills()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.skills"), trUi("Habilidades"));
    m_entries.clear();
    const PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
    if (!member) return;
    for (const QString& id : actorSkillIds(m_session.editor(), *member)) {
        const core::DatabaseRecord* skill = databaseRecord(m_session.editor(), QStringLiteral("skills"), id);
        if (!skill) continue;
        const int cost = qMax(0, skill->data.value(QStringLiteral("mpCost")).toInt());
        m_entries.push_back({QStringLiteral("%1   MP %2").arg(skill->name).arg(cost), id, member->mp >= cost});
    }
    if (m_entries.isEmpty()) m_notice = trUi("Este personagem não possui habilidades configuradas.");
}

void UiMenuController::rebuildSkillTargets()
{
    m_title = trUi("Habilidade — Alvo");
    m_entries.clear();
    for (const PartyMemberState& member : m_session.state().party())
        m_entries.push_back({actorName(m_session.editor(), member.actorId), member.actorId, true});
}

void UiMenuController::rebuildQuests()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.quests"), trUi("Missões"));
    m_entries.clear();
    for (const QString& id : m_session.state().questIds()) {
        const QuestState state = m_session.state().quest(id);
        const core::DatabaseRecord* record = databaseRecord(m_session.editor(), QStringLiteral("quests"), id);
        if (!record) continue;
        const QString status = state.status == QLatin1String("completed") ? trUi("Concluída")
            : state.status == QLatin1String("failed") ? trUi("Falhou") : trUi("Ativa");
        m_entries.push_back({QStringLiteral("%1   ·   %2   %3/%4")
                                 .arg(databaseRecordName(m_session.editor(), QStringLiteral("quests"), record->id, record->name), status).arg(state.progress).arg(state.target), id, true});
    }
    if (m_entries.isEmpty()) m_notice = trUi("Nenhuma missão foi iniciada.");
}

void UiMenuController::rebuildSaves(bool loadMode)
{
    m_title = loadMode ? projectTr(m_session, QStringLiteral("system.menu.load"), trUi("Carregar partida"))
                       : projectTr(m_session, QStringLiteral("system.menu.save"), trUi("Salvar partida"));
    m_entries.clear();
    for (int slot = 1; slot <= 99; ++slot) {
        GameSaveSummary s;
        const bool occupied = readGameSaveSummary(gameSavePath(m_session.editor(), slot), m_session.editor(), &s) && s.valid;
        QString label;
        if (occupied) {
            label = QStringLiteral("Slot %1   %2   %3")
                .arg(slot, 2, 10, QLatin1Char('0')).arg(s.mapName, formattedTime(s.playTimeSeconds));
            if (s.savedAt.isValid()) label += QStringLiteral("   %1").arg(s.savedAt.toString(QStringLiteral("dd/MM HH:mm")));
        } else {
            label = QStringLiteral("Slot %1   — %2 —").arg(slot, 2, 10, QLatin1Char('0')).arg(trUi("vazio"));
        }
        m_entries.push_back({label, QString::number(slot), !loadMode || occupied});
    }
}

void UiMenuController::rebuildDialogueLog()
{
    m_title = projectTr(m_session, QStringLiteral("system.menu.dialogue_history"), trUi("Histórico de diálogos"));
    m_entries.clear();
    const auto& history = m_session.dialogueHistory().entries();
    for (int i = history.size() - 1; i >= 0; --i) {
        const core::DialogueEntry& entry = history.at(i);
        if (m_dialogueUnreadOnly && entry.read) continue;
        const QString who = entry.speaker.isEmpty() ? trUi("Narrador") : entry.speaker;
        QString preview = entry.text.simplified();
        if (preview.size() > 72) preview = preview.left(69) + QStringLiteral("…");
        m_entries.push_back({QStringLiteral("%1%2 — %3").arg(entry.read ? QString() : QStringLiteral("● "), who, preview), QString::number(i), true});
    }
    if (m_entries.isEmpty()) m_notice = m_dialogueUnreadOnly ? trUi("Não há falas não lidas.") : trUi("O histórico está vazio.");
}

void UiMenuController::rebuildOverwriteConfirm()
{
    m_title = trUi("Sobrescrever save?");
    m_entries = {{trUi("Sim, sobrescrever"), QStringLiteral("yes"), true},
                 {trUi("Não"), QStringLiteral("no"), true}};
    m_detail = trUi("O conteúdo atual deste slot será substituído.");
}

void UiMenuController::rebuildSettings()
{
    const auto loc = [this](const QString& key, const QString& fallback) { return projectTr(m_session, key, fallback); };
    m_title = loc(QStringLiteral("system.settings.title"), trUi("Configurações"));
    QSettings settings;
    const core::Editor& ed = m_session.editor();
    const QString filter = ed.runtimeScaleFilter.trimmed().toLower();
    const bool fullscreen = settings.value(QStringLiteral("game/fullscreen"), false).toBool();
    const int master = qBound(0, settings.value(QStringLiteral("audio/master"), 100).toInt(), 100);
    const int bgm = qBound(0, settings.value(QStringLiteral("audio/bgm"), 100).toInt(), 100);
    const int bgs = qBound(0, settings.value(QStringLiteral("audio/bgs"), 100).toInt(), 100);
    const int me = qBound(0, settings.value(QStringLiteral("audio/me"), 100).toInt(), 100);
    const int se = qBound(0, settings.value(QStringLiteral("audio/se"), 100).toInt(), 100);
    const int voice = qBound(0, settings.value(QStringLiteral("audio/voice"), 100).toInt(), 100);
    const int textSpeed = qBound(50, settings.value(QStringLiteral("game/textSpeedPercent"), ed.accessibility.defaultTextSpeedPercent).toInt(), 200);
    const QString locale = m_session.playerLocale();
    const int uiScale = qBound(100, settings.value(QStringLiteral("game/uiScalePercent"), ed.accessibility.defaultUiScalePercent).toInt(), 150);
    const bool reduceShake = settings.value(QStringLiteral("game/reduceShake"), ed.accessibility.reduceShakeDefault).toBool();
    const bool reduceFlash = settings.value(QStringLiteral("game/reduceFlash"), ed.accessibility.reduceFlashDefault).toBool();
    const bool strongFocus = settings.value(QStringLiteral("game/strongFocus"), ed.accessibility.strongFocusDefault).toBool();
    const QString onText = loc(QStringLiteral("system.common.on"), trUi("Ligado"));
    const QString offText = loc(QStringLiteral("system.common.off"), trUi("Desligado"));
    m_entries = {
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.fullscreen"), trUi("Tela cheia")), fullscreen ? onText : offText), QStringLiteral("fullscreen"), true},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.master_volume"), trUi("Volume geral"))).arg(master), QStringLiteral("masterVolume"), true},
        {QStringLiteral("BGM: %1%").arg(bgm), QStringLiteral("bgmVolume"), true},
        {QStringLiteral("BGS: %1%").arg(bgs), QStringLiteral("bgsVolume"), true},
        {QStringLiteral("ME: %1%").arg(me), QStringLiteral("meVolume"), true},
        {QStringLiteral("SE: %1%").arg(se), QStringLiteral("seVolume"), true},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.voice_volume"), trUi("Voz"))).arg(voice), QStringLiteral("voiceVolume"), true},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.ui_volume"), trUi("Volume da UI"))).arg(ed.gameUi.soundVolume), QStringLiteral("uiVolume"), true},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.text_speed"), trUi("Velocidade do texto"))).arg(textSpeed), QStringLiteral("textSpeed"), true},
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.language"), trUi("Idioma")), locale), QStringLiteral("language"), ed.localization.enabled && ed.localization.enabledLocaleCodes().size() > 1},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.ui_scale"), trUi("Escala da UI/texto"))).arg(uiScale), QStringLiteral("uiScale"), ed.accessibility.enabled && ed.accessibility.allowUiScale},
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.reduce_shake"), trUi("Reduzir tremores")), reduceShake ? onText : offText), QStringLiteral("reduceShake"), ed.accessibility.enabled && ed.accessibility.allowReducedMotion},
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.reduce_flash"), trUi("Reduzir flashes")), reduceFlash ? onText : offText), QStringLiteral("reduceFlash"), ed.accessibility.enabled && ed.accessibility.allowReducedMotion},
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.strong_focus"), trUi("Foco reforçado")), strongFocus ? onText : offText), QStringLiteral("strongFocus"), ed.accessibility.enabled},
        {QStringLiteral("%1: %2%").arg(loc(QStringLiteral("system.settings.window_opacity"), trUi("Opacidade das janelas"))).arg(ed.gameUi.windowOpacity), QStringLiteral("opacity"), true},
        {QStringLiteral("%1: %2 ms").arg(loc(QStringLiteral("system.settings.ui_animation"), trUi("Animação da UI"))).arg(ed.gameUi.animationMs), QStringLiteral("animation"), true},
        {QStringLiteral("%1: %2").arg(loc(QStringLiteral("system.settings.scale_filter"), trUi("Filtro de escala")), filter == QLatin1String("bilinear") ? loc(QStringLiteral("system.common.bilinear"), trUi("Bilinear")) : loc(QStringLiteral("system.common.nearest"), trUi("Nearest"))), QStringLiteral("filter"), true}
    };
}

void UiMenuController::refreshDetail()
{
    if (!active() || m_entries.isEmpty() || m_selected < 0 || m_selected >= m_entries.size()) {
        if (m_screen != UiMenuScreen::SaveOverwrite) m_detail.clear();
        return;
    }
    const QString id = m_entries.at(m_selected).value;
    m_detail.clear();
    switch (m_screen) {
    case UiMenuScreen::Main:
        if (id == QLatin1String("status")) m_detail = trUi("Veja atributos, nível, HP, MP e equipamentos do grupo.");
        else if (id == QLatin1String("inventory")) m_detail = trUi("Consulte e use itens do inventário.");
        else if (id == QLatin1String("equipment")) m_detail = trUi("Troque armas, armaduras e acessórios.");
        else if (id == QLatin1String("skills")) m_detail = trUi("Use habilidades de suporte fora da batalha.");
        else if (id == QLatin1String("quests")) m_detail = trUi("Acompanhe missões e progresso.");
        else if (id == QLatin1String("dialogueLog")) m_detail = trUi("Releia falas anteriores e filtre as que ainda não foram abertas.");
        else if (id == QLatin1String("save")) m_detail = trUi("Grave o estado atual da partida.");
        else if (id == QLatin1String("load")) m_detail = trUi("Carregue um slot existente.");
        else if (id == QLatin1String("settings")) m_detail = trUi("Ajuste apresentação e preferências locais do Player.");
        break;
    case UiMenuScreen::Status: {
        const PartyMemberState* member = m_session.state().partyMember(id);
        if (!member) break;
        const CombatStats stats = memberStats(m_session.editor(), *member);
        const core::DatabaseRecord* actor = databaseRecord(m_session.editor(), QStringLiteral("actors"), id);
        const QString klass = actor ? databaseRecordName(m_session.editor(), QStringLiteral("classes"), actor->data.value(QStringLiteral("classId")).toString(), trUi("Sem classe")) : trUi("Sem classe");
        const int next = experienceToNextLevel(m_session.editor(), *member);
        m_detail = QStringLiteral("%1\n%2 · %3 %4\n\nHP  %5 / %6\nMP  %7 / %8\nATQ %9\nDEF %10\nAGI %11\n\nEXP %12\n%13: %14\n\n%15: %16\n%17: %18\n%19: %20")
            .arg(actorName(m_session.editor(), id), klass, trUi("Nível")).arg(member->level)
            .arg(member->hp).arg(stats.maxHp).arg(member->mp).arg(stats.maxMp)
            .arg(stats.attack).arg(stats.defense).arg(stats.agility).arg(member->experience)
            .arg(trUi("Próximo nível"), next > 0 ? QString::number(next) : trUi("Máximo"))
            .arg(trUi("Arma"), databaseRecordName(m_session.editor(), QStringLiteral("weapons"), member->weaponId, trUi("Nenhuma")))
            .arg(trUi("Armadura"), databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->armorId, trUi("Nenhuma")))
            .arg(trUi("Acessório"), databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->accessoryId, trUi("Nenhum")));
        break;
    }
    case UiMenuScreen::Inventory: {
        QString category;
        const core::DatabaseRecord* record = inventoryRecord(m_session.editor(), id, &category);
        if (!record) break;
        QString effect;
        const int hp = record->data.value(QStringLiteral("healHp")).toInt();
        const int mp = record->data.value(QStringLiteral("healMp")).toInt();
        if (hp) effect += QStringLiteral("HP %1  ").arg(signedValue(hp));
        if (mp) effect += QStringLiteral("MP %1").arg(signedValue(mp));
        if (effect.isEmpty()) effect = category == QLatin1String("items") ? trUi("Sem efeito utilizável fora da batalha") : trUi("Equipamento");
        m_detail = QStringLiteral("%1\n\n%2\n\n%3\n%4: %5 G")
            .arg(databaseRecordName(m_session.editor(), category, record->id, record->name),
                 databaseRecordDescription(m_session.editor(), category, record->id, record->description), effect, trUi("Preço"))
            .arg(record->data.value(QStringLiteral("price")).toInt());
        break;
    }
    case UiMenuScreen::InventoryTarget: {
        const PartyMemberState* member = m_session.state().partyMember(id);
        if (member) {
            const CombatStats stats = memberStats(m_session.editor(), *member);
            m_detail = QStringLiteral("%1\nHP %2/%3\nMP %4/%5").arg(actorName(m_session.editor(), id)).arg(member->hp).arg(stats.maxHp).arg(member->mp).arg(stats.maxMp);
        }
        break;
    }
    case UiMenuScreen::EquipmentActor:
    case UiMenuScreen::SkillsActor: {
        const PartyMemberState* member = m_session.state().partyMember(id);
        if (member) {
            const CombatStats stats = memberStats(m_session.editor(), *member);
            m_detail = QStringLiteral("%1\n%2 %3\nHP %4/%5   MP %6/%7").arg(actorName(m_session.editor(), id), trUi("Nível")).arg(member->level).arg(member->hp).arg(stats.maxHp).arg(member->mp).arg(stats.maxMp);
        }
        break;
    }
    case UiMenuScreen::EquipmentSlot: {
        const PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
        if (!member) break;
        QString equipped;
        if (id == QLatin1String("weapon")) equipped = databaseRecordName(m_session.editor(), QStringLiteral("weapons"), member->weaponId, trUi("Nenhuma"));
        else if (id == QLatin1String("armor")) equipped = databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->armorId, trUi("Nenhuma"));
        else equipped = databaseRecordName(m_session.editor(), QStringLiteral("armors"), member->accessoryId, trUi("Nenhum"));
        m_detail = QStringLiteral("%1\n\n%2: %3").arg(actorName(m_session.editor(), m_pendingActorId), trUi("Equipado"), equipped);
        break;
    }
    case UiMenuScreen::EquipmentItem: {
        const PartyMemberState* original = m_session.state().partyMember(m_pendingActorId);
        if (!original) break;
        PartyMemberState preview = *original;
        if (m_pendingEquipSlot == QLatin1String("weapon")) preview.weaponId = id;
        else if (m_pendingEquipSlot == QLatin1String("armor")) preview.armorId = id;
        else preview.accessoryId = id;
        const CombatStats before = memberStats(m_session.editor(), *original);
        const CombatStats after = memberStats(m_session.editor(), preview);
        auto deltaText=[](int a,int b){const int d=b-a;return d==0?QString():QStringLiteral(" (%1)").arg(signedValue(d));};
        m_detail = QStringLiteral("%1\n\nHP %2%3\nMP %4%5\nATQ %6%7\nDEF %8%9\nAGI %10%11")
            .arg(actorName(m_session.editor(), m_pendingActorId))
            .arg(after.maxHp).arg(deltaText(before.maxHp, after.maxHp))
            .arg(after.maxMp).arg(deltaText(before.maxMp, after.maxMp))
            .arg(after.attack).arg(deltaText(before.attack, after.attack))
            .arg(after.defense).arg(deltaText(before.defense, after.defense))
            .arg(after.agility).arg(deltaText(before.agility, after.agility));
        break;
    }
    case UiMenuScreen::Skills: {
        const core::DatabaseRecord* skill = databaseRecord(m_session.editor(), QStringLiteral("skills"), id);
        const PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
        if (!skill || !member) break;
        m_detail = QStringLiteral("%1\n\n%2\n\nMP: %3\n%4: %5\n%6: %7 / %8")
            .arg(databaseRecordName(m_session.editor(), QStringLiteral("skills"), skill->id, skill->name),
                 databaseRecordDescription(m_session.editor(), QStringLiteral("skills"), skill->id, skill->description))
            .arg(skill->data.value(QStringLiteral("mpCost")).toInt())
            .arg(trUi("Escopo"), skill->data.value(QStringLiteral("scope"), QStringLiteral("enemyOne")).toString())
            .arg(trUi("HP/MP atual")).arg(member->hp).arg(member->mp);
        break;
    }
    case UiMenuScreen::SkillTarget: {
        const PartyMemberState* member = m_session.state().partyMember(id);
        if (member) {
            const CombatStats stats = memberStats(m_session.editor(), *member);
            m_detail = QStringLiteral("%1\nHP %2/%3\nMP %4/%5").arg(actorName(m_session.editor(), id)).arg(member->hp).arg(stats.maxHp).arg(member->mp).arg(stats.maxMp);
        }
        break;
    }
    case UiMenuScreen::Quests: {
        const core::DatabaseRecord* record = databaseRecord(m_session.editor(), QStringLiteral("quests"), id);
        if (!record) break;
        const QuestState state = m_session.state().quest(id);
        const QString status = state.status == QLatin1String("completed") ? trUi("Concluída") : state.status == QLatin1String("failed") ? trUi("Falhou") : trUi("Em andamento");
        m_detail = QStringLiteral("%1\n\n%2\n\n%3\n%4: %5 / %6").arg(databaseRecordName(m_session.editor(), QStringLiteral("quests"), record->id, record->name),
                 databaseRecordDescription(m_session.editor(), QStringLiteral("quests"), record->id, record->description), status, trUi("Progresso")).arg(state.progress).arg(state.target);
        break;
    }
    case UiMenuScreen::DialogueLog: {
        const int index = id.toInt();
        const auto& history = m_session.dialogueHistory().entries();
        if (index < 0 || index >= history.size()) break;
        const core::DialogueEntry& entry = history.at(index);
        m_detail = QStringLiteral("%1\n%2\n\n%3\n\nMapa: %4   Evento: %5")
            .arg(entry.speaker.isEmpty() ? trUi("Narrador") : entry.speaker,
                 entry.timestamp.toLocalTime().toString(QStringLiteral("dd/MM/yyyy HH:mm")),
                 entry.text, entry.mapId, entry.eventId);
        break;
    }
    case UiMenuScreen::Save:
        m_detail = trUi("Escolha um slot. Slots ocupados pedem confirmação antes de sobrescrever.");
        break;
    case UiMenuScreen::Load:
        m_detail = trUi("Escolha um slot existente para restaurar a partida.");
        break;
    case UiMenuScreen::SaveOverwrite:
        break;
    case UiMenuScreen::Settings:
        if (id == QLatin1String("renderer")) m_detail = trUi("Define o backend preferido para a próxima execução do jogo.");
        else if (id == QLatin1String("fullscreen")) m_detail = trUi("Alterna imediatamente entre janela e tela cheia.");
        else if (id == QLatin1String("masterVolume")) m_detail = trUi("Volume geral do jogo. Afeta todos os canais de áudio imediatamente.");
        else if (id == QLatin1String("bgmVolume")) m_detail = trUi("Volume das músicas de fundo (BGM).");
        else if (id == QLatin1String("bgsVolume")) m_detail = trUi("Volume dos sons ambientes contínuos (BGS).");
        else if (id == QLatin1String("seVolume")) m_detail = trUi("Volume dos efeitos sonoros (SE) e ME.");
        else if (id == QLatin1String("voiceVolume")) m_detail = trUi("Volume das falas/vozes reproduzidas pelo jogo.");
        else if (id == QLatin1String("uiVolume")) m_detail = trUi("Volume dos sons de cursor, confirmar e cancelar.");
        else if (id == QLatin1String("textSpeed")) m_detail = trUi("Velocidade da digitação das caixas de mensagem, de 50% a 200%.");
        else if (id == QLatin1String("language")) m_detail = trUi("Idioma dos textos localizados do jogo. Textos sem tradução usam o fallback do projeto.");
        else if (id == QLatin1String("uiScale")) m_detail = trUi("Aumenta fontes e espaçamento da interface sem alterar a resolução lógica do jogo.");
        else if (id == QLatin1String("reduceShake")) m_detail = trUi("Reduz tremores de animações e efeitos visuais.");
        else if (id == QLatin1String("reduceFlash")) m_detail = trUi("Reduz ou suprime flashes intensos de animações.");
        else if (id == QLatin1String("strongFocus")) m_detail = trUi("Reforça o destaque do item focado para teclado e gamepad.");
        else if (id == QLatin1String("opacity")) m_detail = trUi("Ajusta a opacidade do Window Skin nesta execução.");
        else if (id == QLatin1String("animation")) m_detail = trUi("Velocidade das animações da interface.");
        else if (id == QLatin1String("filter")) m_detail = trUi("Nearest preserva pixel art; Bilinear suaviza a ampliação final.");
        break;
    case UiMenuScreen::Closed:
        break;
    }
}

void UiMenuController::useInventoryItem(const QString& itemId, const QString& targetActorId)
{
    const core::DatabaseRecord* item = databaseRecord(m_session.editor(), QStringLiteral("items"), itemId);
    if (!item || m_session.state().itemCount(itemId) <= 0) return;
    const int healHp = item->data.value(QStringLiteral("healHp")).toInt();
    const int healMp = item->data.value(QStringLiteral("healMp")).toInt();
    const QString addState = item->data.value(QStringLiteral("stateAddId")).toString();
    const QString removeState = item->data.value(QStringLiteral("stateRemoveId")).toString();
    if (healHp == 0 && healMp == 0 && addState.isEmpty() && removeState.isEmpty()) {
        m_notice = trUi("Este item não possui um efeito utilizável fora da batalha.");
        return;
    }
    QVector<PartyMemberState*> targets;
    const QString scope = item->data.value(QStringLiteral("scope"), QStringLiteral("allyOne")).toString();
    if (scope == QLatin1String("allyAll")) {
        for (PartyMemberState& member : m_session.state().party()) targets.push_back(&member);
    } else if (PartyMemberState* member = m_session.state().partyMember(targetActorId.isEmpty() ? (m_session.state().party().isEmpty() ? QString() : m_session.state().party().first().actorId) : targetActorId)) {
        targets.push_back(member);
    }
    if (targets.isEmpty()) { m_notice = trUi("Nenhum alvo válido."); return; }
    for (PartyMemberState* member : targets) {
        const CombatStats stats = memberStats(m_session.editor(), *member);
        member->hp = qBound(0, member->hp + healHp, stats.maxHp);
        member->mp = qBound(0, member->mp + healMp, stats.maxMp);
        applyStateEffect(m_session.editor(), *member, *item);
    }
    if (item->data.value(QStringLiteral("consumable"), true).toBool()) m_session.state().addItem(itemId, -1);
    m_notice = trUi("Item utilizado.");
}

void UiMenuController::useSkill(const QString& actorId, const QString& skillId,
                                const QString& targetActorId)
{
    PartyMemberState* actor = m_session.state().partyMember(actorId);
    const core::DatabaseRecord* skill = databaseRecord(m_session.editor(), QStringLiteral("skills"), skillId);
    if (!actor || !skill) return;
    const int cost = qMax(0, skill->data.value(QStringLiteral("mpCost")).toInt());
    if (actor->mp < cost) { m_notice = trUi("MP insuficiente."); return; }
    const QString scope = skill->data.value(QStringLiteral("scope"), QStringLiteral("enemyOne")).toString();
    if (scope != QLatin1String("self") && scope != QLatin1String("allyOne") && scope != QLatin1String("allyAll")) {
        m_notice = trUi("Esta habilidade só pode ser usada durante a batalha.");
        return;
    }
    QVector<PartyMemberState*> targets;
    if (scope == QLatin1String("self")) targets.push_back(actor);
    else if (scope == QLatin1String("allyAll")) for (PartyMemberState& member : m_session.state().party()) targets.push_back(&member);
    else if (PartyMemberState* target = m_session.state().partyMember(targetActorId)) targets.push_back(target);
    if (targets.isEmpty()) return;
    actor->mp -= cost;
    const int healHp = skill->data.value(QStringLiteral("healHp")).toInt();
    const int healMp = skill->data.value(QStringLiteral("healMp")).toInt();
    for (PartyMemberState* target : targets) {
        const CombatStats stats = memberStats(m_session.editor(), *target);
        target->hp = qBound(0, target->hp + healHp, stats.maxHp);
        target->mp = qBound(0, target->mp + healMp, stats.maxMp);
        applyStateEffect(m_session.editor(), *target, *skill);
    }
    m_notice = trUi("Habilidade utilizada.");
}

void UiMenuController::applyEquipment(const QString& itemId)
{
    PartyMemberState* member = m_session.state().partyMember(m_pendingActorId);
    if (!member) return;
    QString error;
    if (!equipItem(m_session.editor(), m_session.state(), *member, m_pendingEquipSlot, itemId, &error))
        m_notice = error;
    else
        m_notice = trUi("Equipamento atualizado.");
}

void UiMenuController::performSave(int slot)
{
    QString error;
    if (m_session.saveGame(slot, &error)) m_notice = trUi("Partida salva no slot %1.").arg(slot);
    else m_notice = error;
}

void UiMenuController::performLoad(int slot)
{
    QString error;
    if (m_session.loadGame(slot, &error)) {
        m_notice = trUi("Partida carregada.");
        if (customActive()) closeCustom(); else close();
    } else m_notice = error;
}

} // namespace game::ui
