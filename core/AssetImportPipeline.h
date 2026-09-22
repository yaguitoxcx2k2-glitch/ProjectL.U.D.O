// ============================================================================
// AssetImportPipeline.h — pipeline compartilhado de ingestão de assets 2D.
//
// Asset Import & Autotile Workflow 2.0: Browser e Universal Picker deixam de
// copiar imagens por caminhos paralelos. O perfil de importação descreve unidade de origem, escala física
// e filtro; o primeiro perfil exposto pela UI é pixel art 16 px -> 2x.
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QSize>
#include <QString>

namespace core {

struct AssetImportProfile {
    int sourcePixelUnit = 0;      ///< 0 = não especificado; 16 no perfil pixel-art inicial.
    int scale = 1;                ///< escala física aplicada ao arquivo importado.
    bool nearestNeighbor = true;  ///< pixel art nunca usa interpolação suave.

    bool transformsImage() const { return scale > 1; }

    static AssetImportProfile native();
    static AssetImportProfile pixelArt16To2x();
};

struct AssetImportOutcome {
    QString sourcePath;
    QString outputPath;
    QSize sourceSize;
    QSize outputSize;
    bool transformed = false;
    bool reusedExisting = false; ///< perfil nativo pode reutilizar um asset já dentro de Assets/.
    QString warning;
    QString error;

    bool ok() const { return !outputPath.isEmpty() && error.isEmpty(); }
};

/// Caminho de destino sem sobrescrever arquivos existentes.
QString uniqueAssetDestination(const QString& directory, const QString& fileName);

/// Importa um asset externo para `destinationDirectory`. Arquivos que não são
/// imagens são copiados normalmente. Imagens com `scale > 1` são ampliadas
/// fisicamente com nearest-neighbor, de modo que Editor e Runtime consumam o
/// mesmo arquivo sem uma segunda regra de escala.
AssetImportOutcome importExternalAsset(const QString& sourcePath,
                                       const QString& destinationDirectory,
                                       const AssetImportProfile& profile = AssetImportProfile::native());

/// Entrada de alto nível do projeto. Se a origem já estiver em Assets/ e o
/// perfil for nativo, reutiliza o mesmo arquivo. Se o perfil pedir uma
/// transformação de imagem (por exemplo 16 px -> 2x), materializa uma nova
/// cópia transformada mesmo quando a origem já pertence ao projeto. Assim a
/// opção escolhida na UI nunca é ignorada silenciosamente e o original não é
/// sobrescrito.
AssetImportOutcome importAssetIntoProject(const QString& sourcePath,
                                          const QString& projectRoot,
                                          const QString& destinationDirectory,
                                          const AssetImportProfile& profile = AssetImportProfile::native());

/// Metadata extensível para AssetDatabase. Não altera ProjectFormat.
QJsonObject assetImportMetadata(const AssetImportProfile& profile,
                                const AssetImportOutcome& outcome);

} // namespace core
