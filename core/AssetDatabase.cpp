#include "AssetDatabase.h"
#include "AssetWorkflow.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QUuid>

namespace core {

QString AssetDatabase::normalizePath(const QString& path)
{
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (p == QLatin1String(".")) p.clear();
    while (p.startsWith(QStringLiteral("./"))) p.remove(0, 2);
    return p;
}

QString AssetDatabase::typeForPath(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QSet<QString> images = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("gif"),
    };
    if (images.contains(ext)) return QStringLiteral("image");
    if (QSet<QString>{"wav","ogg","mp3","m4a","flac","opus","aac"}.contains(ext)) return QStringLiteral("audio");
    if (QSet<QString>{"ttf","otf","woff","woff2"}.contains(ext)) return QStringLiteral("font");
    if (QSet<QString>{"mp4","webm","avi","mov","ogv"}.contains(ext)) return QStringLiteral("video");
    return QStringLiteral("other");
}

bool AssetDatabase::isInternalPath(const QString& path)
{
    return normalizePath(path).startsWith(QStringLiteral("Assets/LUDO/Team/"), Qt::CaseInsensitive);
}

QString AssetDatabase::newAssetId()
{
    return QStringLiteral("asset.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool AssetDatabase::fingerprint(const QString& absolutePath, QString* sha256,
                                qint64* size, QString* error)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[128 * 1024];
    qint64 total = 0;
    while (!file.atEnd()) {
        const qint64 n = file.read(buffer, sizeof(buffer));
        if (n < 0) {
            if (error) *error = file.errorString();
            return false;
        }
        if (n == 0) break;
        hash.addData(QByteArrayView(buffer, n));
        total += n;
    }
    if (sha256) *sha256 = QString::fromLatin1(hash.result().toHex());
    if (size) *size = total;
    return true;
}

void AssetDatabase::clear()
{
    m_records.clear();
    m_pathChanges.clear();
}

int AssetDatabase::indexById(const QString& id) const
{
    for (int i = 0; i < m_records.size(); ++i)
        if (m_records.at(i).id == id) return i;
    return -1;
}

int AssetDatabase::indexByPathOrAlias(const QString& normalized) const
{
    if (normalized.isEmpty()) return -1;
    // Caminho atual sempre vence aliases. Isso evita que um alias histórico
    // de outro GUID capture um arquivo que hoje pertence legitimamente a um
    // registro diferente.
    for (int i = 0; i < m_records.size(); ++i)
        if (m_records.at(i).path.compare(normalized, Qt::CaseInsensitive) == 0) return i;
    for (int i = 0; i < m_records.size(); ++i)
        for (const QString& alias : m_records.at(i).aliases)
            if (alias.compare(normalized, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

bool AssetDatabase::synchronize(const QString& projectRoot, QString* error)
{
    m_pathChanges.clear();
    if (projectRoot.trimmed().isEmpty()) return true;

    const QDir root(projectRoot);
    const QString assetsRoot = root.filePath(QStringLiteral("Assets"));
    if (!QFileInfo(assetsRoot).isDir()) {
        for (auto& record : m_records) record.missing = true;
        return true;
    }

    struct DiskAsset { QString path, absolute, sha; qint64 size = 0; };
    QVector<DiskAsset> disk;
    QDirIterator it(assetsRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absolute = it.next();
        if (QFileInfo(absolute).isSymLink()) continue;
        DiskAsset entry;
        entry.absolute = absolute;
        entry.path = normalizePath(root.relativeFilePath(absolute));
        QString fpError;
        if (!fingerprint(absolute, &entry.sha, &entry.size, &fpError)) {
            if (error) *error = QStringLiteral("%1: %2").arg(entry.path, fpError);
            return false;
        }
        disk.push_back(entry);
    }

    QVector<bool> matchedRecord(m_records.size(), false);
    QVector<bool> matchedDisk(disk.size(), false);

    // 1) Mesmo caminho: caso comum e barato de reconciliar.
    for (int d = 0; d < disk.size(); ++d) {
        const int r = indexByPathOrAlias(disk[d].path);
        if (r < 0 || matchedRecord[r]) continue;
        AssetRecord& record = m_records[r];
        // Se veio por alias, significa que o caminho antigo reapareceu. Não
        // troca automaticamente um arquivo atual válido por um alias legado.
        if (!record.missing && record.path.compare(disk[d].path, Qt::CaseInsensitive) != 0)
            continue;
        if (record.path.compare(disk[d].path, Qt::CaseInsensitive) != 0) {
            const QString old = record.path;
            if (!old.isEmpty() && !record.aliases.contains(old, Qt::CaseInsensitive))
                record.aliases.push_back(old);
            record.path = disk[d].path;
            if (!old.isEmpty()) m_pathChanges.push_back({record.id, old, record.path});
        }
        record.sha256 = disk[d].sha;
        record.size = disk[d].size;
        record.type = typeForPath(record.path);
        record.category = (isInternalPath(record.path) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(record.path));
        record.missing = false;
        matchedRecord[r] = true;
        matchedDisk[d] = true;
    }

    // 2) Marca registros que sumiram. Eles podem ter sido movidos/renomeados.
    for (int r = 0; r < m_records.size(); ++r)
        if (!matchedRecord[r]) m_records[r].missing = true;

    // 3) Move/rename: só preserva o GUID quando o hash identifica um único
    // registro ausente. Duplicatas idênticas são tratadas como novos assets.
    for (int d = 0; d < disk.size(); ++d) {
        if (matchedDisk[d]) continue;
        QVector<int> candidates;
        for (int r = 0; r < m_records.size(); ++r) {
            const AssetRecord& record = m_records.at(r);
            if (!record.missing || record.sha256.isEmpty()) continue;
            if (record.size == disk[d].size && record.sha256 == disk[d].sha)
                candidates.push_back(r);
        }
        int sameDiskHash = 0;
        for (int other = 0; other < disk.size(); ++other)
            if (!matchedDisk[other] && disk[other].sha == disk[d].sha && disk[other].size == disk[d].size) ++sameDiskHash;
        if (candidates.size() == 1 && sameDiskHash == 1) {
            const int r = candidates.first();
            AssetRecord& record = m_records[r];
            const QString old = record.path;
            if (!old.isEmpty() && !record.aliases.contains(old, Qt::CaseInsensitive))
                record.aliases.push_back(old);
            record.path = disk[d].path;
            record.type = typeForPath(record.path);
            record.category = (isInternalPath(record.path) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(record.path));
            record.sha256 = disk[d].sha;
            record.size = disk[d].size;
            record.missing = false;
            matchedRecord[r] = true;
            matchedDisk[d] = true;
            if (!old.isEmpty() && old.compare(record.path, Qt::CaseInsensitive) != 0)
                m_pathChanges.push_back({record.id, old, record.path});
        }
    }

    // 4) Arquivos realmente novos recebem GUID novo.
    for (int d = 0; d < disk.size(); ++d) {
        if (matchedDisk[d]) continue;
        AssetRecord record;
        record.id = newAssetId();
        record.path = disk[d].path;
        record.type = typeForPath(record.path);
        record.category = (isInternalPath(record.path) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(record.path));
        record.sha256 = disk[d].sha;
        record.size = disk[d].size;
        record.missing = false;
        m_records.push_back(record);
    }

    return true;
}

QString AssetDatabase::idForPath(const QString& projectRelativePath) const
{
    const int i = indexByPathOrAlias(normalizePath(projectRelativePath));
    return i >= 0 ? m_records.at(i).id : QString();
}

QString AssetDatabase::pathForId(const QString& id) const
{
    const int i = indexById(id);
    if (i < 0 || m_records.at(i).missing) return QString();
    return m_records.at(i).path;
}

QString AssetDatabase::canonicalPath(const QString& currentOrLegacyPath) const
{
    const int i = indexByPathOrAlias(normalizePath(currentOrLegacyPath));
    return i >= 0 && !m_records.at(i).missing ? m_records.at(i).path
                                              : normalizePath(currentOrLegacyPath);
}

const AssetRecord* AssetDatabase::recordById(const QString& id) const
{
    const int i = indexById(id);
    return i >= 0 ? &m_records.at(i) : nullptr;
}

const AssetRecord* AssetDatabase::recordByPath(const QString& path) const
{
    const int i = indexByPathOrAlias(normalizePath(path));
    return i >= 0 ? &m_records.at(i) : nullptr;
}

QJsonObject AssetDatabase::metadataForPath(const QString& path) const
{
    const int i = indexByPathOrAlias(normalizePath(path));
    return i >= 0 ? m_records.at(i).metadata : QJsonObject{};
}

bool AssetDatabase::setMetadataValueForPath(const QString& path, const QString& key,
                                            const QJsonValue& value)
{
    const int i = indexByPathOrAlias(normalizePath(path));
    const QString trimmedKey = key.trimmed();
    if (i < 0 || trimmedKey.isEmpty()) return false;
    if (m_records[i].metadata.value(trimmedKey) == value) return true;
    if (value.isUndefined() || value.isNull()) m_records[i].metadata.remove(trimmedKey);
    else m_records[i].metadata.insert(trimmedKey, value);
    if (metadataChanged) metadataChanged(m_records[i].path);
    return true;
}

bool AssetDatabase::synchronizePaths(const QString& projectRoot,
                                     const QStringList& projectRelativePaths,
                                     QString* error)
{
    if (projectRoot.trimmed().isEmpty() || projectRelativePaths.isEmpty()) return true;
    const QDir root(projectRoot);
    QSet<QString> visited;
    for (const QString& raw : projectRelativePaths) {
        const QString rel = normalizePath(raw);
        if (rel.isEmpty() || visited.contains(rel.toLower())) continue;
        visited.insert(rel.toLower());
        if (!rel.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) continue;


        const QString absolute = root.filePath(rel);
        const QFileInfo info(absolute);
        const int existing = indexByPathOrAlias(rel);
        if (!info.isFile() || info.isSymLink()) {
            if (existing >= 0 && m_records[existing].path.compare(rel, Qt::CaseInsensitive) == 0)
                m_records[existing].missing = true;
            continue;
        }

        QString sha;
        qint64 size = 0;
        QString fpError;
        if (!fingerprint(absolute, &sha, &size, &fpError)) {
            if (error) *error = QStringLiteral("%1: %2").arg(rel, fpError);
            return false;
        }

        int index = existing;
        if (index < 0) {
            AssetRecord record;
            record.id = newAssetId();
            record.path = rel;
            record.type = typeForPath(rel);
            record.category = (isInternalPath(rel) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(rel));
            record.sha256 = sha;
            record.size = size;
            record.missing = false;
            m_records.push_back(record);
            continue;
        }

        AssetRecord& record = m_records[index];
        const QString old = record.path;
        if (old.compare(rel, Qt::CaseInsensitive) != 0) {
            if (!old.isEmpty() && !record.aliases.contains(old, Qt::CaseInsensitive))
                record.aliases.push_back(old);
            record.path = rel;
            if (!old.isEmpty()) m_pathChanges.push_back({record.id, old, rel});
        }
        record.type = typeForPath(rel);
        record.category = (isInternalPath(rel) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(rel));
        record.sha256 = sha;
        record.size = size;
        record.missing = false;
    }
    return true;
}

QVector<AssetRecord> AssetDatabase::records() const { return m_records; }

QVector<AssetRecord> AssetDatabase::recordsForCategory(const QString& categoryId, bool includeMissing) const
{
    QVector<AssetRecord> out;
    const QString wanted = categoryId.trimmed();
    if (wanted.isEmpty()) return out;
    for (const AssetRecord& record : m_records) {
        if (!includeMissing && record.missing) continue;
        const QString actual = record.category.isEmpty()
                                   ? (isInternalPath(record.path) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(record.path))
                                   : record.category;
        if (actual.compare(wanted, Qt::CaseInsensitive) == 0) out.push_back(record);
    }
    return out;
}

QVector<AssetRecord> AssetDatabase::missingRecords() const
{
    QVector<AssetRecord> out;
    for (const AssetRecord& record : m_records)
        if (record.missing) out.push_back(record);
    return out;
}

QVector<AssetPathChange> AssetDatabase::takePathChanges()
{
    QVector<AssetPathChange> out = m_pathChanges;
    m_pathChanges.clear();
    return out;
}

bool AssetDatabase::rebind(const QString& id, const QString& projectRelativePath,
                           const QString& projectRoot, QString* error)
{
    const int i = indexById(id);
    if (i < 0) {
        if (error) *error = QStringLiteral("Asset ID não encontrado: %1").arg(id);
        return false;
    }
    const QString rel = normalizePath(projectRelativePath);
    if (typeForPath(rel) != QLatin1String("image")) {
        if (error) *error = QStringLiteral("O LUDO Map Editor aceita somente imagens no Asset Database.");
        return false;
    }
    if (!rel.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) {
        if (error) *error = QStringLiteral("O arquivo precisa estar dentro de Assets/.");
        return false;
    }
    const int existingPath = indexByPathOrAlias(rel);
    if (existingPath >= 0 && existingPath != i && !m_records.at(existingPath).missing &&
        m_records.at(existingPath).path.compare(rel, Qt::CaseInsensitive) == 0) {
        if (error) *error = QStringLiteral("Este arquivo já pertence ao Asset Database com outro ID (%1).")
                                .arg(m_records.at(existingPath).id);
        return false;
    }
    const QString absolute = QDir(projectRoot).filePath(rel);
    QString sha;
    qint64 size = 0;
    if (!fingerprint(absolute, &sha, &size, error)) return false;

    AssetRecord& record = m_records[i];
    const QString old = record.path;
    if (!old.isEmpty() && !record.aliases.contains(old, Qt::CaseInsensitive))
        record.aliases.push_back(old);
    record.path = rel;
    record.type = typeForPath(rel);
    record.category = (isInternalPath(rel) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(rel));
    record.sha256 = sha;
    record.size = size;
    record.missing = false;
    if (!old.isEmpty() && old.compare(rel, Qt::CaseInsensitive) != 0)
        m_pathChanges.push_back({record.id, old, rel});
    return true;
}

QJsonObject AssetDatabase::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    QJsonArray assets;
    for (const AssetRecord& record : m_records) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), record.id);
        item.insert(QStringLiteral("path"), record.path);
        item.insert(QStringLiteral("type"), record.type);
        if (!record.category.isEmpty()) item.insert(QStringLiteral("category"), record.category);
        item.insert(QStringLiteral("sha256"), record.sha256);
        item.insert(QStringLiteral("size"), double(record.size));
        if (!record.aliases.isEmpty()) {
            QJsonArray aliases;
            for (const QString& alias : record.aliases) aliases.append(alias);
            item.insert(QStringLiteral("aliases"), aliases);
        }
        if (!record.metadata.isEmpty()) item.insert(QStringLiteral("metadata"), record.metadata);
        if (record.missing) item.insert(QStringLiteral("missing"), true);
        assets.append(item);
    }
    object.insert(QStringLiteral("assets"), assets);
    return object;
}

void AssetDatabase::fromJson(const QJsonObject& object)
{
    clear();
    QSet<QString> seenIds;
    QSet<QString> seenPaths;
    for (const QJsonValue& value : object.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject item = value.toObject();
        AssetRecord record;
        record.id = item.value(QStringLiteral("id")).toString().trimmed();
        record.path = normalizePath(item.value(QStringLiteral("path")).toString());
        record.type = typeForPath(record.path);
        // Projetos antigos podem conter registros de áudio/fontes/game UI.
        // Eles não pertencem mais ao formato ativo do LUDO Map Editor.
        if (record.type != QLatin1String("image")) continue;
        // A categoria é derivada do caminho para migrar nomes antigos (Pictures/Panoramas etc.).
        record.category = (isInternalPath(record.path) ? QStringLiteral("internal") : AssetWorkflow::categoryIdForPath(record.path));
        record.sha256 = item.value(QStringLiteral("sha256")).toString().toLower();
        record.size = qMax<qint64>(0, qint64(item.value(QStringLiteral("size")).toDouble()));
        record.metadata = item.value(QStringLiteral("metadata")).toObject();
        record.missing = item.value(QStringLiteral("missing")).toBool(false);
        for (const QJsonValue& alias : item.value(QStringLiteral("aliases")).toArray()) {
            const QString normalized = normalizePath(alias.toString());
            if (!normalized.isEmpty() && normalized.compare(record.path, Qt::CaseInsensitive) != 0 &&
                !record.aliases.contains(normalized, Qt::CaseInsensitive))
                record.aliases.push_back(normalized);
        }
        if (record.id.isEmpty() || seenIds.contains(record.id)) record.id = newAssetId();
        const QString key = record.path.toLower();
        if (!record.path.isEmpty() && seenPaths.contains(key)) continue;
        seenIds.insert(record.id);
        if (!key.isEmpty()) seenPaths.insert(key);
        m_records.push_back(record);
    }
}

} // namespace core
