#pragma once

#include "EventModel.h"

#include <QJsonObject>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace core {

inline constexpr int NoCodePluginFormatVersion = 2;
inline constexpr int NoCodePluginCommandContractVersion = 2;

/// Campo configurável exibido automaticamente pelo editor. Plugins no-code
/// nunca carregam DLL/JavaScript: apenas expandem modelos de comandos nativos.
struct PluginField
{
    QString id;
    QString label;
    QString type = QStringLiteral("text");
    QVariant defaultValue;
    int minimum = -999999999;
    int maximum = 999999999;
    QStringList options;
    bool required = true;
};

struct PluginCommand
{
    QString id;
    QString name;
    QString category;
    QString description;
    QString iconPath;
    QString shortcutKey;
    QStringList tags;
    bool showInCatalog = true;
    QVector<PluginField> fields;
    QVector<EventCommand> commands;
};

struct NoCodePlugin
{
    /// Formato de origem. Escritas novas usam `NoCodePluginFormatVersion`;
    /// manifests v1 continuam carregando e são migrados ao salvar.
    int sourceFormatVersion = NoCodePluginFormatVersion;
    int commandContractVersion = NoCodePluginCommandContractVersion;
    int apiVersion = 1; ///< API de capacidades; ausente em manifests antigos = 1.
    QStringList capabilities; ///< permissões declarativas; vazio mantém compatibilidade v1.
    QString id;
    QString name;
    QString version = QStringLiteral("1.0.0");
    QString author;
    QString description;
    QStringList dependencies; ///< IDs de outros pacotes necessários
    bool enabled = true;
    QVector<PluginCommand> commands;
};

QJsonObject noCodePluginToJson(const NoCodePlugin& plugin);
bool noCodePluginFromJson(const QJsonObject& object, NoCodePlugin* plugin,
                          QString* error = nullptr);
const NoCodePlugin* noCodePluginById(const QVector<NoCodePlugin>& plugins, const QString& id);
const PluginCommand* pluginCommandById(const NoCodePlugin& plugin, const QString& id);
QVector<EventCommand> expandPluginCommand(const PluginCommand& command,
                                          const QVariantMap& arguments);
bool isSafePluginCommandType(const QString& type);
/// Validação única usada pelo importador, compositor, catálogo e runtime.
/// Garante que o comando visual só expanda comandos nativos seguros e que
/// todos os placeholders tenham um campo declarado.
bool validatePluginCommand(const PluginCommand& command, QStringList* errors = nullptr);

/// Validação do pacote inteiro: contrato, IDs, dependências e comandos.
bool validateNoCodePlugin(const NoCodePlugin& plugin, QStringList* errors = nullptr);

/// Normaliza argumentos conforme o formulário declarado. É o mesmo caminho
/// usado por Editor, Validator e runtime: defaults, tipos, ranges e choices.
bool normalizePluginArguments(const PluginCommand& command, const QVariantMap& arguments,
                              QVariantMap* normalized, QStringList* errors = nullptr);

enum class PluginInvocationStatus {
    Ready,
    MissingPlugin,
    DisabledPlugin,
    MissingDependency,
    DisabledDependency,
    DependencyCycle,
    MissingCommand,
    InvalidDefinition,
    InvalidArguments
};

struct PluginInvocationResolution
{
    PluginInvocationStatus status = PluginInvocationStatus::MissingPlugin;
    QString pluginId;
    QString commandId;
    QString error;
    QVariantMap normalizedArguments;
    QVector<EventCommand> expandedCommands;
    bool ready() const { return status == PluginInvocationStatus::Ready; }
};

QString pluginInvocationStatusId(PluginInvocationStatus status);
PluginInvocationResolution resolvePluginInvocation(const QVector<NoCodePlugin>& plugins,
                                                   const QString& pluginId,
                                                   const QString& commandId,
                                                   const QVariantMap& arguments);

/// Tipos nativos efetivamente exigidos pelo pacote; usado pelo contrato v2
/// para diagnóstico e exportação/auditoria determinística.
QStringList pluginRequiredCommandTypes(const NoCodePlugin& plugin);

} // namespace core
