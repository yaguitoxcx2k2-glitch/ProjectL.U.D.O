#include "UiWidgetBehavior.h"

#include "core/Editor.h"

#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace game::ui::behavior {
namespace {
QString typeOf(const QString& type) { return type.trimmed().toLower(); }
bool horizontal(const core::UiWidgetSettings& w) { return w.orientation.trimmed().toLower() != QLatin1String("vertical"); }
}

bool isToggle(const QString& type)
{
    const QString t=typeOf(type); return t==QLatin1String("checkbox")||t==QLatin1String("toggle");
}
bool isRadio(const QString& type){return typeOf(type)==QLatin1String("radio");}
bool isSlider(const QString& type){const QString t=typeOf(type);return t==QLatin1String("slider")||t==QLatin1String("scrollbar");}
bool isStepper(const QString& type){return typeOf(type)==QLatin1String("stepper");}
bool isTabs(const QString& type){const QString t=typeOf(type);return t==QLatin1String("tab-bar")||t==QLatin1String("icon-tabs")||t==QLatin1String("segmented-control");}
bool isDropdown(const QString& type){return typeOf(type)==QLatin1String("dropdown");}
bool isTextInput(const QString& type){return typeOf(type)==QLatin1String("text-input");}
bool isGrid(const QString& type){const QString t=typeOf(type);return t==QLatin1String("grid")||t==QLatin1String("card-view")||t==QLatin1String("inventory-grid");}
bool isListLike(const QString& type){const QString t=typeOf(type);return t==QLatin1String("list")||t==QLatin1String("sidebar")||t==QLatin1String("context-menu")||t==QLatin1String("data-table");}
bool isScrollable(const QString& type)
{
    const QString t=typeOf(type);return t==QLatin1String("scroll-area")||t==QLatin1String("scrollbar")||isGrid(t)||isListLike(t)||isDropdown(t);
}

int itemCount(const core::UiWidgetSettings& w)
{
    if(!w.items.isEmpty()) return w.items.size();
    if(isGrid(w.type)) return qMax(1,qMax(1,w.columns)*qMax(1,w.rows>0?w.rows:2));
    if(typeOf(w.type)==QLatin1String("data-table")) return qMax(1,qMax(1,w.columns)*qMax(1,w.rows));
    return 0;
}

int boundedIndex(const core::UiWidgetSettings& w,int index)
{
    const int count=itemCount(w);return count<=0?0:qBound(0,index,count-1);
}

int movedIndex(const core::UiWidgetSettings& w,int current,core::GameAction action)
{
    const int count=itemCount(w);if(count<=0)return 0;current=qBound(0,current,count-1);
    int delta=0;
    if(isGrid(w.type)){
        const int cols=qMax(1,w.columns);
        if(action==core::GameAction::Left)delta=-1;
        else if(action==core::GameAction::Right)delta=1;
        else if(action==core::GameAction::Up)delta=-cols;
        else if(action==core::GameAction::Down)delta=cols;
    } else if(isTabs(w.type)) {
        if(horizontal(w)){if(action==core::GameAction::Left)delta=-1;else if(action==core::GameAction::Right)delta=1;}
        else {if(action==core::GameAction::Up)delta=-1;else if(action==core::GameAction::Down)delta=1;}
    } else {
        if(action==core::GameAction::Up||action==core::GameAction::Left)delta=-1;
        else if(action==core::GameAction::Down||action==core::GameAction::Right)delta=1;
    }
    if(delta==0)return current;
    int next=current+delta;
    if(w.navigationWrap){while(next<0)next+=count;while(next>=count)next-=count;return next;}
    return qBound(0,next,count-1);
}

double steppedValue(const core::UiWidgetSettings& w,double current,int direction)
{
    const double lo=qMin(w.minimum,w.maximum),hi=qMax(w.minimum,w.maximum);
    const double step=qMax(0.000001,std::abs(w.step));
    double next=qBound(lo,current,hi)+(direction<0?-step:step);
    // Evita resíduos como 0.30000000004 ao exibir valores de Stepper/Slider.
    next=std::round(next/step)*step;
    return qBound(lo,next,hi);
}

double pointerValue(const core::UiWidgetSettings& w,const QPointF&point,const QRectF&rect)
{
    if(rect.width()<=0||rect.height()<=0)return qBound(qMin(w.minimum,w.maximum),w.value,qMax(w.minimum,w.maximum));
    qreal p=0;
    if(horizontal(w))p=(point.x()-rect.left())/rect.width();
    else p=1.0-(point.y()-rect.top())/rect.height();
    p=qBound<qreal>(0,p,1);
    const double lo=qMin(w.minimum,w.maximum),hi=qMax(w.minimum,w.maximum);
    double value=lo+(hi-lo)*p;
    const double step=qMax(0.000001,std::abs(w.step));
    value=std::round(value/step)*step;
    return qBound(lo,value,hi);
}

int pointerItemIndex(const core::UiWidgetSettings& w,const QPointF&point,const QRectF&rect)
{
    const int count=itemCount(w);if(count<=0||!rect.contains(point))return -1;
    if(isTabs(w.type)){
        const qreal p=horizontal(w)?(point.x()-rect.left())/qMax<qreal>(1,rect.width()):(point.y()-rect.top())/qMax<qreal>(1,rect.height());
        return qBound(0,int(std::floor(p*count)),count-1);
    }
    const qreal rowH=rect.height()/qMax(1,count);
    return qBound(0,int((point.y()-rect.top())/qMax<qreal>(1,rowH)),count-1);
}

int pointerGridIndex(const core::UiWidgetSettings&w,const QPointF&point,const QRectF&rect)
{
    const int count=itemCount(w);if(count<=0||!rect.contains(point))return -1;
    const int cols=qMax(1,w.columns),rows=qMax(1,int(std::ceil(count/double(cols))));
    const qreal gap=4.0,cw=(rect.width()-gap*(cols+1))/cols,ch=(rect.height()-gap*(rows+1))/rows;
    if(cw<=0||ch<=0)return -1;
    const qreal lx=point.x()-rect.left()-gap,ly=point.y()-rect.top()-gap;
    if(lx<0||ly<0)return -1;
    const int col=int(lx/(cw+gap)),row=int(ly/(ch+gap));
    if(col<0||col>=cols||row<0||row>=rows)return -1;
    const qreal inX=std::fmod(lx,cw+gap),inY=std::fmod(ly,ch+gap);
    if(inX>cw||inY>ch)return -1;
    const int index=row*cols+col;return index<count?index:-1;
}

bool directionAdjustsValue(const core::UiWidgetSettings&w,core::GameAction action)
{
    if(isStepper(w.type))return action==core::GameAction::Left||action==core::GameAction::Right;
    if(typeOf(w.type)==QLatin1String("scroll-area"))
        return action==core::GameAction::Up||action==core::GameAction::Down;
    if(isSlider(w.type)){
        if(horizontal(w))return action==core::GameAction::Left||action==core::GameAction::Right;
        return action==core::GameAction::Up||action==core::GameAction::Down;
    }
    return false;
}

bool directionAdjustsSelection(const core::UiWidgetSettings&w,core::GameAction action,bool dropdownExpanded)
{
    if(isTabs(w.type))return horizontal(w)?(action==core::GameAction::Left||action==core::GameAction::Right):(action==core::GameAction::Up||action==core::GameAction::Down);
    if(isGrid(w.type))return action==core::GameAction::Up||action==core::GameAction::Down||action==core::GameAction::Left||action==core::GameAction::Right;
    if(isListLike(w.type)||dropdownExpanded)return action==core::GameAction::Up||action==core::GameAction::Down;
    return false;
}

} // namespace game::ui::behavior
