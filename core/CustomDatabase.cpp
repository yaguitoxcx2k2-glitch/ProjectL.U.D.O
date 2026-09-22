#include "CustomDatabase.h"

#include <QCoreApplication>
#include <QMetaType>
#include <QSet>

namespace core {

QString customDatabaseModeId(CustomDatabaseMode mode)
{
    return mode == CustomDatabaseMode::Runtime ? QStringLiteral("runtime") : QStringLiteral("readonly");
}

CustomDatabaseMode customDatabaseModeFromId(const QString& id)
{
    return id.compare(QLatin1String("runtime"), Qt::CaseInsensitive) == 0
        ? CustomDatabaseMode::Runtime : CustomDatabaseMode::ReadOnly;
}

QString customDatabaseModeLabel(CustomDatabaseMode mode)
{
    return mode == CustomDatabaseMode::Runtime
        ? QCoreApplication::translate("CustomDatabase", "Editável durante o jogo")
        : QCoreApplication::translate("CustomDatabase", "Somente leitura");
}

QString customDatabaseFieldTypeId(CustomDatabaseFieldType type)
{
    switch (type) {
    case CustomDatabaseFieldType::Number: return QStringLiteral("number");
    case CustomDatabaseFieldType::Boolean: return QStringLiteral("boolean");
    case CustomDatabaseFieldType::Text: return QStringLiteral("text");
    case CustomDatabaseFieldType::RecordReference: return QStringLiteral("recordReference");
    }
    return QStringLiteral("number");
}

CustomDatabaseFieldType customDatabaseFieldTypeFromId(const QString& id)
{
    if (id.compare(QLatin1String("boolean"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("bool"), Qt::CaseInsensitive) == 0)
        return CustomDatabaseFieldType::Boolean;
    if (id.compare(QLatin1String("text"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("string"), Qt::CaseInsensitive) == 0)
        return CustomDatabaseFieldType::Text;
    if (id.compare(QLatin1String("recordReference"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("record"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("reference"), Qt::CaseInsensitive) == 0)
        return CustomDatabaseFieldType::RecordReference;
    return CustomDatabaseFieldType::Number;
}

QString customDatabaseFieldTypeLabel(CustomDatabaseFieldType type)
{
    switch (type) {
    case CustomDatabaseFieldType::Number:
        return QCoreApplication::translate("CustomDatabase", "Número");
    case CustomDatabaseFieldType::Boolean:
        return QCoreApplication::translate("CustomDatabase", "Booleano");
    case CustomDatabaseFieldType::Text:
        return QCoreApplication::translate("CustomDatabase", "Texto");
    case CustomDatabaseFieldType::RecordReference:
        return QCoreApplication::translate("CustomDatabase", "Referência para registro");
    }
    return QString();
}

const CustomDatabaseDefinition* customDatabaseById(const QVector<CustomDatabaseDefinition>& databases,
                                                    const QString& id)
{
    for (const CustomDatabaseDefinition& database : databases)
        if (database.id == id) return &database;
    return nullptr;
}

CustomDatabaseDefinition* customDatabaseById(QVector<CustomDatabaseDefinition>& databases,
                                              const QString& id)
{
    for (CustomDatabaseDefinition& database : databases)
        if (database.id == id) return &database;
    return nullptr;
}

const CustomDatabaseField* customDatabaseFieldById(const CustomDatabaseDefinition& database,
                                                    const QString& id)
{
    for (const CustomDatabaseField& field : database.fields)
        if (field.id == id) return &field;
    return nullptr;
}

CustomDatabaseField* customDatabaseFieldById(CustomDatabaseDefinition& database, const QString& id)
{
    for (CustomDatabaseField& field : database.fields)
        if (field.id == id) return &field;
    return nullptr;
}

const CustomDatabaseRecord* customDatabaseRecordById(const CustomDatabaseDefinition& database,
                                                      const QString& id)
{
    for (const CustomDatabaseRecord& record : database.records)
        if (record.id == id) return &record;
    return nullptr;
}

CustomDatabaseRecord* customDatabaseRecordById(CustomDatabaseDefinition& database, const QString& id)
{
    for (CustomDatabaseRecord& record : database.records)
        if (record.id == id) return &record;
    return nullptr;
}

QVariant customDatabaseFieldDefault(const CustomDatabaseField& field)
{
    switch (field.type) {
    case CustomDatabaseFieldType::Number: return 0;
    case CustomDatabaseFieldType::Boolean: return false;
    case CustomDatabaseFieldType::Text:
    case CustomDatabaseFieldType::RecordReference: return QString();
    }
    return QVariant();
}

QVariant normalizeCustomDatabaseValue(const QVariant& value, const CustomDatabaseField& field)
{
    switch (field.type) {
    case CustomDatabaseFieldType::Number:
        return value.toInt();
    case CustomDatabaseFieldType::Boolean:
        if (value.metaType().id() == QMetaType::QString) {
            const QString text = value.toString().trimmed().toCaseFolded();
            if (text == QLatin1String("1") || text == QLatin1String("true") ||
                text == QLatin1String("on") || text == QLatin1String("sim") ||
                text == QLatin1String("yes") || text == QLatin1String("ligado")) return true;
            if (text == QLatin1String("0") || text == QLatin1String("false") ||
                text == QLatin1String("off") || text == QLatin1String("nao") ||
                text == QStringLiteral("não") || text == QLatin1String("no") ||
                text == QLatin1String("desligado") || text.isEmpty()) return false;
        }
        return value.toBool();
    case CustomDatabaseFieldType::Text:
    case CustomDatabaseFieldType::RecordReference:
        return value.toString();
    }
    return QVariant();
}

QVariant effectiveCustomDatabaseValue(const CustomDatabaseDefinition&,
                                      const CustomDatabaseRecord& record,
                                      const CustomDatabaseField& field)
{
    const QVariant value = record.values.contains(field.id)
        ? record.values.value(field.id) : field.defaultValue;
    return normalizeCustomDatabaseValue(value.isValid() ? value : customDatabaseFieldDefault(field), field);
}

void normalizeCustomDatabaseDefinition(CustomDatabaseDefinition& database)
{
    if (database.id.isEmpty()) database.id = idGen();
    database.number = qMax(1, database.number);
    database.name = database.name.trimmed().left(128);
    database.description = database.description.left(4096);
    QSet<QString> fieldIds;
    for (int i = 0; i < database.fields.size(); ++i) {
        CustomDatabaseField& field = database.fields[i];
        if (field.id.isEmpty() || fieldIds.contains(field.id)) field.id = idGen();
        fieldIds.insert(field.id);
        field.name = field.name.trimmed().left(128);
        field.defaultValue = normalizeCustomDatabaseValue(
            field.defaultValue.isValid() ? field.defaultValue : customDatabaseFieldDefault(field), field);
        if (field.type != CustomDatabaseFieldType::RecordReference) field.referenceDatabaseId.clear();
    }
    QSet<QString> recordIds;
    for (int i = 0; i < database.records.size(); ++i) {
        CustomDatabaseRecord& record = database.records[i];
        if (record.id.isEmpty() || recordIds.contains(record.id)) record.id = idGen();
        recordIds.insert(record.id);
        record.number = qMax(1, record.number);
        record.name = record.name.trimmed().left(128);
        record.description = record.description.left(4096);
        QVariantMap normalized;
        for (const CustomDatabaseField& field : database.fields)
            normalized[field.id] = effectiveCustomDatabaseValue(database, record, field);
        record.values = normalized;
    }
}

} // namespace core
