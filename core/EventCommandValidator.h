#pragma once

#include "EventModel.h"

namespace core {
class Editor;
struct MapDoc;
struct CommonEvent;
struct ProjectValidationResult;

/// Valida somente a lista de comandos de eventos. Separado de ProjectValidator
/// no Bloco A para reduzir acoplamento e permitir testes do schema de comandos.
void validateEventCommands(const Editor& ed, const QVector<EventCommand>& commands,
                           const QString& location, ProjectValidationResult& result,
                           const MapDoc* contextMap = nullptr,
                           const CommonEvent* contextCommon = nullptr,
                           bool pageActivationReachable = true);

/// Validação única da definição de rota. É usada tanto por `move.route` quanto
/// pela Rota personalizada da página para evitar defaults/erros divergentes.
void validateMoveRouteDefinition(const MoveRoute& route, const QString& location,
                                 ProjectValidationResult& result, bool autonomousPageRoute = false);

} // namespace core
