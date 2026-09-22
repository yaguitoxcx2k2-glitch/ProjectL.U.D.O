// ============================================================================
//  Pictures.cpp — Tweens e física das pictures.
// ============================================================================
#include "Pictures.h"
#include "PictureEffectRuntime.h"
#include "core/PictureState.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace core;

namespace game {

QVariantMap PictureKeyframe::toVariantMap() const
{
    QVariantMap out{{"time",time}};
    if(properties.contains("position")){out["x"]=positionX;out["y"]=positionY;}
    if(properties.contains("opacity"))out["opacity"]=opacity;
    if(properties.contains("scaleX"))out["scaleX"]=scaleX;
    if(properties.contains("scaleY"))out["scaleY"]=scaleY;
    if(properties.contains("rotation"))out["rotation"]=rotation;
    if(properties.contains("tint")&&tint.isValid())out["tint"]=tint.name(QColor::HexArgb);
    return out;
}
PictureKeyframe PictureKeyframe::fromVariantMap(const QVariantMap& v)
{
    PictureKeyframe k;k.time=qMax(0.0,v.value("time").toDouble());
    if(v.contains("position")){const QVariantMap p=v.value("position").toMap();k.positionX=p.value("x").toDouble();k.positionY=p.value("y").toDouble();k.properties.insert("position");}
    else if(v.contains("x")||v.contains("y")){k.positionX=v.value("x").toDouble();k.positionY=v.value("y").toDouble();k.properties.insert("position");}
    for(const auto& key:{QStringLiteral("opacity"),QStringLiteral("scaleX"),QStringLiteral("scaleY"),QStringLiteral("rotation")})if(v.contains(key))k.properties.insert(key);
    k.opacity=qBound(0.0,v.value("opacity",255.0).toDouble(),255.0);k.scaleX=v.value("scaleX",100.0).toDouble();k.scaleY=v.value("scaleY",100.0).toDouble();k.rotation=v.value("rotation").toDouble();
    if(v.contains("tint")){k.tint=QColor(v.value("tint").toString());if(k.tint.isValid())k.properties.insert("tint");}return k;
}
QVariantMap PictureTimeline::toVariantMap() const
{ QVariantList frames;for(const auto& k:keyframes)frames.push_back(k.toVariantMap());return {{"name",name},{"duration",duration},{"loop",loop},{"keyframes",frames}}; }
PictureTimeline PictureTimeline::fromVariantMap(const QVariantMap& v)
{ PictureTimeline t;t.name=v.value("name").toString().trimmed().left(128);t.duration=qMax(0.0,v.value("duration").toDouble());t.loop=v.value("loop").toBool();for(const auto& f:v.value("keyframes").toList())t.keyframes.push_back(PictureKeyframe::fromVariantMap(f.toMap()));std::sort(t.keyframes.begin(),t.keyframes.end(),[](const auto&a,const auto&b){return a.time<b.time;});return t; }

namespace {
QString logicalKey(const QString& name) { return name.trimmed().toCaseFolded(); }
}

QVariantMap PictureAttachment::toVariantMap() const
{
    return {{QStringLiteral("parentId"), parentId}, {QStringLiteral("target"), target},
            {QStringLiteral("followAxis"), followAxis}, {QStringLiteral("offsetX"), offset.x()},
            {QStringLiteral("offsetY"), offset.y()}, {QStringLiteral("inheritRotation"), inheritRotation},
            {QStringLiteral("inheritScale"), inheritScale}, {QStringLiteral("inheritOpacity"), inheritOpacity}};
}

PictureAttachment PictureAttachment::fromVariantMap(const QVariantMap& value)
{
    PictureAttachment attachment;
    attachment.parentId = qMax(0, value.value(QStringLiteral("parentId")).toInt());
    attachment.target = value.value(QStringLiteral("target")).toString().trimmed();
    attachment.followAxis = value.value(QStringLiteral("followAxis"), QStringLiteral("both")).toString().toLower();
    if (attachment.followAxis != QLatin1String("x") && attachment.followAxis != QLatin1String("y"))
        attachment.followAxis = QStringLiteral("both");
    attachment.offset = QPointF(value.value(QStringLiteral("offsetX")).toDouble(),
                                value.value(QStringLiteral("offsetY")).toDouble());
    attachment.inheritRotation = value.value(QStringLiteral("inheritRotation")).toBool();
    attachment.inheritScale = value.value(QStringLiteral("inheritScale")).toBool();
    attachment.inheritOpacity = value.value(QStringLiteral("inheritOpacity")).toBool();
    return attachment;
}

// ============================================================================
//  Imagem em Sequência — estado runtime independente
// ============================================================================
int PictureFramePlayback::capacity() const
{
    const qint64 cap = qint64(qMax(1, columns)) * qint64(qMax(1, rows));
    return int(qMin<qint64>(cap, std::numeric_limits<int>::max()));
}

void PictureFramePlayback::reset(const FrameSequence& sequence)
{
    initialized = true;
    enabled = sequence.enabled;
    columns = qMax(1, sequence.columns);
    rows = qMax(1, sequence.rows);
    requestedCount = qMax(1, sequence.count);
    count = qBound(1, requestedCount, capacity());
    startFrame = qBound(0, sequence.firstFrame, count - 1);
    currentFrame = startFrame;
    fps = std::isfinite(sequence.fps) ? qMax(0.0, sequence.fps) : 0.0;
    accumulated = 0.0;
    loop = sequence.loop;
    paused = false;

    // "Reproduzir animacao" desligado e um quadro fixo de configuracao,
    // nao uma pausa temporaria. A pausa runtime preserva o quadro atual e o
    // acumulador separadamente para poder continuar sem salto.
    const bool configuredToPlay = enabled && sequence.playing && count > 1 && fps > 0.0;
    fixedFrame = configuredToPlay ? -1 : startFrame;
    playing = configuredToPlay;
}

void PictureFramePlayback::restoreLegacy(const FrameSequence& sequence, double ageSeconds)
{
    reset(sequence);
    if (std::isfinite(ageSeconds) && ageSeconds > 0.0) update(ageSeconds);
}

bool PictureFramePlayback::dynamic() const
{
    return initialized && enabled && playing && !paused && fixedFrame < 0 && fps > 0.0 && count > 1;
}

void PictureFramePlayback::pause()
{
    if (!initialized || !enabled || !playing || fixedFrame >= 0 || count <= 1) return;
    paused = true;
    playing = false;
}

void PictureFramePlayback::resume()
{
    if (!initialized || !enabled || !paused || fixedFrame >= 0 || count <= 1 || fps <= 0.0) return;
    paused = false;
    playing = true;
}

void PictureFramePlayback::setFixedFrame(int frame)
{
    if (!initialized) return;
    fixedFrame = qBound(0, frame, qMax(0, count - 1));
    currentFrame = fixedFrame;
    accumulated = 0.0;
    paused = false;
    playing = false;
}

QString PictureFramePlayback::modeId() const
{
    if (fixedFrame >= 0) return QStringLiteral("fixed");
    if (paused) return QStringLiteral("paused");
    if (!loop && !playing && currentFrame >= qMax(0, count - 1)) return QStringLiteral("finished");
    return playing ? QStringLiteral("playing") : QStringLiteral("stopped");
}

void PictureFramePlayback::update(double dt)
{
    if (fixedFrame >= 0) {
        currentFrame = qBound(0, fixedFrame, qMax(0, count - 1));
        return;
    }
    if (!dynamic() || !std::isfinite(dt) || dt <= 0.0) return;

    const double frameSeconds = 1.0 / fps;
    accumulated += dt;
    if (!std::isfinite(accumulated)) {
        accumulated = 0.0;
        return;
    }

    const qint64 steps = qint64(std::floor(accumulated / frameSeconds + 1e-12));
    if (steps <= 0) return;

    if (loop) {
        currentFrame = int((qint64(currentFrame) + (steps % count)) % count);
        accumulated = std::fmod(accumulated, frameSeconds);
        if (accumulated < 0.0) accumulated = 0.0;
        return;
    }

    const qint64 remaining = qMax<qint64>(0, qint64(count - 1 - currentFrame));
    if (steps >= remaining) {
        currentFrame = count - 1;
        accumulated = 0.0;
        playing = false;
        paused = false;
    } else {
        currentFrame += int(steps);
        accumulated -= double(steps) * frameSeconds;
        if (accumulated < 0.0) accumulated = 0.0;
    }
}

FrameSequenceValidation PictureFramePlayback::validationForImage(const QSize& imageSize) const
{
    FrameSequence sequence;
    sequence.enabled = enabled || requestedCount > 1;
    sequence.count = qMax(1, requestedCount);
    sequence.columns = qMax(1, columns);
    sequence.rows = qMax(1, rows);
    sequence.firstFrame = qBound(0, startFrame, qMax(0, sequence.count - 1));
    sequence.fps = fps;
    sequence.loop = loop;
    sequence.playing = playing;
    return sequence.validate(imageSize);
}

bool PictureFramePlayback::validForImage(const QSize& imageSize) const
{
    if (imageSize.isEmpty()) return false;
    if (!usesFrames()) return true;
    if (columns <= 0 || rows <= 0 || count <= 0) return false;
    if (count > capacity()) return false;

    const FrameSequenceValidation validation = validationForImage(imageSize);
    // CountExceedsGrid e normalizado de forma deterministica para manter
    // compatibilidade com projetos RC2.12+. Os demais problemas tornam o
    // recorte ambiguo e usam a imagem inteira como fallback seguro.
    if (validation.issue != FrameSequenceIssue::None &&
        validation.issue != FrameSequenceIssue::CountExceedsGrid)
        return false;
    return currentFrame >= 0 && currentFrame < count;
}

QRect PictureFramePlayback::rectAt(const QSize& imageSize) const
{
    if (imageSize.isEmpty()) return QRect();
    if (!usesFrames()) return QRect(QPoint(0, 0), imageSize);
    if (!validForImage(imageSize)) return QRect();

    const int fw = imageSize.width() / qMax(1, columns);
    const int fh = imageSize.height() / qMax(1, rows);
    if (fw <= 0 || fh <= 0) return QRect();
    const int frame = qBound(0, currentFrame, count - 1);
    const int col = frame % columns;
    const int row = frame / columns;
    const QRect rect(col * fw, row * fh, fw, fh);
    return rect.intersected(QRect(QPoint(0, 0), imageSize));
}

QJsonObject PictureFramePlayback::toJson() const
{
    return QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("startFrame"), startFrame},
        {QStringLiteral("currentFrame"), currentFrame},
        {QStringLiteral("requestedCount"), requestedCount},
        {QStringLiteral("count"), count},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("rows"), rows},
        {QStringLiteral("fps"), fps},
        {QStringLiteral("accumulated"), accumulated},
        {QStringLiteral("loop"), loop},
        {QStringLiteral("playing"), playing},
        {QStringLiteral("paused"), paused},
        {QStringLiteral("fixedFrame"), fixedFrame}
    };
}

bool PictureFramePlayback::fromJson(const QJsonObject& object, const FrameSequence& fallback)
{
    if (object.isEmpty()) {
        reset(fallback);
        return false;
    }

    initialized = true;
    const int payloadVersion = object.value(QStringLiteral("version")).toInt(1);
    enabled = object.value(QStringLiteral("enabled")).toBool(fallback.enabled);
    columns = qMax(1, object.value(QStringLiteral("columns")).toInt(qMax(1, fallback.columns)));
    rows = qMax(1, object.value(QStringLiteral("rows")).toInt(qMax(1, fallback.rows)));
    requestedCount = payloadVersion >= 2
        ? qMax(1, object.value(QStringLiteral("requestedCount")).toInt(qMax(1, fallback.count)))
        : qMax(1, fallback.count);
    count = qBound(1, object.value(QStringLiteral("count")).toInt(qMin(requestedCount, capacity())), capacity());
    startFrame = qBound(0, object.value(QStringLiteral("startFrame")).toInt(fallback.firstFrame), count - 1);
    currentFrame = qBound(0, object.value(QStringLiteral("currentFrame")).toInt(startFrame), count - 1);
    const double savedFps = object.value(QStringLiteral("fps")).toDouble(fallback.fps);
    fps = std::isfinite(savedFps) ? qMax(0.0, savedFps) : 0.0;
    const double savedAccumulated = object.value(QStringLiteral("accumulated")).toDouble(0.0);
    accumulated = std::isfinite(savedAccumulated) ? qMax(0.0, savedAccumulated) : 0.0;
    loop = object.value(QStringLiteral("loop")).toBool(fallback.loop);
    playing = enabled && object.value(QStringLiteral("playing")).toBool(fallback.playing);

    if (payloadVersion >= 2) {
        paused = object.value(QStringLiteral("paused")).toBool(false);
        const int savedFixed = object.value(QStringLiteral("fixedFrame")).toInt(-1);
        fixedFrame = savedFixed >= 0 ? qBound(0, savedFixed, count - 1) : -1;
    } else {
        // RC2.12..RC2.17 nao diferenciavam "pausado" de "quadro fixo".
        // Se o projeto ja dizia framePlaying=false, o estado antigo era um
        // quadro fixo; caso contrario playing=false pode significar apenas
        // que uma sequencia sem loop chegou ao fim.
        paused = false;
        fixedFrame = (!fallback.playing && !playing) ? currentFrame : -1;
    }

    if (fixedFrame >= 0) {
        currentFrame = fixedFrame;
        playing = false;
        paused = false;
        accumulated = 0.0;
    } else if (paused) {
        playing = false;
    } else if (fps <= 0.0 && playing) {
        fixedFrame = currentFrame;
        playing = false;
    }

    if (fps > 0.0) accumulated = std::fmod(accumulated, 1.0 / fps);
    else accumulated = 0.0;
    return true;
}

// ============================================================================
//  Valores efetivos
// ============================================================================
// RC2.56: Float/Breathing/Sway/Spin são avaliados por uma única fundação.
// Preview, CPU e QRhi recebem os mesmos valores; PictureDef mantém os campos
// planos para compatibilidade com projetos anteriores.
double LivePicture::effectiveX() const
{
    const double base = attachedTransform.valid ? attachedTransform.x : def.x;
    return base + evaluatePictureMotion(def, age).offset.x();
}

double LivePicture::effectiveY() const
{
    const double base = attachedTransform.valid ? attachedTransform.y : def.y;
    return base + evaluatePictureMotion(def, age).offset.y();
}

double LivePicture::effectiveScaleX() const
{
    const double own = def.scaleX + evaluatePictureMotion(def, age).scaleAddX;
    return attachedTransform.valid && attachment.inheritScale ? own * attachedTransform.scaleX / 100.0 : own;
}

double LivePicture::effectiveScaleY() const
{
    const double own = def.scaleY + evaluatePictureMotion(def, age).scaleAddY;
    return attachedTransform.valid && attachment.inheritScale ? own * attachedTransform.scaleY / 100.0 : own;
}

double LivePicture::effectiveAngle() const
{
    const double own = def.angle + evaluatePictureMotion(def, age).angleAdd;
    return attachedTransform.valid && attachment.inheritRotation ? own + attachedTransform.angle : own;
}

double LivePicture::effectiveOpacity() const
{
    const double own = qBound(0.0, def.opacity, 255.0);
    return attachedTransform.valid && attachment.inheritOpacity
        ? qBound(0.0, own * attachedTransform.opacity / 255.0, 255.0) : own;
}

int LivePicture::effectiveFrameIndex() const
{
    if (framePlayback.initialized) return framePlayback.currentFrame;
    PictureFramePlayback legacy;
    legacy.restoreLegacy(def.frameSequence(), age);
    return legacy.currentFrame;
}

QRect LivePicture::frameRect(const QSize& imageSize) const
{
    if (framePlayback.initialized) return framePlayback.rectAt(imageSize);
    PictureFramePlayback legacy;
    legacy.restoreLegacy(def.frameSequence(), age);
    return legacy.rectAt(imageSize);
}

bool LivePicture::frameLayoutValid(const QSize& imageSize) const
{
    if (framePlayback.initialized) return framePlayback.validForImage(imageSize);
    PictureFramePlayback legacy;
    legacy.restoreLegacy(def.frameSequence(), age);
    return legacy.validForImage(imageSize);
}

FrameSequenceValidation LivePicture::frameValidation(const QSize& imageSize) const
{
    if (framePlayback.initialized) return framePlayback.validationForImage(imageSize);
    PictureFramePlayback legacy;
    legacy.restoreLegacy(def.frameSequence(), age);
    return legacy.validationForImage(imageSize);
}

bool LivePicture::frameConfigurationValid(const QSize& imageSize) const
{
    return frameValidation(imageSize).valid();
}

bool LivePicture::frameDynamic() const
{
    if (framePlayback.initialized) return framePlayback.dynamic();
    return def.frameSequence().dynamic();
}

// ============================================================================
//  Gerência dos slots
// ============================================================================
double LivePicture::effectiveNegativeStrength() const
{
    // RC2.68.2: `strength` guarda o valor configurável mesmo quando o efeito
    // está desligado (o default é 1.0). Sem respeitar `enabled`, toda Picture
    // sem Negative explícito era tratada como inversão 100% — inclusive
    // Picture Text. Durante um tween usamos os endpoints do LivePicture; fora
    // dele, efeito desligado significa intensidade efetiva ZERO.
    if (negativeDuration <= 0.0)
        return def.fx.negative.enabled ? qBound(0.0, def.fx.negative.strength, 1.0) : 0.0;
    const double t = qBound(0.0, negativeElapsed / negativeDuration, 1.0);
    return qBound(0.0, negativeFrom + (negativeTo - negativeFrom) * t, 1.0);
}

void PictureManager::show(const PictureDef& def)
{
    LivePicture lp;
    lp.def = def;
    lp.def.number = qMax(1, def.number);
    lp.age = 0.0;
    // Mostrar Imagem é o ÚNICO ponto normal de inicialização da sequência.
    // Comandos posteriores de movimento/efeito/flip não tocam neste estado.
    lp.framePlayback.reset(lp.def.frameSequence());
    // Transição de entrada: a imagem nasce no meio dela, não pronta.
    if (def.fx.transitionIn != PictureTransition::None) {
        lp.phase = LivePicture::In;
        lp.phaseType = def.fx.transitionIn;
        lp.phaseTime = 0.0;
        lp.phaseDuration = qMax(1, def.fx.transitionInFrames) / 60.0;
    }
    // Negative também pode entrar gradualmente já no Mostrar Imagem.
    // O alvo continua armazenado em def.fx; LivePicture guarda somente o tween.
    if (def.fx.negative.enabled && def.fx.negative.transitionFrames > 0 && def.fx.negative.strength > 0.0) {
        lp.negativeFrom = 0.0;
        lp.negativeTo = qBound(0.0, def.fx.negative.strength, 1.0);
        lp.negativeElapsed = 0.0;
        lp.negativeDuration = def.fx.negative.transitionFrames / 60.0;
    }
    if (const LivePicture* previous = at(lp.def.number)) removeIndexesFor(*previous);
    m_pics[lp.def.number] = lp;
}

int PictureManager::showByName(const QString& logicalName, const PictureDef& definition)
{
    PictureDef def = definition;
    int slot = findByName(logicalName);
    const bool replacingNamedPicture = slot > 0;
    if (!replacingNamedPicture) slot = def.number;
    if (slot <= 0) {
        slot = 1;
        while (m_pics.contains(slot) && slot < 9999) ++slot;
    }
    def.number = qMax(1, slot);
    show(def);
    setLogicalName(def.number, logicalName);
    return def.number;
}

int PictureManager::findByName(const QString& logicalName) const
{
    return m_logicalNameMap.value(logicalKey(logicalName), 0);
}

void PictureManager::removeIndexesFor(const LivePicture& picture)
{
    const QString key = logicalKey(picture.logicalName);
    if (!key.isEmpty() && m_logicalNameMap.value(key) == picture.def.number) m_logicalNameMap.remove(key);
}

void PictureManager::setLogicalName(int number, const QString& logicalName)
{
    LivePicture* picture = ensure(number);
    if (!picture) return;
    removeIndexesFor(*picture);
    const QString name = logicalName.trimmed();
    const QString key = logicalKey(name);
    if (!key.isEmpty()) {
        const int previous = m_logicalNameMap.value(key, 0);
        if (previous > 0 && previous != number) if (LivePicture* other = ensure(previous)) other->logicalName.clear();
        m_logicalNameMap.insert(key, number);
    }
    picture->logicalName = name;
}

void PictureManager::setGroup(int number, const QString& group)
{
    if (LivePicture* picture = ensure(number)) picture->pictureGroup = group.trimmed();
}

QVector<int> PictureManager::groupMembers(const QString& group) const
{
    QVector<int> members; const QString key = group.trimmed();
    for (auto it=m_pics.cbegin(); it!=m_pics.cend(); ++it) if (it.value().pictureGroup == key) members.push_back(it.key());
    return members;
}

void PictureManager::eraseGroup(const QString& group)
{
    const QVector<int> members = groupMembers(group);
    for (int number : members) erase(number);
}

void PictureManager::moveGroup(const QString& group, const QMap<PictureProp, double>& targets,
                               double duration, PictureEase ease)
{
    for (int number : groupMembers(group)) moveTo(number, targets, duration, ease);
}

void PictureManager::bindDynamicValue(int number, PictureProp prop, const QVariantMap& sourceSpec)
{
    LivePicture* picture = ensure(number); if (!picture) return;
    for (int i=picture->tweens.size()-1;i>=0;--i) if (picture->tweens.at(i).prop==prop) picture->tweens.remove(i);
    if (sourceSpec.isEmpty()) picture->dynamicBindings.remove(prop); else picture->dynamicBindings.insert(prop, sourceSpec);
}

void PictureManager::clearDynamicValue(int number, PictureProp prop)
{
    if (LivePicture* picture=ensure(number)) picture->dynamicBindings.remove(prop);
}

void PictureManager::attach(int number, const PictureAttachment& attachment)
{
    LivePicture* picture=ensure(number); if(!picture)return;
    picture->attachment=attachment; picture->attachedTransform={};
}

void PictureManager::detach(int number)
{
    if(LivePicture* picture=ensure(number)){picture->attachment={};picture->attachedTransform={};}
}

bool PictureManager::defineTimeline(const PictureTimeline& value)
{
    PictureTimeline normalized=value;normalized.name=normalized.name.trimmed().left(128);
    if(!normalized.valid())return false;
    std::sort(normalized.keyframes.begin(),normalized.keyframes.end(),[](const auto&a,const auto&b){return a.time<b.time;});
    for(auto& keyframe:normalized.keyframes)keyframe.time=qBound(0.0,keyframe.time,normalized.duration);
    m_timelines.insert(normalized.name.toCaseFolded(),normalized);return true;
}

const PictureTimeline* PictureManager::timeline(const QString& name) const
{ auto it=m_timelines.constFind(name.trimmed().toCaseFolded());return it==m_timelines.cend()?nullptr:&it.value(); }

bool PictureManager::playTimeline(int number,const QString& name)
{
    LivePicture* picture=ensure(number);const PictureTimeline* definition=timeline(name);
    if(!picture||!definition)return false;
    picture->timeline={definition->name,0.0,true};applyTimeline(*picture,0.0);return true;
}
void PictureManager::stopTimeline(int number){if(LivePicture* p=ensure(number))p->timeline.playing=false;}
void PictureManager::setOnTouch(int number,const QString& id){if(LivePicture* p=ensure(number))p->onTouchCommonEventId=id.trimmed();}
void PictureManager::setOnClick(int number,const QString& id){if(LivePicture* p=ensure(number))p->onClickCommonEventId=id.trimmed();}
QString PictureManager::interactionCommonEvent(int number,bool click) const{const LivePicture*p=at(number);return p?(click?p->onClickCommonEventId:p->onTouchCommonEventId):QString();}

void PictureManager::applyTimeline(LivePicture& picture,double dt)
{
    if(!picture.timeline.playing)return;const PictureTimeline* definition=timeline(picture.timeline.name);
    if(!definition){picture.timeline.playing=false;return;}picture.timeline.elapsed+=qMax(0.0,dt);
    if(definition->loop)picture.timeline.elapsed=std::fmod(picture.timeline.elapsed,definition->duration);
    else if(picture.timeline.elapsed>=definition->duration){picture.timeline.elapsed=definition->duration;picture.timeline.playing=false;}
    const double now=picture.timeline.elapsed;
    auto sample=[&](const QString& property,double fallback,auto getter){const PictureKeyframe*a=nullptr,*b=nullptr;for(const auto&f:definition->keyframes)if(f.properties.contains(property)){if(f.time<=now)a=&f;if(f.time>=now){b=&f;break;}}if(!a)a=b;if(!b)b=a;if(!a)return fallback;const double av=getter(*a);if(a==b||b->time<=a->time)return av;const double t=qBound(0.0,(now-a->time)/(b->time-a->time),1.0);return av+(getter(*b)-av)*t;};
    picture.def.x=sample("position",picture.def.x,[](const auto&k){return k.positionX;});picture.def.y=sample("position",picture.def.y,[](const auto&k){return k.positionY;});
    picture.def.opacity=qBound(0.0,sample("opacity",picture.def.opacity,[](const auto&k){return k.opacity;}),255.0);picture.def.scaleX=sample("scaleX",picture.def.scaleX,[](const auto&k){return k.scaleX;});picture.def.scaleY=sample("scaleY",picture.def.scaleY,[](const auto&k){return k.scaleY;});picture.def.angle=sample("rotation",picture.def.angle,[](const auto&k){return k.rotation;});
    const PictureKeyframe* a=nullptr,*b=nullptr;for(const auto&f:definition->keyframes)if(f.properties.contains("tint")){if(f.time<=now)a=&f;if(f.time>=now){b=&f;break;}}if(!a)a=b;if(!b)b=a;if(a&&a->tint.isValid()){QColor c=a->tint;if(b&&b!=a&&b->time>a->time){const double t=qBound(0.0,(now-a->time)/(b->time-a->time),1.0);c.setRedF(c.redF()+(b->tint.redF()-c.redF())*t);c.setGreenF(c.greenF()+(b->tint.greenF()-c.greenF())*t);c.setBlueF(c.blueF()+(b->tint.blueF()-c.blueF())*t);c.setAlphaF(c.alphaF()+(b->tint.alphaF()-c.alphaF())*t);}picture.def.fx.tint.enabled=true;picture.def.fx.tint.color=c;picture.def.fx.tint.strength=1.0;}
}

void PictureManager::setEffects(int number, const PictureEffects& fx)
{
    if (LivePicture* lp = ensure(number)) {
        const double currentNegative = lp->effectiveNegativeStrength();
        lp->def.fx = fx;
        const double targetNegative = fx.negative.enabled ? qBound(0.0, fx.negative.strength, 1.0) : 0.0;
        if (fx.negative.transitionFrames > 0 && !qFuzzyCompare(currentNegative + 1.0, targetNegative + 1.0)) {
            lp->negativeFrom = currentNegative;
            lp->negativeTo = targetNegative;
            lp->negativeElapsed = 0.0;
            lp->negativeDuration = fx.negative.transitionFrames / 60.0;
        } else {
            lp->negativeFrom = lp->negativeTo = targetNegative;
            lp->negativeElapsed = lp->negativeDuration = 0.0;
        }
    }
}

void PictureManager::clearEffects(int number)
{
    if (LivePicture* lp = ensure(number)) {
        // Transições NÃO são efeito de aparência: apagar a de saída aqui
        // faria a imagem sumir sem aviso no meio de uma cutscene.
        const PictureTransition entrada = lp->def.fx.transitionIn;
        const PictureTransition saida = lp->def.fx.transitionOut;
        const int fe = lp->def.fx.transitionInFrames, fs = lp->def.fx.transitionOutFrames;
        lp->def.fx = PictureEffects();
        lp->def.fx.transitionIn = entrada;
        lp->def.fx.transitionOut = saida;
        lp->def.fx.transitionInFrames = fe;
        lp->def.fx.transitionOutFrames = fs;
    }
}

void PictureManager::startTransitionOut(int number, PictureTransition tipo, int frames)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;
    if (tipo == PictureTransition::None) tipo = lp->def.fx.transitionOut;
    if (tipo == PictureTransition::None) { erase(number); return; }
    lp->phase = LivePicture::Out;
    lp->phaseType = tipo;
    lp->phaseTime = 0.0;
    lp->phaseDuration = qMax(1, frames > 0 ? frames : lp->def.fx.transitionOutFrames) / 60.0;
}

LivePicture* PictureManager::ensure(int number)
{
    auto it = m_pics.find(number);
    return it == m_pics.end() ? nullptr : &it.value();
}

LivePicture* PictureManager::at(int number)
{
    return ensure(number);
}

const LivePicture* PictureManager::at(int number) const
{
    auto it = m_pics.constFind(number);
    return it == m_pics.constEnd() ? nullptr : &it.value();
}

double PictureManager::readProp(const PictureDef& d, PictureProp p)
{
    switch (p) {
    case PictureProp::X:       return d.x;
    case PictureProp::Y:       return d.y;
    case PictureProp::ScaleX:  return d.scaleX;
    case PictureProp::ScaleY:  return d.scaleY;
    case PictureProp::Opacity: return d.opacity;
    case PictureProp::Angle:   return d.angle;
    }
    return 0.0;
}

void PictureManager::applyProp(PictureDef& d, PictureProp p, double v)
{
    switch (p) {
    case PictureProp::X:       d.x = v; break;
    case PictureProp::Y:       d.y = v; break;
    case PictureProp::ScaleX:  d.scaleX = v; break;
    case PictureProp::ScaleY:  d.scaleY = v; break;
    case PictureProp::Opacity: d.opacity = qBound(0.0, v, 255.0); break;
    case PictureProp::Angle:   d.angle = v; break;
    }
}

void PictureManager::tween(int number, PictureProp prop, double target,
                           double duration, PictureEase ease)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;                       // slot vazio: comando ignorado

    // Um tween novo na MESMA propriedade substitui o antigo — senão dois
    // comandos seguidos brigariam pelo valor e a imagem tremeria.
    for (int i = lp->tweens.size() - 1; i >= 0; --i)
        if (lp->tweens[i].prop == prop) lp->tweens.remove(i);

    if (duration <= 0.0) { applyProp(lp->def, prop, target); return; }

    PictureTween t;
    t.prop = prop;
    t.from = readProp(lp->def, prop);
    t.to = target;
    t.duration = duration;
    t.ease = ease;
    lp->tweens.push_back(t);
}

void PictureManager::moveTo(int number, const QMap<PictureProp, double>& alvos,
                            double duration, PictureEase ease)
{
    for (auto it = alvos.constBegin(); it != alvos.constEnd(); ++it)
        tween(number, it.key(), it.value(), duration, ease);
}

void PictureManager::setMotionEffects(int number, const PicturePhysicsState& state)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;
    lp->def.setPhysicsState(state);
    lp->age = 0.0;      // recomeça o relógio compartilhado sem alterar transforms base
}

void PictureManager::setPhysics(int number, double floatSpeed, double floatRange,
                                double swaySpeed, double swayRange,
                                double spinSpeed, double pulseSpeed, double pulseRange)
{
    PicturePhysicsState state;
    state.floatSpeed = floatSpeed;
    state.floatRange = floatRange;
    state.swaySpeed = swaySpeed;
    state.swayRange = swayRange;
    state.spinSpeed = spinSpeed;
    state.pulseSpeed = pulseSpeed;
    state.pulseRange = pulseRange;
    setMotionEffects(number, state);
}

void PictureManager::setAnchor(int number, PictureAnchor a, double cx, double cy)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;
    lp->def.anchor = a;
    lp->def.anchorX = cx;
    lp->def.anchorY = cy;
}

void PictureManager::setFlip(int number, bool flipH, bool flipV)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;
    lp->def.flipH = flipH;
    lp->def.flipV = flipV;
}

void PictureManager::pauseSequence(int number)
{
    if (LivePicture* lp = ensure(number)) lp->framePlayback.pause();
}

void PictureManager::resumeSequence(int number)
{
    if (LivePicture* lp = ensure(number)) lp->framePlayback.resume();
}

void PictureManager::setSequenceFixedFrame(int number, int frame)
{
    if (LivePicture* lp = ensure(number)) lp->framePlayback.setFixedFrame(frame);
}

void PictureManager::setDisplaySettings(int number, PictureSpace space, PictureLayer layer,
                                        bool duringBattle, bool eraseOnMapChange,
                                        bool affectedByTone)
{
    LivePicture* lp = ensure(number);
    if (!lp) return;
    lp->def.space = space;
    lp->def.layer = layer;
    lp->def.duringBattle = duringBattle;
    lp->def.eraseOnMapChange = eraseOnMapChange;
    lp->def.affectedByTone = affectedByTone;
}

void PictureManager::erase(int number)
{
    if (const LivePicture* picture=at(number)) removeIndexesFor(*picture);
    m_pics.remove(number);
}

void PictureManager::eraseOnMapChange()
{
    QVector<int> remove;
    for (auto it = m_pics.constBegin(); it != m_pics.constEnd(); ++it)
        if (it.value().def.eraseOnMapChange) remove.push_back(it.key());
    for (int key : remove) erase(key);
}

void PictureManager::clearAll()
{
    m_pics.clear();
    m_logicalNameMap.clear();
}

void PictureManager::finishAnimations()
{
    QVector<int> eraseSlots;
    for(auto it=m_pics.begin();it!=m_pics.end();++it){LivePicture&lp=it.value();for(const PictureTween&t:std::as_const(lp.tweens))applyProp(lp.def,t.prop,t.to);lp.tweens.clear();if(lp.timeline.playing){if(const PictureTimeline* definition=timeline(lp.timeline.name)){lp.timeline.elapsed=definition->duration;applyTimeline(lp,0.0);}lp.timeline.playing=false;}if(lp.negativeDuration>0.0){lp.def.fx.negative.strength=lp.negativeTo;lp.def.fx.negative.enabled=lp.negativeTo>0.0;lp.negativeFrom=lp.negativeTo;lp.negativeElapsed=lp.negativeDuration=0.0;}if(lp.phase==LivePicture::Out)eraseSlots.push_back(it.key());else lp.phase=LivePicture::Normal;lp.phaseTime=lp.phaseDuration=0.0;}
    for(int slot:eraseSlots)erase(slot);
}

void PictureManager::update(double dt)
{
    if (dt <= 0.0) return;
    QVector<int> apagar;
    for (auto it = m_pics.begin(); it != m_pics.end(); ++it) {
        LivePicture& lp = it.value();
        lp.age += dt;
        lp.framePlayback.update(dt);
        applyTimeline(lp,dt);
        if (lp.negativeDuration > 0.0) {
            lp.negativeElapsed += dt;
            if (lp.negativeElapsed >= lp.negativeDuration) {
                lp.def.fx.negative.strength = lp.negativeTo;
                lp.def.fx.negative.enabled = lp.negativeTo > 0.0;
                lp.negativeFrom = lp.negativeTo;
                lp.negativeElapsed = lp.negativeDuration = 0.0;
            }
        }
        if (lp.phase != LivePicture::Normal) {
            lp.phaseTime += dt;
            if (lp.phaseTime >= lp.phaseDuration) {
                if (lp.phase == LivePicture::Out) apagar.push_back(it.key());
                lp.phase = LivePicture::Normal;
                lp.phaseTime = 0.0;
                lp.phaseDuration = 0.0;
                lp.phaseType = PictureTransition::None;
            }
        }
        for (int i = lp.tweens.size() - 1; i >= 0; --i) {
            PictureTween& t = lp.tweens[i];
            t.elapsed += dt;
            if (t.finished()) {
                // Grava o destino EXATO: deixar o último quadro no valor
                // interpolado deixaria a imagem um pixel fora do lugar para
                // sempre (e o elástico, bem mais que um pixel).
                applyProp(lp.def, t.prop, t.to);
                lp.tweens.remove(i);
            } else {
                applyProp(lp.def, t.prop, t.value());
            }
        }
    }
    refreshBindings();
    // A imagem que terminou de sair some do jogo: o slot fica livre para outra.
    for (int n : apagar) erase(n);
}

void PictureManager::refreshBindings()
{
    if(m_dynamicValueResolver)for(auto it=m_pics.begin();it!=m_pics.end();++it){
        LivePicture& picture=it.value();
        for(auto binding=picture.dynamicBindings.cbegin();binding!=picture.dynamicBindings.cend();++binding){
            const QVariant value=m_dynamicValueResolver(binding.value());bool ok=false;const double number=value.toDouble(&ok);
            if(ok&&std::isfinite(number))applyProp(picture.def,binding.key(),number);
        }
    }
    for(auto it=m_pics.begin();it!=m_pics.end();++it)it.value().attachedTransform={};
    for(auto it=m_pics.cbegin();it!=m_pics.cend();++it){QSet<int> visiting;resolveAttachment(it.key(),visiting);}
}

bool PictureManager::resolveAttachment(int number, QSet<int>& visiting)
{
    LivePicture* picture=ensure(number);
    if(!picture||!picture->attachment.active())return true;
    if(visiting.contains(number))return false;
    visiting.insert(number);
    PictureTargetState target;
    if(picture->attachment.parentId>0){
        if(!resolveAttachment(picture->attachment.parentId,visiting)){visiting.remove(number);return false;}
        if(const LivePicture* parent=at(picture->attachment.parentId)){
            target.valid=true;target.x=parent->effectiveX();target.y=parent->effectiveY();
            target.angle=parent->effectiveAngle();target.scaleX=parent->effectiveScaleX();
            target.scaleY=parent->effectiveScaleY();target.opacity=parent->effectiveOpacity();
        }
    }else if(m_targetResolver)target=m_targetResolver(picture->attachment.target);
    visiting.remove(number);
    if(!target.valid)return false;
    PictureTargetState resolved=target;
    const QString axis=picture->attachment.followAxis;
    resolved.x=(axis==QLatin1String("y"))?picture->def.x:target.x+picture->attachment.offset.x();
    resolved.y=(axis==QLatin1String("x"))?picture->def.y:target.y+picture->attachment.offset.y();
    picture->attachedTransform=resolved;
    return true;
}

bool PictureManager::busy(int number) const
{
    const LivePicture* lp = at(number);
    return lp && lp->animating();
}

bool PictureManager::anyBusy() const
{
    for (auto it = m_pics.constBegin(); it != m_pics.constEnd(); ++it)
        if (it.value().animating()) return true;
    return false;
}

QVector<const LivePicture*> PictureManager::ordered() const
{
    QVector<const LivePicture*> v;
    v.reserve(int(m_pics.size()));
    for (auto it = m_pics.constBegin(); it != m_pics.constEnd(); ++it)
        v.push_back(&it.value());
    return v;
}

QJsonObject PictureManager::toJson() const
{
    QJsonArray pictures;
    for (auto it = m_pics.cbegin(); it != m_pics.cend(); ++it) {
        const LivePicture& live = it.value();
        QJsonArray tweens;
        for (const PictureTween& tween : live.tweens) {
            tweens.append(QJsonObject{
                {QStringLiteral("property"), picturePropId(tween.prop)},
                {QStringLiteral("from"), tween.from},
                {QStringLiteral("to"), tween.to},
                {QStringLiteral("duration"), tween.duration},
                {QStringLiteral("elapsed"), tween.elapsed},
                {QStringLiteral("ease"), pictureEaseId(tween.ease)}
            });
        }
        QJsonObject bindings;
        for(auto binding=live.dynamicBindings.cbegin();binding!=live.dynamicBindings.cend();++binding)
            bindings.insert(picturePropId(binding.key()),QJsonObject::fromVariantMap(binding.value()));
        pictures.append(QJsonObject{
            {QStringLiteral("definition"), QJsonObject::fromVariantMap(live.def.toParams())},
            {QStringLiteral("logicalName"), live.logicalName},
            {QStringLiteral("pictureGroup"), live.pictureGroup},
            {QStringLiteral("attachment"), QJsonObject::fromVariantMap(live.attachment.toVariantMap())},
            {QStringLiteral("dynamicBindings"), bindings},
            {QStringLiteral("timelineName"), live.timeline.name},
            {QStringLiteral("timelineElapsed"), live.timeline.elapsed},
            {QStringLiteral("timelinePlaying"), live.timeline.playing},
            {QStringLiteral("onTouchCommonEventId"), live.onTouchCommonEventId},
            {QStringLiteral("onClickCommonEventId"), live.onClickCommonEventId},
            {QStringLiteral("age"), live.age},
            {QStringLiteral("framePlayback"), live.framePlayback.toJson()},
            {QStringLiteral("phase"), int(live.phase)},
            {QStringLiteral("phaseType"), pictureTransitionId(live.phaseType)},
            {QStringLiteral("phaseTime"), live.phaseTime},
            {QStringLiteral("phaseDuration"), live.phaseDuration},
            {QStringLiteral("negativeFrom"), live.negativeFrom},
            {QStringLiteral("negativeTo"), live.negativeTo},
            {QStringLiteral("negativeElapsed"), live.negativeElapsed},
            {QStringLiteral("negativeDuration"), live.negativeDuration},
            {QStringLiteral("tweens"), tweens}
        });
    }
    QJsonArray timelines;for(auto it=m_timelines.cbegin();it!=m_timelines.cend();++it)timelines.append(QJsonObject::fromVariantMap(it.value().toVariantMap()));
    return QJsonObject{{QStringLiteral("pictures"), pictures},{QStringLiteral("timelines"),timelines}};
}

bool PictureManager::fromJson(const QJsonObject& object)
{
    if (!object.value(QStringLiteral("pictures")).isArray()) return false;
    QMap<int, LivePicture> restored;
    for (const QJsonValue& value : object.value(QStringLiteral("pictures")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject saved = value.toObject();
        LivePicture live;
        live.def = PictureDef::fromParams(saved.value(QStringLiteral("definition")).toObject().toVariantMap());
        live.def.number = qMax(1, live.def.number);
        live.logicalName=saved.value(QStringLiteral("logicalName")).toString().trimmed();
        live.pictureGroup=saved.value(QStringLiteral("pictureGroup")).toString().trimmed();
        live.attachment=PictureAttachment::fromVariantMap(saved.value(QStringLiteral("attachment")).toObject().toVariantMap());
        live.timeline.name=saved.value(QStringLiteral("timelineName")).toString();
        live.timeline.elapsed=qMax(0.0,saved.value(QStringLiteral("timelineElapsed")).toDouble());
        live.timeline.playing=saved.value(QStringLiteral("timelinePlaying")).toBool();
        live.onTouchCommonEventId=saved.value(QStringLiteral("onTouchCommonEventId")).toString();
        live.onClickCommonEventId=saved.value(QStringLiteral("onClickCommonEventId")).toString();
        const QJsonObject bindings=saved.value(QStringLiteral("dynamicBindings")).toObject();
        for(auto binding=bindings.begin();binding!=bindings.end();++binding)
            live.dynamicBindings.insert(picturePropFromId(binding.key()),binding.value().toObject().toVariantMap());
        live.age = qMax(0.0, saved.value(QStringLiteral("age")).toDouble());
        if (saved.value(QStringLiteral("framePlayback")).isObject())
            live.framePlayback.fromJson(saved.value(QStringLiteral("framePlayback")).toObject(), live.def.frameSequence());
        else
            // Save 4.0 RC2.11 ou anterior: reconstrói o MESMO frame que era
            // calculado por age e passa a usar o relógio independente dali em diante.
            live.framePlayback.restoreLegacy(live.def.frameSequence(), live.age);
        live.phase = static_cast<LivePicture::Phase>(qBound(0, saved.value(QStringLiteral("phase")).toInt(), 2));
        live.phaseType = pictureTransitionFromId(saved.value(QStringLiteral("phaseType")).toString());
        live.phaseDuration = qMax(0.0, saved.value(QStringLiteral("phaseDuration")).toDouble());
        live.phaseTime = qBound(0.0, saved.value(QStringLiteral("phaseTime")).toDouble(), live.phaseDuration);
        live.negativeFrom = qBound(0.0, saved.value(QStringLiteral("negativeFrom")).toDouble(live.def.fx.negative.strength), 1.0);
        live.negativeTo = qBound(0.0, saved.value(QStringLiteral("negativeTo")).toDouble(live.def.fx.negative.strength), 1.0);
        live.negativeDuration = qMax(0.0, saved.value(QStringLiteral("negativeDuration")).toDouble());
        live.negativeElapsed = qBound(0.0, saved.value(QStringLiteral("negativeElapsed")).toDouble(), live.negativeDuration);
        for (const QJsonValue& tweenValue : saved.value(QStringLiteral("tweens")).toArray()) {
            if (!tweenValue.isObject()) continue;
            const QJsonObject t = tweenValue.toObject();
            PictureTween tween;
            tween.prop = picturePropFromId(t.value(QStringLiteral("property")).toString());
            tween.from = t.value(QStringLiteral("from")).toDouble();
            tween.to = t.value(QStringLiteral("to")).toDouble();
            tween.duration = qMax(0.0, t.value(QStringLiteral("duration")).toDouble());
            tween.elapsed = qBound(0.0, t.value(QStringLiteral("elapsed")).toDouble(), tween.duration);
            tween.ease = pictureEaseFromId(t.value(QStringLiteral("ease")).toString());
            if (tween.duration > 0.0 && tween.elapsed < tween.duration) live.tweens.push_back(tween);
        }
        restored[live.def.number] = live;
    }
    m_pics = std::move(restored);
    m_timelines.clear();for(const auto& value:object.value(QStringLiteral("timelines")).toArray())if(value.isObject())defineTimeline(PictureTimeline::fromVariantMap(value.toObject().toVariantMap()));
    m_logicalNameMap.clear();
    for(auto it=m_pics.begin();it!=m_pics.end();++it){
        const QString key=logicalKey(it.value().logicalName);
        if(key.isEmpty())continue;
        if(m_logicalNameMap.contains(key))it.value().logicalName.clear();
        else m_logicalNameMap.insert(key,it.key());
    }
    return true;
}

} // namespace game
