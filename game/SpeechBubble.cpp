#include "SpeechBubble.h"
#include "core/Model.h"

#include <cmath>

namespace game {

QString SpeechBubbleManager::show(SpeechBubble bubble)
{
    bubble.target=bubble.target.trimmed();if(bubble.target.isEmpty())bubble.target=QStringLiteral("player");
    if(bubble.notification)hideNotification();else hide(bubble.target);bubble.id=core::idGen();bubble.duration=qMax(.05,bubble.duration);bubble.remaining=bubble.duration;bubble.maxWidth=qBound(80,bubble.maxWidth,1200);bubble.fontSize=qBound(8,bubble.fontSize,96);bubble.opacity=0.0;bubble.revealed=bubble.typewriter?0:bubble.text.size();m_active.push_back(bubble);return bubble.id;
}
QString SpeechBubbleManager::showNotification(SpeechBubble bubble)
{
    hideNotification();bubble.notification=true;bubble.target=QStringLiteral("notification");bubble.tailDirection=QStringLiteral("none");bubble.offsetY=0;return show([&]{SpeechBubble value=bubble;value.notification=true;return value;}());
}
void SpeechBubbleManager::hide(const QString& target){for(int i=m_active.size()-1;i>=0;--i)if(!m_active[i].notification&&m_active[i].target.compare(target.trimmed(),Qt::CaseInsensitive)==0)m_active.remove(i);}
void SpeechBubbleManager::hideAll(){m_active.clear();}
void SpeechBubbleManager::hideNotification(){for(int i=m_active.size()-1;i>=0;--i)if(m_active[i].notification)m_active.remove(i);}
void SpeechBubbleManager::update(double dt)
{
    if(dt<=0)return;for(int i=m_active.size()-1;i>=0;--i){SpeechBubble& bubble=m_active[i];bubble.remaining-=dt;const double fade=.15;bubble.opacity=qMin(1.0,bubble.opacity+dt/fade);if(bubble.remaining<fade)bubble.opacity=qMin(bubble.opacity,qMax(0.0,bubble.remaining/fade));if(bubble.typewriter&&bubble.revealed<bubble.text.size()){bubble.revealAccumulator+=dt*qMax(1.0,bubble.charsPerSecond);const int count=int(std::floor(bubble.revealAccumulator));if(count>0){bubble.revealAccumulator-=count;bubble.revealed=qMin(bubble.text.size(),bubble.revealed+count);}}if(bubble.remaining<=0)m_active.remove(i);}
}

} // namespace game
