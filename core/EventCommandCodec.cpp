#include "EventCommandCodec.h"
#include "LudoCommandSystem.h"

#include <QtGlobal>
#include <utility>

namespace core {
namespace {

EventCommand canonicalizeNestedCommands(EventCommand command)
{
    const QVariantMap editorMetadata = command.editorMetadata;
    command = normalizeLegacyLudoCommand(command);
    if (command.editorMetadata.isEmpty()) command.editorMetadata = editorMetadata;
    if (command.type != QLatin1String("choice.show")) return command;

    const QVariantList rawBranches = command.params.value(QStringLiteral("branches")).toList();
    if (rawBranches.isEmpty()) return command;

    QVariantList canonicalBranches;
    canonicalBranches.reserve(rawBranches.size());
    for (const QVariant& rawBranch : rawBranches)
        canonicalBranches.push_back(eventCommandsToVariantList(
            eventCommandsFromVariantList(rawBranch.toList())));
    command.params[QStringLiteral("branches")] = canonicalBranches;
    return command;
}

} // namespace

QVariantMap eventCommandToVariantMap(const EventCommand& source)
{
    const EventCommand command = canonicalizeNestedCommands(source);
    QVariantMap map{{QStringLiteral("type"), command.type},
                    {QStringLiteral("params"), command.params}};
    if (command.executionMode != EventExecutionMode::Normal)
        map.insert(QStringLiteral("executionMode"), eventExecutionModeId(command.executionMode));
    if (!command.editorMetadata.isEmpty())
        map.insert(QStringLiteral("editorMetadata"), command.editorMetadata);
    return map;
}

EventCommand eventCommandFromVariantMap(const QVariantMap& map)
{
    EventCommand command;
    command.type = map.value(QStringLiteral("type")).toString().trimmed().left(128);
    command.params = map.value(QStringLiteral("params")).toMap();
    command.executionMode = eventExecutionModeFromId(map.value(QStringLiteral("executionMode")).toString());
    command.editorMetadata = map.value(QStringLiteral("editorMetadata")).toMap();
    return canonicalizeNestedCommands(command);
}

QVariantList eventCommandsToVariantList(const QVector<EventCommand>& commands)
{
    QVariantList out;
    out.reserve(commands.size());
    for (const EventCommand& command : commands)
        out.push_back(eventCommandToVariantMap(command));
    return out;
}

QVector<EventCommand> eventCommandsFromVariantList(const QVariantList& values, int maxCommands)
{
    QVector<EventCommand> out;
    const int limit = qBound(0, maxCommands, 1000000);
    out.reserve(qMin(values.size(), limit));
    for (const QVariant& value : values) {
        if (out.size() >= limit) break;
        const QVariantMap map = value.toMap();
        if (map.isEmpty()) continue;
        EventCommand command = eventCommandFromVariantMap(map);
        if (!command.type.isEmpty()) out.push_back(std::move(command));
    }
    return out;
}

QJsonObject eventCommandToJson(const EventCommand& source)
{
    return QJsonObject::fromVariantMap(eventCommandToVariantMap(source));
}

EventCommand eventCommandFromJson(const QJsonObject& object)
{
    return eventCommandFromVariantMap(object.toVariantMap());
}

QJsonArray eventCommandsToJson(const QVector<EventCommand>& commands)
{
    QJsonArray out;
    for (const EventCommand& command : commands)
        out.append(eventCommandToJson(command));
    return out;
}

QVector<EventCommand> eventCommandsFromJson(const QJsonArray& array, int maxCommands)
{
    QVector<EventCommand> out;
    const int limit = qBound(0, maxCommands, 1000000);
    out.reserve(qMin(array.size(), limit));
    for (const QJsonValue& value : array) {
        if (out.size() >= limit) break;
        if (!value.isObject()) continue;
        EventCommand command = eventCommandFromJson(value.toObject());
        if (!command.type.isEmpty()) out.push_back(std::move(command));
    }
    return out;
}

} // namespace core
