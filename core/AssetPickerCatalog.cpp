#include "AssetPickerCatalog.h"

#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace core {
namespace {

bool mediaMatches(const AssetRecord& record, const QString& mediaType)
{
    return mediaType.trimmed().isEmpty() ||
           record.type.compare(mediaType.trimmed(), Qt::CaseInsensitive) == 0;
}

bool searchMatches(const AssetRecord& record, const QString& search)
{
    const QString needle = search.trimmed();
    if (needle.isEmpty()) return true;
    return record.path.contains(needle, Qt::CaseInsensitive) ||
           QFileInfo(record.path).fileName().contains(needle, Qt::CaseInsensitive) ||
           record.category.contains(needle, Qt::CaseInsensitive) ||
           record.type.contains(needle, Qt::CaseInsensitive);
}

} // namespace

QString AssetPickerCatalog::categoryIdForContext(const QString& context)
{
    const QString folder = AssetWorkflow::initialFolderForContext(context);
    if (folder.isEmpty()) return QString();
    for (const AssetCategoryInfo& category : AssetWorkflow::categories()) {
        const QString canonicalLeaf = category.relativeFolder.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)
                                          ? category.relativeFolder.mid(7)
                                          : category.relativeFolder;
        if (category.id.compare(context, Qt::CaseInsensitive) == 0 ||
            category.displayName.compare(context, Qt::CaseInsensitive) == 0 ||
            canonicalLeaf.compare(folder, Qt::CaseInsensitive) == 0)
            return category.id;
    }
    return QString();
}

AssetPickerScope AssetPickerCatalog::scopeForContext(const QString& context,
                                                     const QString& requiredMediaType)
{
    AssetPickerScope scope;
    Q_UNUSED(requiredMediaType);
    scope.mediaType = QStringLiteral("image");
    scope.initialCategoryId = categoryIdForContext(context);
    scope.categories = AssetWorkflow::categories();
    return scope;
}

QVector<AssetRecord> AssetPickerCatalog::query(const AssetDatabase& database,
                                               const QString& categoryId,
                                               const QString& mediaType,
                                               const QString& search,
                                               bool includeMissing)
{
    QVector<AssetRecord> out;
    const QString wantedCategory = categoryId.trimmed();
    QSet<QString> allowedCategories;
    for (const AssetCategoryInfo& category : AssetWorkflow::categories())
        allowedCategories.insert(category.id.toLower());

    for (const AssetRecord& record : database.records()) {
        if (AssetDatabase::isInternalPath(record.path)) continue;
        if (!includeMissing && record.missing) continue;
        const QString actualCategory = record.category.isEmpty()
                                           ? AssetWorkflow::categoryIdForPath(record.path)
                                           : record.category;
        // Arquivos de antigas áreas de gameplay/UI podem continuar no disco,
        // mas não fazem parte do catálogo do Map Editor.
        if (!allowedCategories.contains(actualCategory.toLower())) continue;
        if (!wantedCategory.isEmpty() &&
            actualCategory.compare(wantedCategory, Qt::CaseInsensitive) != 0)
            continue;
        if (!mediaMatches(record, mediaType)) continue;
        if (!searchMatches(record, search)) continue;
        AssetRecord normalizedRecord = record;
        normalizedRecord.category = actualCategory;
        out.push_back(normalizedRecord);
    }

    std::sort(out.begin(), out.end(), [](const AssetRecord& a, const AssetRecord& b) {
        const int categoryCompare = QString::compare(a.category, b.category, Qt::CaseInsensitive);
        if (categoryCompare != 0) return categoryCompare < 0;
        return QString::compare(a.path, b.path, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString AssetPickerCatalog::aggregateLabel(const QString& mediaType)
{
    Q_UNUSED(mediaType);
    return QStringLiteral("Todas as imagens do mapa");
}

} // namespace core
