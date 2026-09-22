#pragma once

#include <QString>

namespace core {

enum class RpgMakerEngine
{
    MV,
    MZ
};

inline QString rpgMakerEngineId(RpgMakerEngine engine)
{
    return engine == RpgMakerEngine::MV ? QStringLiteral("mv") : QStringLiteral("mz");
}

inline QString rpgMakerEngineName(RpgMakerEngine engine)
{
    return engine == RpgMakerEngine::MV ? QStringLiteral("RPG Maker MV") : QStringLiteral("RPG Maker MZ");
}

inline QString rpgMakerProjectKind(RpgMakerEngine engine)
{
    return engine == RpgMakerEngine::MV
               ? QStringLiteral("rpg-maker-mv-map")
               : QStringLiteral("rpg-maker-mz-map");
}

inline QString rpgMakerProjectExtension(RpgMakerEngine engine)
{
    return engine == RpgMakerEngine::MV ? QStringLiteral(".rpgproject") : QStringLiteral(".rmmzproject");
}

// O MV trabalha com grade nativa fixa de 48x48. No MZ o LUDO mantém o
// comportamento histórico do editor (32x32 por padrão), já que projetos MZ
// podem usar tamanhos diferentes e vários projetos LUDO existentes usam 32.
inline int rpgMakerDefaultAuthoringTileSize(RpgMakerEngine engine)
{
    return engine == RpgMakerEngine::MV ? 48 : 32;
}

inline RpgMakerEngine rpgMakerEngineFromId(const QString& value,
                                            RpgMakerEngine fallback = RpgMakerEngine::MZ)
{
    const QString id = value.trimmed().toLower();
    if (id == QLatin1String("mv") || id == QLatin1String("rpg-maker-mv") ||
        id == QLatin1String("rpg-maker-mv-map"))
        return RpgMakerEngine::MV;
    if (id == QLatin1String("mz") || id == QLatin1String("rpg-maker-mz") ||
        id == QLatin1String("rpg-maker-mz-map"))
        return RpgMakerEngine::MZ;
    return fallback;
}

inline bool isRpgMakerMv(RpgMakerEngine engine) { return engine == RpgMakerEngine::MV; }
inline bool isRpgMakerMz(RpgMakerEngine engine) { return engine == RpgMakerEngine::MZ; }

} // namespace core
