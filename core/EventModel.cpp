#include "EventModel.h"
#include <QSet>

namespace core {

int directionalSensorIndexForDelta(int dx, int dy)
{
    if (dx == 0 && dy == 0) return -1;
    if (dx == 0) return dy > 0 ? 0 : 3;
    if (dy == 0) return dx < 0 ? 1 : 2;
    if (dx < 0 && dy > 0) return 4;
    if (dx > 0 && dy > 0) return 5;
    if (dx < 0) return 6;
    return 7;
}

bool directionalSensorContains(const QVector<int>& ranges, int dx, int dy)
{
    const int direction = directionalSensorIndexForDelta(dx, dy);
    if (direction < 0 || direction >= ranges.size()) return false;
    const int range = qMax(0, ranges.at(direction));
    return range > 0 && qMax(qAbs(dx), qAbs(dy)) <= range;
}

QRect EventGraphic::charsetFrameRect() const
{
    if (charset.isNull() || charsetCols <= 0 || charsetRows <= 0) return QRect();
    const int tc = totalCols(), tr = totalRows();
    const int fw = charset.width() / tc;
    const int fh = charset.height() / tr;
    if (fw <= 0 || fh <= 0) return QRect();
    const int personagem = clampi(characterIndex, 0, characterCount() - 1);
    const int blocoX = personagem % qMax(1, characterCols);
    const int blocoY = personagem / qMax(1, characterCols);
    const int c = blocoX * charsetCols + clampi(frame, 0, charsetCols - 1);
    const int r = blocoY * charsetRows + clampi(dir, 0, charsetRows - 1);
    return QRect(c * fw, r * fh, fw, fh);
}

EventPage& MapEvent::page(int i)
{
    if (pages.isEmpty()) pages.push_back(EventPage());
    return pages[clampi(i, 0, pages.size() - 1)];
}

const EventPage& MapEvent::page(int i) const
{
    static const EventPage fallback;
    if (pages.isEmpty()) return fallback;
    return pages[clampi(i, 0, pages.size() - 1)];
}

QString eventTriggerId(EventTrigger t)
{
    switch (t) {
    case EventTrigger::ActionKey:   return QStringLiteral("action");
    case EventTrigger::PlayerTouch: return QStringLiteral("playerTouch");
    case EventTrigger::EventTouch:  return QStringLiteral("eventTouch");
    case EventTrigger::DirectionalSensor: return QStringLiteral("directionalSensor");
    case EventTrigger::Autorun:     return QStringLiteral("autorun");
    case EventTrigger::Parallel:    return QStringLiteral("parallel");
    }
    return QStringLiteral("action");
}

EventTrigger eventTriggerFromId(const QString& id)
{
    if (id == QLatin1String("playerTouch")) return EventTrigger::PlayerTouch;
    if (id == QLatin1String("eventTouch"))  return EventTrigger::EventTouch;
    if (id == QLatin1String("directionalSensor")) return EventTrigger::DirectionalSensor;
    if (id == QLatin1String("autorun"))     return EventTrigger::Autorun;
    if (id == QLatin1String("parallel"))    return EventTrigger::Parallel;
    return EventTrigger::ActionKey;
}

QString eventTriggerLabel(EventTrigger t)
{
    switch (t) {
    case EventTrigger::ActionKey:   return QObject::tr("Tecla de ação");
    case EventTrigger::PlayerTouch: return QObject::tr("Toque do jogador");
    case EventTrigger::EventTouch:  return QObject::tr("Toque do evento");
    case EventTrigger::DirectionalSensor: return QObject::tr("Sensor direcional (8 direções)");
    case EventTrigger::Autorun:     return QObject::tr("Automático");
    case EventTrigger::Parallel:    return QObject::tr("Paralelo");
    }
    return QString();
}

QString eventPriorityId(EventPriority p)
{
    switch (p) {
    case EventPriority::Below: return QStringLiteral("below");
    case EventPriority::Same:  return QStringLiteral("same");
    case EventPriority::Above: return QStringLiteral("above");
    }
    return QStringLiteral("same");
}

EventPriority eventPriorityFromId(const QString& id)
{
    if (id == QLatin1String("below")) return EventPriority::Below;
    if (id == QLatin1String("above")) return EventPriority::Above;
    return EventPriority::Same;
}

QString eventPriorityLabel(EventPriority p)
{
    switch (p) {
    case EventPriority::Below: return QObject::tr("Abaixo do personagem");
    case EventPriority::Same:  return QObject::tr("Igual ao personagem");
    case EventPriority::Above: return QObject::tr("Acima do personagem");
    }
    return QString();
}

QString eventMoveId(EventMove m)
{
    switch (m) {
    case EventMove::Fixed:    return QStringLiteral("fixed");
    case EventMove::Random:   return QStringLiteral("random");
    case EventMove::Approach: return QStringLiteral("approach");
    case EventMove::Custom:   return QStringLiteral("custom");
    }
    return QStringLiteral("fixed");
}

EventMove eventMoveFromId(const QString& id)
{
    if (id == QLatin1String("random"))   return EventMove::Random;
    if (id == QLatin1String("approach")) return EventMove::Approach;
    if (id == QLatin1String("custom"))   return EventMove::Custom;
    return EventMove::Fixed;
}

QString eventMoveLabel(EventMove m)
{
    switch (m) {
    case EventMove::Fixed:    return QObject::tr("Parado");
    case EventMove::Random:   return QObject::tr("Aleatório");
    case EventMove::Approach: return QObject::tr("Persegue o jogador");
    case EventMove::Custom:   return QObject::tr("Rota personalizada");
    }
    return QString();
}

QString moveRouteBlockedPolicyId(MoveRouteBlockedPolicy policy)
{
    switch (policy) {
    case MoveRouteBlockedPolicy::Wait:   return QStringLiteral("wait");
    case MoveRouteBlockedPolicy::Skip:   return QStringLiteral("skip");
    case MoveRouteBlockedPolicy::Cancel: return QStringLiteral("cancel");
    }
    return QStringLiteral("wait");
}

MoveRouteBlockedPolicy moveRouteBlockedPolicyFromId(const QString& id)
{
    const QString value = id.trimmed().toLower();
    if (value == QLatin1String("skip")) return MoveRouteBlockedPolicy::Skip;
    if (value == QLatin1String("cancel")) return MoveRouteBlockedPolicy::Cancel;
    return MoveRouteBlockedPolicy::Wait;
}

QString moveRouteStartModeId(MoveRouteStartMode mode)
{
    return mode == MoveRouteStartMode::Queue ? QStringLiteral("queue")
                                              : QStringLiteral("replace");
}

MoveRouteStartMode moveRouteStartModeFromId(const QString& id)
{
    return id.trimmed().compare(QLatin1String("queue"), Qt::CaseInsensitive) == 0
        ? MoveRouteStartMode::Queue : MoveRouteStartMode::Replace;
}

QString moveRoutePathBehaviorId(MoveRoutePathBehavior behavior)
{
    switch (behavior) {
    case MoveRoutePathBehavior::Follow: return QStringLiteral("follow");
    case MoveRoutePathBehavior::Flee: return QStringLiteral("flee");
    case MoveRoutePathBehavior::KeepDistance: return QStringLiteral("keepDistance");
    case MoveRoutePathBehavior::Reach: break;
    }
    return QStringLiteral("reach");
}

MoveRoutePathBehavior moveRoutePathBehaviorFromId(const QString& id)
{
    const QString value=id.trimmed();
    if(value.compare(QLatin1String("follow"),Qt::CaseInsensitive)==0)return MoveRoutePathBehavior::Follow;
    if(value.compare(QLatin1String("flee"),Qt::CaseInsensitive)==0)return MoveRoutePathBehavior::Flee;
    if(value.compare(QLatin1String("keepDistance"),Qt::CaseInsensitive)==0)return MoveRoutePathBehavior::KeepDistance;
    return MoveRoutePathBehavior::Reach;
}

QString moveRoutePathTargetKindId(MoveRoutePathTargetKind kind)
{
    switch(kind){
    case MoveRoutePathTargetKind::Player:return QStringLiteral("player");
    case MoveRoutePathTargetKind::SourceEvent:return QStringLiteral("sourceEvent");
    case MoveRoutePathTargetKind::Event:return QStringLiteral("event");
    case MoveRoutePathTargetKind::Cell:break;
    }
    return QStringLiteral("cell");
}

MoveRoutePathTargetKind moveRoutePathTargetKindFromId(const QString& id)
{
    const QString value=id.trimmed();
    if(value.compare(QLatin1String("player"),Qt::CaseInsensitive)==0)return MoveRoutePathTargetKind::Player;
    if(value.compare(QLatin1String("sourceEvent"),Qt::CaseInsensitive)==0)return MoveRoutePathTargetKind::SourceEvent;
    if(value.compare(QLatin1String("event"),Qt::CaseInsensitive)==0)return MoveRoutePathTargetKind::Event;
    return MoveRoutePathTargetKind::Cell;
}

MoveRoutePathOptions moveRoutePathOptionsFromCommand(const MoveCommand& command)
{
    MoveRoutePathOptions out;
    const QVariantMap& p=command.params;
    out.behavior=moveRoutePathBehaviorFromId(p.value(QStringLiteral("behavior"),QStringLiteral("reach")).toString());
    out.targetKind=moveRoutePathTargetKindFromId(p.value(QStringLiteral("targetKind"),QStringLiteral("cell")).toString());
    out.eventId=p.value(QStringLiteral("eventId")).toString().trimmed();
    out.cell=QPoint(p.value(QStringLiteral("x")).toInt(),p.value(QStringLiteral("y")).toInt());
    out.minDistance=qBound(0,p.value(QStringLiteral("minDistance"),0).toInt(),999);
    const int defaultMax=(out.behavior==MoveRoutePathBehavior::Follow)?1:
                         (out.behavior==MoveRoutePathBehavior::KeepDistance)?2:0;
    out.maxDistance=qBound(0,p.value(QStringLiteral("maxDistance"),defaultMax).toInt(),999);
    if(out.maxDistance<out.minDistance&&out.behavior==MoveRoutePathBehavior::KeepDistance)
        out.maxDistance=out.minDistance;
    out.diagonal=p.value(QStringLiteral("diagonal"),true).toBool();
    // Continuous faz parte da semantica do comportamento, nao e um flag
    // independente. Isso impede arquivos antigos/manuais de criarem um Reach
    // infinito ou um Follow que para silenciosamente.
    out.continuous=out.behavior==MoveRoutePathBehavior::Follow||
                   out.behavior==MoveRoutePathBehavior::KeepDistance;
    out.maxSearchNodes=qBound(64,p.value(QStringLiteral("maxSearchNodes"),4096).toInt(),65536);
    return out;
}

QVariantMap moveRoutePathOptionsToParams(const MoveRoutePathOptions& options)
{
    return {
        {QStringLiteral("behavior"),moveRoutePathBehaviorId(options.behavior)},
        {QStringLiteral("targetKind"),moveRoutePathTargetKindId(options.targetKind)},
        {QStringLiteral("eventId"),options.eventId},
        {QStringLiteral("x"),options.cell.x()},
        {QStringLiteral("y"),options.cell.y()},
        {QStringLiteral("minDistance"),qBound(0,options.minDistance,999)},
        {QStringLiteral("maxDistance"),qBound(qMax(0,options.minDistance),options.maxDistance,999)},
        {QStringLiteral("diagonal"),options.diagonal},
        {QStringLiteral("continuous"),options.behavior==MoveRoutePathBehavior::Follow||
                                      options.behavior==MoveRoutePathBehavior::KeepDistance},
        {QStringLiteral("maxSearchNodes"),qBound(64,options.maxSearchNodes,65536)}
    };
}

QStringList moveRouteCommandTypes()
{
    // Ordem semântica estável. Adicionar um comando aqui obriga os testes de
    // contrato a comprovarem que o executor também o reconhece.
    return {
        QStringLiteral("moveDown"), QStringLiteral("moveLeft"), QStringLiteral("moveRight"), QStringLiteral("moveUp"),
        QStringLiteral("moveDownLeft"), QStringLiteral("moveDownRight"), QStringLiteral("moveUpLeft"), QStringLiteral("moveUpRight"),
        QStringLiteral("moveRandom"), QStringLiteral("moveTowardPlayer"), QStringLiteral("moveAwayPlayer"),
        QStringLiteral("pathfind"),
        QStringLiteral("stepForward"), QStringLiteral("stepBackward"), QStringLiteral("jump"),
        QStringLiteral("turnDown"), QStringLiteral("turnLeft"), QStringLiteral("turnRight"), QStringLiteral("turnUp"),
        QStringLiteral("turnRight90"), QStringLiteral("turnLeft90"), QStringLiteral("turn180"), QStringLiteral("turn90Random"),
        QStringLiteral("turnRandom"), QStringLiteral("turnTowardPlayer"), QStringLiteral("turnAwayPlayer"),
        QStringLiteral("wait"), QStringLiteral("speed"), QStringLiteral("frequency"),
        QStringLiteral("walkAnimOn"), QStringLiteral("walkAnimOff"), QStringLiteral("stepAnimOn"), QStringLiteral("stepAnimOff"),
        QStringLiteral("dirFixOn"), QStringLiteral("dirFixOff"), QStringLiteral("throughOn"), QStringLiteral("throughOff"),
        QStringLiteral("transparentOn"), QStringLiteral("transparentOff"), QStringLiteral("opacity"), QStringLiteral("blend"),
        QStringLiteral("switchOn"), QStringLiteral("switchOff"), QStringLiteral("playSE"), QStringLiteral("changeGraphic"),
        QStringLiteral("shake"), QStringLiteral("rememberPosition")
    };
}

bool isKnownMoveRouteCommandType(const QString& type)
{
    static const QSet<QString> known = [] {
        QSet<QString> result;
        const QStringList types = moveRouteCommandTypes();
        for (const QString& value : types) result.insert(value);
        return result;
    }();
    return known.contains(type);
}

QVariantMap moveRouteToMap(const MoveRoute& route)
{
    QVariantList commands;
    for (const MoveCommand& c : route.commands) {
        if (c.type.compare(QLatin1String("script"), Qt::CaseInsensitive) == 0) continue;
        commands.push_back(QVariantMap{{QStringLiteral("type"), c.type},
                                       {QStringLiteral("params"), c.params}});
    }
    const bool legacySkip = route.blockedPolicy == MoveRouteBlockedPolicy::Skip;
    return {{QStringLiteral("target"), route.target},
            {QStringLiteral("repeat"), route.repeat},
            {QStringLiteral("blockedPolicy"), moveRouteBlockedPolicyId(route.blockedPolicy)},
            // Gravado apenas como ponte para builds antigos; o modelo atual
            // possui uma única fonte de verdade: blockedPolicy.
            {QStringLiteral("skipIfBlocked"), legacySkip},
            {QStringLiteral("startMode"), moveRouteStartModeId(route.startMode)},
            {QStringLiteral("waitForCompletion"), route.waitForCompletion},
            {QStringLiteral("commands"), commands}};
}

MoveRoute moveRouteFromMap(const QVariantMap& map)
{
    MoveRoute r;
    r.target = map.value(QStringLiteral("target"), QStringLiteral("self")).toString().trimmed();
    if (r.target.isEmpty()) r.target = QStringLiteral("self");
    r.repeat = map.value(QStringLiteral("repeat"), true).toBool();
    if (map.contains(QStringLiteral("blockedPolicy")))
        r.blockedPolicy = moveRouteBlockedPolicyFromId(map.value(QStringLiteral("blockedPolicy")).toString());
    else if (map.value(QStringLiteral("skipIfBlocked"), false).toBool())
        r.blockedPolicy = MoveRouteBlockedPolicy::Skip;
    r.startMode = moveRouteStartModeFromId(map.value(QStringLiteral("startMode"), QStringLiteral("replace")).toString());
    r.waitForCompletion = map.value(QStringLiteral("waitForCompletion"), false).toBool();
    for (const QVariant& v : map.value(QStringLiteral("commands")).toList()) {
        const QVariantMap m = v.toMap();
        MoveCommand c{m.value(QStringLiteral("type")).toString().trimmed(),
                      m.value(QStringLiteral("params")).toMap()};
        if (!c.type.isEmpty() && c.type.compare(QLatin1String("script"), Qt::CaseInsensitive) != 0)
            r.commands.push_back(c);
    }
    return r;
}

} // namespace core
