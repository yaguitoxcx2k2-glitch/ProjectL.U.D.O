// ============================================================================
// CommandRegistry.h — schema central dos comandos no-code.
//
// Bloco A / 3.23.0: além da classificação usada pelo runtime, este registro é
// agora a fonte de verdade para os tipos nativos conhecidos e para a política
// de comandos permitidos em plugins declarativos. O editor continua dono dos
// rótulos traduzidos e dos diálogos visuais.
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core {

enum class CommandDomain {
    Unknown,
    Text,
    Flow,
    Movement,
    Fog,
    Picture,
    Runtime,
    Plugin
};

/// Política única de exposição no catálogo visual. Isso evita que testes/UI
/// mantenham listas paralelas de tipos "ocultos".
enum class CommandCatalogPolicy {
    Visible,     ///< o autor pode inserir diretamente pelo catálogo
    Structural,  ///< marcador de fechamento inserido automaticamente com o bloco
    Dynamic,     ///< aparece por uma fonte dinâmica (ex.: plugin.call:<id>:<cmd>)
    Indirect,    ///< criado por outra UI/diálogo, não por item próprio do catálogo
    Legacy       ///< reconhecido para migração/diagnóstico, mas não oferecido
};

/// Rota executável canônica. Bloco 39: um comando registrado não pode existir
/// sem declarar por qual autoridade ele produz efeito no Player.
enum class CommandExecutionRoute {
    Unknown,         ///< estado inválido para um schema persistido
    Interpreter,     ///< executado/normalizado diretamente pelo Interpreter
    GameSession,     ///< encaminhado para GameSession/runtime world
    PluginExpansion, ///< plugin.call é expandido para comandos nativos
    LegacyNoOp       ///< lido para migração, mas deliberadamente sem efeito
};

QString commandDomainId(CommandDomain domain);
QString commandDomainLabel(CommandDomain domain);
QString commandCatalogPolicyId(CommandCatalogPolicy policy);
QString commandExecutionRouteId(CommandExecutionRoute route);

struct CommandDescriptor {
    CommandDomain domain = CommandDomain::Unknown;
    bool sceneBoundary = false;
    bool skipDuringCutscene = false;
    bool registered = false;      ///< tipo exato consta no schema nativo
    bool pluginSafe = false;      ///< pode aparecer dentro de .ludoplugin
    bool stateMutation = false;   ///< altera GameState lógico e deve ser observável antes do próximo comando
    bool awaitable = false;       ///< pode ocupar o slot canônico de espera do Interpreter
    bool pageActivation = false;  ///< pode pertencer ao lifecycle OnPageActivated de um Map Event
    CommandCatalogPolicy catalogPolicy = CommandCatalogPolicy::Visible;
    CommandExecutionRoute executionRoute = CommandExecutionRoute::Unknown;
};

/// Metadata estável e não traduzida. `parameterKeys` documenta o contrato
/// conhecido sem obrigar parâmetros opcionais a existirem no projeto.
struct CommandSchema {
    QString type;
    CommandDomain domain = CommandDomain::Unknown;
    bool sceneBoundary = false;
    bool skipDuringCutscene = false;
    bool pluginSafe = false;
    bool stateMutation = false;
    bool awaitable = false;
    bool pageActivation = false;
    CommandCatalogPolicy catalogPolicy = CommandCatalogPolicy::Visible;
    CommandExecutionRoute executionRoute = CommandExecutionRoute::Unknown;
    QStringList parameterKeys;
};

class CommandRegistry
{
public:
    static CommandDescriptor describe(const QString& type);
    static CommandDomain domain(const QString& type) { return describe(type).domain; }
    static bool isRuntimeForwarded(const QString& type)
    { return executionRoute(type) == CommandExecutionRoute::GameSession; }
    static bool isSceneBoundary(const QString& type)
    { return describe(type).sceneBoundary; }
    static bool shouldSkipDuringCutscene(const QString& type)
    { return describe(type).skipDuringCutscene; }
    static bool isKnown(const QString& type)
    { return describe(type).registered; }
    static bool isPluginSafe(const QString& type)
    { return describe(type).pluginSafe; }
    static bool isStateMutation(const QString& type)
    { return describe(type).stateMutation; }
    static bool canAwait(const QString& type)
    { return describe(type).awaitable; }
    static bool supportsPageActivation(const QString& type)
    { return describe(type).pageActivation; }
    static CommandCatalogPolicy catalogPolicy(const QString& type)
    { return describe(type).catalogPolicy; }
    static CommandExecutionRoute executionRoute(const QString& type)
    { return describe(type).executionRoute; }
    static bool isCatalogVisible(const QString& type)
    { return isKnown(type) && catalogPolicy(type) == CommandCatalogPolicy::Visible; }

    /// Versão do contrato compartilhado Registry/Catalog/Plugin/Validator/Runtime.
    static constexpr int ContractVersion = 2;
    static QString contractId();
    static QVector<CommandSchema> schemas();
    static QStringList registeredTypes();
    /// Autoauditoria do schema: IDs duplicados, domínios inválidos, parâmetros
    /// duplicados e políticas incompatíveis retornam mensagens determinísticas.
    static QStringList contractIssues();
};

} // namespace core
