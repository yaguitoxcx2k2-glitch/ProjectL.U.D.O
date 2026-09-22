#pragma once

#include <QJsonObject>
#include <QString>

namespace core::serialization {

struct ProjectSchemaVersions final
{
    int project = 1;
    int maps = 1;
    int events = 1;
    int database = 1;
    int ui = 1;
    int save = 1;
};

/// Valida somente a fronteira de versão/formato. A migração de conteúdo ainda
/// ocorre no loader legado e será extraída domínio a domínio sem alterar o JSON.
bool validateProjectEnvelope(const QJsonObject& root, int supportedProjectFormat,
                             int* loadedProjectFormat, bool* migrationRequired,
                             QString* error = nullptr);

} // namespace core::serialization
