// ============================================================================
// AssetPickerCatalog.h — catálogo compartilhado do Universal Asset Picker.
//
// RC2.62 / Bloco K. Mantém a lógica de contexto/categoria/pesquisa fora da UI,
// para todos os seletores consumirem o mesmo AssetWorkflow + AssetDatabase.
// ============================================================================
#pragma once

#include "AssetDatabase.h"
#include "AssetWorkflow.h"

#include <QString>
#include <QVector>

namespace core {

struct AssetPickerScope {
    QString mediaType = QStringLiteral("image"); ///< Map Editor: somente imagens de autoria
    QString initialCategoryId;          ///< categoria inicial; vazio = visão agregada
    QVector<AssetCategoryInfo> categories;
};

class AssetPickerCatalog
{
public:
    /// Resolve o escopo visual a partir do contexto de autoria (Tilesets, Autotiles, References).
    static AssetPickerScope scopeForContext(const QString& context,
                                            const QString& requiredMediaType = QString());

    /// Consulta registros já sincronizados no Asset Database. A categoria
    /// vazia representa todas as categorias permitidas pelo mediaType.
    static QVector<AssetRecord> query(const AssetDatabase& database,
                                      const QString& categoryId,
                                      const QString& mediaType,
                                      const QString& search = QString(),
                                      bool includeMissing = false);

    /// Nome amigável da visão agregada usado pelo Picker.
    static QString aggregateLabel(const QString& mediaType);

private:
    static QString categoryIdForContext(const QString& context);
};

} // namespace core
