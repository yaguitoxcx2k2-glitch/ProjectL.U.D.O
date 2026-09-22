#include "ProjectHeaderSerializer.h"

#include "core/Editor.h"
#include "core/RpgMakerTarget.h"
#include "core/Version.h"

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
}

} // namespace core::serialization
