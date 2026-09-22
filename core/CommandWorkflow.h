#pragma once

#include "GameData.h"

namespace core {

struct ParameterizedCommandBlock {
    QVector<EventCommand> commands;
    QVector<CommandTemplateParameter> parameters;
    QVariantMap arguments;
};

/// Localiza Value Specs externos dentro do bloco, cria parâmetros estáveis e
/// troca as ocorrências por placeholders. `placeholderSource` deve ser
/// `commonValue` (Evento Comum) ou `templateParameter` (template do Editor).
ParameterizedCommandBlock parameterizeCommandBlock(
    const QVector<EventCommand>& commands,
    const QString& placeholderSource);

QVector<EventCommand> instantiateCommandTemplate(
    const CommandTemplate& commandTemplate,
    const QVariantMap& arguments);

struct ExtractedCommonEvent {
    CommonEvent commonEvent;
    EventCommand callCommand;
};

/// Constrói uma extração atômica. O chamador só altera o projeto depois de
/// validar/confirmar o resultado, preservando Undo e integridade estrutural.
ExtractedCommonEvent buildExtractedCommonEvent(
    const QVector<EventCommand>& commands,
    const QString& name,
    int number);

} // namespace core
