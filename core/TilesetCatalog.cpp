#include "TilesetCatalog.h"

#include "TilesetOps.h"
#include "Wang.h"

#include <QSet>
#include <QPair>
#include <algorithm>
#include <utility>

namespace core {
namespace {

bool wangDataUsesColor(const WangTileData& data, int colorId)
{
    if (colorId < 0) return false;
    if (data.isolatedColorId == colorId) return true;
    for (const QString& position : WangTileData::positions())
        if (data.get(position) == colorId) return true;
    return false;
}

const AnimatedAutotile* animationById(const Tileset& ts, const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (const AnimatedAutotile& animation : ts.animatedAutotiles)
        if (animation.id == id) return &animation;
    return nullptr;
}

QRect explicitAutotileDomain(const Tileset& ts, const TilesetAutotile& definition)
{
    if (const AnimatedAutotile* animation = animationById(ts, definition.animatedAutotileId))
        if (animation->valid()) return animation->baseRect();
    if (definition.hasRegion()) return definition.baseRect();
    return QRect();
}

bool pointBelongsToExplicitDomain(const Tileset& ts, const TilesetAutotile& definition,
                                  const QPoint& canonical)
{
    const QRect domain = explicitAutotileDomain(ts, definition);
    return !domain.isValid() || domain.contains(canonical);
}

bool definitionUsesTile(const Editor& ed, int tilesetIdx,
                        const TilesetAutotile& definition, int tx, int ty)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || !ts->contains(tx, ty)) return false;
    const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);

    // Recursos importados/gerados possuem um dominio fisico explicito. Wang
    // nunca pode fazer uma cor "vazar" para outro Autotile que reutilize a
    // mesma cor no mesmo atlas. Somente recursos legados sem regiao podem ser
    // inferidos exclusivamente pelos rotulos Wang.
    if (!pointBelongsToExplicitDomain(*ts, definition, canonical)) return false;

    if (definition.hasTerrain()) {
        const WangSet* set = wangSetById(ed, definition.wangSetId);
        if (set) {
            const WangTileData data = set->tiles.value(
                WangSet::tileKeyOf(tilesetIdx, canonical.x(), canonical.y()));
            if (wangDataUsesColor(data, definition.wangColorId)) return true;
        }
    }

    if (const AnimatedAutotile* animation = animationById(*ts, definition.animatedAutotileId))
        if (animation->baseRect().contains(canonical)) return true;
    if (definition.hasRegion() && definition.baseRect().contains(canonical)) return true;
    return false;
}

QString defaultAutotileName(const WangSet& set, const WangColor& color)
{
    if (!color.name.trimmed().isEmpty()) return color.name.trimmed();
    if (!set.name.trimmed().isEmpty()) return set.name.trimmed();
    return QObject::tr("Autotile");
}

int clearTerrainBindingLabels(Editor& ed, int tilesetIdx, const QString& setId, int colorId,
                              const QString& excludedAutotileId, bool* colorRemoved = nullptr)
{
    if (colorRemoved) *colorRemoved = false;
    WangSet* set = wangSetById(ed, setId);
    if (!set || colorId < 0) return 0;

    const Tileset* owner = ed.tilesetAt(tilesetIdx);
    const QString ownerId = owner ? owner->id : QString();
    const TilesetAutotile* excluded = nullptr;
    for (const TilesetAutotile& a : ed.autotiles) {
        if (a.id == excludedAutotileId && a.tilesetId == ownerId) {
            excluded = &a;
            break;
        }
    }

    const QRect scope = owner && excluded ? explicitAutotileDomain(*owner, *excluded) : QRect();

    // Recursos que compartilham a mesma cor podem coexistir no mesmo backing
    // legado. Em recurso com dominio explicito limpamos apenas o seu dominio e
    // preservamos qualquer intersecao que pertença a outro recurso. Sem dominio
    // explicito a autoria e ambigua, portanto o comportamento seguro e nao
    // apagar rotulos owner-wide.
    QVector<QRect> protectedDomains;
    bool sharedWithoutDomain = false;
    for (const TilesetAutotile& a : ed.autotiles) {
        if (a.id == excludedAutotileId || a.tilesetId != ownerId) continue;
        if (a.wangSetId != setId || a.wangColorId != colorId) continue;
        if (!owner) { sharedWithoutDomain = true; continue; }
        const QRect other = explicitAutotileDomain(*owner, a);
        if (other.isValid()) protectedDomains.push_back(other);
        else sharedWithoutDomain = true;
    }

    int touched = 0;
    if (scope.isValid() || (!sharedWithoutDomain && protectedDomains.isEmpty())) {
        QStringList dropKeys;
        for (auto it = set->tiles.begin(); it != set->tiles.end(); ++it) {
            int ownerIdx = -1, tx = 0, ty = 0;
            if (!WangSet::parseTileKey(it.key(), &ownerIdx, &tx, &ty) || ownerIdx != tilesetIdx)
                continue;

            const QPoint tile(tx, ty);
            if (scope.isValid() && !scope.contains(tile)) continue;
            bool protectedBySibling = false;
            for (const QRect& other : protectedDomains) {
                if (other.contains(tile)) { protectedBySibling = true; break; }
            }
            if (protectedBySibling) continue;

            WangTileData& data = it.value();
            bool changed = false;
            for (const QString& position : WangTileData::positions()) {
                if (data.get(position) == colorId) {
                    data.set(position, -1);
                    changed = true;
                }
            }
            if (data.isolatedColorId == colorId) {
                data.isolatedColorId = -1;
                changed = true;
            }
            if (changed) ++touched;
            if (data.isEmpty()) dropKeys.push_back(it.key());
        }
        for (const QString& key : dropKeys) set->tiles.remove(key);
    }

    bool colorStillReferenced = false;
    for (const TilesetAutotile& a : ed.autotiles) {
        if (a.id == excludedAutotileId) continue;
        if (a.wangSetId == setId && a.wangColorId == colorId) {
            colorStillReferenced = true;
            break;
        }
    }
    if (!colorStillReferenced) {
        for (auto it = set->tiles.constBegin(); it != set->tiles.constEnd() && !colorStillReferenced; ++it)
            colorStillReferenced = wangDataUsesColor(it.value(), colorId);
    }
    if (!colorStillReferenced) {
        for (int i = 0; i < set->colors.size(); ++i) {
            if (set->colors.at(i).id != colorId) continue;
            set->colors.removeAt(i);
            if (colorRemoved) *colorRemoved = true;
            if (ed.activeWangSet() == set && ed.session.activeWangColorId == colorId)
                ed.session.activeWangColorId = set->colors.isEmpty() ? -1 : set->colors.first().id;
            break;
        }
    }
    return touched;
}

QString requiredCanonicalPresetName(int columns, int rows, const QString& type)
{
    if (type != QLatin1String("mixed")) return QString();
    if (columns == 12 && rows == 4) return QStringLiteral("Terrenos (12x4)");
    if (columns == 4 && rows == 4) return QStringLiteral("4x4");
    return QString();
}

WangPreset canonicalBundledPreset(int columns, int rows, const QString& type)
{
    if (type != QLatin1String("mixed")) return WangPreset();
    if (columns == 12 && rows == 4) return wang::builtinTerrenos12x4();
    if (columns == 4 && rows == 4) return wang::builtinAutotile4x4();
    return WangPreset();
}

// Convenção única de importação: o primeiro tile da última fileira horizontal
// é sempre a variante isolada. O preset bruto continua preservado; trabalhamos
// numa cópia para não reescrever o catálogo/manual presets do usuário.
// Se o preset já possuía o isolado em outro slot, fazemos swap com o conteúdo
// do slot canônico para preservar a geometria. Mais de um isolado ou um slot
// canônico ambíguo é rejeitado em vez de sobrescrito silenciosamente.
bool normalizeAutomaticIsolatedPreset(WangPreset* preset, QString* error = nullptr)
{
    if (!preset || preset->w <= 0 || preset->h <= 0) {
        if (error) *error = QObject::tr("Preset Wang sem dimensões válidas.");
        return false;
    }

    const QString isolatedKey = QStringLiteral("0,%1").arg(preset->h - 1);
    QString previousIsolatedKey;
    int isolatedCount = 0;
    for (auto it = preset->positions.constBegin(); it != preset->positions.constEnd(); ++it) {
        if (!it.value().contains(QStringLiteral("isolated"))) continue;
        ++isolatedCount;
        previousIsolatedKey = it.key();
    }
    if (isolatedCount > 1) {
        if (error) *error = QObject::tr("O preset Wang possui mais de um tile isolado.");
        return false;
    }

    QSet<QString> targetFlags = preset->positions.value(isolatedKey);
    if (targetFlags.contains(QStringLiteral("isolated")) && targetFlags.size() > 1) {
        if (error) *error = QObject::tr("O tile isolado canônico também possui rótulos de vizinhança.");
        return false;
    }

    if (previousIsolatedKey == isolatedKey) {
        preset->positions.insert(isolatedKey, QSet<QString>{ QStringLiteral("isolated") });
        return true;
    }

    if (!previousIsolatedKey.isEmpty()) {
        const QSet<QString> previousFlags = preset->positions.value(previousIsolatedKey);
        if (previousFlags.size() != 1 || !previousFlags.contains(QStringLiteral("isolated"))) {
            if (error) *error = QObject::tr("O tile isolado anterior possui rótulos incompatíveis.");
            return false;
        }
        // Swap: a variante normal que ocupava o primeiro tile da última linha
        // assume a posição antiga do isolado, mantendo o conjunto de máscaras.
        if (targetFlags.isEmpty()) preset->positions.remove(previousIsolatedKey);
        else preset->positions.insert(previousIsolatedKey, targetFlags);
        preset->positions.insert(isolatedKey, QSet<QString>{ QStringLiteral("isolated") });
        return true;
    }

    // Presets fornecidos 12x4 e 4x4 deixam esse slot propositalmente vazio.
    // Se não existe isolado anterior, só promovemos um slot realmente vazio;
    // um preset que usa essa posição como variante precisa ser corrigido na
    // autoria em vez de perder dados durante importação.
    if (!targetFlags.isEmpty()) {
        if (error) *error = QObject::tr(
            "O preset Wang usa o primeiro tile da última fileira como variante normal; "
            "esse slot é reservado ao tile isolado na importação automática.");
        return false;
    }
    preset->positions.insert(isolatedKey, QSet<QString>{ QStringLiteral("isolated") });
    return true;
}

const WangPreset* legacyPersistedPresetByExactName(const Editor& ed, const QString& name,
                                                   int columns, int rows, const QString& type)
{
    if (name.isEmpty()) return nullptr;
    // Usado somente para reconhecer/migrar um vínculo criado pela revisão
    // anterior, quando 12x4 ainda dependia do QSettings. Nunca é a fonte
    // canônica de uma nova importação.
    for (int i = ed.wangPresets.size() - 1; i >= 0; --i) {
        const WangPreset& preset = ed.wangPresets.at(i);
        if (preset.builtin || preset.w != columns || preset.h != rows || preset.type != type)
            continue;
        if (preset.name.trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return &preset;
    }
    return nullptr;
}

QSet<QString> autotileDomainKeys(int tilesetIdx, const QRect& region)
{
    QSet<QString> keys;
    if (!region.isValid()) return keys;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            keys.insert(WangSet::tileKeyOf(tilesetIdx, x, y));
    return keys;
}

bool bindingMatchesPreset(const WangSet& set, int colorId, int tilesetIdx,
                          const QRect& region, const WangPreset& preset)
{
    if (colorId < 0 || !region.isValid() || set.type != preset.type ||
        region.width() != preset.w || region.height() != preset.h)
        return false;
    for (int dy = 0; dy < region.height(); ++dy) {
        for (int dx = 0; dx < region.width(); ++dx) {
            const WangTileData data = set.tiles.value(
                WangSet::tileKeyOf(tilesetIdx, region.x() + dx, region.y() + dy));
            QSet<QString> actual;
            for (const QString& pos : WangTileData::positions())
                if (data.get(pos) == colorId) actual.insert(pos);
            if (data.isolatedColorId == colorId) actual.insert(QStringLiteral("isolated"));
            const QSet<QString> expected = preset.positions.value(
                QStringLiteral("%1,%2").arg(dx).arg(dy));
            if (actual != expected) return false;
        }
    }
    return true;
}

} // namespace

bool autotileGridRequiresAutomaticWang(int columns, int rows, const QString& type)
{
    if (columns <= 0 || rows <= 0) return false;
    if (!requiredCanonicalPresetName(columns, rows, type).isEmpty()) return true;
    return type == QLatin1String("mixed") && columns * rows == 48;
}

const WangSet* wangSetById(const Editor& ed, const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (const WangSet& set : ed.wangSets)
        if (set.id == id) return &set;
    return nullptr;
}

WangSet* wangSetById(Editor& ed, const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (WangSet& set : ed.wangSets)
        if (set.id == id) return &set;
    return nullptr;
}

int tilesetIndexForAutotile(const Editor& ed, const TilesetAutotile& autotile)
{
    if (autotile.tilesetId.isEmpty()) return -1;
    for (int i = 0; i < ed.tilesets.size(); ++i)
        if (ed.tilesets.at(i).id == autotile.tilesetId) return i;
    return -1;
}

bool isVisibleTileset(const Tileset& tileset)
{
    return !tileset.internalAutotileAtlas;
}

QVector<int> visibleTilesetIndices(const Editor& ed)
{
    QVector<int> out;
    for (int i = 0; i < ed.tilesets.size(); ++i)
        if (isVisibleTileset(ed.tilesets.at(i))) out.push_back(i);
    return out;
}

QVector<int> paletteTilesetIndices(const Editor& ed)
{
    QVector<int> out;
    for (int i = 0; i < ed.tilesets.size(); ++i) {
        const Tileset& ts = ed.tilesets.at(i);
        if (isVisibleTileset(ts) && ts.paletteVisible) out.push_back(i);
    }
    return out;
}

QString registerTilesetAutotile(Editor& ed, int tilesetIdx, const QString& name,
                                const QRect& logicalRegion,
                                const QString& animatedAutotileId,
                                const QString& wangSetId, int wangColorId,
                                bool extendAtMapBoundary, const QPoint& previewTile)
{
    Tileset* tileset = ed.tilesetAt(tilesetIdx);
    if (!tileset || logicalRegion.width() <= 0 || logicalRegion.height() <= 0 ||
        logicalRegion.left() < 0 || logicalRegion.top() < 0 ||
        !tileset->contains(logicalRegion.left(), logicalRegion.top()) ||
        !tileset->contains(logicalRegion.right(), logicalRegion.bottom()))
        return QString();

    if (!animatedAutotileId.isEmpty() && !animationById(*tileset, animatedAutotileId))
        return QString();

    // Identidade e global. Reimportar a mesma regiao do mesmo backing preserva
    // ID e Terrain; dados nao fornecidos nunca limpam configuracao existente.
    for (TilesetAutotile& existing : ed.autotiles) {
        if (existing.tilesetId != tileset->id || existing.baseRect() != logicalRegion) continue;
        if (!name.trimmed().isEmpty()) existing.name = name.trimmed();
        if (!animatedAutotileId.isEmpty()) existing.animatedAutotileId = animatedAutotileId;
        if (!wangSetId.isEmpty() && wangColorId >= 0) {
            existing.wangSetId = wangSetId;
            existing.wangColorId = wangColorId;
        }
        if (previewTile.x() >= 0 && previewTile.y() >= 0 &&
            tileset->contains(previewTile.x(), previewTile.y())) {
            existing.previewTx = previewTile.x();
            existing.previewTy = previewTile.y();
        }
        existing.extendAtMapBoundary = extendAtMapBoundary;
        return existing.id;
    }

    TilesetAutotile autotile;
    autotile.name = name.trimmed().isEmpty() ? QObject::tr("Autotile") : name.trimmed();
    autotile.tilesetId = tileset->id;
    autotile.baseX = logicalRegion.x();
    autotile.baseY = logicalRegion.y();
    autotile.cols = logicalRegion.width();
    autotile.rows = logicalRegion.height();
    autotile.animatedAutotileId = animatedAutotileId;
    if (!wangSetId.isEmpty() && wangColorId >= 0) {
        autotile.wangSetId = wangSetId;
        autotile.wangColorId = wangColorId;
    }
    // Contrato visual: primeiro tile da ultima fileira horizontal.
    const QPoint defaultPreview(logicalRegion.left(), logicalRegion.bottom());
    const QPoint selectedPreview = previewTile.x() >= 0 && previewTile.y() >= 0
        ? previewTile : defaultPreview;
    if (tileset->contains(selectedPreview.x(), selectedPreview.y())) {
        autotile.previewTx = selectedPreview.x();
        autotile.previewTy = selectedPreview.y();
    }
    autotile.extendAtMapBoundary = extendAtMapBoundary;
    const QString id = autotile.id;
    ed.autotiles.push_back(autotile);
    return id;
}

bool autoConfigureTilesetAutotileWang(Editor& ed, int tilesetIdx,
                                      const QString& autotileId,
                                      AutotileWangAutoConfigResult* result,
                                      const QString& preferredType)
{
    AutotileWangAutoConfigResult local;
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    const Tileset* tileset = ed.tilesetAt(tilesetIdx);
    if (!autotile || !tileset) {
        if (result) *result = local;
        return false;
    }

    const QRect region = autotile->baseRect();
    if (!region.isValid() || region.width() <= 0 || region.height() <= 0 ||
        !tileset->contains(region.left(), region.top()) ||
        !tileset->contains(region.right(), region.bottom())) {
        if (result) *result = local;
        return false;
    }

    const QString requiredPreset = requiredCanonicalPresetName(
        region.width(), region.height(), preferredType);
    local.requiredPresetName = requiredPreset;

    WangPreset chosen;
    bool found = false;
    bool generatedFallback = false;

    // Grades canônicas fornecidas pelo projeto nunca dependem de QSettings nem
    // de reconstrução matemática. 12x4 usa "Terrenos (12x4)" e 4x4 usa o
    // preset "4x4" exatamente como fornecidos. A variante isolada é adicionada
    // depois, pela convenção única de importação.
    if (!requiredPreset.isEmpty()) {
        chosen = canonicalBundledPreset(region.width(), region.height(), preferredType);
        found = chosen.w == region.width() && chosen.h == region.height() &&
                chosen.type == preferredType && chosen.name == requiredPreset;
        if (!found) {
            local.failureMessage = QObject::tr(
                "O preset interno obrigatório “%1” não está disponível para o Autotile %2×%3.")
                .arg(requiredPreset).arg(region.width()).arg(region.height());
            if (result) *result = local;
            return false;
        }
        local.usedRequiredNamedPreset = true;
        local.usedBundledPreset = true;
    } else {
        // Outras geometrias preservam o comportamento genérico: preset
        // personalizado completo/compatível primeiro; fallback só depois.
        // A completude é avaliada DEPOIS de aplicar a convenção do isolado.
        for (int i = ed.wangPresets.size() - 1; i >= 0; --i) {
            WangPreset candidate = ed.wangPresets.at(i);
            if (candidate.builtin) continue;
            if (candidate.type != preferredType) continue;
            if (!normalizeAutomaticIsolatedPreset(&candidate)) continue;
            if (!wang::presetIsCompleteForGrid(candidate, region.width(), region.height())) continue;
            chosen = candidate;
            found = true;
            local.usedCustomPreset = true;
            break;
        }
        if (!found) {
            for (const WangPreset& rawCandidate : ed.wangPresets) {
                if (rawCandidate.type != preferredType) continue;
                WangPreset candidate = rawCandidate;
                if (!normalizeAutomaticIsolatedPreset(&candidate)) continue;
                if (!wang::presetIsCompleteForGrid(candidate, region.width(), region.height())) continue;
                chosen = candidate;
                found = true;
                local.usedCustomPreset = !rawCandidate.builtin;
                break;
            }
        }
        if (!found && preferredType == QLatin1String("mixed") &&
            region.width() * region.height() == 48) {
            chosen = wang::builtinBlob47ForGrid(region.width(), region.height());
            found = normalizeAutomaticIsolatedPreset(&chosen) &&
                    wang::presetIsCompleteForGrid(chosen, region.width(), region.height());
            generatedFallback = found;
        }
        if (!found) {
            if (result) *result = local;
            return false;
        }
    }

    QString isolatedError;
    if (!normalizeAutomaticIsolatedPreset(&chosen, &isolatedError)) {
        local.failureMessage = QObject::tr(
            "O preset Wang “%1” não respeita a convenção do tile isolado: %2")
            .arg(chosen.name, isolatedError);
        if (result) *result = local;
        return false;
    }

    const QSet<QString> allowedTileKeys = autotileDomainKeys(tilesetIdx, region);

    // Recurso já configurado: nunca sobrescrevemos autoria manual. Para grades
    // canônicas, migramos somente vínculos que correspondem exatamente a uma
    // configuração automática anterior (fallback gerado ou preset persistido).
    // A Wang Color do próprio recurso é a única coisa reescrita.
    if (autotile->hasTerrain()) {
        WangSet* currentSet = wangSetById(ed, autotile->wangSetId);
        if (currentSet && !requiredPreset.isEmpty()) {
            if (bindingMatchesPreset(*currentSet, autotile->wangColorId,
                                     tilesetIdx, region, chosen)) {
                local.configured = true;
                local.alreadyConfigured = true;
                local.presetId = chosen.id;
                local.presetName = chosen.name;
                local.wangSetId = autotile->wangSetId;
                local.wangColorId = autotile->wangColorId;
                if (result) *result = local;
                return true;
            }

            const WangPreset oldGenerated = wang::builtinBlob47ForGrid(
                region.width(), region.height());
            const WangPreset* oldPersisted = legacyPersistedPresetByExactName(
                ed, requiredPreset, region.width(), region.height(), preferredType);
            const bool matchesPreviousAutoConfig =
                bindingMatchesPreset(*currentSet, autotile->wangColorId,
                                     tilesetIdx, region, oldGenerated) ||
                (oldPersisted && oldPersisted->id != chosen.id &&
                 bindingMatchesPreset(*currentSet, autotile->wangColorId,
                                      tilesetIdx, region, *oldPersisted));
            if (matchesPreviousAutoConfig) {
                WangSet staged = *currentSet;
                staged.type = chosen.type;
                const int touched = wang::applyPreset(staged, chosen, tilesetIdx,
                                                      region.x(), region.y(),
                                                      autotile->wangColorId,
                                                      region, allowedTileKeys);
                if (touched > 0) {
                    *currentSet = staged;
                    ed.markDirty();
                    emit ed.wangChanged();
                    local.configured = true;
                    local.migratedGeneratedFallback = true;
                    local.presetId = chosen.id;
                    local.presetName = chosen.name;
                    local.wangSetId = autotile->wangSetId;
                    local.wangColorId = autotile->wangColorId;
                    local.labeledTiles = touched;
                    if (result) *result = local;
                    return true;
                }
            }
        }

        local.configured = true;
        local.alreadyConfigured = true;
        local.wangSetId = autotile->wangSetId;
        local.wangColorId = autotile->wangColorId;
        if (result) *result = local;
        return true;
    }

    // Montagem inteiramente local: somente depois de o preset ser aplicado
    // com sucesso o novo Wang Set entra no Editor. Isso mantém a importação
    // transacional e impede tocar em Terrains existentes em caso de falha.
    WangSet set;
    set.name = autotile->name.trimmed().isEmpty()
        ? QObject::tr("Autotile") : autotile->name.trimmed();
    set.type = chosen.type.trimmed().isEmpty() ? QStringLiteral("mixed") : chosen.type;
    WangColor color;
    color.id = 1;
    color.name = set.name;
    color.color = QColor("#4a90d7");
    color.hasIcon = true;
    color.iconTilesetIdx = tilesetIdx;
    color.iconTx = autotile->previewTx >= 0 ? autotile->previewTx : region.left();
    color.iconTy = autotile->previewTy >= 0 ? autotile->previewTy : region.bottom();
    set.colors.push_back(color);
    set.hasIcon = true;
    set.iconTilesetIdx = color.iconTilesetIdx;
    set.iconTx = color.iconTx;
    set.iconTy = color.iconTy;

    const int touched = wang::applyPreset(set, chosen, tilesetIdx,
                                          region.x(), region.y(), color.id,
                                          region, allowedTileKeys);
    const bool requireCompleteCoverage = requiredPreset.isEmpty();
    if (touched <= 0 || (requireCompleteCoverage &&
                         wang::coverage(set, color.id, allowedTileKeys) < 0.999)) {
        if (local.failureMessage.isEmpty()) {
            local.failureMessage = QObject::tr(
                "O preset Wang “%1” não produziu cobertura completa para o Autotile %2×%3.")
                .arg(chosen.name).arg(region.width()).arg(region.height());
        }
        if (result) *result = local;
        return false;
    }

    const QString setId = set.id;
    ed.wangSets.push_back(set);
    autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) {
        ed.wangSets.removeLast();
        if (result) *result = local;
        return false;
    }
    autotile->wangSetId = setId;
    autotile->wangColorId = color.id;
    if (generatedFallback) {
        bool presetKnown = false;
        for (const WangPreset& preset : ed.wangPresets)
            if (preset.id == chosen.id) { presetKnown = true; break; }
        if (!presetKnown) ed.wangPresets.push_back(chosen);
    }
    ed.markDirty();
    emit ed.wangChanged();

    local.configured = true;
    local.presetId = chosen.id;
    local.presetName = chosen.name;
    local.wangSetId = setId;
    local.wangColorId = color.id;
    local.labeledTiles = touched;
    if (result) *result = local;
    return true;
}

QVector<QPoint> tilesetAutotileTiles(const Editor& ed, int tilesetIdx,
                                     const TilesetAutotile& autotile)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) return {};

    QSet<QPoint> unique;
    if (autotile.hasTerrain()) {
        if (const WangSet* set = wangSetById(ed, autotile.wangSetId)) {
            for (auto it = set->tiles.constBegin(); it != set->tiles.constEnd(); ++it) {
                int owner = -1, tx = 0, ty = 0;
                if (!WangSet::parseTileKey(it.key(), &owner, &tx, &ty) || owner != tilesetIdx)
                    continue;
                if (!ts->contains(tx, ty) || !wangDataUsesColor(it.value(), autotile.wangColorId))
                    continue;
                const QPoint canonical = canonicalAnimatedTile(*ts, tx, ty);
                if (pointBelongsToExplicitDomain(*ts, autotile, canonical))
                    unique.insert(canonical);
            }
        }
    }

    if (const AnimatedAutotile* animation = animationById(*ts, autotile.animatedAutotileId)) {
        for (int y = animation->baseY; y < animation->baseY + animation->rows; ++y)
            for (int x = animation->baseX; x < animation->baseX + animation->cols; ++x)
                if (ts->contains(x, y)) unique.insert(QPoint(x, y));
    }
    if (autotile.hasRegion()) {
        for (int y = autotile.baseY; y < autotile.baseY + autotile.rows; ++y)
            for (int x = autotile.baseX; x < autotile.baseX + autotile.cols; ++x)
                if (ts->contains(x, y)) unique.insert(QPoint(x, y));
    }

    QVector<QPoint> out;
    out.reserve(unique.size());
    for (const QPoint& tile : unique) out.push_back(tile);
    std::sort(out.begin(), out.end(), [](const QPoint& a, const QPoint& b) {
        return a.y() == b.y() ? a.x() < b.x() : a.y() < b.y();
    });
    return out;
}

QRect tilesetAutotileBounds(const Editor& ed, int tilesetIdx,
                            const TilesetAutotile& autotile)
{
    const QVector<QPoint> tiles = tilesetAutotileTiles(ed, tilesetIdx, autotile);
    if (tiles.isEmpty()) return QRect();
    int left = tiles.first().x(), right = left, top = tiles.first().y(), bottom = top;
    for (const QPoint& tile : tiles) {
        left = qMin(left, tile.x()); right = qMax(right, tile.x());
        top = qMin(top, tile.y()); bottom = qMax(bottom, tile.y());
    }
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

const TilesetAutotile* tilesetAutotileAt(const Editor& ed, int tilesetIdx,
                                         int tx, int ty)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) return nullptr;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        if (autotile.tilesetId != ts->id) continue;
        if (definitionUsesTile(ed, tilesetIdx, autotile, tx, ty)) return &autotile;
    }
    return nullptr;
}

TilesetReverseSelection resolveTilesetReverseSelection(const Editor& ed,
                                                        const TileRef& tile)
{
    TilesetReverseSelection out;
    if (!tile.isValid()) return out;
    const Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
    if (!ts) return out;

    const QPoint canonical = canonicalAnimatedTile(*ts, tile.tx, tile.ty);
    if (!ts->contains(canonical.x(), canonical.y())) return out;
    out.tilesetIdx = tile.tilesetIdx;
    out.tile = canonical;

    const TilesetAutotile* match = nullptr;
    // Uma celula Terrain ja carrega a semantica Wang que a produziu. Em
    // regioes sobrepostas, isso e mais forte que a mera posicao no atlas.
    if (!tile.wangSetId.isEmpty() && tile.wangColorId >= 0) {
        for (const TilesetAutotile& autotile : ed.autotiles) {
            if (autotile.tilesetId != ts->id) continue;
            if (autotile.wangSetId != tile.wangSetId ||
                autotile.wangColorId != tile.wangColorId) continue;
            if (definitionUsesTile(ed, tile.tilesetIdx, autotile, canonical.x(), canonical.y())) {
                match = &autotile;
                break;
            }
        }
    }
    if (!match)
        match = tilesetAutotileAt(ed, tile.tilesetIdx, canonical.x(), canonical.y());

    if (match) {
        out.autotileId = match->id;
        out.wangSetId = match->wangSetId;
        out.wangColorId = match->wangColorId;
    }
    return out;
}

bool applyTilesetReverseSelection(Editor& ed, const TilesetReverseSelection& selection)
{
    if (!selection.valid()) return false;
    const Tileset* ts = ed.tilesetAt(selection.tilesetIdx);
    if (!ts || !ts->contains(selection.tile.x(), selection.tile.y())) return false;

    if (selection.isAutotile())
        return activateTilesetAutotileForPainting(ed, selection.tilesetIdx,
                                                   selection.autotileId);

    ed.session.authoringContext = AuthoringContext::Tileset;
    ed.session.activeTilesetIdx = selection.tilesetIdx;
    ed.session.activeAutotileId.clear();
    ed.session.customStamp.clear();
    ed.session.tsSel = TilesetSelection{selection.tilesetIdx, selection.tile.x(),
                                selection.tile.y(), 1, 1};
    ed.session.tool = Tool::Stamp;
    ed.session.wangBrushActive = false;
    emit ed.selectionChanged();
    return true;
}

bool selectPlacedTileInPalette(Editor& ed, const TileRef& tile,
                               TilesetReverseSelection* resolved)
{
    const TilesetReverseSelection selection = resolveTilesetReverseSelection(ed, tile);
    if (resolved) *resolved = selection;
    return applyTilesetReverseSelection(ed, selection);
}

QVector<TilesetAutotileInfo> tilesetAutotileCatalog(const Editor& ed)
{
    QVector<TilesetAutotileInfo> out;
    out.reserve(ed.autotiles.size());
    for (const TilesetAutotile& autotile : ed.autotiles) {
        const int tilesetIdx = tilesetIndexForAutotile(ed, autotile);
        const Tileset* ts = ed.tilesetAt(tilesetIdx);
        if (!ts) continue;
        TilesetAutotileInfo info;
        info.tilesetIdx = tilesetIdx;
        info.tileset = ts;
        info.autotile = &autotile;
        info.wangSet = wangSetById(ed, autotile.wangSetId);
        info.wangColor = info.wangSet ? info.wangSet->colorById(autotile.wangColorId) : nullptr;
        info.tiles = tilesetAutotileTiles(ed, tilesetIdx, autotile);
        info.bounds = tilesetAutotileBounds(ed, tilesetIdx, autotile);
        out.push_back(info);
    }
    return out;
}

const TilesetAutotile* tilesetAutotileById(const Editor& ed, int tilesetIdx,
                                            const QString& autotileId)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || autotileId.isEmpty()) return nullptr;
    for (const TilesetAutotile& autotile : ed.autotiles)
        if (autotile.id == autotileId && autotile.tilesetId == ts->id) return &autotile;
    return nullptr;
}

TilesetAutotile* tilesetAutotileById(Editor& ed, int tilesetIdx,
                                      const QString& autotileId)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || autotileId.isEmpty()) return nullptr;
    for (TilesetAutotile& autotile : ed.autotiles)
        if (autotile.id == autotileId && autotile.tilesetId == ts->id) return &autotile;
    return nullptr;
}

const TilesetAutotile* tilesetAutotileById(const Editor& ed, const QString& autotileId,
                                            int* ownerTilesetIdx)
{
    if (ownerTilesetIdx) *ownerTilesetIdx = -1;
    if (autotileId.isEmpty()) return nullptr;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        if (autotile.id != autotileId) continue;
        const int owner = tilesetIndexForAutotile(ed, autotile);
        if (owner < 0) return nullptr;
        if (ownerTilesetIdx) *ownerTilesetIdx = owner;
        return &autotile;
    }
    return nullptr;
}

bool activateTilesetAutotileForPainting(Editor& ed, int tilesetIdx, const QString& autotileId)
{
    if (ed.activeLayer() && ed.activeLayer()->type == LayerType::Object) return false;
    const TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return false;

    ed.session.authoringContext = AuthoringContext::Tileset;
    // O Tileset normal e o Autotile sao duas paletas independentes. O recurso
    // guarda seu owner no proprio catalogo; nao sequestramos o combo normal.
    ed.session.activeAutotileId = autotile->id;
    ed.session.customStamp.clear();

    const QVector<QPoint> logicalTiles = tilesetAutotileTiles(ed, tilesetIdx, *autotile);
    if (!logicalTiles.isEmpty()) {
        const QPoint first = logicalTiles.first();
        ed.session.tsSel = TilesetSelection{tilesetIdx, first.x(), first.y(), 1, 1};
    } else if (autotile->hasRegion()) {
        ed.session.tsSel = TilesetSelection{tilesetIdx, autotile->baseX, autotile->baseY, 1, 1};
    } else {
        ed.session.tsSel = TilesetSelection();
    }

    ed.session.tool = Tool::Terrain;
    ed.session.wangBrushActive = true;

    int setIndex = -1;
    if (autotile->hasTerrain()) {
        for (int i = 0; i < ed.wangSets.size(); ++i) {
            if (ed.wangSets.at(i).id == autotile->wangSetId) {
                setIndex = i;
                break;
            }
        }
    }
    ed.setActiveWangSet(setIndex);
    if (setIndex < 0) ed.session.activeWangColorId = 0;
    if (setIndex >= 0) {
        if (const WangSet* set = ed.activeWangSet()) {
            if (set->colorById(autotile->wangColorId))
                ed.session.activeWangColorId = autotile->wangColorId;
        }
        emit ed.wangChanged();
    }
    emit ed.selectionChanged();
    return true;
}

void activateRegularTilesetPainting(Editor& ed)
{
    ed.session.authoringContext = AuthoringContext::Tileset;
    ed.session.activeAutotileId.clear();
    ed.session.tool = Tool::Stamp;
    ed.session.wangBrushActive = false;
    emit ed.selectionChanged();
}

bool renameTilesetAutotile(Editor& ed, int tilesetIdx, const QString& autotileId,
                           const QString& name)
{
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    const QString clean = name.trimmed();
    if (!autotile || clean.isEmpty()) return false;
    if (autotile->name == clean) return true;
    autotile->name = clean;
    ed.markDirty();
    return true;
}

bool setTilesetAutotileCategory(Editor& ed, int tilesetIdx, const QString& autotileId,
                                const QString& category)
{
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return false;
    const QString clean = category.trimmed().left(128);
    if (autotile->category == clean) return true;
    autotile->category = clean;
    ed.markDirty();
    return true;
}

QStringList autotileCategories(const Editor& ed)
{
    QSet<QString> unique;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        const QString category = autotile.category.trimmed();
        if (!category.isEmpty()) unique.insert(category);
    }
    QStringList ordered = unique.values();
    ordered.sort(Qt::CaseInsensitive);
    return ordered;
}

namespace {
int rebuildAutotileTopologyImpl(Editor& ed, int tilesetIdx, const QString& autotileId,
                                bool boundaryOnly, bool notify)
{
    const TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile || !autotile->hasTerrain()) return 0;
    const WangSet* set = wangSetById(ed, autotile->wangSetId);
    if (!set || !set->colorById(autotile->wangColorId)) return 0;

    const wang::TerrainPaintContext context = wang::contextForAutotile(ed, tilesetIdx, autotileId);
    int changed = 0;
    bool projectChanged = false;
    for (MapDoc& map : ed.docs) {
        int mapChanged = 0;
        const QVector<LayerPtr> layers = flattenRenderableLayers(map.layers);
        for (const LayerPtr& layer : layers) {
            if (!layer || layer->type != LayerType::Tile) continue;
            mapChanged += boundaryOnly
                ? wang::retileBoundaryCells(layer, *set, autotile->wangColorId, context)
                : wang::retileTerrainCells(layer, *set, autotile->wangColorId, context);
        }
        if (mapChanged > 0) {
            map.dirty = true;
            changed += mapChanged;
            projectChanged = true;
        }
    }
    if (projectChanged) ed.projectDirty = true;
    if (notify && projectChanged) {
        emit ed.docsChanged();
        emit ed.mapChanged();
        emit ed.projectChanged();
    }
    return changed;
}

QVector<QPair<int, QString>> terrainAutotileKeys(const Editor& ed)
{
    QVector<QPair<int, QString>> resources;
    for (const TilesetAutotile& autotile : ed.autotiles) {
        if (!autotile.hasTerrain()) continue;
        const int owner = tilesetIndexForAutotile(ed, autotile);
        if (owner >= 0) resources.push_back(qMakePair(owner, autotile.id));
    }
    return resources;
}
} // namespace

int rebuildTilesetAutotileBoundaryTopology(Editor& ed, int tilesetIdx,
                                           const QString& autotileId, bool notify)
{
    return rebuildAutotileTopologyImpl(ed, tilesetIdx, autotileId, true, notify);
}

int rebuildTilesetAutotileTopology(Editor& ed, int tilesetIdx,
                                   const QString& autotileId, bool notify)
{
    return rebuildAutotileTopologyImpl(ed, tilesetIdx, autotileId, false, notify);
}

int rebuildAllAutotileBoundaryTopologies(Editor& ed, bool notify)
{
    int changed = 0;
    for (const auto& resource : terrainAutotileKeys(ed))
        changed += rebuildTilesetAutotileBoundaryTopology(ed, resource.first, resource.second, false);
    if (notify && changed > 0) {
        emit ed.docsChanged();
        emit ed.mapChanged();
        emit ed.projectChanged();
    }
    return changed;
}

int rebuildAllAutotileTopologies(Editor& ed, bool notify)
{
    int changed = 0;
    for (const auto& resource : terrainAutotileKeys(ed))
        changed += rebuildTilesetAutotileTopology(ed, resource.first, resource.second, false);
    if (notify && changed > 0) {
        emit ed.docsChanged();
        emit ed.mapChanged();
        emit ed.projectChanged();
    }
    return changed;
}

bool setTilesetAutotileBoundaryExtension(Editor& ed, int tilesetIdx,
                                          const QString& autotileId, bool enabled)
{
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return false;
    if (autotile->extendAtMapBoundary == enabled) return true;
    autotile->extendAtMapBoundary = enabled;
    ed.markDirty();
    rebuildTilesetAutotileBoundaryTopology(ed, tilesetIdx, autotileId, true);
    return true;
}

bool setTilesetAutotileTerrain(Editor& ed, int tilesetIdx, const QString& autotileId,
                               const QString& wangSetId, int wangColorId)
{
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    WangSet* targetSet = wangSetById(ed, wangSetId);
    if (!autotile || !targetSet || !targetSet->colorById(wangColorId)) return false;
    if (autotile->wangSetId == wangSetId && autotile->wangColorId == wangColorId) return true;

    const QString oldSetId = autotile->wangSetId;
    const int oldColorId = autotile->wangColorId;
    if (autotile->hasTerrain())
        clearTerrainBindingLabels(ed, tilesetIdx, oldSetId, oldColorId, autotileId);

    // O helper acima pode remover uma cor do vetor e invalidar ponteiros.
    targetSet = wangSetById(ed, wangSetId);
    if (!targetSet || !targetSet->colorById(wangColorId)) return false;
    autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return false;
    autotile->wangSetId = wangSetId;
    autotile->wangColorId = wangColorId;
    for (int i = 0; i < ed.wangSets.size(); ++i) {
        if (ed.wangSets.at(i).id == wangSetId) {
            ed.setActiveWangSet(i);
            break;
        }
    }
    ed.session.activeWangColorId = wangColorId;
    ed.markDirty();
    return true;
}

bool clearTilesetAutotileTerrain(Editor& ed, int tilesetIdx, const QString& autotileId)
{
    TilesetAutotile* autotile = tilesetAutotileById(ed, tilesetIdx, autotileId);
    if (!autotile) return false;
    if (!autotile->hasTerrain()) return true;
    const QString setId = autotile->wangSetId;
    const int colorId = autotile->wangColorId;
    autotile->wangSetId.clear();
    autotile->wangColorId = -1;
    clearTerrainBindingLabels(ed, tilesetIdx, setId, colorId, autotileId);
    ed.markDirty();
    return true;
}

bool removeTilesetAutotile(Editor& ed, int tilesetIdx, const QString& autotileId,
                           TilesetAutotileRemovalResult* result)
{
    Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts || autotileId.trimmed().isEmpty()) return false;
    int index = -1;
    for (int i = 0; i < ed.autotiles.size(); ++i) {
        if (ed.autotiles.at(i).id == autotileId && ed.autotiles.at(i).tilesetId == ts->id) {
            index = i; break;
        }
    }
    if (index < 0) return false;

    TilesetAutotileRemovalResult local;
    const TilesetAutotile removed = ed.autotiles.at(index);
    if (removed.hasTerrain())
        local.terrainTilesCleared = clearTerrainBindingLabels(
            ed, tilesetIdx, removed.wangSetId, removed.wangColorId, autotileId,
            &local.terrainColorRemoved);

    if (removed.animated()) {
        bool animationShared = false;
        for (const TilesetAutotile& a : ed.autotiles) {
            if (a.id != autotileId && a.tilesetId == removed.tilesetId &&
                a.animatedAutotileId == removed.animatedAutotileId) {
                animationShared = true; break;
            }
        }
        if (!animationShared) {
            for (int i = 0; i < ts->animatedAutotiles.size(); ++i) {
                if (ts->animatedAutotiles.at(i).id == removed.animatedAutotileId) {
                    ts->animatedAutotiles.removeAt(i);
                    local.animationRemoved = true;
                    break;
                }
            }
        }
    }

    ed.autotiles.removeAt(index);
    if (ed.session.activeAutotileId == autotileId) activateRegularTilesetPainting(ed);

    // Atlas interno pertence exclusivamente ao recurso. Se ficou sem nenhum
    // Autotile, removemos o backing; Tilesets normais nunca sao tocados aqui.
    if (ts->internalAutotileAtlas) {
        bool stillUsed = false;
        for (const TilesetAutotile& a : ed.autotiles)
            if (a.tilesetId == ts->id) { stillUsed = true; break; }
        if (!stillUsed) {
            ed.removeTileset(tilesetIdx);
            if (result) *result = local;
            return true;
        }
    }

    ed.markDirty();
    if (result) *result = local;
    return true;
}

int reconcileTilesetAutotiles(Editor& ed)
{
    int changes = 0;

    // Limpa referencias quebradas do catalogo GLOBAL, mas nunca apaga Terrain
    // de outro recurso. O owner e resolvido por ID estavel do atlas.
    for (int i = ed.autotiles.size() - 1; i >= 0; --i) {
        TilesetAutotile& autotile = ed.autotiles[i];
        const int ownerIdx = tilesetIndexForAutotile(ed, autotile);
        Tileset* ts = ed.tilesetAt(ownerIdx);
        if (!ts) { ed.autotiles.removeAt(i); ++changes; continue; }
        if (!autotile.wangSetId.isEmpty()) {
            const WangSet* set = wangSetById(ed, autotile.wangSetId);
            if (!set || !set->colorById(autotile.wangColorId)) {
                autotile.wangSetId.clear(); autotile.wangColorId = -1; ++changes;
            }
        }
        if (!autotile.animatedAutotileId.isEmpty() &&
            !animationById(*ts, autotile.animatedAutotileId)) {
            autotile.animatedAutotileId.clear(); ++changes;
        }
        if (!autotile.hasTerrain() && !autotile.animated() && !autotile.hasRegion()) {
            ed.autotiles.removeAt(i); ++changes;
        }
    }

    // Wang legado -> recurso global. Isso e apenas migracao; importadores 2.0
    // registram explicitamente recursos e nao passam por inferencia visual.
    for (int tilesetIdx = 0; tilesetIdx < ed.tilesets.size(); ++tilesetIdx) {
        Tileset& ts = ed.tilesets[tilesetIdx];
        for (const WangSet& set : ed.wangSets) {
            for (const WangColor& color : set.colors) {
                bool usedHere = false;
                for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
                    int owner = -1, tx = 0, ty = 0;
                    if (!WangSet::parseTileKey(it.key(), &owner, &tx, &ty) || owner != tilesetIdx) continue;
                    if (wangDataUsesColor(it.value(), color.id)) { usedHere = true; break; }
                }
                if (!usedHere) continue;

                TilesetAutotile* existing = nullptr;
                for (TilesetAutotile& a : ed.autotiles) {
                    if (a.tilesetId == ts.id && a.wangSetId == set.id && a.wangColorId == color.id) {
                        existing = &a; break;
                    }
                }
                if (!existing) {
                    TilesetAutotile a;
                    a.name = defaultAutotileName(set, color);
                    a.tilesetId = ts.id;
                    a.wangSetId = set.id;
                    a.wangColorId = color.id;
                    ed.autotiles.push_back(a);
                    existing = &ed.autotiles.last();
                    ++changes;
                }
                if (existing->animatedAutotileId.isEmpty()) {
                    const QVector<QPoint> terrainTiles = tilesetAutotileTiles(ed, tilesetIdx, *existing);
                    for (const AnimatedAutotile& animation : ts.animatedAutotiles) {
                        bool overlaps = false;
                        for (const QPoint& tile : terrainTiles)
                            if (animation.baseRect().contains(tile)) { overlaps = true; break; }
                        if (overlaps) { existing->animatedAutotileId = animation.id; ++changes; break; }
                    }
                }
            }
        }

        for (const AnimatedAutotile& animation : ts.animatedAutotiles) {
            bool linked = false;
            for (const TilesetAutotile& a : ed.autotiles)
                if (a.tilesetId == ts.id && a.animatedAutotileId == animation.id) { linked = true; break; }
            if (linked) continue;
            TilesetAutotile a;
            a.name = animation.name.trimmed().isEmpty() ? QObject::tr("Autotile animado") : animation.name.trimmed();
            a.tilesetId = ts.id;
            a.baseX = animation.baseX; a.baseY = animation.baseY;
            a.cols = animation.cols; a.rows = animation.rows;
            a.previewTx = animation.baseX;
            a.previewTy = animation.baseY + qMax(0, animation.rows - 1);
            a.animatedAutotileId = animation.id;
            ed.autotiles.push_back(a);
            ++changes;
        }
    }

    // Migração/autorreparo do contrato Wang automático: projetos salvos entre
    // o Resource Architecture Rework e esta revisão podem conter um backing
    // interno de geometria reconhecida (12x4, 4x4 ou fallback 48 variantes) já
    // registrado, mas ainda sem Terrain/Wang. Esses recursos são inequivocamente
    // produzidos pelos importadores de Autotile,
    // portanto recebem o mesmo setup canônico usado na criação nova. Recursos
    // normais ou geometrias não reconhecidas continuam intocados.
    QVector<QPair<int, QString>> pendingAutoWang;
    pendingAutoWang.reserve(ed.autotiles.size());
    for (const TilesetAutotile& autotile : std::as_const(ed.autotiles)) {
        if (!autotile.hasRegion()) continue;
        const int ownerIdx = tilesetIndexForAutotile(ed, autotile);
        const Tileset* ts = ed.tilesetAt(ownerIdx);
        if (!ts || !ts->internalAutotileAtlas) continue;
        const QRect region = autotile.baseRect();
        if (!region.isValid()) continue;
        const bool canonicalGrid = !requiredCanonicalPresetName(
            region.width(), region.height(), QStringLiteral("mixed")).isEmpty();
        const bool unconfiguredAutoGrid = !autotile.hasTerrain() &&
            (canonicalGrid || region.width() * region.height() == 48);
        const bool maybeOldCanonical = autotile.hasTerrain() && canonicalGrid;
        if (!unconfiguredAutoGrid && !maybeOldCanonical) continue;
        pendingAutoWang.push_back(qMakePair(ownerIdx, autotile.id));
    }
    for (const auto& pending : std::as_const(pendingAutoWang)) {
        AutotileWangAutoConfigResult setup;
        if (autoConfigureTilesetAutotileWang(ed, pending.first, pending.second, &setup) &&
            setup.configured && (!setup.alreadyConfigured || setup.migratedGeneratedFallback))
            ++changes;
    }

    return changes;
}

void regenerateTilesetResourceIds(Tileset& tileset)
{
    tileset.id = idGen();
    for (AnimatedAutotile& animation : tileset.animatedAutotiles) animation.id = idGen();
}

int copyTilesetTerrainLabels(Editor& ed, int sourceTilesetIdx, int targetTilesetIdx)
{
    if (!ed.tilesetAt(sourceTilesetIdx) || !ed.tilesetAt(targetTilesetIdx) ||
        sourceTilesetIdx == targetTilesetIdx) return 0;
    int copied = 0;
    for (WangSet& set : ed.wangSets) {
        QVector<QPair<QString, WangTileData>> additions;
        for (auto it = set.tiles.constBegin(); it != set.tiles.constEnd(); ++it) {
            int owner = -1, tx = 0, ty = 0;
            if (!WangSet::parseTileKey(it.key(), &owner, &tx, &ty) || owner != sourceTilesetIdx)
                continue;
            additions.push_back(qMakePair(WangSet::tileKeyOf(targetTilesetIdx, tx, ty), it.value()));
        }
        for (const auto& addition : additions) {
            set.tiles.insert(addition.first, addition.second);
            ++copied;
        }
    }
    return copied;
}

} // namespace core
