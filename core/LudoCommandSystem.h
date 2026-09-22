#pragma once

#include "EventModel.h"

#include <QString>
#include <QVector>

namespace core {

/// Resultado do parser canônico da sintaxe textual <Ludo...>.
/// `recognized` distingue texto que não é Ludo de um Ludo inválido; `valid`
/// só fica true quando nome, parâmetros, tipos e limites passaram pelo schema.
struct LudoCommandParseResult {
    bool recognized = false;
    bool valid = false;
    QString code;
    QString message;
    QString sourceText;
    EventCommand command;
};

/// Converte um único comando textual Ludo para o EventCommand nativo.
/// O parser é a única autoridade para aliases legados, defaults, tipos e ranges.
LudoCommandParseResult parseLudoCommandText(
    const QString& text,
    EventExecutionMode executionMode = EventExecutionMode::Normal);

/// Extrai tags Ludo de Comentários. Tags desconhecidas/malformadas também são
/// retornadas como diagnósticos, para que nunca virem metadado silencioso.
QVector<LudoCommandParseResult> parseLudoCommentCommands(const QString& text);

/// Migra o wrapper legado `ludo.command` para o tipo nativo o mais cedo
/// possível. Entradas inválidas são preservadas para Validator/editor poderem
/// mostrá-las e corrigi-las, mas recebem o ExecutionMode legado correspondente.
EventCommand normalizeLegacyLudoCommand(const EventCommand& command,
                                        LudoCommandParseResult* parseResult = nullptr);

/// Retorna somente os comandos que pertencem ao lifecycle OnPageActivated.
/// Comentários e `ludo.command` legados passam pelo mesmo parser/schema.
QVector<EventCommand> ludoPageActivationCommands(
    const EventCommand& source,
    QVector<LudoCommandParseResult>* diagnostics = nullptr);

bool ludoCommandSupportsPageActivation(const EventCommand& command);

} // namespace core
