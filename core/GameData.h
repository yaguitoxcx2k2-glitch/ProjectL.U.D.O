// ============================================================================
//  GameData.h — O "banco de dados" do jogo: switches, variáveis e eventos
//  comuns.
//
//  É dado de PROJETO (o editor edita, o ProjectIO grava). O valor que muda
//  durante a partida não mora aqui — mora no `game::GameState`, que nasce
//  destes valores iniciais. Essa separação é o que mantém a regra de ouro do
//  runtime: jogar nunca altera o projeto.
// ============================================================================
#pragma once

#include "EventModel.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QHash>

namespace core {

/// Um interruptor: liga/desliga. É a memória mais simples que um jogo tem —
/// "o baú já foi aberto", "a ponte caiu".
struct SwitchDef {
    int     id = 1;
    QString name;
    bool    initial = false;
};

/// Uma variável numérica: contadores, progresso, quantidade.
struct VariableDef {
    int     id = 1;
    QString name;
    int     initial = 0;
};

/// Uma String global de primeira classe. Diferente do Texto local de um
/// Evento Comum, este valor pertence ao estado da partida, participa de
/// save/load, debugger e pode ser usado por qualquer evento.
struct StringDef {
    int     id = 1;
    QString name;
    QString initial;
};

// ---------------------------------------------------------------- Footsteps
/// Um arquivo participante de uma superfície de passos. O caminho relativo
/// permanece por compatibilidade/portabilidade e o assetId acompanha moves e
/// renames via Asset Database. `weight` permite sons raros sem script.
struct FootstepSound {
    QString assetId;
    QString sourcePath;
    int weight = 1;
};

/// Perfil de superfície reutilizável por tiles, terrenos, jogador e eventos.
/// É dado de projeto: o runtime só consulta e nunca modifica esta definição.
struct FootstepSurface {
    QString id = idGen();
    QString name = QStringLiteral("Superfície");
    QVector<FootstepSound> sounds;
    int volume = 80;
    int pitchMin = 95;
    int pitchMax = 105;
    bool avoidImmediateRepeat = true;
};

/// Configuração global do resolver. Terrain/Tag é o mesmo inteiro exposto por
/// map.terrain e pelo Runtime Map Management; zero significa sem tag explícita.
struct FootstepSettings {
    QString defaultSurfaceId;
    QHash<int, QString> terrainSurfaceIds;
};

/// Como um evento comum é disparado.
enum class CommonTrigger {
    None,       ///< só roda quando alguém chama
    Autorun,    ///< roda sozinho enquanto o interruptor estiver ligado
    Parallel    ///< roda em paralelo enquanto o interruptor estiver ligado
};

QString       commonTriggerId(CommonTrigger t);
CommonTrigger commonTriggerFromId(const QString& id);
QString       commonTriggerLabel(CommonTrigger t);

/// Política de repetição para gatilhos automáticos/paralelos avançados.
enum class CommonSchedulePolicy {
    WhileTrue,   ///< compatibilidade: reinicia enquanto a condição for verdadeira
    OnTrue,      ///< executa uma vez na borda falso -> verdadeiro
    Interval     ///< executa periodicamente enquanto a condição for verdadeira
};
QString commonSchedulePolicyId(CommonSchedulePolicy p);
CommonSchedulePolicy commonSchedulePolicyFromId(const QString& id);
QString commonSchedulePolicyLabel(CommonSchedulePolicy p);

/// Tipos aceitos pela assinatura de um Evento Comum. O runtime armazena os
/// valores como QVariant, mas o tipo continua no projeto para que Editor,
/// Validador e Interpreter compartilhem o mesmo contrato No-Code.
enum class CommonValueType {
    Number,   ///< inteiro; interoperável com Variáveis Globais
    Boolean,  ///< ligado/desligado; interoperável com Switches Globais
    Text      ///< texto de primeira classe; interoperável com Strings Globais a partir da RC2.43
};
QString commonValueTypeId(CommonValueType t);
CommonValueType commonValueTypeFromId(const QString& id);
QString commonValueTypeLabel(CommonValueType t);
QVariant commonValueDefault(CommonValueType t);
/// Converte um QVariant para a representação canônica do tipo da assinatura.
/// Editor, persistência e runtime usam esta mesma regra para não divergirem.
QVariant normalizeCommonValue(const QVariant& value, CommonValueType t);

struct CommonEventParameter {
    QString id = idGen();
    QString name;
    CommonValueType type = CommonValueType::Number;
    QVariant defaultValue = 0;
    bool required = false;
    QString description;
};

struct CommonEventLocal {
    QString id = idGen();
    QString name;
    CommonValueType type = CommonValueType::Number;
    QVariant initialValue = 0;
};

struct CommonEventReturn {
    bool enabled = false;
    QString name = QStringLiteral("Resultado");
    CommonValueType type = CommonValueType::Number;
    QVariant defaultValue = 0;
};

/// Parâmetro visual de um template de comandos. `defaultSource` usa o mesmo
/// Value Resolver dos comandos; ao inserir o template o placeholder é
/// substituído antes de chegar ao Validator ou ao runtime.
struct CommandTemplateParameter {
    QString id = idGen();
    QString name;
    CommonValueType type = CommonValueType::Number;
    QVariantMap defaultSource;
    QString description;
};

/// Bloco reutilizável pertencente ao projeto. Diferente de um Evento Comum,
/// o template é expandido no Editor e não cria uma linguagem no runtime.
struct CommandTemplate {
    QString id = idGen();
    QString name;
    QString description;
    QVector<EventCommand> commands;
    QVector<CommandTemplateParameter> parameters;
};

/// Um evento comum: lista de comandos reutilizável, chamável de qualquer
/// evento do jogo. É o que evita copiar a mesma cutscene em dez mapas.
struct CommonEvent {
    QString               id = idGen();
    int                   number = 1;      ///< número mostrado ao usuário
    QString               name;
    QString               category;        ///< pasta/categoria visual no editor
    QString               description;     ///< notas de uso; não afeta o runtime
    CommonTrigger         trigger = CommonTrigger::None;
    int                   switchId = 0;    ///< interruptor legado que liga o gatilho
    /// G / RC2.46: condição tipada opcional usando o mesmo Value Resolver dos comandos.
    /// Quando false, preserva exatamente o comportamento legado por Switch.
    bool                  advancedTrigger = false;
    CommonValueType       triggerValueType = CommonValueType::Boolean;
    QVariantMap           triggerLeft;
    QString               triggerOp = QStringLiteral("==");
    QVariantMap           triggerRight;
    CommonSchedulePolicy  schedulePolicy = CommonSchedulePolicy::WhileTrue;
    int                   intervalFrames = 60;
    int                   priority = 0;
    QVector<CommonEventParameter> parameters;
    QVector<CommonEventLocal> locals;
    CommonEventReturn     returnValue;
    QVector<EventCommand> commands;
};

/// Condições de uma página de evento. A página só vale se TODAS as ativas
/// baterem — e o jogo usa a ÚLTIMA página válida, como no editores de RPG.
struct PageConditions {
    bool    useSwitchA = false;
    int     switchAId = 1;
    bool    useSwitchB = false;
    int     switchBId = 1;
    bool    useVariable = false;
    int     variableId = 1;
    QString variableOp = QStringLiteral(">=");   ///< >= <= > < == !=
    int     variableValue = 0;
    bool    useSelfSwitch = false;
    QString selfSwitchLetter = QStringLiteral("A");   ///< A, B, C ou D

    bool any() const {
        return useSwitchA || useSwitchB || useVariable || useSelfSwitch;
    }
    QJsonObject toJson() const;
    static PageConditions fromJson(const QJsonObject& o);
};

/// Compara dois números com o operador em texto (">=", "==" ...).
bool compareWithOp(int a, const QString& op, int b);
/// Comparador tipado compartilhado pelo scheduler/validator/testes.
bool compareCommonValues(const QVariant& a, CommonValueType type, const QString& op, const QVariant& b);

} // namespace core
