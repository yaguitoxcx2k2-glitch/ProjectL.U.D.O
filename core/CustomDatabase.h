#pragma once

#include "Model.h"

#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace core {

enum class CustomDatabaseMode { ReadOnly, Runtime };
enum class CustomDatabaseFieldType { Number, Boolean, Text, RecordReference };

QString customDatabaseModeId(CustomDatabaseMode mode);
CustomDatabaseMode customDatabaseModeFromId(const QString& id);
QString customDatabaseModeLabel(CustomDatabaseMode mode);
QString customDatabaseFieldTypeId(CustomDatabaseFieldType type);
CustomDatabaseFieldType customDatabaseFieldTypeFromId(const QString& id);
QString customDatabaseFieldTypeLabel(CustomDatabaseFieldType type);

struct CustomDatabaseField {
    QString id = idGen();
    QString name;
    CustomDatabaseFieldType type = CustomDatabaseFieldType::Number;
    QVariant defaultValue = 0;
    /// Somente para RecordReference. Guarda o ID estável do banco alvo.
    QString referenceDatabaseId;
};

struct CustomDatabaseRecord {
    QString id = idGen();
    int number = 1;
    QString name;
    QString description;
    /// Valores indexados pelo ID estável de CustomDatabaseField.
    QVariantMap values;
};

struct CustomDatabaseDefinition {
    QString id = idGen();
    int number = 1;
    QString name;
    QString description;
    CustomDatabaseMode mode = CustomDatabaseMode::ReadOnly;
    QVector<CustomDatabaseField> fields;
    QVector<CustomDatabaseRecord> records;
};

const CustomDatabaseDefinition* customDatabaseById(const QVector<CustomDatabaseDefinition>& databases,
                                                    const QString& id);
CustomDatabaseDefinition* customDatabaseById(QVector<CustomDatabaseDefinition>& databases,
                                              const QString& id);
const CustomDatabaseField* customDatabaseFieldById(const CustomDatabaseDefinition& database,
                                                    const QString& id);
CustomDatabaseField* customDatabaseFieldById(CustomDatabaseDefinition& database,
                                              const QString& id);
const CustomDatabaseRecord* customDatabaseRecordById(const CustomDatabaseDefinition& database,
                                                      const QString& id);
CustomDatabaseRecord* customDatabaseRecordById(CustomDatabaseDefinition& database,
                                                const QString& id);
QVariant customDatabaseFieldDefault(const CustomDatabaseField& field);
QVariant normalizeCustomDatabaseValue(const QVariant& value, const CustomDatabaseField& field);
QVariant effectiveCustomDatabaseValue(const CustomDatabaseDefinition& database,
                                      const CustomDatabaseRecord& record,
                                      const CustomDatabaseField& field);
void normalizeCustomDatabaseDefinition(CustomDatabaseDefinition& database);

} // namespace core
