#include "NoCodePlugin.h"
#include "CommandRegistry.h"
#include "PluginCompatibility.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace core {
namespace {

bool validId(const QString& id)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_.-]{1,80}$"));
    return expression.match(id).hasMatch();
}

QVariant expandValue(const QVariant& value, const QVariantMap& arguments)
{
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap source = value.toMap();
        for (auto it = source.cbegin(); it != source.cend(); ++it)
            result[it.key()] = expandValue(it.value(), arguments);
        return result;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList result;
        for (const QVariant& entry : value.toList()) result.push_back(expandValue(entry, arguments));
        return result;
    }
    if (value.typeId() != QMetaType::QString) return value;
    QString text = value.toString();
    static const QRegularExpression exact(QStringLiteral("^\\$\\{([A-Za-z0-9_.-]+)\\}$"));
    const QRegularExpressionMatch match = exact.match(text);
    if (match.hasMatch() && arguments.contains(match.captured(1)))
        return arguments.value(match.captured(1));
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it)
        text.replace(QStringLiteral("${%1}").arg(it.key()), it.value().toString());
    return text;
}

bool parseBoolValue(const QVariant& value, bool* ok)
{
    if (value.typeId() == QMetaType::Bool) { if (ok) *ok = true; return value.toBool(); }
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("on") || text == QLatin1String("yes") || text == QLatin1String("1")) { if (ok) *ok = true; return true; }
    if (text == QLatin1String("false") || text == QLatin1String("off") || text == QLatin1String("no") || text == QLatin1String("0")) { if (ok) *ok = true; return false; }
    if (ok) *ok = false; return false;
}

QStringList normalizedRequiredTypes(const NoCodePlugin& plugin)
{
    QSet<QString> types;
    for (const PluginCommand& command : plugin.commands)
        for (const EventCommand& event : command.commands)
            if (!event.type.trimmed().isEmpty()) types.insert(event.type.trimmed());
    QStringList result = types.values();
    std::sort(result.begin(), result.end());
    return result;
}

bool dependencyChainReady(const QVector<NoCodePlugin>& plugins, const NoCodePlugin& plugin,
                          QSet<QString>* visiting, QSet<QString>* visited,
                          PluginInvocationStatus* status, QString* error)
{
    if (visited->contains(plugin.id)) return true;
    if (visiting->contains(plugin.id)) {
        if (status) *status = PluginInvocationStatus::DependencyCycle;
        if (error) *error = QObject::tr("As dependências da extensão formam um ciclo envolvendo %1.").arg(plugin.name);
        return false;
    }
    visiting->insert(plugin.id);
    for (const QString& dependencyId : plugin.dependencies) {
        const NoCodePlugin* dependency = noCodePluginById(plugins, dependencyId);
        if (!dependency) {
            if (status) *status = PluginInvocationStatus::MissingDependency;
            if (error) *error = QObject::tr("A dependência %1 não está instalada.").arg(dependencyId);
            visiting->remove(plugin.id);
            return false;
        }
        if (!dependency->enabled) {
            if (status) *status = PluginInvocationStatus::DisabledDependency;
            if (error) *error = QObject::tr("A dependência %1 está desativada.").arg(dependency->name);
            visiting->remove(plugin.id);
            return false;
        }
        if (!dependencyChainReady(plugins, *dependency, visiting, visited, status, error)) {
            visiting->remove(plugin.id);
            return false;
        }
    }
    visiting->remove(plugin.id);
    visited->insert(plugin.id);
    return true;
}

} // namespace

bool isSafePluginCommandType(const QString& type)
{
    // Bloco A / 3.23.0: a política de segurança mora no CommandRegistry.
    // Isso evita que um comando novo seja adicionado ao runtime/editor e
    // esquecido numa segunda allowlist exclusiva dos plugins.
    return CommandRegistry::isPluginSafe(type);
}

bool validatePluginCommand(const PluginCommand& command, QStringList* errors)
{
    QStringList local;
    const auto fail=[&](const QString& text){local.push_back(text);};
    if(!validId(command.id)||command.name.trimmed().isEmpty())
        fail(QObject::tr("O comando precisa de ID seguro e nome."));
    if(command.fields.size()>32)fail(QObject::tr("O comando excedeu 32 campos visuais."));
    QSet<QString> fieldIds;
    static const QSet<QString> fieldTypes={
        QStringLiteral("text"),QStringLiteral("number"),QStringLiteral("integer"),
        QStringLiteral("boolean"),QStringLiteral("choice"),QStringLiteral("select"),
        QStringLiteral("asset"),QStringLiteral("color"),QStringLiteral("expression"),
        QStringLiteral("switch"),QStringLiteral("variable"),QStringLiteral("map"),
        QStringLiteral("actor"),QStringLiteral("item"),QStringLiteral("troop"),
        QStringLiteral("quest"),QStringLiteral("state"),QStringLiteral("skill"),
        QStringLiteral("animation")};
    for(const PluginField& field:command.fields){
        if(!validId(field.id)||fieldIds.contains(field.id)||field.label.trimmed().isEmpty())
            fail(QObject::tr("Há um campo obrigatório com ID/rótulo inválido ou duplicado."));
        if(!fieldTypes.contains(field.type))fail(QObject::tr("Tipo de campo desconhecido: %1").arg(field.type));
        if(field.minimum>field.maximum)fail(QObject::tr("Faixa inválida no campo %1.").arg(field.label));
        if((field.type==QLatin1String("choice")||field.type==QLatin1String("select"))&&field.options.isEmpty())
            fail(QObject::tr("O campo %1 precisa de opções.").arg(field.label));
        if(field.required&&!field.defaultValue.isValid())
            fail(QObject::tr("O campo obrigatório %1 precisa de valor inicial.").arg(field.label));
        fieldIds.insert(field.id);
    }
    if(command.commands.isEmpty()||command.commands.size()>500)
        fail(QObject::tr("A expansão precisa conter de 1 a 500 comandos nativos."));
    static const QRegularExpression placeholder(QStringLiteral("\\$\\{([A-Za-z0-9_.-]+)\\}"));
    for(const EventCommand& event:command.commands){
        if(!isSafePluginCommandType(event.type))
            fail(QObject::tr("Comando não permitido na expansão: %1").arg(event.type));
        const QString json=QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(event.params)).toJson(QJsonDocument::Compact));
        auto it=placeholder.globalMatch(json);while(it.hasNext()){const QString id=it.next().captured(1);if(!fieldIds.contains(id))fail(QObject::tr("Placeholder sem campo declarado: %1").arg(id));}
    }
    local.removeDuplicates();if(errors)*errors=local;return local.isEmpty();
}

QJsonObject noCodePluginToJson(const NoCodePlugin& plugin)
{
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("LudoNoCodePlugin")},
                     {QStringLiteral("formatVersion"), NoCodePluginFormatVersion},
                     {QStringLiteral("commandContractVersion"), NoCodePluginCommandContractVersion},
                     {QStringLiteral("commandContractId"), CommandRegistry::contractId()},
                     {QStringLiteral("apiVersion"), plugin.apiVersion},
                     {QStringLiteral("id"), plugin.id},
                     {QStringLiteral("name"), plugin.name}, {QStringLiteral("version"), plugin.version},
                     {QStringLiteral("author"), plugin.author},
                     {QStringLiteral("description"), plugin.description},
                     {QStringLiteral("enabled"), plugin.enabled}};
    if(!plugin.capabilities.isEmpty()){QJsonArray caps;for(const QString& capability:plugin.capabilities)caps.append(capability);root[QStringLiteral("capabilities")]=caps;}
    QJsonArray dependencies;for(const QString& id:plugin.dependencies)dependencies.append(id);
    if(!dependencies.isEmpty())root[QStringLiteral("dependencies")]=dependencies;
    QJsonArray requiredTypes;for(const QString& type:normalizedRequiredTypes(plugin))requiredTypes.append(type);
    root[QStringLiteral("requiredCommandTypes")]=requiredTypes;
    QJsonArray commands;
    for (const PluginCommand& command : plugin.commands) {
        QJsonObject object{{QStringLiteral("id"), command.id}, {QStringLiteral("name"), command.name},
                           {QStringLiteral("category"), command.category},
                           {QStringLiteral("description"), command.description},
                           {QStringLiteral("iconPath"),command.iconPath},
                           {QStringLiteral("shortcutKey"),command.shortcutKey},
                           {QStringLiteral("showInCatalog"),command.showInCatalog}};
        QJsonArray tags;for(const QString& tag:command.tags)tags.append(tag);object[QStringLiteral("tags")]=tags;
        QJsonArray fields;
        for (const PluginField& field : command.fields) {
            QJsonObject item{{QStringLiteral("id"), field.id}, {QStringLiteral("label"), field.label},
                             {QStringLiteral("type"), field.type},
                             {QStringLiteral("default"), QJsonValue::fromVariant(field.defaultValue)},
                             {QStringLiteral("minimum"), field.minimum},
                             {QStringLiteral("maximum"), field.maximum}};
            item[QStringLiteral("required")]=field.required;
            QJsonArray options;for(const QString& option:field.options)options.append(option);
            if(!options.isEmpty())item[QStringLiteral("options")]=options;
            fields.append(item);
        }
        object[QStringLiteral("fields")] = fields;
        QJsonArray templates;
        for (const EventCommand& event : command.commands)
            templates.append(QJsonObject{{QStringLiteral("type"), event.type},
                                         {QStringLiteral("params"), QJsonObject::fromVariantMap(event.params)}});
        object[QStringLiteral("commands")] = templates;
        commands.append(object);
    }
    root[QStringLiteral("commands")] = commands;
    return root;
}

bool noCodePluginFromJson(const QJsonObject& root, NoCodePlugin* output, QString* error)
{
    if (!output) return false;
    const int formatVersion = root.value(QStringLiteral("formatVersion")).toInt(1);
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("LudoNoCodePlugin") ||
        formatVersion < 1 || formatVersion > NoCodePluginFormatVersion) {
        if (error) *error = QObject::tr("O arquivo não é uma extensão visual compatível com a LUDO.");
        return false;
    }
    const int commandContractVersion = formatVersion >= 2
        ? root.value(QStringLiteral("commandContractVersion")).toInt(0) : 1;
    if (commandContractVersion < 1 || commandContractVersion > NoCodePluginCommandContractVersion) {
        if (error) *error = QObject::tr("O plugin exige uma versão de contrato de comandos mais nova que esta Ludo.");
        return false;
    }
    if (formatVersion >= 2 && root.value(QStringLiteral("commandContractId")).toString() != CommandRegistry::contractId()) {
        if (error) *error = QObject::tr("O plugin declara um contrato de comandos incompatível com esta Ludo.");
        return false;
    }
    NoCodePlugin plugin;
    plugin.sourceFormatVersion = formatVersion;
    plugin.commandContractVersion = commandContractVersion;
    plugin.apiVersion = root.value(QStringLiteral("apiVersion")).toInt(1);
    for(const QJsonValue& capability:root.value(QStringLiteral("capabilities")).toArray()) if(capability.isString()) plugin.capabilities.push_back(capability.toString().trimmed());
    plugin.capabilities.removeDuplicates();
    plugin.id = root.value(QStringLiteral("id")).toString();
    plugin.name = root.value(QStringLiteral("name")).toString();
    plugin.version = root.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
    plugin.author = root.value(QStringLiteral("author")).toString();
    plugin.description = root.value(QStringLiteral("description")).toString();
    plugin.enabled = root.contains(QStringLiteral("enabled"))
        ? root.value(QStringLiteral("enabled")).toBool(true) : true;
    if (!validId(plugin.id) || plugin.name.trimmed().isEmpty()) {
        if (error) *error = QObject::tr("O plugin precisa de ID seguro e nome.");
        return false;
    }
    for(const QJsonValue& value:root.value(QStringLiteral("dependencies")).toArray()){
        const QString dependency=value.toString();
        if(!validId(dependency)||dependency==plugin.id||plugin.dependencies.contains(dependency)||plugin.dependencies.size()>=32){
            if(error)*error=QObject::tr("A lista de dependências do plugin é inválida.");return false;
        }
        plugin.dependencies.push_back(dependency);
    }
    QSet<QString> commandIds;
    const QJsonArray commandArray = root.value(QStringLiteral("commands")).toArray();
    if (commandArray.isEmpty() || commandArray.size() > 200) {
        if (error) *error = QObject::tr("O plugin precisa ter entre 1 e 200 comandos.");
        return false;
    }
    for (const QJsonValue& value : commandArray) {
        const QJsonObject object = value.toObject();
        PluginCommand command;
        command.id = object.value(QStringLiteral("id")).toString();
        command.name = object.value(QStringLiteral("name")).toString();
        command.category = object.value(QStringLiteral("category")).toString(QObject::tr("Plugins"));
        command.description = object.value(QStringLiteral("description")).toString();
        command.iconPath=object.value(QStringLiteral("iconPath")).toString();
        command.shortcutKey=object.value(QStringLiteral("shortcutKey")).toString();
        command.showInCatalog=object.value(QStringLiteral("showInCatalog")).toBool(true);
        for(const QJsonValue& tag:object.value(QStringLiteral("tags")).toArray())if(tag.isString())command.tags.push_back(tag.toString().trimmed());
        if (!validId(command.id) || command.name.trimmed().isEmpty() || commandIds.contains(command.id)) {
            if (error) *error = QObject::tr("Há um comando de plugin com ID/nome inválido ou duplicado.");
            return false;
        }
        commandIds.insert(command.id);
        QSet<QString> fieldIds;
        const QJsonArray fieldArray = object.value(QStringLiteral("fields")).toArray();
        if (fieldArray.size() > 32) {
            if (error) *error = QObject::tr("Um comando de plugin excedeu 32 campos visuais.");
            return false;
        }
        for (const QJsonValue& fieldValue : fieldArray) {
            const QJsonObject fieldObject = fieldValue.toObject();
            PluginField field;
            field.id = fieldObject.value(QStringLiteral("id")).toString();
            field.label = fieldObject.value(QStringLiteral("label")).toString(field.id);
            field.type = fieldObject.value(QStringLiteral("type")).toString(QStringLiteral("text"));
            field.defaultValue = fieldObject.value(QStringLiteral("default")).toVariant();
            field.minimum = fieldObject.value(QStringLiteral("minimum")).toInt(-999999999);
            field.maximum = fieldObject.value(QStringLiteral("maximum")).toInt(999999999);
            field.required=fieldObject.value(QStringLiteral("required")).toBool(true);
            for (const QJsonValue& option : fieldObject.value(QStringLiteral("options")).toArray())
                if (option.isString()) field.options.push_back(option.toString());
            static const QSet<QString> fieldTypes = {
                QStringLiteral("text"), QStringLiteral("number"), QStringLiteral("integer"), QStringLiteral("boolean"),
                QStringLiteral("choice"), QStringLiteral("select"), QStringLiteral("asset"), QStringLiteral("color"), QStringLiteral("expression"), QStringLiteral("switch"), QStringLiteral("variable"),
                QStringLiteral("map"), QStringLiteral("actor"), QStringLiteral("item"),
                QStringLiteral("troop"), QStringLiteral("quest"), QStringLiteral("state"),
                QStringLiteral("skill"), QStringLiteral("animation")
            };
            if (!validId(field.id) || fieldIds.contains(field.id) ||
                !fieldTypes.contains(field.type) || field.minimum > field.maximum ||
                ((field.type == QLatin1String("select")||field.type==QLatin1String("choice")) && field.options.isEmpty())) {
                if (error) *error = QObject::tr("Há um campo visual inválido ou duplicado.");
                return false;
            }
            fieldIds.insert(field.id);
            command.fields.push_back(field);
        }
        for (const QJsonValue& templateValue : object.value(QStringLiteral("commands")).toArray()) {
            const QJsonObject templateObject = templateValue.toObject();
            EventCommand event;
            event.type = templateObject.value(QStringLiteral("type")).toString();
            event.params = templateObject.value(QStringLiteral("params")).toObject().toVariantMap();
            if (!isSafePluginCommandType(event.type)) {
                if (error) *error = QObject::tr("O plugin tenta usar um comando não permitido: %1").arg(event.type);
                return false;
            }
            command.commands.push_back(event);
        }
        if (command.commands.isEmpty() || command.commands.size() > 500) {
            if (error) *error = QObject::tr("Um comando de plugin precisa expandir para 1 a 500 comandos nativos.");
            return false;
        }
        QStringList validationErrors;if(!validatePluginCommand(command,&validationErrors)){if(error)*error=validationErrors.join(QLatin1Char('\n'));return false;}
        plugin.commands.push_back(command);
    }
    if (formatVersion >= 2 && root.contains(QStringLiteral("requiredCommandTypes"))) {
        QStringList declared;
        for (const QJsonValue& value : root.value(QStringLiteral("requiredCommandTypes")).toArray())
            if (value.isString()) declared.push_back(value.toString().trimmed());
        declared.removeDuplicates(); std::sort(declared.begin(), declared.end());
        if (declared != normalizedRequiredTypes(plugin)) {
            if (error) *error = QObject::tr("O manifesto do plugin não corresponde aos comandos nativos realmente usados.");
            return false;
        }
    }
    QStringList packageErrors;
    if (!validateNoCodePlugin(plugin, &packageErrors)) {
        if (error) *error = packageErrors.join(QLatin1Char('\n'));
        return false;
    }
    *output = plugin;
    return true;
}

const NoCodePlugin* noCodePluginById(const QVector<NoCodePlugin>& plugins, const QString& id)
{
    for (const NoCodePlugin& plugin : plugins) if (plugin.id == id) return &plugin;
    return nullptr;
}

const PluginCommand* pluginCommandById(const NoCodePlugin& plugin, const QString& id)
{
    for (const PluginCommand& command : plugin.commands) if (command.id == id) return &command;
    return nullptr;
}

QVector<EventCommand> expandPluginCommand(const PluginCommand& command, const QVariantMap& arguments)
{
    QVariantMap values;
    for (const PluginField& field : command.fields)
        values[field.id] = arguments.contains(field.id) ? arguments.value(field.id) : field.defaultValue;
    QVector<EventCommand> result;
    result.reserve(command.commands.size());
    for (const EventCommand& source : command.commands) {
        EventCommand event;
        event.type = source.type;
        event.params = expandValue(source.params, values).toMap();
        result.push_back(event);
    }
    return result;
}

bool normalizePluginArguments(const PluginCommand& command, const QVariantMap& arguments,
                              QVariantMap* normalized, QStringList* errors)
{
    QVariantMap values; QStringList local;
    const auto fail=[&](const QString& message){local.push_back(message);};
    QSet<QString> known;
    for (const PluginField& field : command.fields) {
        known.insert(field.id);
        QVariant value = arguments.contains(field.id) ? arguments.value(field.id) : field.defaultValue;
        if ((!value.isValid() || value.isNull()) && field.required) {
            fail(QObject::tr("O argumento obrigatório %1 não foi informado.").arg(field.label));
            continue;
        }
        if (!value.isValid() || value.isNull()) { values[field.id] = value; continue; }
        if (field.type == QLatin1String("integer") || field.type == QLatin1String("switch") || field.type == QLatin1String("variable")) {
            bool ok=false; const qlonglong number=value.toLongLong(&ok);
            if(!ok) fail(QObject::tr("O argumento %1 precisa ser inteiro.").arg(field.label));
            else if(field.type==QLatin1String("integer")&&(number<field.minimum||number>field.maximum))
                fail(QObject::tr("O argumento %1 está fora da faixa %2…%3.").arg(field.label).arg(field.minimum).arg(field.maximum));
            else values[field.id]=number;
        } else if (field.type == QLatin1String("number")) {
            bool ok=false; const double number=value.toDouble(&ok);
            if(!ok||!std::isfinite(number)) fail(QObject::tr("O argumento %1 precisa ser numérico.").arg(field.label));
            else if(number<field.minimum||number>field.maximum)
                fail(QObject::tr("O argumento %1 está fora da faixa %2…%3.").arg(field.label).arg(field.minimum).arg(field.maximum));
            else values[field.id]=number;
        } else if (field.type == QLatin1String("boolean")) {
            bool ok=false; const bool flag=parseBoolValue(value,&ok);
            if(!ok) fail(QObject::tr("O argumento %1 precisa ser booleano.").arg(field.label));
            else values[field.id]=flag;
        } else if (field.type == QLatin1String("choice") || field.type == QLatin1String("select")) {
            const QString choice=value.toString();
            if(!field.options.contains(choice)) fail(QObject::tr("O argumento %1 usa uma opção inexistente.").arg(field.label));
            else values[field.id]=choice;
        } else {
            values[field.id]=value.toString();
        }
    }
    for (auto it=arguments.cbegin(); it!=arguments.cend(); ++it)
        if(!known.contains(it.key())) fail(QObject::tr("Argumento não declarado no contrato do plugin: %1").arg(it.key()));
    local.removeDuplicates(); if(normalized)*normalized=values; if(errors)*errors=local; return local.isEmpty();
}

bool validateNoCodePlugin(const NoCodePlugin& plugin, QStringList* errors)
{
    QStringList local; const auto fail=[&](const QString& message){local.push_back(message);};
    if(plugin.commandContractVersion<1||plugin.commandContractVersion>NoCodePluginCommandContractVersion)
        fail(QObject::tr("Versão do contrato de comandos do plugin não suportada."));
    const PluginCompatibilityReport compatibility=PluginCompatibility::evaluate(plugin);
    if(!compatibility.compatible) fail(compatibility.message);
    if(!validId(plugin.id)||plugin.name.trimmed().isEmpty()) fail(QObject::tr("O plugin precisa de ID seguro e nome."));
    QSet<QString> dependencies;
    for(const QString& dependency:plugin.dependencies){
        if(!validId(dependency)||dependency==plugin.id||dependencies.contains(dependency))
            fail(QObject::tr("Dependência inválida ou duplicada: %1").arg(dependency));
        dependencies.insert(dependency);
    }
    if(plugin.dependencies.size()>32)fail(QObject::tr("O plugin excedeu 32 dependências."));
    if(plugin.commands.isEmpty()||plugin.commands.size()>200)fail(QObject::tr("O plugin precisa ter entre 1 e 200 comandos."));
    QSet<QString> commandIds;
    for(const PluginCommand& command:plugin.commands){
        if(commandIds.contains(command.id))fail(QObject::tr("ID de comando duplicado: %1").arg(command.id));
        commandIds.insert(command.id);
        QStringList commandErrors;if(!validatePluginCommand(command,&commandErrors))
            for(const QString& message:commandErrors)fail(QObject::tr("%1: %2").arg(command.name,message));
    }
    local.removeDuplicates();if(errors)*errors=local;return local.isEmpty();
}

QString pluginInvocationStatusId(PluginInvocationStatus status)
{
    switch(status){
    case PluginInvocationStatus::Ready:return QStringLiteral("ready");
    case PluginInvocationStatus::MissingPlugin:return QStringLiteral("missingPlugin");
    case PluginInvocationStatus::DisabledPlugin:return QStringLiteral("disabledPlugin");
    case PluginInvocationStatus::MissingDependency:return QStringLiteral("missingDependency");
    case PluginInvocationStatus::DisabledDependency:return QStringLiteral("disabledDependency");
    case PluginInvocationStatus::DependencyCycle:return QStringLiteral("dependencyCycle");
    case PluginInvocationStatus::MissingCommand:return QStringLiteral("missingCommand");
    case PluginInvocationStatus::InvalidDefinition:return QStringLiteral("invalidDefinition");
    case PluginInvocationStatus::InvalidArguments:return QStringLiteral("invalidArguments");
    }
    return QStringLiteral("invalidDefinition");
}

PluginInvocationResolution resolvePluginInvocation(const QVector<NoCodePlugin>& plugins,
                                                   const QString& pluginId,
                                                   const QString& commandId,
                                                   const QVariantMap& arguments)
{
    PluginInvocationResolution result;result.pluginId=pluginId;result.commandId=commandId;
    const NoCodePlugin* plugin=noCodePluginById(plugins,pluginId);
    if(!plugin){result.status=PluginInvocationStatus::MissingPlugin;result.error=QObject::tr("O plugin %1 não está instalado.").arg(pluginId);return result;}
    if(!plugin->enabled){result.status=PluginInvocationStatus::DisabledPlugin;result.error=QObject::tr("O plugin %1 está desativado.").arg(plugin->name);return result;}
    QStringList packageErrors;
    if(!validateNoCodePlugin(*plugin,&packageErrors)){result.status=PluginInvocationStatus::InvalidDefinition;result.error=packageErrors.join(QLatin1Char('\n'));return result;}
    QSet<QString> visiting,visited;
    if(!dependencyChainReady(plugins,*plugin,&visiting,&visited,&result.status,&result.error))return result;
    const PluginCommand* command=pluginCommandById(*plugin,commandId);
    if(!command){result.status=PluginInvocationStatus::MissingCommand;result.error=QObject::tr("O comando %1 não existe no plugin %2.").arg(commandId,plugin->name);return result;}
    QStringList definitionErrors;if(!validatePluginCommand(*command,&definitionErrors)){result.status=PluginInvocationStatus::InvalidDefinition;result.error=definitionErrors.join(QLatin1Char('\n'));return result;}
    QStringList argumentErrors;if(!normalizePluginArguments(*command,arguments,&result.normalizedArguments,&argumentErrors)){result.status=PluginInvocationStatus::InvalidArguments;result.error=argumentErrors.join(QLatin1Char('\n'));return result;}
    result.expandedCommands=expandPluginCommand(*command,result.normalizedArguments);result.status=PluginInvocationStatus::Ready;return result;
}

QStringList pluginRequiredCommandTypes(const NoCodePlugin& plugin)
{
    return normalizedRequiredTypes(plugin);
}

} // namespace core
