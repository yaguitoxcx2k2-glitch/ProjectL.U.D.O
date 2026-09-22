#pragma once

#include <QString>
#include <QVector>

namespace ui {

enum class ExportBuildProfile { Development, Testing, Release };

struct ExportBuildProfileDefaults {
    ExportBuildProfile profile = ExportBuildProfile::Release;
    QString id;
    QString displayName;
    bool portableZip = true;
    bool protectAssets = true;
    bool cleanupUnused = true;
};

class ExportBuildProfilePolicy {
public:
    static QVector<ExportBuildProfileDefaults> profiles();
    static ExportBuildProfileDefaults profile(ExportBuildProfile value);
    static ExportBuildProfileDefaults profileFromId(const QString& id);
};

} // namespace ui
