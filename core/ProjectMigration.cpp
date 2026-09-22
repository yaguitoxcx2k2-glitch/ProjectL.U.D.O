#include "ProjectMigration.h"

#include "AssetDatabase.h"
#include "Editor.h"
#include "LayerTree.h"
#include "TilesetCatalog.h"
#include "TilesetOps.h"
#include "legacy/LegacyProjectImporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace core { namespace migration {
namespace {

bool looksLikeRecoverableLegacyAsset(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 4096 ||
        trimmed.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("qrc:"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral(":/")) ||
        trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return false;
    return AssetDatabase::typeForPath(trimmed) == QLatin1String("image");
}

QString uniqueLegacyAssetByName(const QString& projectRoot, const QString& baseName)
{
    if (baseName.trimmed().isEmpty()) return QString();
    QString match;
    int matches = 0;
    QDirIterator it(projectRoot, QStringList{baseName},
                    QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        match = QDir::cleanPath(it.next());
        if (++matches > 1) return QString();
    }
    return matches == 1 ? match : QString();
}

QString resolveLegacyAssetSource(const QString& projectRoot, const QString& storedPath)
{
    const QString normalized = AssetDatabase::normalizePath(storedPath);
    if (normalized.isEmpty()) return QString();
    const QFileInfo directInfo(normalized);
    if (directInfo.isAbsolute() && directInfo.isFile()) return directInfo.absoluteFilePath();
    const QFileInfo relativeInfo(QDir(projectRoot).filePath(normalized));
    if (relativeInfo.isFile()) return relativeInfo.absoluteFilePath();
    return uniqueLegacyAssetByName(projectRoot, QFileInfo(normalized).fileName());
}

bool sameFileBytes(const QString& a, const QString& b)
{
    QFileInfo ia(a), ib(b);
    if (!ia.isFile() || !ib.isFile() || ia.size() != ib.size()) return false;
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    constexpr qint64 kChunk = 128 * 1024;
    while (!fa.atEnd() || !fb.atEnd())
        if (fa.read(kChunk) != fb.read(kChunk)) return false;
    return true;
}

QString recoveredAssetDestination(const QString& recoveredDir, const QString& source)
{
    const QFileInfo info(source);
    const QString stem = info.completeBaseName().isEmpty() ? QStringLiteral("asset") : info.completeBaseName();
    const QString suffix = info.suffix();
    for (int n = 1; n < 10000; ++n) {
        const QString serial = n <= 1 ? QString() : QStringLiteral("-%1").arg(n);
        const QString candidate = QDir(recoveredDir).filePath(
            stem + serial + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
        if (!QFileInfo::exists(candidate) || sameFileBytes(source, candidate)) return candidate;
    }
    return QString();
}

QJsonValue recoverLegacyAssetStrings(const QJsonValue& value, const QString& projectRoot, bool* changed)
{
    if (value.isString()) {
        const QString stored = value.toString();
        if (!looksLikeRecoverableLegacyAsset(stored)) return value;
        const QString source = resolveLegacyAssetSource(projectRoot, stored);
        if (source.isEmpty()) return value;

        const QDir root(projectRoot);
        const QString absoluteAssets = QDir::cleanPath(root.filePath(QStringLiteral("Assets")));
        const QString absoluteSource = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
        const QString assetsPrefix = absoluteAssets + QDir::separator();
        if (absoluteSource.compare(absoluteAssets, Qt::CaseInsensitive) == 0 ||
            absoluteSource.startsWith(assetsPrefix, Qt::CaseInsensitive)) {
            const QString relative = AssetDatabase::normalizePath(root.relativeFilePath(absoluteSource));
            if (relative != stored && changed) *changed = true;
            return relative;
        }

        const QString recoveredDir = root.filePath(QStringLiteral("Assets/Recovered"));
        if (!QDir().mkpath(recoveredDir)) return value;
        const QString destination = recoveredAssetDestination(recoveredDir, absoluteSource);
        if (destination.isEmpty()) return value;
        if (!QFileInfo::exists(destination) && !QFile::copy(absoluteSource, destination)) return value;
        if (changed) *changed = true;
        return AssetDatabase::normalizePath(root.relativeFilePath(destination));
    }
    if (value.isArray()) {
        QJsonArray output;
        for (const QJsonValue& child : value.toArray())
            output.append(recoverLegacyAssetStrings(child, projectRoot, changed));
        return output;
    }
    if (value.isObject()) {
        QJsonObject output;
        const QJsonObject input = value.toObject();
        for (auto it = input.constBegin(); it != input.constEnd(); ++it)
            output.insert(it.key(), recoverLegacyAssetStrings(it.value(), projectRoot, changed));
        return output;
    }
    return value;
}

} // namespace

QJsonObject normalizeHistoricalProjectPayload(const QJsonObject& input,
                                              const QString& projectRoot,
                                              bool recoverLegacyAssets,
                                              bool* changed)
{
    QJsonObject root = input;
    if (legacy::isLegacyEngineProject(root)) {
        bool legacyChanged = false;
        root = legacy::extractMapAuthoringPayload(root, &legacyChanged);
        if (changed && legacyChanged) *changed = true;
    }
    if (recoverLegacyAssets)
        root = recoverLegacyAssetStrings(root, projectRoot, changed).toObject();
    return root;
}

bool migrateLegacyTilesetPages(Editor& ed)
{
    bool changed = false;
    const QRegularExpression partRe(QStringLiteral(R"(^(.*?)\s+—\s+parte\s+(\d+)\s*/\s*(\d+)\s*$)"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression contRe(QStringLiteral(R"(^(.*?)\s+—\s+continuação\s+(\d+)\s*$)"),
                                    QRegularExpression::CaseInsensitiveOption);
    QHash<QString, QString> groupBySignature;
    auto signature = [](const Tileset& ts, const QString& base) {
        return QStringLiteral("%1|%2|%3|%4|%5")
            .arg(base, ts.category).arg(ts.tilewidth).arg(ts.tileheight).arg(ts.sourcePath);
    };

    for (Tileset& ts : ed.tilesets) {
        if (!ts.pageGroupId.trimmed().isEmpty()) continue;
        const QRegularExpressionMatch m = partRe.match(ts.name);
        if (!m.hasMatch()) continue;
        const QString base = m.captured(1).trimmed();
        const QString sig = signature(ts, base);
        QString group = groupBySignature.value(sig);
        if (group.isEmpty()) { group = ts.id; groupBySignature.insert(sig, group); }
        ts.pageGroupId = group;
        ts.pageIndex = qMax(0, m.captured(2).toInt() - 1);
        ts.name = base;
        changed = true;
    }

    for (Tileset& ts : ed.tilesets) {
        if (!ts.pageGroupId.trimmed().isEmpty()) continue;
        const QRegularExpressionMatch m = contRe.match(ts.name);
        if (!m.hasMatch()) continue;
        const QString base = m.captured(1).trimmed();
        const QString sig = signature(ts, base);
        QString group = groupBySignature.value(sig);
        if (group.isEmpty()) {
            for (Tileset& candidate : ed.tilesets) {
                if (&candidate == &ts || candidate.name != base || candidate.internalAutotileAtlas) continue;
                if (candidate.tilewidth != ts.tilewidth || candidate.tileheight != ts.tileheight || candidate.category != ts.category) continue;
                group = candidate.pageGroupId.trimmed().isEmpty() ? candidate.id : candidate.pageGroupId;
                candidate.pageGroupId = group;
                candidate.pageIndex = 0;
                break;
            }
        }
        if (group.isEmpty()) group = ts.id;
        groupBySignature.insert(sig, group);
        ts.pageGroupId = group;
        ts.pageIndex = qMax(1, m.captured(2).toInt());
        ts.name = base;
        changed = true;
    }

    QSet<QString> pageGroups;
    for (const Tileset& ts : ed.tilesets)
        if (!ts.pageGroupId.trimmed().isEmpty()) pageGroups.insert(ts.pageGroupId.trimmed());
    for (const QString& group : pageGroups) renumberTilesetPages(ed, group);
    return changed;
}

void migrateLegacyTileMetadata(Editor& ed, const QJsonObject& root)
{
    const QJsonObject star = root.value(QStringLiteral("starTiles")).toObject();
    for (auto it = star.constBegin(); it != star.constEnd(); ++it) {
        if (!it.value().toBool()) continue;
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 3) continue;
        const int tsIdx = parts[0].toInt(), tx = parts[1].toInt(), ty = parts[2].toInt();
        Tileset* ts = ed.tilesetAt(tsIdx);
        if (!ts || !ts->contains(tx, ty)) continue;
        const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
        if (ts->tilePriority(canonical.x(), canonical.y()) == 0)
            ts->setTilePriority(canonical.x(), canonical.y(), 1);
        ed.starTiles.insert(tileKey(tsIdx, canonical.x(), canonical.y()), true);
    }
    for (int tsIdx = 0; tsIdx < ed.tilesets.size(); ++tsIdx) {
        const Tileset& ts = ed.tilesets.at(tsIdx);
        for (auto it = ts.tilePriorities.constBegin(); it != ts.tilePriorities.constEnd(); ++it) {
            if (it.value() <= 0) continue;
            const QStringList parts = it.key().split(QLatin1Char(':'));
            if (parts.size() == 2)
                ed.starTiles.insert(tileKey(tsIdx, parts[0].toInt(), parts[1].toInt()), true);
        }
    }

    const QJsonObject coll = root.value(QStringLiteral("collisionTiles")).toObject();
    for (auto it = coll.constBegin(); it != coll.constEnd(); ++it) {
        int mask = 0;
        if (it.value().isBool()) mask = it.value().toBool() ? Editor::SideAll : 0;
        else if (it.value().isDouble()) mask = it.value().toInt() & Editor::SideAll;
        if (!mask) continue;
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 3) continue;
        const int tsIdx = parts[0].toInt(), tx = parts[1].toInt(), ty = parts[2].toInt();
        Tileset* ts = ed.tilesetAt(tsIdx);
        if (!ts || !ts->contains(tx, ty)) continue;
        const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
        if (ts->tileCollisionMask(canonical.x(), canonical.y()) == 0)
            ts->setTileCollisionMask(canonical.x(), canonical.y(), mask);
    }

    const QJsonObject prob = root.value(QStringLiteral("tileProbability")).toObject();
    for (auto it = prob.constBegin(); it != prob.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        if (parts.size() != 3) continue;
        const int tsIdx = parts[0].toInt(), tx = parts[1].toInt(), ty = parts[2].toInt();
        Tileset* ts = ed.tilesetAt(tsIdx);
        if (!ts || !ts->contains(tx, ty)) continue;
        const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
        if (qFuzzyCompare(ts->tileProbability(canonical.x(), canonical.y()), 1.0))
            ts->setTileProbability(canonical.x(), canonical.y(), it.value().toDouble(1.0));
    }
}

void finalizeLoadedHistoricalState(Editor& ed, const QJsonDocument& originalDocument, bool* changed)
{
    for (MapDoc& mapDoc : ed.docs) {
        const QVector<LayerPtr> flat = flattenRenderableLayers(mapDoc.layers);
        if (flat.isEmpty()) {
            mapDoc.activeLayerIdx = -1;
            mapDoc.activeLayerId.clear();
            continue;
        }
        int resolved = qBound(0, mapDoc.activeLayerIdx, flat.size() - 1);
        if (!mapDoc.activeLayerId.isEmpty()) {
            for (int i = 0; i < flat.size(); ++i)
                if (flat[i] && flat[i]->id == mapDoc.activeLayerId) { resolved = i; break; }
        }
        if (mapDoc.activeLayerId.isEmpty() || mapDoc.activeLayerIdx != resolved) {
            if (changed) *changed = true;
        }
        mapDoc.activeLayerIdx = resolved;
        mapDoc.activeLayerId = flat[resolved] ? flat[resolved]->id : QString();
    }

    if (ed.projectId.isEmpty()) {
        const QByteArray seed = originalDocument.toJson(QJsonDocument::Compact);
        ed.projectId = QString::fromLatin1(
            QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(24));
        if (changed) *changed = true;
    }
    if (rebuildAllAutotileBoundaryTopologies(ed, false) > 0 && changed) *changed = true;
}

}} // namespace core::migration
