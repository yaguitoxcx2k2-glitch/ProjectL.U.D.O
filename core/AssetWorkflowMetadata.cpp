#include "AssetWorkflowMetadata.h"

#include <QJsonArray>
#include <QtGlobal>

namespace core {

QStringList normalizeAssetWorkflowTags(const QStringList& tags, int maxTags, int maxLength)
{
    QStringList result;
    for (const QString& raw : tags) {
        const QString tag = raw.trimmed().left(qMax(1, maxLength));
        if (tag.isEmpty() || result.contains(tag, Qt::CaseInsensitive)) continue;
        result.push_back(tag);
        if (result.size() >= qMax(1, maxTags)) break;
    }
    return result;
}

AssetWorkflowMetadata assetWorkflowMetadata(const QJsonObject& metadata)
{
    AssetWorkflowMetadata result;
    result.favorite = metadata.value(QStringLiteral("workflowFavorite")).toBool(false);
    QStringList tags;
    for (const QJsonValue& value : metadata.value(QStringLiteral("workflowTags")).toArray())
        tags.push_back(value.toString());
    result.tags = normalizeAssetWorkflowTags(tags);
    return result;
}

} // namespace core
