#pragma once

#include <QString>
#include <QStringList>

namespace core {
class Editor;

struct CommonEventPackageSummary {
    int commonEvents = 0;
    int switches = 0;
    int variables = 0;
    int strings = 0;
    int customDatabases = 0;
    int plugins = 0;
    int maps = 0;
    int databaseRecords = 0;
    int tilesets = 0;
    int footstepSurfaces = 0;
    int assets = 0;
    qint64 assetBytes = 0;
    QStringList warnings;
};

struct CommonEventPackageImportResult : CommonEventPackageSummary {
    QString rootCommonEventId;
    QString rootCommonEventName;
};

/// Exporta um Evento Comum e suas dependências transitivas para um único
/// arquivo JSON portátil `.ludocommon`. Assets abaixo de Assets/ são embutidos
/// em base64; mapas externos são declarados e remapeados por ID/nome no import.
bool exportCommonEventPackage(const Editor& ed, const QString& commonEventId,
                              const QString& path, QString* error = nullptr,
                              CommonEventPackageSummary* summary = nullptr);

/// Importa sem sobrescrever dados incompatíveis. IDs estáveis são preservados
/// quando livres e remapeados quando há colisão; todas as referências internas
/// são reescritas na mesma operação.
bool importCommonEventPackage(Editor& ed, const QString& path, QString* error = nullptr,
                              CommonEventPackageImportResult* result = nullptr);

} // namespace core
