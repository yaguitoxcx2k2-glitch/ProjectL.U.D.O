#include "NavigationCatalog.h"

#include "core/AssetDatabase.h"
#include "core/Editor.h"

#include <algorithm>

#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

namespace ui {
namespace {
QString normalized(const QString& text) { return text.trimmed().toCaseFolded(); }

QString referenceKindId(core::ReferenceSymbolKind kind)
{
    return core::referenceSymbolKindId(kind);
}

NavigationKind navKind(core::ReferenceSymbolKind kind)
{
    switch (kind) {
    case core::ReferenceSymbolKind::Map: return NavigationKind::Map;
    case core::ReferenceSymbolKind::MapEvent: return NavigationKind::MapEvent;
    case core::ReferenceSymbolKind::CutsceneRegion: return NavigationKind::Map;
    case core::ReferenceSymbolKind::Picture: return NavigationKind::CommonEvent;
    case core::ReferenceSymbolKind::Speaker:
    case core::ReferenceSymbolKind::SubtitleTrack: return NavigationKind::GameData;
    case core::ReferenceSymbolKind::CommandTemplate: return NavigationKind::CommonEvent;
    case core::ReferenceSymbolKind::CommonEvent: return NavigationKind::CommonEvent;
    case core::ReferenceSymbolKind::Switch:
    case core::ReferenceSymbolKind::Variable:
    case core::ReferenceSymbolKind::String: return NavigationKind::GameData;
    case core::ReferenceSymbolKind::CustomDatabase: return NavigationKind::CustomDatabase;
    case core::ReferenceSymbolKind::CustomRecord:
    case core::ReferenceSymbolKind::CustomField: return NavigationKind::CustomRecord;
    case core::ReferenceSymbolKind::DatabaseRecord: return NavigationKind::DatabaseRecord;
    case core::ReferenceSymbolKind::Tileset: return NavigationKind::Tileset;
    case core::ReferenceSymbolKind::Plugin: return NavigationKind::Plugin;
    case core::ReferenceSymbolKind::FootstepSurface: return NavigationKind::FootstepSurface;
    }
    return NavigationKind::Map;
}

core::ProjectReferenceLocation locationFor(const core::ProjectReferenceSymbol& symbol)
{
    core::ProjectReferenceLocation location;
    location.ownerId = symbol.id;
    location.ownerName = symbol.qualifiedName.isEmpty() ? symbol.name : symbol.qualifiedName;
    location.detail = referenceKindId(symbol.kind);
    switch (symbol.kind) {
    case core::ReferenceSymbolKind::Map:
        location.ownerType = QStringLiteral("map");
        location.mapId = symbol.id;
        break;
    case core::ReferenceSymbolKind::MapEvent:
        location.ownerType = QStringLiteral("mapEvent");
        location.mapId = symbol.parentId;
        break;
    case core::ReferenceSymbolKind::CutsceneRegion:
        location.ownerType = QStringLiteral("cutsceneRegion");
        location.mapId = symbol.parentId;
        break;
    case core::ReferenceSymbolKind::Picture:
        location.ownerType=QStringLiteral("picture");
        break;
    case core::ReferenceSymbolKind::Speaker:
        location.ownerType=QStringLiteral("speaker");
        break;
    case core::ReferenceSymbolKind::SubtitleTrack:
        location.ownerType=QStringLiteral("subtitleTrack");
        break;
    case core::ReferenceSymbolKind::CommandTemplate:
        location.ownerType=QStringLiteral("commandTemplate");
        break;
    case core::ReferenceSymbolKind::CommonEvent:
        location.ownerType = QStringLiteral("commonEvent");
        break;
    case core::ReferenceSymbolKind::Switch:
    case core::ReferenceSymbolKind::Variable:
    case core::ReferenceSymbolKind::String:
        location.ownerType = QStringLiteral("gameData");
        break;
    case core::ReferenceSymbolKind::CustomDatabase:
        location.ownerType = QStringLiteral("customDatabase");
        location.detail = referenceKindId(symbol.kind);
        break;
    case core::ReferenceSymbolKind::CustomField:
    case core::ReferenceSymbolKind::CustomRecord:
        location.ownerType = QStringLiteral("customDatabase");
        location.ownerId = symbol.parentId;
        location.detail = referenceKindId(symbol.kind);
        break;
    case core::ReferenceSymbolKind::Plugin:
        location.ownerType = QStringLiteral("plugin");
        break;
    case core::ReferenceSymbolKind::DatabaseRecord:
        location.ownerType = QStringLiteral("databaseRecord");
        location.detail = symbol.parentId;
        break;
    case core::ReferenceSymbolKind::Tileset:
        location.ownerType = QStringLiteral("tileset");
        break;
    case core::ReferenceSymbolKind::FootstepSurface:
        location.ownerType = QStringLiteral("footstepSurface");
        break;
    }
    return location;
}

int textualScore(const NavigationItem& item, const QString& query)
{
    if (query.isEmpty()) return 1000;
    const QString q = normalized(query);
    const QString label = normalized(item.label);
    const QString context = normalized(item.context);
    const QString kind = normalized(navigationKindLabel(item.kind));
    const QString haystack = label + QLatin1Char(' ') + context + QLatin1Char(' ') +
                             normalized(item.keywords) + QLatin1Char(' ') + kind;

    const QStringList tokens = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString& token : tokens)
        if (!haystack.contains(token)) return -1;

    int score = 0;
    if (label == q) score += 400;
    else if (label.startsWith(q)) score += 300;
    else if (label.contains(q)) score += 220;
    if (context.contains(q)) score += 100;
    if (kind.contains(q)) score += 60;
    for (const QString& token : tokens) {
        if (label.startsWith(token)) score += 35;
        else if (label.contains(token)) score += 20;
        if (context.contains(token)) score += 8;
    }
    score -= qMax(0, label.size() - q.size()) / 8;
    return score;
}
}

QString navigationKindId(NavigationKind kind)
{
    switch (kind) {
    case NavigationKind::Map: return QStringLiteral("map");
    case NavigationKind::MapEvent: return QStringLiteral("event");
    case NavigationKind::CommonEvent: return QStringLiteral("commonEvent");
    case NavigationKind::GameData: return QStringLiteral("gameData");
    case NavigationKind::CustomDatabase: return QStringLiteral("customDatabase");
    case NavigationKind::CustomRecord: return QStringLiteral("customRecord");
    case NavigationKind::DatabaseRecord: return QStringLiteral("databaseRecord");
    case NavigationKind::Tileset: return QStringLiteral("tileset");
    case NavigationKind::Asset: return QStringLiteral("asset");
    case NavigationKind::Plugin: return QStringLiteral("plugin");
    case NavigationKind::FootstepSurface: return QStringLiteral("footstepSurface");
    }
    return QStringLiteral("item");
}

QString navigationKindLabel(NavigationKind kind)
{
    switch (kind) {
    case NavigationKind::Map: return QObject::tr("Mapa");
    case NavigationKind::MapEvent: return QObject::tr("Evento do mapa");
    case NavigationKind::CommonEvent: return QObject::tr("Evento comum");
    case NavigationKind::GameData: return QObject::tr("Switch / Variável");
    case NavigationKind::CustomDatabase: return QObject::tr("Banco personalizado");
    case NavigationKind::CustomRecord: return QObject::tr("Registro personalizado");
    case NavigationKind::DatabaseRecord: return QObject::tr("Banco RPG");
    case NavigationKind::Tileset: return QObject::tr("Tileset");
    case NavigationKind::Asset: return QObject::tr("Asset");
    case NavigationKind::Plugin: return QObject::tr("Extensão");
    case NavigationKind::FootstepSurface: return QObject::tr("Superfície de passos");
    }
    return QObject::tr("Item");
}

QString navigationGroupId(NavigationKind kind)
{
    switch (kind) {
    case NavigationKind::Map: return QStringLiteral("maps");
    case NavigationKind::MapEvent:
    case NavigationKind::CommonEvent: return QStringLiteral("events");
    case NavigationKind::Asset: return QStringLiteral("assets");
    case NavigationKind::Tileset: return QStringLiteral("tilesets");
    case NavigationKind::Plugin: return QStringLiteral("plugins");
    case NavigationKind::GameData:
    case NavigationKind::CustomDatabase:
    case NavigationKind::CustomRecord:
    case NavigationKind::DatabaseRecord:
    case NavigationKind::FootstepSurface: return QStringLiteral("data");
    }
    return QStringLiteral("all");
}

QString navigationReferenceKey(core::ReferenceSymbolKind kind, const QString& id,
                               const QString& parentId)
{
    return QStringLiteral("ref:%1:%2:%3")
        .arg(referenceKindId(kind), parentId, id);
}

QString navigationKeyForReference(const core::ProjectReferenceSymbol& symbol)
{
    return navigationReferenceKey(symbol.kind, symbol.id, symbol.parentId);
}

QVector<NavigationItem> navigationItems(const core::Editor& ed)
{
    QVector<NavigationItem> out;
    const QVector<core::ProjectReferenceSymbol> symbols = core::projectReferenceSymbols(ed);
    out.reserve(symbols.size() + ed.assetDatabase.records().size());

    for (const core::ProjectReferenceSymbol& symbol : symbols) {
        NavigationItem item;
        item.kind = navKind(symbol.kind);
        item.key = navigationKeyForReference(symbol);
        item.label = symbol.name.trimmed().isEmpty() ? symbol.qualifiedName : symbol.name;
        item.context = symbol.qualifiedName;
        if (item.context == item.label) item.context.clear();
        if (symbol.number > 0)
            item.keywords += QStringLiteral(" %1").arg(symbol.number);
        item.keywords += QStringLiteral(" %1 %2").arg(referenceKindId(symbol.kind), symbol.id);
        item.location = locationFor(symbol);
        out.push_back(item);
    }

    for (const core::AssetRecord& record : ed.assetDatabase.records()) {
        NavigationItem item;
        item.kind = NavigationKind::Asset;
        item.key = QStringLiteral("asset:%1").arg(record.id);
        item.label = QFileInfo(record.path).fileName();
        if (item.label.isEmpty()) item.label = record.path;
        item.context = record.path;
        item.assetPath = record.path;
        item.missing = record.missing;
        item.keywords = QStringLiteral("%1 %2 %3 %4")
                            .arg(record.type, record.category, record.id,
                                 record.aliases.join(QLatin1Char(' ')));
        item.location.ownerType = QStringLiteral("asset");
        item.location.ownerId = record.id;
        item.location.ownerName = item.label;
        item.location.detail = record.path;
        out.push_back(item);
    }

    return out;
}

QVector<int> rankedNavigationMatches(const QVector<NavigationItem>& items,
                                     const QString& query,
                                     const QString& groupId)
{
    struct Match { int index = -1; int score = 0; };
    QVector<Match> matches;
    matches.reserve(items.size());
    const QString wantedGroup = groupId.trimmed();
    for (int i = 0; i < items.size(); ++i) {
        const NavigationItem& item = items.at(i);
        if (!wantedGroup.isEmpty() && wantedGroup != QLatin1String("all") &&
            navigationGroupId(item.kind) != wantedGroup) continue;
        const int score = textualScore(item, query);
        if (score >= 0) matches.push_back({i, score});
    }
    if (!query.trimmed().isEmpty()) {
        std::stable_sort(matches.begin(), matches.end(), [&](const Match& a, const Match& b) {
            if (a.score != b.score) return a.score > b.score;
            return QString::localeAwareCompare(items.at(a.index).label, items.at(b.index).label) < 0;
        });
    }
    QVector<int> result;
    result.reserve(matches.size());
    for (const Match& match : matches) result.push_back(match.index);
    return result;
}

} // namespace ui
