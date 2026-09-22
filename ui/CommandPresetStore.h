#pragma once

#include <QVariantMap>
#include <QVector>

namespace ui {

struct CommandPreset {
    QString id;
    QString name;
    QString commandType;
    QVariantMap params;
    QString description;
};

QVector<CommandPreset> loadCommandPresets();
bool saveCommandPreset(const CommandPreset& preset);
bool removeCommandPreset(const QString& id);
CommandPreset commandPresetById(const QString& id);
bool exportCommandPresets(const QString& filePath, QString* error = nullptr);
bool importCommandPresets(const QString& filePath, QString* error = nullptr);

} // namespace ui
