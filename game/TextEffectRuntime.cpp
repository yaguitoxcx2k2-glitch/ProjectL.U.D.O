#include "TextEffectRuntime.h"

#include <QtMath>
#include <cmath>

namespace game {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;

double ease(QString id, double t)
{
    t = qBound(0.0, t, 1.0);
    id = id.trimmed().toLower();
    if (id == QLatin1String("quad-in")) return t*t;
    if (id == QLatin1String("quad-out")) return 1.0-(1.0-t)*(1.0-t);
    if (id == QLatin1String("quad-in-out")) return t < .5 ? 2*t*t : 1-std::pow(-2*t+2,2)/2;
    if (id == QLatin1String("expo-out")) return t >= 1.0 ? 1.0 : 1.0-std::pow(2.0,-10.0*t);
    if (id == QLatin1String("back-out")) {
        constexpr double c1=1.70158, c3=c1+1.0;
        return 1.0+c3*std::pow(t-1.0,3)+c1*std::pow(t-1.0,2);
    }
    if (id == QLatin1String("elastic-out")) {
        if (t <= 0.0 || t >= 1.0) return t;
        constexpr double c4 = 2.0 * kPi / 3.0;
        return std::pow(2.0,-10.0*t)*std::sin((t*10.0-.75)*c4)+1.0;
    }
    return t;
}

double lerp(double a,double b,double t){return a+(b-a)*t;}

int targetIndex(const core::TextEffectPhaseSpec& p,int ch,int word)
{
    if (p.target == QLatin1String("word")) return qMax(0,word);
    if (p.target == QLatin1String("block")) return 0;
    return qMax(0,ch);
}
int targetCount(const core::TextEffectPhaseSpec& p,int chars,int words)
{
    if (p.target == QLatin1String("word")) return qMax(1,words);
    if (p.target == QLatin1String("block")) return 1;
    return qMax(1,chars);
}

int phaseCyclesForFiniteGate(const core::TextEffectPhaseSpec& p)
{
    const QString mode=p.loopMode.trimmed().toLower();
    if (mode==QLatin1String("count")) return qMax(1,p.loopCount);
    if (mode==QLatin1String("ping-pong")) return 2;
    return 1;
}

double phaseTargetStartSec(const core::TextEffectPhaseSpec& p,int ch,int word,double notBeforeSec)
{
    const int idx=targetIndex(p,ch,word);
    const double scheduled=(p.delayMs + p.staggerMs*idx)/1000.0;
    return notBeforeSec >= 0.0 ? qMax(scheduled,notBeforeSec) : scheduled;
}

double phaseTargetEndSec(const core::TextEffectPhaseSpec& p,int ch,int word,double notBeforeSec)
{
    const double duration=qMax(0.001,p.durationMs/1000.0);
    return phaseTargetStartSec(p,ch,word,notBeforeSec) + duration*phaseCyclesForFiniteGate(p);
}

TextEffectSample samplePhase(const core::TextEffectPhaseSpec& p,double timeSec,int ch,int word,int chars,int words,
                             double notBeforeSec=-1.0)
{
    TextEffectSample out;
    if (!p.enabled) return out;
    const int idx=targetIndex(p,ch,word);
    const double start=phaseTargetStartSec(p,ch,word,notBeforeSec);
    const double duration=qMax(0.001,p.durationMs/1000.0);
    double local=timeSec-start;
    double progress=local/duration;
    bool active=local>=0.0;

    const QString mode=p.loopMode.trimmed().toLower();
    if (mode==QLatin1String("while-visible") || mode==QLatin1String("ping-pong") || mode==QLatin1String("count")) {
        if (!active) progress=0.0;
        else {
            const double cycles=qMax(0.0, local/duration);
            if (mode==QLatin1String("count") && cycles>=qMax(1,p.loopCount)) progress=1.0;
            else {
                const double frac=cycles-std::floor(cycles);
                if (mode==QLatin1String("ping-pong")) {
                    const int cycle=int(std::floor(cycles));
                    progress=(cycle%2)==0 ? frac : 1.0-frac;
                } else progress=frac;
            }
        }
    }
    const double e=ease(p.easing,progress);
    out.opacity=lerp(p.opacityFrom,p.opacityTo,e);
    if (p.fadeOnReveal && notBeforeSec >= 0.0) {
        const double fadeSec = qMax(0.001, p.fadeRevealMs / 1000.0);
        const double revealAge = qMax(0.0, timeSec - notBeforeSec);
        out.opacity *= qBound(0.0, revealAge / fadeSec, 1.0);
    }
    out.offset=QPointF(lerp(p.translateXFrom,p.translateXTo,e),lerp(p.translateYFrom,p.translateYTo,e));
    out.scaleX=lerp(p.scaleXFrom,p.scaleXTo,e);
    out.scaleY=lerp(p.scaleYFrom,p.scaleYTo,e);
    out.rotation=lerp(p.rotationFrom,p.rotationTo,e);

    const double t=qMax(0.0,local);
    const double phase=t*p.frequency*2.0*kPi + idx*.43;
    const QString motion=p.motion.trimmed().toLower();
    if (motion==QLatin1String("wave")) out.offset.ry() += std::sin(phase)*p.amount;
    else if (motion==QLatin1String("float")) out.offset.ry() += std::sin(phase)*p.amount;
    else if (motion==QLatin1String("jitter")) {
        out.offset.rx() += std::sin(phase*1.73+idx*1.11)*p.amount;
        out.offset.ry() += std::cos(phase*2.17+idx*.71)*p.amount;
    } else if (motion==QLatin1String("shake")) {
        const double n1=std::sin(t*p.frequency*37.0+idx*12.9898)*43758.5453;
        const double n2=std::sin(t*p.frequency*41.0+idx*78.2330)*12345.6789;
        out.offset.rx() += (n1-std::floor(n1)-.5)*2.0*p.amount;
        out.offset.ry() += (n2-std::floor(n2)-.5)*2.0*p.amount;
    } else if (motion==QLatin1String("pulse")) {
        const double s=1.0+std::sin(phase)*p.amount;
        out.scaleX*=s; out.scaleY*=s;
    } else if (motion==QLatin1String("rainbow")) {
        const double h=std::fmod((t*p.frequency*360.0)+idx*8.0,360.0);
        out.colorOverride=QColor::fromHsv(int(h),220,255);
    } else if (motion==QLatin1String("glow")) {
        out.brighten=qBound(0.0,(.5+.5*std::sin(phase))*p.amount,1.0);
    } else if (motion==QLatin1String("sweep")) {
        const int count=targetCount(p,chars,words);
        const double pos=std::fmod(t*qMax(.01,p.frequency)*(count+6.0),count+6.0)-3.0;
        const double d=std::fabs(idx-pos);
        out.brighten=d<3.0 ? qBound(0.0,(1.0-d/3.0)*p.amount,1.0) : 0.0;
    }
    return out;
}

TextEffectSample combine(const TextEffectSample&a,const TextEffectSample&b)
{
    TextEffectSample r;
    r.offset=a.offset+b.offset;
    r.scaleX=a.scaleX*b.scaleX;r.scaleY=a.scaleY*b.scaleY;
    r.rotation=a.rotation+b.rotation;
    r.opacity=a.opacity*b.opacity;
    r.colorOverride=b.colorOverride.isValid()?b.colorOverride:a.colorOverride;
    r.brighten=qBound(0.0,a.brighten+b.brighten,1.0);
    return r;
}
}

double textEffectPhaseSpanSec(const core::TextEffectPhaseSpec& p,int characterCount,int wordCount)
{
    if (!p.enabled) return 0.0;
    const int count=targetCount(p,characterCount,wordCount);
    int cycles = 1;
    const QString mode = p.loopMode.trimmed().toLower();
    if (mode == QLatin1String("count")) cycles = qMax(1, p.loopCount);
    else if (mode == QLatin1String("ping-pong")) cycles = 2;
    // while-visible não tem fim natural. Para Entrada/Saída, o consumidor
    // precisa de um gate finito; uma duração completa é o contrato seguro.
    const qint64 totalMs = qint64(p.delayMs) + qint64(p.staggerMs) * qMax(0,count-1)
                         + qint64(p.durationMs) * cycles;
    return qMax<qint64>(0,totalMs)/1000.0;
}

double textEffectTypewriterCharsPerSecond(const core::TextEffectPhaseSpec& p)
{
    if (!p.enabled || p.motion != QLatin1String("typewriter")) return 0.0;
    if (p.amount > 0.0) return p.amount;
    if (p.staggerMs > 0) return 1000.0/p.staggerMs;
    return 45.0;
}

bool textEffectTargetRevealed(const core::TextEffectPhaseSpec& p,double visibleTimeSec,
                              int ch,int word,int chars,int words)
{
    Q_UNUSED(chars);
    Q_UNUSED(words);
    const double cps=textEffectTypewriterCharsPerSecond(p);
    if (cps<=0.0) return true;
    const int idx=targetIndex(p,ch,word);
    const double bySpeed=idx/cps;
    const double byStagger=(p.staggerMs>0 ? p.staggerMs*idx/1000.0 : 0.0);
    const double revealAt=p.delayMs/1000.0 + qMax(bySpeed,byStagger);
    return visibleTimeSec + 1e-9 >= revealAt;
}

TextEffectSample evaluateTextEffects(const core::TextEffectStack& stack,double visible,double exitTime,
                                     int ch,int word,int chars,int words,
                                     double entranceNotBeforeSec,double loopStartOverrideSec)
{
    if (exitTime>=0.0 && stack.exit.enabled)
        return samplePhase(stack.exit,exitTime,ch,word,chars,words);

    TextEffectSample base;
    double defaultEntranceSpan = 0.0;
    if (stack.entrance.enabled) {
        defaultEntranceSpan=textEffectPhaseSpanSec(stack.entrance,chars,words);
        const double targetEnd=phaseTargetEndSec(stack.entrance,ch,word,entranceNotBeforeSec);
        if (visible<targetEnd)
            return samplePhase(stack.entrance,visible,ch,word,chars,words,entranceNotBeforeSec);
        // Hold the actual final state for this target. When a Message reveals a
        // glyph later than the preset's nominal stagger, its Entrance begins at
        // reveal time instead of expiring while the glyph is still invisible.
        base=samplePhase(stack.entrance,qMax(0.0,targetEnd-1e-9),ch,word,chars,words,entranceNotBeforeSec);
    }
    if (stack.loop.enabled) {
        double start=stack.entrance.enabled?defaultEntranceSpan:0.0;
        if (loopStartOverrideSec>=0.0) start=qMax(start,loopStartOverrideSec);
        if (visible < start) return base;
        return combine(base,samplePhase(stack.loop,qMax(0.0,visible-start),ch,word,chars,words));
    }
    return base;
}

} // namespace game
