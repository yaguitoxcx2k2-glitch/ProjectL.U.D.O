#pragma once
#include "Model.h"
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <QVector>
namespace core {
struct DatabaseRecord {QString id=idGen();int number=1;QString name,description;int icon=-1;QVariantMap data;};
QStringList databaseCategories();
QString databaseCategoryLabel(const QString&id);
void populateDefaultDatabase(QHash<QString,QVector<DatabaseRecord>>& database);
}
