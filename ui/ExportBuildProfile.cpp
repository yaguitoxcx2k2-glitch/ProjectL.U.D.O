#include "ExportBuildProfile.h"

namespace ui {

QVector<ExportBuildProfileDefaults> ExportBuildProfilePolicy::profiles()
{
    return {
        {ExportBuildProfile::Development, QStringLiteral("development"), QStringLiteral("Development — rápido para testar"), false, false, false},
        {ExportBuildProfile::Testing, QStringLiteral("testing"), QStringLiteral("Testing — validação e pacote portátil"), true, false, true},
        {ExportBuildProfile::Release, QStringLiteral("release"), QStringLiteral("Release — distribuição final"), true, true, true}
    };
}

ExportBuildProfileDefaults ExportBuildProfilePolicy::profile(ExportBuildProfile value)
{
    for (const ExportBuildProfileDefaults& current : profiles())
        if (current.profile == value) return current;
    return profiles().constLast();
}

ExportBuildProfileDefaults ExportBuildProfilePolicy::profileFromId(const QString& id)
{
    for (const ExportBuildProfileDefaults& current : profiles())
        if (current.id.compare(id.trimmed(), Qt::CaseInsensitive) == 0) return current;
    return profile(ExportBuildProfile::Release);
}

} // namespace ui
