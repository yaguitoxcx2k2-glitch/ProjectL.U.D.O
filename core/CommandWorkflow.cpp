#include "CommandWorkflow.h"
#include "GameValueRegistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMetaType>
#include <QSet>

namespace core {
namespace {

bool isExternalValueSource(const QString& source)
{
    static const QSet<QString> sources{
        QStringLiteral("variable"), QStringLiteral("switch"), QStringLiteral("string"),
        QStringLiteral("gameValue"), QStringLiteral("database"), QStringLiteral("databaseField"),
        QStringLiteral("stringMetric"), QStringLiteral("numberText"), QStringLiteral("switchText"),
        QStringLiteral("expression"), QStringLiteral("commonValue"),
        QStringLiteral("parameter"), QStringLiteral("local")
    };
    return sources.contains(source);
}

CommonValueType inferredType(const QVariantMap& spec)
{
    const QString explicitType = spec.value(QStringLiteral("valueType")).toString();
    if (!explicitType.isEmpty()) return commonValueTypeFromId(explicitType);
    const QString source = spec.value(QStringLiteral("source")).toString();
    if (source == QLatin1String("gameValue"))
        return gameValueType(spec.value(QStringLiteral("query")).toMap().value(QStringLiteral("key")).toString());
    if (source == QLatin1String("switch")) return CommonValueType::Boolean;
    if (source == QLatin1String("string") || source == QLatin1String("numberText") ||
        source == QLatin1String("switchText")) return CommonValueType::Text;
    return CommonValueType::Number;
}

QString suggestedName(const QVariantMap& spec, int ordinal)
{
    const QString source = spec.value(QStringLiteral("source")).toString();
    QString identity = spec.value(QStringLiteral("name")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("key")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("id")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("variableId")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("switchId")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("stringId")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("commonValueId")).toString().trimmed();
    if (identity.isEmpty()) identity = spec.value(QStringLiteral("query")).toMap().value(QStringLiteral("key")).toString().trimmed();
    if (identity.isEmpty()) identity = QString::number(ordinal);
    QString label;
    if (source == QLatin1String("switch")) label = QStringLiteral("Switch");
    else if (source == QLatin1String("string")) label = QStringLiteral("String");
    else if (source == QLatin1String("gameValue")) label = QStringLiteral("Game Value");
    else if (source == QLatin1String("database")) label = QStringLiteral("Database");
    else if (source == QLatin1String("databaseField")) label = QStringLiteral("Campo");
    else if (source == QLatin1String("expression")) label = QStringLiteral("Expressão");
    else if (source == QLatin1String("parameter")) label = QStringLiteral("Parâmetro");
    else if (source == QLatin1String("local")) label = QStringLiteral("Local");
    else label = QStringLiteral("Variável");
    return QStringLiteral("%1 %2").arg(label, identity).left(128);
}

QString canonicalSpecKey(const QVariantMap& spec)
{
    // `valueType` é uma anotação do campo consumidor. Dois campos diferentes
    // que apontam para a mesma origem externa (ex.: variable 7) devem
    // deduplicar quando o tipo semântico inferido é o mesmo, mesmo que apenas
    // um deles tenha recebido a anotação explícita pelo GameValueRegistry.
    QVariantMap normalized = spec;
    normalized[QStringLiteral("valueType")] = commonValueTypeId(inferredType(spec));
    return QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(normalized)).toJson(QJsonDocument::Compact));
}

QVariant parameterizeValue(const QVariant& value, const QString& placeholderSource,
                           QVector<CommandTemplateParameter>& parameters,
                           QVariantMap& arguments, QHash<QString, QString>& ids)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap original = value.toMap();
        const QString source = original.value(QStringLiteral("source")).toString();
        if (isExternalValueSource(source)) {
            const QString key = canonicalSpecKey(original);
            QString parameterId = ids.value(key);
            if (parameterId.isEmpty()) {
                CommandTemplateParameter parameter;
                parameter.name = suggestedName(original, parameters.size() + 1);
                parameter.type = inferredType(original);
                parameter.defaultSource = original;
                parameter.description = QStringLiteral("Detectado automaticamente ao reutilizar o bloco.");
                parameterId = parameter.id;
                ids.insert(key, parameterId);
                parameters.push_back(parameter);
                arguments.insert(parameterId, original);
            }
            if (placeholderSource == QLatin1String("commonValue"))
                return QVariantMap{{QStringLiteral("source"), placeholderSource},
                                   {QStringLiteral("commonValueId"), parameterId}};
            return QVariantMap{{QStringLiteral("source"), placeholderSource},
                               {QStringLiteral("id"), parameterId}};
        }
        QVariantMap mapped;
        for (auto it = original.cbegin(); it != original.cend(); ++it)
            mapped.insert(it.key(), parameterizeValue(it.value(), placeholderSource,
                                                      parameters, arguments, ids));
        return mapped;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList mapped;
        const QVariantList original = value.toList();
        mapped.reserve(original.size());
        for (const QVariant& item : original)
            mapped.push_back(parameterizeValue(item, placeholderSource, parameters, arguments, ids));
        return mapped;
    }
    return value;
}

QVariant instantiateValue(const QVariant& value, const CommandTemplate& commandTemplate,
                          const QVariantMap& arguments)
{
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap original = value.toMap();
        if (original.value(QStringLiteral("source")).toString() == QLatin1String("templateParameter")) {
            const QString id = original.value(QStringLiteral("id")).toString();
            if (arguments.contains(id)) return arguments.value(id);
            for (const CommandTemplateParameter& parameter : commandTemplate.parameters)
                if (parameter.id == id) return parameter.defaultSource;
            return QVariantMap{{QStringLiteral("source"), QStringLiteral("constant")},
                               {QStringLiteral("value"), 0}};
        }
        QVariantMap mapped;
        for (auto it = original.cbegin(); it != original.cend(); ++it)
            mapped.insert(it.key(), instantiateValue(it.value(), commandTemplate, arguments));
        return mapped;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList mapped;
        for (const QVariant& item : value.toList())
            mapped.push_back(instantiateValue(item, commandTemplate, arguments));
        return mapped;
    }
    return value;
}

} // namespace

ParameterizedCommandBlock parameterizeCommandBlock(
    const QVector<EventCommand>& commands, const QString& placeholderSource)
{
    ParameterizedCommandBlock result;
    QHash<QString, QString> ids;
    result.commands.reserve(commands.size());
    for (EventCommand command : commands) {
        for (const CommandValueFieldDescriptor& field : commandValueFields(command.type)) {
            if (!command.params.contains(field.parameter) ||
                command.params.value(field.parameter).metaType().id() != QMetaType::QVariantMap) continue;
            QVariantMap typed = command.params.value(field.parameter).toMap();
            if (isExternalValueSource(typed.value(QStringLiteral("source")).toString())) {
                typed[QStringLiteral("valueType")] = commonValueTypeId(field.type);
                command.params[field.parameter] = typed;
            }
        }
        command.params = parameterizeValue(command.params, placeholderSource,
                                           result.parameters, result.arguments, ids).toMap();
        result.commands.push_back(command);
    }
    return result;
}

QVector<EventCommand> instantiateCommandTemplate(
    const CommandTemplate& commandTemplate, const QVariantMap& arguments)
{
    QVector<EventCommand> commands;
    commands.reserve(commandTemplate.commands.size());
    for (EventCommand command : commandTemplate.commands) {
        command.params = instantiateValue(command.params, commandTemplate, arguments).toMap();
        commands.push_back(command);
    }
    return commands;
}

ExtractedCommonEvent buildExtractedCommonEvent(
    const QVector<EventCommand>& commands, const QString& name, int number)
{
    const ParameterizedCommandBlock block = parameterizeCommandBlock(commands, QStringLiteral("commonValue"));
    ExtractedCommonEvent result;
    result.commonEvent.name = name.trimmed().left(128);
    if (result.commonEvent.name.isEmpty()) result.commonEvent.name = QStringLiteral("Fluxo reutilizável");
    result.commonEvent.category = QStringLiteral("Extraídos");
    result.commonEvent.description = QStringLiteral("Extraído pelo Event Editor a partir de um bloco de comandos.");
    result.commonEvent.number = qMax(1, number);
    result.commonEvent.commands = block.commands;
    for (const CommandTemplateParameter& source : block.parameters) {
        CommonEventParameter parameter;
        parameter.id = source.id;
        parameter.name = source.name;
        parameter.type = source.type;
        parameter.defaultValue = commonValueDefault(source.type);
        parameter.required = true;
        parameter.description = source.description;
        result.commonEvent.parameters.push_back(parameter);
    }
    result.callCommand.type = QStringLiteral("common.call");
    result.callCommand.params = {
        {QStringLiteral("commonId"), result.commonEvent.id},
        {QStringLiteral("number"), result.commonEvent.number},
        {QStringLiteral("arguments"), block.arguments},
        {QStringLiteral("returnTarget"), QVariantMap()}
    };
    return result;
}

} // namespace core
