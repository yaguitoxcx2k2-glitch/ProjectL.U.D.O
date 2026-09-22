#pragma once

#include <QJsonObject>
#include <QStringList>

namespace core {

struct AssetWorkflowMetadata {
    bool favorite = false;
    QStringList tags;
};

AssetWorkflowMetadata assetWorkflowMetadata(const QJsonObject& metadata);
QStringList normalizeAssetWorkflowTags(const QStringList& tags, int maxTags = 16, int maxLength = 40);

} // namespace core
