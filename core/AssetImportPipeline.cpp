#include "AssetImportPipeline.h"

#include "AssetDatabase.h"
#include "AssetWorkflow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>

namespace core {
namespace {

bool supportsStaticImageTransform(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    // GIF pode ser animado; QImageReader/QImageWriter simples preservaria só
    // um frame. Nesse caso mantemos o arquivo original em vez de degradá-lo.
    return suffix != QLatin1String("gif");
}

} // namespace

AssetImportProfile AssetImportProfile::native()
{
    AssetImportProfile profile;
    profile.scale = 1;
    profile.sourcePixelUnit = 0;
    profile.nearestNeighbor = true;
    return profile;
}

AssetImportProfile AssetImportProfile::pixelArt16To2x()
{
    AssetImportProfile profile;
    profile.sourcePixelUnit = 16;
    profile.scale = 2;
    profile.nearestNeighbor = true;
    return profile;
}

QString uniqueAssetDestination(const QString& directory, const QString& fileName)
{
    const QFileInfo info(fileName);
    QString candidate = QDir(directory).filePath(info.fileName());
    int suffixIndex = 2;
    while (QFileInfo::exists(candidate)) {
        const QString extension = info.suffix().isEmpty()
            ? QString()
            : QLatin1Char('.') + info.suffix();
        candidate = QDir(directory).filePath(
            QStringLiteral("%1_%2%3")
                .arg(info.completeBaseName()).arg(suffixIndex++).arg(extension));
    }
    return candidate;
}

AssetImportOutcome importExternalAsset(const QString& sourcePath,
                                       const QString& destinationDirectory,
                                       const AssetImportProfile& profile)
{
    AssetImportOutcome result;
    result.sourcePath = QFileInfo(sourcePath).absoluteFilePath();

    const QFileInfo sourceInfo(result.sourcePath);
    if (!sourceInfo.isFile()) {
        result.error = QStringLiteral("Arquivo de origem inexistente: %1").arg(sourcePath);
        return result;
    }
    if (destinationDirectory.trimmed().isEmpty() || !QDir().mkpath(destinationDirectory)) {
        result.error = QStringLiteral("Não foi possível preparar a pasta de destino: %1")
                           .arg(destinationDirectory);
        return result;
    }

    result.outputPath = uniqueAssetDestination(destinationDirectory, sourceInfo.fileName());
    const bool image = AssetDatabase::typeForPath(sourcePath) == QLatin1String("image");
    if (!image || !profile.transformsImage()) {
        if (!QFile::copy(result.sourcePath, result.outputPath)) {
            result.error = QStringLiteral("Falha ao copiar %1").arg(sourceInfo.fileName());
            result.outputPath.clear();
        }
        return result;
    }

    if (!supportsStaticImageTransform(sourcePath)) {
        if (!QFile::copy(result.sourcePath, result.outputPath)) {
            result.error = QStringLiteral("Falha ao copiar %1").arg(sourceInfo.fileName());
            result.outputPath.clear();
            return result;
        }
        result.warning = QStringLiteral("GIF preservado no tamanho original para não perder animação.");
        return result;
    }

    QImageReader reader(result.sourcePath);
    reader.setAutoTransform(true);
    const QImage source = reader.read();
    if (source.isNull()) {
        result.error = QStringLiteral("Não foi possível ler a imagem %1: %2")
                           .arg(sourceInfo.fileName(), reader.errorString());
        result.outputPath.clear();
        return result;
    }

    result.sourceSize = source.size();
    const int scale = qMax(1, profile.scale);
    const QSize targetSize(source.width() * scale, source.height() * scale);
    const Qt::TransformationMode mode = profile.nearestNeighbor
        ? Qt::FastTransformation
        : Qt::SmoothTransformation;
    const QImage scaled = source.scaled(targetSize, Qt::IgnoreAspectRatio, mode);

    QImageWriter writer(result.outputPath);
    if (!writer.write(scaled)) {
        result.error = QStringLiteral("Não foi possível gravar %1: %2")
                           .arg(QFileInfo(result.outputPath).fileName(), writer.errorString());
        QFile::remove(result.outputPath);
        result.outputPath.clear();
        return result;
    }

    result.outputSize = scaled.size();
    result.transformed = true;
    return result;
}

AssetImportOutcome importAssetIntoProject(const QString& sourcePath,
                                          const QString& projectRoot,
                                          const QString& destinationDirectory,
                                          const AssetImportProfile& profile)
{
    const QString absoluteSource = QFileInfo(sourcePath).absoluteFilePath();
    const QFileInfo sourceInfo(absoluteSource);
    if (!sourceInfo.isFile()) {
        AssetImportOutcome result;
        result.sourcePath = absoluteSource;
        result.error = QStringLiteral("Arquivo de origem inexistente: %1").arg(sourcePath);
        return result;
    }

    const bool insideProjectAssets = AssetWorkflow::isInsideAssets(projectRoot, absoluteSource);
    const bool image = AssetDatabase::typeForPath(absoluteSource) == QLatin1String("image");
    const bool transformableImage = image && profile.transformsImage() &&
                                    supportsStaticImageTransform(absoluteSource);

    // Um asset já pertencente ao projeto só pode ser reutilizado quando a
    // escolha de importação não exige materialização. O perfil 2x, portanto,
    // nunca é descartado por causa da localização do arquivo.
    if (insideProjectAssets && !transformableImage) {
        AssetImportOutcome result;
        result.sourcePath = absoluteSource;
        result.outputPath = absoluteSource;
        result.reusedExisting = true;
        if (image) {
            QImageReader reader(absoluteSource);
            const QSize size = reader.size();
            if (size.isValid()) {
                result.sourceSize = size;
                result.outputSize = size;
            }
        }
        if (image && profile.transformsImage() && !supportsStaticImageTransform(absoluteSource))
            result.warning = QStringLiteral("GIF preservado no tamanho original para não perder animação.");
        return result;
    }

    return importExternalAsset(absoluteSource, destinationDirectory, profile);
}

QJsonObject assetImportMetadata(const AssetImportProfile& profile,
                                const AssetImportOutcome& outcome)
{
    QJsonObject metadata;
    metadata.insert(QStringLiteral("sourcePixelUnit"), profile.sourcePixelUnit);
    metadata.insert(QStringLiteral("scale"), qMax(1, profile.scale));
    metadata.insert(QStringLiteral("filter"), profile.nearestNeighbor
        ? QStringLiteral("nearest") : QStringLiteral("smooth"));
    metadata.insert(QStringLiteral("physicalScaleApplied"), outcome.transformed);
    if (outcome.sourceSize.isValid()) {
        metadata.insert(QStringLiteral("sourceWidth"), outcome.sourceSize.width());
        metadata.insert(QStringLiteral("sourceHeight"), outcome.sourceSize.height());
    }
    if (outcome.outputSize.isValid()) {
        metadata.insert(QStringLiteral("outputWidth"), outcome.outputSize.width());
        metadata.insert(QStringLiteral("outputHeight"), outcome.outputSize.height());
    }
    return metadata;
}

} // namespace core
