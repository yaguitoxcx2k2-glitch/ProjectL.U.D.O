#include "ProjectSchema.h"

#include <QObject>

namespace core::serialization {

bool validateProjectEnvelope(const QJsonObject& root, int supportedProjectFormat,
                             int* loadedProjectFormat, bool* migrationRequired,
                             QString* error)
{
    const QString signature = root.value(QStringLiteral("format")).toString();
    const bool legacyEngineProject = signature == QLatin1String("LudoEngineProject");
    const bool mapEditorProject = signature == QLatin1String("LudoMapProject");
    if (!signature.isEmpty() && !legacyEngineProject && !mapEditorProject) {
        if (error) *error = QObject::tr("Este arquivo não é um projeto reconhecido pelo LUDO Map Editor.");
        return false;
    }

    int loaded = 1;
    // Projetos da antiga LUDO Engine continuam abrindo para migração. Ao salvar
    // novamente, passam ao formato LudoMapProject e os dados de gameplay não
    // são mais persistidos.
    bool migrate = !root.contains(QStringLiteral("formatVersion")) || legacyEngineProject;
    if (root.contains(QStringLiteral("formatVersion"))) {
        if (!root.value(QStringLiteral("formatVersion")).isDouble()) {
            if (error) *error = QObject::tr("ProjectFormat inválido no arquivo.");
            return false;
        }
        const double raw = root.value(QStringLiteral("formatVersion")).toDouble();
        loaded = int(raw);
        if (raw != double(loaded)) {
            if (error) *error = QObject::tr("ProjectFormat inválido no arquivo: a versão deve ser um inteiro.");
            return false;
        }
        if (loaded < 1) {
            if (error) *error = QObject::tr("ProjectFormat inválido: %1.").arg(loaded);
            return false;
        }
        if (loaded > supportedProjectFormat) {
            if (error) *error = QObject::tr("Este projeto usa ProjectFormat %1, mas esta versão da LUDO suporta até %2.")
                                    .arg(loaded).arg(supportedProjectFormat);
            return false;
        }
    }
    if (loaded < supportedProjectFormat) migrate = true;
    if (loadedProjectFormat) *loadedProjectFormat = loaded;
    if (migrationRequired) *migrationRequired = migrate;
    return true;
}

} // namespace core::serialization
