#include "LegacyProjectImporter.h"

#include "core/io/MapProjectPayload.h"

namespace core::legacy {

bool isLegacyEngineProject(const QJsonObject& root)
{
    return root.value(QStringLiteral("format")).toString() == QLatin1String("LudoEngineProject");
}

QJsonObject extractMapAuthoringPayload(const QJsonObject& root, bool* changed)
{
    bool sanitized = false;
    QJsonObject out = io::mapproject::sanitize(root, &sanitized);

    // A identidade do projeto continua sendo reaproveitada, mas a sessão em
    // memória passa a obedecer imediatamente ao contrato Map Editor.
    if (out.value(QStringLiteral("format")).toString() != QLatin1String("LudoMapProject")) {
        out[QStringLiteral("format")] = QStringLiteral("LudoMapProject");
        sanitized = true;
    }
    // Projetos da antiga LUDO Engine não carregavam engine-alvo. Mantemos MZ
    // como padrão de migração para preservar exatamente o comportamento antigo.
    const QString kind = out.value(QStringLiteral("projectKind")).toString();
    if (kind != QLatin1String("rpg-maker-mz-map") && kind != QLatin1String("rpg-maker-mv-map")) {
        out[QStringLiteral("projectKind")] = QStringLiteral("rpg-maker-mz-map");
        out[QStringLiteral("targetEngine")] = QStringLiteral("mz");
        sanitized = true;
    } else if (!out.contains(QStringLiteral("targetEngine"))) {
        out[QStringLiteral("targetEngine")] = kind.contains(QStringLiteral("-mv-"))
                                                  ? QStringLiteral("mv") : QStringLiteral("mz");
        sanitized = true;
    }

    if (changed) *changed = sanitized;
    return out;
}

} // namespace core::legacy
