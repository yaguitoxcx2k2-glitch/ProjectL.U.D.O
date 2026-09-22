#pragma once

#include "EventModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

namespace core {

/// Codec canônico para listas de comandos de evento. O mesmo formato é usado
/// pelo ProjectIO, pacotes .ludocommon e pelo clipboard do Editor, evitando
/// representações paralelas entre Map Events e Common Events.
QJsonObject eventCommandToJson(const EventCommand& command);
EventCommand eventCommandFromJson(const QJsonObject& object);
QJsonArray eventCommandsToJson(const QVector<EventCommand>& commands);
QVector<EventCommand> eventCommandsFromJson(const QJsonArray& array,
                                             int maxCommands = 100000);

/// Representação canônica quando comandos precisam viver dentro de params
/// (hoje, principalmente os ramos de choice.show). Mantém os mesmos campos
/// do JSON top-level, inclusive executionMode, e aplica a mesma normalização
/// de Ludo Commands de forma recursiva.
QVariantMap eventCommandToVariantMap(const EventCommand& command);
EventCommand eventCommandFromVariantMap(const QVariantMap& map);
QVariantList eventCommandsToVariantList(const QVector<EventCommand>& commands);
QVector<EventCommand> eventCommandsFromVariantList(const QVariantList& values,
                                                    int maxCommands = 100000);

} // namespace core
