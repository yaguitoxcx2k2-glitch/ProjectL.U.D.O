#include "MapProjectPayload.h"

#include <QJsonArray>
#include <QString>

namespace core::io::mapproject {
namespace {

void removeKey(QJsonObject& object, const QString& key, bool* changed)
{
    if (!object.contains(key)) return;
    object.remove(key);
    if (changed) *changed = true;
}

void stripMapGameplay(QJsonObject& map, bool* changed)
{
    // O RPG Maker escolhido pelo projeto é a fonte de verdade para lógica/eventos e propriedades
    // de gameplay. O Editor conserva apenas geometria/camadas do mapa.
    removeKey(map, QStringLiteral("environment"), changed);
    removeKey(map, QStringLiteral("encounters"), changed);
    removeKey(map, QStringLiteral("cutsceneRegions"), changed);
    removeKey(map, QStringLiteral("events"), changed);
    removeKey(map, QStringLiteral("spawn"), changed);
}

void stripTilesetGameplay(QJsonObject& tileset, bool* changed)
{
    removeKey(tileset, QStringLiteral("tileFootstepSurfaces"), changed);
}

void stripWangGameplay(QJsonObject& wang, bool* changed)
{
    QJsonArray colors = wang.value(QStringLiteral("colors")).toArray();
    bool localChanged = false;
    for (int i = 0; i < colors.size(); ++i) {
        QJsonObject color = colors.at(i).toObject();
        if (color.contains(QStringLiteral("footstepSurfaceId"))) {
            color.remove(QStringLiteral("footstepSurfaceId"));
            colors[i] = color;
            localChanged = true;
        }
    }
    if (localChanged) {
        wang[QStringLiteral("colors")] = colors;
        if (changed) *changed = true;
    }
}

} // namespace

QJsonObject sanitize(QJsonObject root, bool* changed)
{
    if (changed) *changed = false;

    static const char* const engineKeys[] = {
        "footsteps", "player", "game", "fonts", "iconSet", "input",
        "ludoInputSystem", "ludoCutsceneSkip", "localization",
        "accessibility", "subtitleStyle", "speakerDatabase",
        "textEffectPresets", "titleScreen", "gameUi", "plugins",
        "switches", "variables", "strings", "commonEvents",
        "commandTemplates", "database", "customDatabases", "pictures",
        "terrainSets", "startMapId", "startPosition", "version",
        "engineVersion"
    };
    for (const char* key : engineKeys)
        removeKey(root, QString::fromLatin1(key), changed);

    // Aliases do formato antigo. `maps` é a fonte de verdade moderna.
    removeKey(root, QStringLiteral("map"), changed);
    removeKey(root, QStringLiteral("layers"), changed);

    QJsonArray maps = root.value(QStringLiteral("maps")).toArray();
    for (int i = 0; i < maps.size(); ++i) {
        QJsonObject map = maps.at(i).toObject();
        stripMapGameplay(map, changed);
        maps[i] = map;
    }
    root[QStringLiteral("maps")] = maps;

    QJsonArray tilesets = root.value(QStringLiteral("tilesets")).toArray();
    for (int i = 0; i < tilesets.size(); ++i) {
        QJsonObject tileset = tilesets.at(i).toObject();
        stripTilesetGameplay(tileset, changed);
        tilesets[i] = tileset;
    }
    root[QStringLiteral("tilesets")] = tilesets;

    QJsonArray wangSets = root.value(QStringLiteral("wangSets")).toArray();
    for (int i = 0; i < wangSets.size(); ++i) {
        QJsonObject wang = wangSets.at(i).toObject();
        stripWangGameplay(wang, changed);
        wangSets[i] = wang;
    }
    root[QStringLiteral("wangSets")] = wangSets;

    return root;
}

} // namespace core::io::mapproject
