#pragma once

#include "core/ProjectHealth.h"

#include <QStringList>
#include <QtGlobal>

namespace core { class Editor; }

namespace ui {

struct ExportPreflightResult
{
    core::ProjectHealthSnapshot health;
    QString sourcePlayerName;
    QString sourcePlayerPath;
    QStringList usedAssets;
    QStringList allAssets;
    QStringList missingUsedAssets;
    QStringList blockers;
    QStringList warnings;
    bool audioRequired = false;

    bool ready() const { return blockers.isEmpty(); }
    int unusedAssetCount() const { return qMax(0, allAssets.size() - usedAssets.size()); }
};

class ExportPreflight
{
public:
    /// Consome ProjectHealth + AssetDatabase + runtime payload. Nenhuma regra de
    /// validação estrutural é recriada aqui: erros de projeto vêm do snapshot.
    static ExportPreflightResult inspect(const core::Editor& editor);
};

} // namespace ui
