#include "AssetWorkflow.h"

#include "AssetDatabase.h"

#include <QDir>
#include <QFileInfo>

namespace core {
namespace {

QString normalized(QString path)
{
    return AssetDatabase::normalizePath(path);
}

bool sameFolderOrChild(const QString& relative, const QString& folder)
{
    return relative.compare(folder, Qt::CaseInsensitive) == 0 ||
           relative.startsWith(folder + QLatin1Char('/'), Qt::CaseInsensitive);
}

} // namespace

QVector<AssetCategoryInfo> AssetWorkflow::categories()
{
    // O editor mantém apenas recursos necessários para construir mapas.
    // Pictures/Panoramas antigos viram aliases de References sem mover arquivos.
    return {
        {QStringLiteral("tilesets"), QStringLiteral("Tilesets"),
         QStringLiteral("Assets/Tilesets"), {}, QStringLiteral("image")},
        {QStringLiteral("autotiles"), QStringLiteral("Autotiles"),
         QStringLiteral("Assets/Autotiles"), {}, QStringLiteral("image")},
        {QStringLiteral("references"), QStringLiteral("Referências"),
         QStringLiteral("Assets/References"),
         {QStringLiteral("Assets/Pictures"), QStringLiteral("Assets/Panoramas"),
          QStringLiteral("Assets/Panorama")}, QStringLiteral("image")}
    };
}

QStringList AssetWorkflow::standardProjectFolders()
{
    QStringList out{QStringLiteral("Assets")};
    for (const AssetCategoryInfo& category : categories())
        if (!out.contains(category.relativeFolder, Qt::CaseInsensitive)) out.push_back(category.relativeFolder);
    return out;
}

bool AssetWorkflow::ensureProjectFolders(const QString& projectRoot, QString* error)
{
    if (projectRoot.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("A pasta do projeto está vazia.");
        return false;
    }
    QDir dir;
    for (const QString& rel : standardProjectFolders()) {
        if (!dir.mkpath(QDir(projectRoot).filePath(rel))) {
            if (error) *error = QStringLiteral("Não foi possível criar %1.").arg(rel);
            return false;
        }
    }
    return true;
}

QString AssetWorkflow::categoryIdForPath(const QString& path)
{
    const QString clean = normalized(path);
    QString rel = clean;
    const int marker = clean.indexOf(QStringLiteral("Assets/"), 0, Qt::CaseInsensitive);
    if (marker >= 0) rel = clean.mid(marker);
    if (rel.compare(QStringLiteral("Assets"), Qt::CaseInsensitive) == 0) return QStringLiteral("other");
    if (!rel.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) return QString();

    const QString inside = rel.mid(7);
    for (const AssetCategoryInfo& category : categories()) {
        const QString canonical = category.relativeFolder.mid(7);
        if (sameFolderOrChild(inside, canonical)) return category.id;
        for (const QString& alias : category.aliases) {
            const QString aliasInside = alias.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)
                                            ? alias.mid(7) : alias;
            if (sameFolderOrChild(inside, aliasInside)) return category.id;
        }
    }
    return QStringLiteral("other");
}

QString AssetWorkflow::initialFolderForContext(const QString& contextOrFolder)
{
    QString token = normalized(contextOrFolder);
    if (token.isEmpty()) return QString();
    if (token.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) token = token.mid(7);
    const QString first = token.section(QLatin1Char('/'), 0, 0);

    for (const AssetCategoryInfo& category : categories()) {
        const QString canonicalLeaf = category.relativeFolder.mid(7);
        if (category.id.compare(token, Qt::CaseInsensitive) == 0 ||
            category.displayName.compare(token, Qt::CaseInsensitive) == 0 ||
            canonicalLeaf.compare(token, Qt::CaseInsensitive) == 0 ||
            canonicalLeaf.compare(first, Qt::CaseInsensitive) == 0)
            return canonicalLeaf;
        for (const QString& alias : category.aliases) {
            const QString aliasLeaf = alias.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)
                                          ? alias.mid(7) : alias;
            if (aliasLeaf.compare(token, Qt::CaseInsensitive) == 0 ||
                aliasLeaf.compare(first, Qt::CaseInsensitive) == 0)
                return canonicalLeaf;
        }
    }
    return QString();
}

QString AssetWorkflow::projectFolderForContext(const QString& contextOrFolder)
{
    const QString folder = initialFolderForContext(contextOrFolder);
    return folder.isEmpty() ? QStringLiteral("Assets") : QStringLiteral("Assets/") + folder;
}

bool AssetWorkflow::isInsideAssets(const QString& projectRoot, const QString& absoluteOrRelativePath)
{
    if (projectRoot.trimmed().isEmpty() || absoluteOrRelativePath.trimmed().isEmpty()) return false;
    const QDir root(projectRoot);
    QString absolute;
    const QFileInfo info(absoluteOrRelativePath);
    if (info.isAbsolute()) absolute = QDir::cleanPath(info.absoluteFilePath());
    else absolute = QDir::cleanPath(root.filePath(absoluteOrRelativePath));
    const QString assets = QDir::cleanPath(root.filePath(QStringLiteral("Assets")));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return absolute.compare(assets, cs) == 0 || absolute.startsWith(assets + QDir::separator(), cs) ||
           QDir::fromNativeSeparators(absolute).startsWith(QDir::fromNativeSeparators(assets) + QLatin1Char('/'), cs);
}

} // namespace core
