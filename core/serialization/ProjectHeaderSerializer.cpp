#include "ProjectHeaderSerializer.h"

#include "core/Editor.h"
#include "core/RpgMakerTarget.h"
#include "core/Version.h"

#include <QJsonArray>
#include <QList>
#include <algorithm>

namespace core::serialization {

QJsonObject writeProjectHeader(const Editor& editor)
{
    // ProjectFormat 3 continua compatível. A engine-alvo é um metadado
    // adicional e opcional: projetos antigos sem targetEngine abrem como MZ.
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("LudoMapProject");
    root[QStringLiteral("formatVersion")] = version::ProjectFormat;
    root[QStringLiteral("editorVersion")] = QString::fromLatin1(version::Editor);
    root[QStringLiteral("projectKind")] = rpgMakerProjectKind(editor.rpgMakerEngine);
    root[QStringLiteral("targetEngine")] = rpgMakerEngineId(editor.rpgMakerEngine);
    root[QStringLiteral("projectName")] = editor.projectName;
    root[QStringLiteral("projectId")] = editor.projectId;
    if (!editor.rpgMakerProjectRoot.isEmpty())
        root[QStringLiteral("rpgMakerProjectRoot")] = editor.rpgMakerProjectRoot;
    if (editor.rpgMakerStructurePending)
        root[QStringLiteral("rpgMakerStructurePending")] = true;
    if (!editor.rpgMakerPendingDeletedMapIds.isEmpty()) {
        QJsonArray pendingDeletes;
        QList<int> ids = editor.rpgMakerPendingDeletedMapIds.values();
        std::sort(ids.begin(), ids.end());
        for (int id : ids) if (id > 0) pendingDeletes.append(id);
        if (!pendingDeletes.isEmpty())
            root[QStringLiteral("rpgMakerPendingDeletedMapIds")] = pendingDeletes;
    }
    root[QStringLiteral("activeMapDocIdx")] = qMax(0, editor.activeDocIdx);
    return root;
}

void applyProjectIdentity(Editor& editor, const QJsonObject& root)
{
    editor.projectName = root.value(QStringLiteral("projectName")).toString(QStringLiteral("Meu projeto"));
    editor.projectId = root.value(QStringLiteral("projectId")).toString();
    const QString engineToken = root.value(QStringLiteral("targetEngine")).toString(
        root.value(QStringLiteral("projectKind")).toString());
    editor.rpgMakerEngine = rpgMakerEngineFromId(engineToken, RpgMakerEngine::MZ);
    editor.rpgMakerProjectRoot = root.value(QStringLiteral("rpgMakerProjectRoot")).toString();
    editor.rpgMakerStructurePending = root.value(QStringLiteral("rpgMakerStructurePending")).toBool(false);
    editor.rpgMakerPendingDeletedMapIds.clear();
    for (const QJsonValue& value : root.value(QStringLiteral("rpgMakerPendingDeletedMapIds")).toArray()) {
        const int id = value.toInt();
        if (id > 0) editor.rpgMakerPendingDeletedMapIds.insert(id);
    }
    if (!editor.rpgMakerPendingDeletedMapIds.isEmpty()) editor.rpgMakerStructurePending = true;
}

} // namespace core::serialization
