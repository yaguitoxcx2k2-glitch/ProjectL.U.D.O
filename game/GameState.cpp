#include "GameState.h"

#include "core/GameData.h"
#include "game/RpgSystem.h"

#include <QJsonArray>
#include <QList>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>

using namespace core;

namespace game {

namespace {

constexpr int kRuntimeMapMaxMaps = 2048;
constexpr int kRuntimeMapMaxCellOverrides = 1000000;
constexpr int kRuntimeMapMaxPassageOverrides = 1000000;
constexpr int kRuntimeMapMaxTerrainOverrides = 1000000;
constexpr int kRuntimeMapMaxTilesetRemaps = 16384;

const DatabaseRecord* actorRecord(const Editor& ed, const QString& id)
{
    const auto it = ed.database.constFind(QStringLiteral("actors"));
    if (it == ed.database.cend()) return nullptr;
    for (const DatabaseRecord& record : it.value())
        if (record.id == id) return &record;
    return nullptr;
}

const LayerPtr findLayerRecursive(const QVector<LayerPtr>& nodes, const QString& id)
{
    for (const LayerPtr& layer : nodes) {
        if (!layer) continue;
        if (layer->id == id) return layer;
        if (!layer->children.isEmpty()) {
            const LayerPtr found = findLayerRecursive(layer->children, id);
            if (found) return found;
        }
    }
    return {};
}

int tilesetIndexByStableId(const Editor& ed, const QString& id)
{
    for (int i = 0; i < ed.tilesets.size(); ++i)
        if (ed.tilesets.at(i).id == id) return i;
    return -1;
}

QString tilesetStableId(const Editor& ed, int index)
{
    const Tileset* tileset = ed.tilesetAt(index);
    return tileset ? tileset->id : QString();
}

QString runtimeCellKey(const QString& layerId, int x, int y)
{
    return layerId + QLatin1Char('\n') + QString::number(x) + QLatin1Char('\n') + QString::number(y);
}

QString runtimePointKey(int x, int y)
{
    return QString::number(x) + QLatin1Char(':') + QString::number(y);
}

bool parseRuntimeCellKey(const QString& key, QString* layerId, int* x, int* y)
{
    const int first = key.indexOf(QLatin1Char('\n'));
    const int second = first < 0 ? -1 : key.indexOf(QLatin1Char('\n'), first + 1);
    if (first <= 0 || second <= first + 1) return false;
    bool okX = false, okY = false;
    const int px = key.mid(first + 1, second - first - 1).toInt(&okX);
    const int py = key.mid(second + 1).toInt(&okY);
    if (!okX || !okY) return false;
    if (layerId) *layerId = key.left(first);
    if (x) *x = px;
    if (y) *y = py;
    return true;
}

bool parseRuntimePointKey(const QString& key, int* x, int* y)
{
    const int split = key.indexOf(QLatin1Char(':'));
    if (split <= 0) return false;
    bool okX = false, okY = false;
    const int px = key.left(split).toInt(&okX);
    const int py = key.mid(split + 1).toInt(&okY);
    if (!okX || !okY) return false;
    if (x) *x = px;
    if (y) *y = py;
    return true;
}

} // namespace

void GameState::resetForNewGame(const Editor& ed)
{
    m_pureState.clear();
    m_customDatabaseRuntime.clear();
    m_runtimeMaps.clear();
    if (++m_runtimeMapRevision == 0) m_runtimeMapRevision = 1;
    m_quests.clear();
    m_party.clear();
    for (const SwitchDef& s : ed.switches) m_pureState.setSwitch(s.id, s.initial);
    for (const VariableDef& v : ed.variables) m_pureState.setVariable(v.id, v.initial);
    for (const StringDef& v : ed.strings) m_pureState.setStringValue(v.id, v.initial.toUtf8().toStdString());
    resetCustomDatabasesFrom(ed);

    ensurePartyFrom(ed);
    if (!m_party.isEmpty())
        if (const DatabaseRecord* actor = actorRecord(ed, m_party.first().actorId))
            m_pureState.setGold(qMax(0, actor->data.value(QStringLiteral("initialGold"), 0).toInt()));
    for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"), QStringLiteral("armors")}) {
        const auto it = ed.database.constFind(category);
        if (it == ed.database.cend()) continue;
        for (const DatabaseRecord& record : it.value()) {
            const int amount = qBound(0, record.data.value(QStringLiteral("initialAmount"), 0).toInt(), 9999);
            if (amount > 0) m_pureState.setItemCount(record.id.toUtf8().toStdString(), amount);
        }
    }
}


void GameState::resetFrom(const Editor& ed)
{
    resetForNewGame(ed);
}

void GameState::reconcileLoadedSave(const Editor& ed)
{
    for (const SwitchDef& value : ed.switches)
        if (value.id > 0 && !m_pureState.hasSwitch(value.id)) m_pureState.setSwitch(value.id, value.initial);
    for (const VariableDef& value : ed.variables)
        if (value.id > 0 && !m_pureState.hasVariable(value.id)) m_pureState.setVariable(value.id, value.initial);
    for (const StringDef& value : ed.strings)
        if (value.id > 0 && !m_pureState.hasString(value.id)) m_pureState.setStringValue(value.id, value.initial.toUtf8().toStdString());
    ensureCustomDatabasesFrom(ed);
    ensureRuntimeMapsFrom(ed);
}

void GameState::ensureGlobalsFrom(const Editor& ed)
{
    reconcileLoadedSave(ed);
}


void GameState::resetCustomDatabasesFrom(const Editor& ed)
{
    m_customDatabaseRuntime.clear();
    for (const CustomDatabaseDefinition& database : ed.customDatabases) {
        if (database.mode != CustomDatabaseMode::Runtime || database.id.isEmpty()) continue;
        QHash<QString, QVariantMap> records;
        for (const CustomDatabaseRecord& record : database.records) {
            QVariantMap values;
            for (const CustomDatabaseField& field : database.fields)
                values[field.id] = effectiveCustomDatabaseValue(database, record, field);
            records.insert(record.id, values);
        }
        m_customDatabaseRuntime.insert(database.id, records);
    }
}

void GameState::ensureCustomDatabasesFrom(const Editor& ed)
{
    QSet<QString> validDatabaseIds;
    for (const CustomDatabaseDefinition& database : ed.customDatabases) {
        if (database.mode != CustomDatabaseMode::Runtime || database.id.isEmpty()) continue;
        validDatabaseIds.insert(database.id);
        QHash<QString, QVariantMap>& runtimeRecords = m_customDatabaseRuntime[database.id];
        QSet<QString> validRecordIds;
        for (const CustomDatabaseRecord& record : database.records) {
            validRecordIds.insert(record.id);
            QVariantMap& values = runtimeRecords[record.id];
            QSet<QString> validFieldIds;
            for (const CustomDatabaseField& field : database.fields) {
                validFieldIds.insert(field.id);
                QVariant value = values.contains(field.id) ? values.value(field.id)
                                                           : effectiveCustomDatabaseValue(database, record, field);
                value = normalizeCustomDatabaseValue(value, field);
                if (field.type == CustomDatabaseFieldType::RecordReference && !value.toString().isEmpty()) {
                    const CustomDatabaseDefinition* target = ed.customDatabase(field.referenceDatabaseId);
                    if (!target || !customDatabaseRecordById(*target, value.toString())) value = QString();
                }
                values[field.id] = value;
            }
            const QStringList storedFields = values.keys();
            for (const QString& fieldId : storedFields)
                if (!validFieldIds.contains(fieldId)) values.remove(fieldId);
        }
        const QStringList storedRecords = runtimeRecords.keys();
        for (const QString& recordId : storedRecords)
            if (!validRecordIds.contains(recordId)) runtimeRecords.remove(recordId);
    }
    const QStringList storedDatabases = m_customDatabaseRuntime.keys();
    for (const QString& databaseId : storedDatabases)
        if (!validDatabaseIds.contains(databaseId)) m_customDatabaseRuntime.remove(databaseId);
}

QVariant GameState::customDatabaseValue(const Editor& ed, const QString& databaseId,
                                        const QString& recordId, const QString& fieldId) const
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) return QVariant();
    const CustomDatabaseRecord* record = customDatabaseRecordById(*database, recordId);
    const CustomDatabaseField* field = customDatabaseFieldById(*database, fieldId);
    if (!record || !field) return QVariant();
    if (database->mode == CustomDatabaseMode::Runtime) {
        const auto dbIt = m_customDatabaseRuntime.constFind(databaseId);
        if (dbIt != m_customDatabaseRuntime.cend()) {
            const QHash<QString, QVariantMap>& records = dbIt.value();
            const auto recordIt = records.constFind(recordId);
            if (recordIt != records.cend() && recordIt.value().contains(fieldId))
                return normalizeCustomDatabaseValue(recordIt.value().value(fieldId), *field);
        }
    }
    return effectiveCustomDatabaseValue(*database, *record, *field);
}

bool GameState::setCustomDatabaseValue(const Editor& ed, const QString& databaseId,
                                       const QString& recordId, const QString& fieldId,
                                       const QVariant& incoming, QString* error)
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) { if (error) *error = QStringLiteral("Banco de Dados Personalizado não encontrado."); return false; }
    if (database->mode != CustomDatabaseMode::Runtime) {
        if (error) *error = QStringLiteral("O Banco de Dados Personalizado é somente leitura."); return false;
    }
    const CustomDatabaseRecord* record = customDatabaseRecordById(*database, recordId);
    const CustomDatabaseField* field = customDatabaseFieldById(*database, fieldId);
    if (!record || !field) { if (error) *error = QStringLiteral("Registro ou campo do banco não encontrado."); return false; }
    QVariant value = normalizeCustomDatabaseValue(incoming, *field);
    if (field->type == CustomDatabaseFieldType::RecordReference && !value.toString().isEmpty()) {
        const CustomDatabaseDefinition* target = ed.customDatabase(field->referenceDatabaseId);
        if (!target || !customDatabaseRecordById(*target, value.toString())) {
            if (error) *error = QStringLiteral("A referência aponta para um registro inexistente.");
            return false;
        }
    }
    m_customDatabaseRuntime[databaseId][recordId][fieldId] = value;
    return true;
}

bool GameState::resetCustomDatabaseRecord(const Editor& ed, const QString& databaseId,
                                          const QString& recordId, QString* error)
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) { if (error) *error = QStringLiteral("Banco de Dados Personalizado não encontrado."); return false; }
    if (database->mode != CustomDatabaseMode::Runtime) {
        if (error) *error = QStringLiteral("O Banco de Dados Personalizado é somente leitura."); return false;
    }
    const CustomDatabaseRecord* record = customDatabaseRecordById(*database, recordId);
    if (!record) { if (error) *error = QStringLiteral("Registro não encontrado."); return false; }
    QVariantMap values;
    for (const CustomDatabaseField& field : database->fields)
        values[field.id] = effectiveCustomDatabaseValue(*database, *record, field);
    m_customDatabaseRuntime[databaseId][recordId] = values;
    return true;
}

bool GameState::copyCustomDatabaseRecord(const Editor& ed, const QString& databaseId,
                                         const QString& sourceRecordId, const QString& targetRecordId,
                                         QString* error)
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) { if (error) *error = QStringLiteral("Banco de Dados Personalizado não encontrado."); return false; }
    if (database->mode != CustomDatabaseMode::Runtime) {
        if (error) *error = QStringLiteral("O Banco de Dados Personalizado é somente leitura."); return false;
    }
    if (!customDatabaseRecordById(*database, sourceRecordId) || !customDatabaseRecordById(*database, targetRecordId)) {
        if (error) *error = QStringLiteral("Registro de origem ou destino não encontrado."); return false;
    }
    QVariantMap values;
    for (const CustomDatabaseField& field : database->fields)
        values[field.id] = customDatabaseValue(ed, databaseId, sourceRecordId, field.id);
    m_customDatabaseRuntime[databaseId][targetRecordId] = values;
    return true;
}

static bool customDatabaseCompare(const QVariant& left, const QVariant& right,
                                  const CustomDatabaseField& field, const QString& operation)
{
    const QString op = operation.isEmpty() ? QStringLiteral("equals") : operation;
    if (field.type == CustomDatabaseFieldType::Number) {
        const qint64 a = left.toLongLong(); const qint64 b = right.toLongLong();
        if (op == QLatin1String("notEquals")) return a != b;
        if (op == QLatin1String("less")) return a < b;
        if (op == QLatin1String("lessEqual")) return a <= b;
        if (op == QLatin1String("greater")) return a > b;
        if (op == QLatin1String("greaterEqual")) return a >= b;
        return a == b;
    }
    if (field.type == CustomDatabaseFieldType::Boolean) {
        const bool a = left.toBool(), b = right.toBool();
        return op == QLatin1String("notEquals") ? a != b : a == b;
    }
    const QString a = left.toString(), b = right.toString();
    if (op == QLatin1String("notEquals")) return a != b;
    if (op == QLatin1String("contains")) return a.contains(b, Qt::CaseInsensitive);
    if (op == QLatin1String("startsWith")) return a.startsWith(b, Qt::CaseInsensitive);
    if (op == QLatin1String("endsWith")) return a.endsWith(b, Qt::CaseInsensitive);
    return a == b;
}

QString GameState::findCustomDatabaseRecord(const Editor& ed, const QString& databaseId,
                                            const QString& fieldId, const QString& operation,
                                            const QVariant& needle) const
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) return QString();
    const CustomDatabaseField* field = customDatabaseFieldById(*database, fieldId);
    if (!field) return QString();
    const QVariant normalizedNeedle = normalizeCustomDatabaseValue(needle, *field);
    for (const CustomDatabaseRecord& record : database->records)
        if (customDatabaseCompare(customDatabaseValue(ed, databaseId, record.id, fieldId),
                                  normalizedNeedle, *field, operation)) return record.id;
    return QString();
}

int GameState::customDatabaseRecordCount(const Editor& ed, const QString& databaseId) const
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    return database ? database->records.size() : 0;
}

bool GameState::customDatabaseRecordExists(const Editor& ed, const QString& databaseId,
                                           const QString& recordId) const
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    return database && customDatabaseRecordById(*database, recordId);
}

QStringList GameState::customDatabaseRecordIds(const Editor& ed, const QString& databaseId) const
{
    const CustomDatabaseDefinition* database = ed.customDatabase(databaseId);
    if (!database) return {};
    QStringList ids;
    ids.reserve(database->records.size());
    for (const CustomDatabaseRecord& record : database->records) ids.push_back(record.id);
    return ids;
}

bool GameState::runtimeMapCapacityAvailable(const QString& mapId, int addCells, int addPassage,
                                                int addTerrain, int addRemaps, QString* error) const
{
    const bool createsMap = !m_runtimeMaps.contains(mapId) && (addCells > 0 || addPassage > 0 || addTerrain > 0 || addRemaps > 0);
    if (createsMap && m_runtimeMaps.size() >= kRuntimeMapMaxMaps) {
        if (error) *error = QStringLiteral("O limite defensivo de mapas alterados em runtime foi atingido.");
        return false;
    }
    qint64 cells=0, passage=0, terrain=0, remaps=0;
    for (auto it=m_runtimeMaps.cbegin(); it!=m_runtimeMaps.cend(); ++it) {
        cells += it->cells.size(); passage += it->passage.size();
        terrain += it->terrain.size(); remaps += it->tilesetRemap.size();
    }
    if (cells + addCells > kRuntimeMapMaxCellOverrides ||
        passage + addPassage > kRuntimeMapMaxPassageOverrides ||
        terrain + addTerrain > kRuntimeMapMaxTerrainOverrides ||
        remaps + addRemaps > kRuntimeMapMaxTilesetRemaps) {
        if (error) *error = QStringLiteral("A quantidade de alterações do mapa em runtime excedeu o limite seguro da partida.");
        return false;
    }
    return true;
}

void GameState::pruneRuntimeMapIfEmpty(const QString& mapId)
{
    auto it=m_runtimeMaps.find(mapId);
    if(it!=m_runtimeMaps.end() && it->cells.isEmpty() && it->passage.isEmpty() &&
       it->terrain.isEmpty() && it->tilesetRemap.isEmpty()) m_runtimeMaps.erase(it);
}

core::Cell GameState::runtimeMapCell(const Editor& ed, const QString& mapId,
                                     const QString& layerId, int x, int y) const
{
    const MapDoc* map = ed.mapById(mapId);
    if (!map) return {};
    const LayerPtr layer = findLayerRecursive(map->layers, layerId);
    if (!layer || layer->type != LayerType::Tile || !layer->inBounds(x, y)) return {};

    Cell base = layer->cellAt(x, y);
    const auto mapIt = m_runtimeMaps.constFind(mapId);
    if (mapIt == m_runtimeMaps.cend()) return base;
    const RuntimeMapData& runtime = mapIt.value();
    const auto cellIt = runtime.cells.constFind(runtimeCellKey(layerId, x, y));
    if (cellIt != runtime.cells.cend()) {
        base.clear();
        for (const RuntimeMapTileRef& saved : cellIt.value()) {
            QString stableId = saved.tilesetId;
            const QString remapped = runtime.tilesetRemap.value(stableId);
            if (!remapped.isEmpty()) {
                const int remappedIndex = tilesetIndexByStableId(ed, remapped);
                const Tileset* remappedTileset = ed.tilesetAt(remappedIndex);
                if (remappedTileset && remappedTileset->contains(saved.tx, saved.ty)) stableId = remapped;
            }
            const int tilesetIndex = tilesetIndexByStableId(ed, stableId);
            const Tileset* tileset = ed.tilesetAt(tilesetIndex);
            if (!tileset || !tileset->contains(saved.tx, saved.ty)) continue;
            TileRef tile;
            tile.tilesetIdx = tilesetIndex;
            tile.tx = saved.tx; tile.ty = saved.ty;
            tile.wangSetId = saved.wangSetId; tile.wangColorId = saved.wangColorId;
            base.push_back(tile);
        }
        return base;
    }

    // Sem override de célula ainda pode existir remapeamento de tileset.
    if (runtime.tilesetRemap.isEmpty()) return base;
    for (TileRef& tile : base) {
        const QString sourceId = tilesetStableId(ed, tile.tilesetIdx);
        const QString targetId = runtime.tilesetRemap.value(sourceId);
        if (targetId.isEmpty()) continue;
        const int targetIndex = tilesetIndexByStableId(ed, targetId);
        const Tileset* target = ed.tilesetAt(targetIndex);
        if (target && target->contains(tile.tx, tile.ty)) tile.tilesetIdx = targetIndex;
    }
    return base;
}

bool GameState::runtimeMapCellChanged(const QString& mapId, const QString& layerId, int x, int y) const
{
    const auto mapIt = m_runtimeMaps.constFind(mapId);
    return mapIt != m_runtimeMaps.cend() && mapIt->cells.contains(runtimeCellKey(layerId, x, y));
}

bool GameState::setRuntimeMapTile(const Editor& ed, const QString& mapId, const QString& layerId,
                                  int x, int y, const QString& tilesetId, int tx, int ty,
                                  bool clearCell, QString* error)
{
    const MapDoc* map = ed.mapById(mapId);
    if (!map) { if (error) *error = QStringLiteral("Mapa não encontrado."); return false; }
    const LayerPtr layer = findLayerRecursive(map->layers, layerId);
    if (!layer || layer->type != LayerType::Tile) {
        if (error) *error = QStringLiteral("A camada selecionada não é uma camada de tiles."); return false;
    }
    if (!layer->inBounds(x, y)) { if (error) *error = QStringLiteral("A posição está fora da camada."); return false; }

    RuntimeMapCell cell;
    if (!clearCell) {
        const int tilesetIndex = tilesetIndexByStableId(ed, tilesetId);
        const Tileset* tileset = ed.tilesetAt(tilesetIndex);
        if (!tileset || !tileset->contains(tx, ty)) {
            if (error) *error = QStringLiteral("O tile ou tileset selecionado não existe."); return false;
        }
        RuntimeMapTileRef tile; tile.tilesetId = tilesetId; tile.tx = tx; tile.ty = ty;
        cell.push_back(tile);
    }
    const QString key=runtimeCellKey(layerId,x,y);
    const auto existingMap=m_runtimeMaps.constFind(mapId);
    const int addCells=(existingMap==m_runtimeMaps.cend()||!existingMap->cells.contains(key))?1:0;
    if(!runtimeMapCapacityAvailable(mapId,addCells,0,0,0,error))return false;
    m_runtimeMaps[mapId].cells[key] = cell;
    if (++m_runtimeMapRevision == 0) m_runtimeMapRevision = 1;
    return true;
}

bool GameState::fillRuntimeMapArea(const Editor& ed, const QString& mapId, const QString& layerId,
                                   int x, int y, int width, int height, const QString& tilesetId,
                                   int tx, int ty, bool clearCell, QString* error)
{
    const MapDoc* map = ed.mapById(mapId);
    if (!map) { if (error) *error = QStringLiteral("Mapa não encontrado."); return false; }
    const LayerPtr layer = findLayerRecursive(map->layers, layerId);
    if (!layer || layer->type != LayerType::Tile) {
        if (error) *error = QStringLiteral("A camada selecionada não é uma camada de tiles."); return false;
    }
    width = qBound(1, width, 512); height = qBound(1, height, 512);
    if (!clearCell) {
        const int tilesetIndex = tilesetIndexByStableId(ed, tilesetId);
        const Tileset* tileset = ed.tilesetAt(tilesetIndex);
        if (!tileset || !tileset->contains(tx, ty)) {
            if (error) *error = QStringLiteral("O tile ou tileset selecionado não existe."); return false;
        }
    }
    const auto existingMap=m_runtimeMaps.constFind(mapId);
    int addCells=0, validCells=0;
    for (int oy = 0; oy < height; ++oy) for (int ox = 0; ox < width; ++ox) {
        const qint64 xx64=qint64(x)+ox,yy64=qint64(y)+oy;
        if(xx64<0||yy64<0||xx64>=layer->cols||yy64>=layer->rows)continue;
        ++validCells;
        const QString key=runtimeCellKey(layerId,int(xx64),int(yy64));
        if(existingMap==m_runtimeMaps.cend()||!existingMap->cells.contains(key))++addCells;
    }
    if(validCells==0){if(error)*error=QStringLiteral("A área não intersecta a camada.");return false;}
    if(!runtimeMapCapacityAvailable(mapId,addCells,0,0,0,error))return false;
    RuntimeMapData& runtime = m_runtimeMaps[mapId];
    int changed = 0;
    for (int oy = 0; oy < height; ++oy) for (int ox = 0; ox < width; ++ox) {
        const qint64 xx64 = qint64(x) + ox, yy64 = qint64(y) + oy;
        if (xx64 < 0 || yy64 < 0 || xx64 >= layer->cols || yy64 >= layer->rows) continue;
        const int xx = int(xx64), yy = int(yy64);
        RuntimeMapCell cell;
        if (!clearCell) { RuntimeMapTileRef tile; tile.tilesetId=tilesetId; tile.tx=tx; tile.ty=ty; cell.push_back(tile); }
        runtime.cells[runtimeCellKey(layerId, xx, yy)] = cell; ++changed;
    }
    if (changed > 0 && ++m_runtimeMapRevision == 0) m_runtimeMapRevision = 1;
    if (changed == 0) { pruneRuntimeMapIfEmpty(mapId); if (error) *error = QStringLiteral("A área não intersecta a camada."); }
    return changed > 0;
}

bool GameState::copyRuntimeMapArea(const Editor& ed, const QString& mapId,
                                   const QString& sourceLayerId, int sourceX, int sourceY,
                                   int width, int height, const QString& targetLayerId,
                                   int targetX, int targetY, QString* error)
{
    const MapDoc* map = ed.mapById(mapId);
    if (!map) { if (error) *error = QStringLiteral("Mapa não encontrado."); return false; }
    const LayerPtr source = findLayerRecursive(map->layers, sourceLayerId);
    const LayerPtr target = findLayerRecursive(map->layers, targetLayerId);
    if (!source || !target || source->type != LayerType::Tile || target->type != LayerType::Tile) {
        if (error) *error = QStringLiteral("Origem e destino precisam ser camadas de tiles."); return false;
    }
    width=qBound(1,width,512); height=qBound(1,height,512);
    QVector<QPair<QPoint, Cell>> snapshot;
    snapshot.reserve(qMin(width*height, 262144));
    for (int oy=0; oy<height; ++oy) for (int ox=0; ox<width; ++ox) {
        const qint64 sx64=qint64(sourceX)+ox, sy64=qint64(sourceY)+oy, dx64=qint64(targetX)+ox, dy64=qint64(targetY)+oy;
        if (sx64<0||sy64<0||dx64<0||dy64<0||sx64>=source->cols||sy64>=source->rows||dx64>=target->cols||dy64>=target->rows) continue;
        const int sx=int(sx64),sy=int(sy64),dx=int(dx64),dy=int(dy64);
        snapshot.push_back({QPoint(dx,dy), runtimeMapCell(ed,mapId,sourceLayerId,sx,sy)});
    }
    if (snapshot.isEmpty()) { if(error)*error=QStringLiteral("A área de cópia não possui células válidas."); return false; }
    const auto existingMap=m_runtimeMaps.constFind(mapId);int addCells=0;
    for(const auto& entry:snapshot){const QString key=runtimeCellKey(targetLayerId,entry.first.x(),entry.first.y());if(existingMap==m_runtimeMaps.cend()||!existingMap->cells.contains(key))++addCells;}
    if(!runtimeMapCapacityAvailable(mapId,addCells,0,0,0,error))return false;
    RuntimeMapData& runtime=m_runtimeMaps[mapId];
    for (const auto& entry : snapshot) {
        RuntimeMapCell stored;
        for (const TileRef& tile : entry.second) {
            const QString stableId=tilesetStableId(ed,tile.tilesetIdx); if(stableId.isEmpty())continue;
            RuntimeMapTileRef value;value.tilesetId=stableId;value.tx=tile.tx;value.ty=tile.ty;value.wangSetId=tile.wangSetId;value.wangColorId=tile.wangColorId;stored.push_back(value);
        }
        runtime.cells[runtimeCellKey(targetLayerId,entry.first.x(),entry.first.y())]=stored;
    }
    if (++m_runtimeMapRevision == 0) m_runtimeMapRevision=1;
    return true;
}

int GameState::runtimeMapPassageMask(const QString& mapId, int x, int y, int inheritedMask) const
{
    const auto mapIt=m_runtimeMaps.constFind(mapId); if(mapIt==m_runtimeMaps.cend())return inheritedMask;
    const auto it=mapIt->passage.constFind(runtimePointKey(x,y));
    return it==mapIt->passage.cend()?inheritedMask:qBound(0,it.value(),15);
}

bool GameState::runtimeMapHasPassageOverride(const QString& mapId,int x,int y) const
{
    const auto it=m_runtimeMaps.constFind(mapId);return it!=m_runtimeMaps.cend()&&it->passage.contains(runtimePointKey(x,y));
}

bool GameState::setRuntimeMapPassage(const Editor& ed,const QString& mapId,int x,int y,int mask,QString* error)
{
    const MapDoc* map=ed.mapById(mapId);if(!map){if(error)*error=QStringLiteral("Mapa não encontrado.");return false;}
    if(x<0||y<0||x>=map->map.width||y>=map->map.height){if(error)*error=QStringLiteral("A posição está fora do mapa.");return false;}
    const QString key=runtimePointKey(x,y);const auto existing=m_runtimeMaps.constFind(mapId);const int add=(existing==m_runtimeMaps.cend()||!existing->passage.contains(key))?1:0;if(!runtimeMapCapacityAvailable(mapId,0,add,0,0,error))return false;
    m_runtimeMaps[mapId].passage[key]=qBound(0,mask,15);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;return true;
}

bool GameState::resetRuntimeMapPassage(const QString& mapId,int x,int y)
{
    auto it=m_runtimeMaps.find(mapId);if(it==m_runtimeMaps.end())return false;const bool removed=it->passage.remove(runtimePointKey(x,y))>0;if(removed){pruneRuntimeMapIfEmpty(mapId);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;}return removed;
}

int GameState::runtimeMapTerrain(const QString& mapId,int x,int y,int fallback) const
{
    const auto mapIt=m_runtimeMaps.constFind(mapId);if(mapIt==m_runtimeMaps.cend())return fallback;return mapIt->terrain.value(runtimePointKey(x,y),fallback);
}

bool GameState::runtimeMapHasTerrainOverride(const QString& mapId,int x,int y) const
{
    const auto it=m_runtimeMaps.constFind(mapId);return it!=m_runtimeMaps.cend()&&it->terrain.contains(runtimePointKey(x,y));
}

bool GameState::setRuntimeMapTerrain(const Editor& ed,const QString& mapId,int x,int y,int terrainId,QString* error)
{
    const MapDoc* map=ed.mapById(mapId);if(!map){if(error)*error=QStringLiteral("Mapa não encontrado.");return false;}
    if(x<0||y<0||x>=map->map.width||y>=map->map.height){if(error)*error=QStringLiteral("A posição está fora do mapa.");return false;}
    const QString key=runtimePointKey(x,y);const auto existing=m_runtimeMaps.constFind(mapId);const int add=(existing==m_runtimeMaps.cend()||!existing->terrain.contains(key))?1:0;if(!runtimeMapCapacityAvailable(mapId,0,0,add,0,error))return false;
    m_runtimeMaps[mapId].terrain[key]=qBound(0,terrainId,999999);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;return true;
}

bool GameState::setRuntimeMapTilesetRemap(const Editor& ed,const QString& mapId,const QString& sourceTilesetId,const QString& targetTilesetId,QString* error)
{
    if(!ed.mapById(mapId)){if(error)*error=QStringLiteral("Mapa não encontrado.");return false;}
    const int sourceIndex=tilesetIndexByStableId(ed,sourceTilesetId),targetIndex=tilesetIndexByStableId(ed,targetTilesetId);
    const Tileset* source=ed.tilesetAt(sourceIndex);const Tileset* target=ed.tilesetAt(targetIndex);
    if(!source||!target){if(error)*error=QStringLiteral("Tileset de origem ou destino não encontrado.");return false;}
    if(target->columns<source->columns||target->rows<source->rows){if(error)*error=QStringLiteral("O tileset de destino não cobre todos os índices X/Y do tileset de origem.");return false;}
    if(sourceTilesetId==targetTilesetId){auto it=m_runtimeMaps.find(mapId);if(it!=m_runtimeMaps.end()&&it->tilesetRemap.remove(sourceTilesetId)>0){pruneRuntimeMapIfEmpty(mapId);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;}return true;}
    const auto existing=m_runtimeMaps.constFind(mapId);const int add=(existing==m_runtimeMaps.cend()||!existing->tilesetRemap.contains(sourceTilesetId))?1:0;if(!runtimeMapCapacityAvailable(mapId,0,0,0,add,error))return false;
    m_runtimeMaps[mapId].tilesetRemap[sourceTilesetId]=targetTilesetId;if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;return true;
}

QString GameState::runtimeMapTilesetRemap(const QString& mapId,const QString& sourceTilesetId) const
{
    const auto it=m_runtimeMaps.constFind(mapId);return it==m_runtimeMaps.cend()?QString():it->tilesetRemap.value(sourceTilesetId);
}

bool GameState::resetRuntimeMapTilesetRemap(const QString& mapId,const QString& sourceTilesetId)
{
    auto it=m_runtimeMaps.find(mapId);if(it==m_runtimeMaps.end())return false;const bool removed=it->tilesetRemap.remove(sourceTilesetId)>0;if(removed){pruneRuntimeMapIfEmpty(mapId);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;}return removed;
}

bool GameState::resetRuntimeMapCell(const QString& mapId,const QString& layerId,int x,int y,bool resetPassage,bool resetTerrain)
{
    auto it=m_runtimeMaps.find(mapId);if(it==m_runtimeMaps.end())return false;bool changed=it->cells.remove(runtimeCellKey(layerId,x,y))>0;if(resetPassage)changed=it->passage.remove(runtimePointKey(x,y))>0||changed;if(resetTerrain)changed=it->terrain.remove(runtimePointKey(x,y))>0||changed;if(changed){pruneRuntimeMapIfEmpty(mapId);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;}return changed;
}

int GameState::resetRuntimeMapArea(const Editor& ed,const QString& mapId,int x,int y,int width,int height)
{
    const MapDoc* map=ed.mapById(mapId);if(!map)return 0;width=qBound(1,width,512);height=qBound(1,height,512);auto it=m_runtimeMaps.find(mapId);if(it==m_runtimeMaps.end())return 0;int changed=0;
    const qint64 right=qint64(x)+width,bottom=qint64(y)+height;const auto inside=[&](int cx,int cy){return qint64(cx)>=x&&qint64(cy)>=y&&qint64(cx)<right&&qint64(cy)<bottom;};
    const QStringList cellKeys=it->cells.keys();for(const QString& key:cellKeys){QString layer;int cx=0,cy=0;if(parseRuntimeCellKey(key,&layer,&cx,&cy)&&inside(cx,cy)){it->cells.remove(key);++changed;}}
    const QStringList passageKeys=it->passage.keys();for(const QString& key:passageKeys){int cx=0,cy=0;if(parseRuntimePointKey(key,&cx,&cy)&&inside(cx,cy)){it->passage.remove(key);++changed;}}
    const QStringList terrainKeys=it->terrain.keys();for(const QString& key:terrainKeys){int cx=0,cy=0;if(parseRuntimePointKey(key,&cx,&cy)&&inside(cx,cy)){it->terrain.remove(key);++changed;}}
    if(changed>0){pruneRuntimeMapIfEmpty(mapId);if(++m_runtimeMapRevision==0)m_runtimeMapRevision=1;}return changed;
}

int GameState::resetRuntimeMap(const QString& mapId)
{
    auto it=m_runtimeMaps.find(mapId);if(it==m_runtimeMaps.end())return 0;const int count=it->cells.size()+it->passage.size()+it->terrain.size()+it->tilesetRemap.size();m_runtimeMaps.erase(it);if(count>0&&++m_runtimeMapRevision==0)m_runtimeMapRevision=1;return count;
}

bool GameState::runtimeMapChangedAt(const QString& mapId,int x,int y) const
{
    const auto it=m_runtimeMaps.constFind(mapId);if(it==m_runtimeMaps.cend())return false;const QString point=runtimePointKey(x,y);if(it->passage.contains(point)||it->terrain.contains(point))return true;for(auto c=it->cells.cbegin();c!=it->cells.cend();++c){int cx=0,cy=0;if(parseRuntimeCellKey(c.key(),nullptr,&cx,&cy)&&cx==x&&cy==y)return true;}return false;
}

int GameState::runtimeMapOverrideCount(const QString& mapId) const
{
    auto countOne=[](const RuntimeMapData& data){return data.cells.size()+data.passage.size()+data.terrain.size()+data.tilesetRemap.size();};
    if(!mapId.isEmpty()){const auto it=m_runtimeMaps.constFind(mapId);return it==m_runtimeMaps.cend()?0:countOne(it.value());}int total=0;for(auto it=m_runtimeMaps.cbegin();it!=m_runtimeMaps.cend();++it)total+=countOne(it.value());return total;
}

void GameState::ensureRuntimeMapsFrom(const Editor& ed)
{
    bool changed=false;const QStringList mapIds=m_runtimeMaps.keys();for(const QString& mapId:mapIds){const MapDoc* map=ed.mapById(mapId);if(!map){m_runtimeMaps.remove(mapId);changed=true;continue;}RuntimeMapData& data=m_runtimeMaps[mapId];
        const QStringList cellKeys=data.cells.keys();for(const QString& key:cellKeys){QString layerId;int x=0,y=0;const LayerPtr layer=parseRuntimeCellKey(key,&layerId,&x,&y)?findLayerRecursive(map->layers,layerId):LayerPtr();if(!layer||layer->type!=LayerType::Tile||!layer->inBounds(x,y)){data.cells.remove(key);changed=true;continue;}RuntimeMapCell& cell=data.cells[key];for(int i=cell.size()-1;i>=0;--i){const int idx=tilesetIndexByStableId(ed,cell[i].tilesetId);const Tileset* ts=ed.tilesetAt(idx);if(!ts||!ts->contains(cell[i].tx,cell[i].ty)){cell.remove(i);changed=true;}}}
        for(auto* table:{&data.passage,&data.terrain}){const QStringList keys=table->keys();for(const QString& key:keys){int x=0,y=0;if(!parseRuntimePointKey(key,&x,&y)||x<0||y<0||x>=map->map.width||y>=map->map.height){table->remove(key);changed=true;}}}
        const QStringList remapKeys=data.tilesetRemap.keys();for(const QString& source:remapKeys)if(tilesetIndexByStableId(ed,source)<0||tilesetIndexByStableId(ed,data.tilesetRemap.value(source))<0){data.tilesetRemap.remove(source);changed=true;}
        if(data.cells.isEmpty()&&data.passage.isEmpty()&&data.terrain.isEmpty()&&data.tilesetRemap.isEmpty()){m_runtimeMaps.remove(mapId);changed=true;}
    }if(changed&&++m_runtimeMapRevision==0)m_runtimeMapRevision=1;
}

QStringList GameState::questIds() const
{
    QStringList ids = m_quests.keys();
    ids.sort();
    return ids;
}

void GameState::startQuest(const QString& id, int target)
{
    if (id.isEmpty()) return;
    QuestState& quest = m_quests[id];
    quest.status = QStringLiteral("active");
    quest.progress = 0;
    quest.target = qBound(1, target, 999999999);
}

void GameState::setQuestProgress(const QString& id, int progress)
{
    if (id.isEmpty()) return;
    QuestState& quest = m_quests[id];
    if (quest.target <= 0) quest.target = 1;
    quest.progress = qBound(0, progress, quest.target);
    if (quest.progress >= quest.target) quest.status = QStringLiteral("completed");
    else if (quest.status != QLatin1String("failed")) quest.status = QStringLiteral("active");
}

void GameState::addQuestProgress(const QString& id, int amount)
{
    const QuestState current = quest(id);
    const qint64 sum = qint64(current.progress) + qint64(amount);
    // Qt 6.8 adicionou sobrecargas heterogêneas de qBound; no MinGW, usar
    // literais `int` junto de qint64 deixa a chamada ambígua. A limitação
    // explícita também evita overflow sem depender da versão do Qt.
    const qint64 bounded = sum < qint64(0)
        ? qint64(0) : (sum > qint64(999999999) ? qint64(999999999) : sum);
    setQuestProgress(id, int(bounded));
}

void GameState::setQuestStatus(const QString& id, const QString& status)
{
    if (id.isEmpty()) return;
    QuestState& quest = m_quests[id];
    if (quest.target <= 0) quest.target = 1;
    quest.status = status == QLatin1String("completed") || status == QLatin1String("failed")
        ? status : QStringLiteral("active");
    if (quest.status == QLatin1String("completed")) quest.progress = quest.target;
}

void GameState::ensurePartyFrom(const Editor& ed)
{
    if (!m_party.isEmpty()) return;
    const auto it = ed.database.constFind(QStringLiteral("actors"));
    if (it == ed.database.cend()) return;
    const QVector<DatabaseRecord>& actors = it.value();
    for (const DatabaseRecord& actor : actors) {
        if (!actor.data.value(QStringLiteral("initialParty"), false).toBool()) continue;
        addActor(ed, actor.id);
        if (m_party.size() >= 4) break;
    }
    // Projetos antigos não possuem initialParty. O primeiro personagem passa
    // a ser o herói automaticamente, preservando compatibilidade.
    if (m_party.isEmpty() && !actors.isEmpty()) addActor(ed, actors.first().id);
}

PartyMemberState* GameState::partyMember(const QString& actorId)
{
    for (PartyMemberState& member : m_party)
        if (member.actorId == actorId) return &member;
    return nullptr;
}

const PartyMemberState* GameState::partyMember(const QString& actorId) const
{
    for (const PartyMemberState& member : m_party)
        if (member.actorId == actorId) return &member;
    return nullptr;
}

bool GameState::addActor(const Editor& ed, const QString& actorId, int requestedLevel)
{
    if (actorId.isEmpty() || partyMember(actorId) || m_party.size() >= 4) return false;
    const DatabaseRecord* actor = actorRecord(ed, actorId);
    if (!actor) return false;
    PartyMemberState member;
    member.actorId = actorId;
    const int initialLevel = actor->data.value(QStringLiteral("initialLevel"), 1).toInt();
    const int maximumLevel = qBound(1, actor->data.value(QStringLiteral("maxLevel"), 99).toInt(), 999);
    member.level = qBound(1, requestedLevel > 0 ? requestedLevel : initialLevel, maximumLevel);
    member.experience = requestedLevel > 0
        ? experienceForLevel(member.level)
        : qMax(experienceForLevel(member.level),
               actor->data.value(QStringLiteral("initialExp"), 0).toInt());
    member.weaponId = actor->data.value(QStringLiteral("initialWeaponId")).toString();
    member.armorId = actor->data.value(QStringLiteral("initialArmorId")).toString();
    member.accessoryId = actor->data.value(QStringLiteral("initialAccessoryId")).toString();
    const CombatStats stats = memberStats(ed, member);
    member.hp = stats.maxHp;
    member.mp = stats.maxMp;
    setExperience(ed, member, member.experience);
    m_party.push_back(member);
    return true;
}

bool GameState::removeActor(const QString& actorId)
{
    for (int index = 0; index < m_party.size(); ++index) {
        if (m_party[index].actorId != actorId) continue;
        m_party.remove(index);
        return true;
    }
    return false;
}

QJsonObject GameState::toJson() const
{
    QJsonObject root;
    QJsonArray switches;
    for (int id : m_pureState.switchIds())
        switches.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("value"), m_pureState.switchOn(id)}});
    root[QStringLiteral("switches")] = switches;

    QJsonArray variables;
    for (int id : m_pureState.variableIds())
        variables.append(QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("value"), m_pureState.variable(id)}});
    root[QStringLiteral("variables")] = variables;

    QJsonArray strings;
    for (int id : m_pureState.stringIds()) {
        const std::string raw = m_pureState.stringValue(id);
        strings.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("value"), QString::fromUtf8(raw.data(), int(raw.size()))}});
    }
    root[QStringLiteral("strings")] = strings;

    QJsonArray customDatabases;
    QStringList databaseIds = m_customDatabaseRuntime.keys();
    databaseIds.sort();
    for (const QString& databaseId : databaseIds) {
        QJsonObject databaseObject;
        databaseObject[QStringLiteral("id")] = databaseId;
        QJsonArray records;
        QStringList recordIds = m_customDatabaseRuntime.value(databaseId).keys();
        recordIds.sort();
        for (const QString& recordId : recordIds) {
            QJsonObject recordObject;
            recordObject[QStringLiteral("id")] = recordId;
            recordObject[QStringLiteral("values")] = QJsonObject::fromVariantMap(
                m_customDatabaseRuntime.value(databaseId).value(recordId));
            records.append(recordObject);
        }
        databaseObject[QStringLiteral("records")] = records;
        customDatabases.append(databaseObject);
    }
    root[QStringLiteral("customDatabases")] = customDatabases;

    QJsonArray runtimeMaps;
    QStringList runtimeMapIds=m_runtimeMaps.keys();runtimeMapIds.sort();
    for(const QString& mapId:runtimeMapIds){const RuntimeMapData& data=m_runtimeMaps.value(mapId);QJsonObject mapObject{{QStringLiteral("id"),mapId}};
        QJsonArray cells;QStringList cellKeys=data.cells.keys();cellKeys.sort();for(const QString& key:cellKeys){QString layerId;int x=0,y=0;if(!parseRuntimeCellKey(key,&layerId,&x,&y))continue;QJsonArray tiles;for(const RuntimeMapTileRef& tile:data.cells.value(key))tiles.append(QJsonObject{{QStringLiteral("tilesetId"),tile.tilesetId},{QStringLiteral("tx"),tile.tx},{QStringLiteral("ty"),tile.ty},{QStringLiteral("wangSetId"),tile.wangSetId},{QStringLiteral("wangColorId"),tile.wangColorId}});cells.append(QJsonObject{{QStringLiteral("layerId"),layerId},{QStringLiteral("x"),x},{QStringLiteral("y"),y},{QStringLiteral("tiles"),tiles}});}mapObject[QStringLiteral("cells")]=cells;
        QJsonArray passage;QStringList passageKeys=data.passage.keys();passageKeys.sort();for(const QString& key:passageKeys){int x=0,y=0;if(parseRuntimePointKey(key,&x,&y))passage.append(QJsonObject{{QStringLiteral("x"),x},{QStringLiteral("y"),y},{QStringLiteral("mask"),data.passage.value(key)}});}mapObject[QStringLiteral("passage")]=passage;
        QJsonArray terrain;QStringList terrainKeys=data.terrain.keys();terrainKeys.sort();for(const QString& key:terrainKeys){int x=0,y=0;if(parseRuntimePointKey(key,&x,&y))terrain.append(QJsonObject{{QStringLiteral("x"),x},{QStringLiteral("y"),y},{QStringLiteral("value"),data.terrain.value(key)}});}mapObject[QStringLiteral("terrain")]=terrain;
        QJsonArray remaps;QStringList sources=data.tilesetRemap.keys();sources.sort();for(const QString& source:sources)remaps.append(QJsonObject{{QStringLiteral("source"),source},{QStringLiteral("target"),data.tilesetRemap.value(source)}});mapObject[QStringLiteral("tilesetRemap")]=remaps;
        runtimeMaps.append(mapObject);
    }
    root[QStringLiteral("runtimeMaps")]=runtimeMaps;

    QJsonArray selfSwitches;
    for (const std::string& rawKey : m_pureState.selfSwitchKeys()) {
        const QString key = QString::fromUtf8(rawKey.data(), int(rawKey.size()));
        selfSwitches.append(QJsonObject{{QStringLiteral("key"), key},
                                        {QStringLiteral("value"), m_pureState.selfSwitch(rawKey)}});
    }
    root[QStringLiteral("selfSwitches")] = selfSwitches;

    QJsonArray inventory;
    for (const std::string& rawId : m_pureState.inventoryIds()) {
        const QString id = QString::fromUtf8(rawId.data(), int(rawId.size()));
        inventory.append(QJsonObject{{QStringLiteral("id"), id},
                                     {QStringLiteral("amount"), m_pureState.itemCount(rawId)}});
    }
    root[QStringLiteral("inventory")] = inventory;
    root[QStringLiteral("gold")] = m_pureState.gold();

    QJsonArray quests;
    for (const QString& id : questIds()) {
        const QuestState value = m_quests.value(id);
        quests.append(QJsonObject{{QStringLiteral("id"), id},
                                  {QStringLiteral("status"), value.status},
                                  {QStringLiteral("progress"), value.progress},
                                  {QStringLiteral("target"), value.target}});
    }
    root[QStringLiteral("quests")] = quests;

    QJsonArray party;
    for (const PartyMemberState& member : m_party) {
        QJsonArray states;
        for (const QString& id : member.states) states.append(id);
        QJsonObject stateTurns;
        for(auto it=member.stateTurns.cbegin();it!=member.stateTurns.cend();++it)
            stateTurns[it.key()]=qMax(0,it.value());
        party.append(QJsonObject{
            {QStringLiteral("actorId"), member.actorId},
            {QStringLiteral("level"), member.level},
            {QStringLiteral("experience"), member.experience},
            {QStringLiteral("hp"), member.hp},
            {QStringLiteral("mp"), member.mp},
            {QStringLiteral("weaponId"), member.weaponId},
            {QStringLiteral("armorId"), member.armorId},
            {QStringLiteral("accessoryId"), member.accessoryId},
            {QStringLiteral("states"), states},
            {QStringLiteral("stateTurns"), stateTurns}
        });
    }
    root[QStringLiteral("party")] = party;
    return root;
}

bool GameState::fromJson(const QJsonObject& root, QString* error)
{
    const auto requireArray = [&](const QString& key) {
        return root.contains(key) && root.value(key).isArray();
    };
    if (!requireArray(QStringLiteral("switches")) ||
        !requireArray(QStringLiteral("variables")) ||
        !requireArray(QStringLiteral("selfSwitches")) ||
        !requireArray(QStringLiteral("inventory"))) {
        if (error) *error = QStringLiteral("Estado da partida incompleto.");
        return false;
    }

    QHash<int, bool> switches;
    QHash<int, int> variables;
    QHash<int, QString> strings;
    QHash<QString, QHash<QString, QVariantMap>> customDatabaseRuntime;
    QHash<QString, RuntimeMapData> runtimeMaps;
    QHash<QString, bool> selfSwitches;
    QHash<QString, int> inventory;
    QHash<QString, QuestState> quests;
    QVector<PartyMemberState> party;
    for (const QJsonValue& value : root.value(QStringLiteral("switches")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        switches[object.value(QStringLiteral("id")).toInt()] =
            object.value(QStringLiteral("value")).toBool();
    }
    for (const QJsonValue& value : root.value(QStringLiteral("variables")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        variables[object.value(QStringLiteral("id")).toInt()] =
            object.value(QStringLiteral("value")).toInt();
    }
    // SaveFormat 3 continua válido: `strings` é um campo aditivo opcional.
    for (const QJsonValue& value : root.value(QStringLiteral("strings")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const int id = object.value(QStringLiteral("id")).toInt();
        if (id > 0) strings[id] = object.value(QStringLiteral("value")).toString();
    }
    const QJsonArray savedDatabases = root.value(QStringLiteral("customDatabases")).toArray();
    const int savedDatabaseLimit = qMin(savedDatabases.size(), 512);
    for (int databaseIndex = 0; databaseIndex < savedDatabaseLimit; ++databaseIndex) {
        const QJsonObject databaseObject = savedDatabases.at(databaseIndex).toObject();
        const QString databaseId = databaseObject.value(QStringLiteral("id")).toString();
        if (databaseId.isEmpty()) continue;
        QHash<QString, QVariantMap> records;
        const QJsonArray savedRecords = databaseObject.value(QStringLiteral("records")).toArray();
        const int savedRecordLimit = qMin(savedRecords.size(), 100000);
        for (int recordIndex = 0; recordIndex < savedRecordLimit; ++recordIndex) {
            const QJsonObject recordObject = savedRecords.at(recordIndex).toObject();
            const QString recordId = recordObject.value(QStringLiteral("id")).toString();
            if (recordId.isEmpty()) continue;
            QVariantMap values = recordObject.value(QStringLiteral("values")).toObject().toVariantMap();
            if (values.size() > 512) {
                QVariantMap limited;
                int count = 0;
                for (auto it = values.cbegin(); it != values.cend() && count < 512; ++it, ++count)
                    limited.insert(it.key(), it.value());
                values = limited;
            }
            records.insert(recordId, values);
        }
        customDatabaseRuntime.insert(databaseId, records);
    }

    // Bloco I / RC2.47: budgets GLOBAIS impedem que um save hostil use o
    // limite por mapa para multiplicar milhões de entradas em memória. Os
    // limites continuam muito acima do uso normal, mas são compartilhados por
    // toda a carga do save e só depois reconciliados com os mapas do projeto.
    const QJsonArray savedRuntimeMaps=root.value(QStringLiteral("runtimeMaps")).toArray();
    const int runtimeMapLimit=qMin(savedRuntimeMaps.size(),kRuntimeMapMaxMaps);
    int remainingCells=kRuntimeMapMaxCellOverrides, remainingPassage=kRuntimeMapMaxPassageOverrides, remainingTerrain=kRuntimeMapMaxTerrainOverrides, remainingRemaps=kRuntimeMapMaxTilesetRemaps;
    for(int mapIndex=0;mapIndex<runtimeMapLimit;++mapIndex){
        const QJsonObject mapObject=savedRuntimeMaps.at(mapIndex).toObject();
        const QString mapId=mapObject.value(QStringLiteral("id")).toString();
        if(mapId.isEmpty())continue;
        RuntimeMapData data;
        const QJsonArray cells=mapObject.value(QStringLiteral("cells")).toArray();
        const int cellLimit=qMin(cells.size(),remainingCells);
        for(int i=0;i<cellLimit;++i){
            const QJsonObject o=cells.at(i).toObject();const QString layerId=o.value(QStringLiteral("layerId")).toString();if(layerId.isEmpty())continue;
            const int x=o.value(QStringLiteral("x")).toInt(),y=o.value(QStringLiteral("y")).toInt();RuntimeMapCell cell;const QJsonArray tiles=o.value(QStringLiteral("tiles")).toArray();
            for(int ti=0;ti<qMin(tiles.size(),64);++ti){const QJsonObject t=tiles.at(ti).toObject();RuntimeMapTileRef tile;tile.tilesetId=t.value(QStringLiteral("tilesetId")).toString();tile.tx=t.value(QStringLiteral("tx")).toInt();tile.ty=t.value(QStringLiteral("ty")).toInt();tile.wangSetId=t.value(QStringLiteral("wangSetId")).toString();tile.wangColorId=t.value(QStringLiteral("wangColorId")).toInt(-1);if(!tile.tilesetId.isEmpty())cell.push_back(tile);}
            data.cells.insert(runtimeCellKey(layerId,x,y),cell);
        }
        remainingCells-=cellLimit;
        const QJsonArray passage=mapObject.value(QStringLiteral("passage")).toArray();const int passageLimit=qMin(passage.size(),remainingPassage);
        for(int i=0;i<passageLimit;++i){const QJsonObject o=passage.at(i).toObject();data.passage[runtimePointKey(o.value(QStringLiteral("x")).toInt(),o.value(QStringLiteral("y")).toInt())]=qBound(0,o.value(QStringLiteral("mask")).toInt(),15);}remainingPassage-=passageLimit;
        const QJsonArray terrain=mapObject.value(QStringLiteral("terrain")).toArray();const int terrainLimit=qMin(terrain.size(),remainingTerrain);
        for(int i=0;i<terrainLimit;++i){const QJsonObject o=terrain.at(i).toObject();data.terrain[runtimePointKey(o.value(QStringLiteral("x")).toInt(),o.value(QStringLiteral("y")).toInt())]=qBound(0,o.value(QStringLiteral("value")).toInt(),999999);}remainingTerrain-=terrainLimit;
        const QJsonArray remaps=mapObject.value(QStringLiteral("tilesetRemap")).toArray();const int remapLimit=qMin(remaps.size(),remainingRemaps);
        for(int i=0;i<remapLimit;++i){const QJsonObject o=remaps.at(i).toObject();const QString source=o.value(QStringLiteral("source")).toString(),target=o.value(QStringLiteral("target")).toString();if(!source.isEmpty()&&!target.isEmpty())data.tilesetRemap[source]=target;}remainingRemaps-=remapLimit;
        if(!data.cells.isEmpty()||!data.passage.isEmpty()||!data.terrain.isEmpty()||!data.tilesetRemap.isEmpty())runtimeMaps.insert(mapId,data);
        if(remainingCells<=0&&remainingPassage<=0&&remainingTerrain<=0&&remainingRemaps<=0)break;
    }

    for (const QJsonValue& value : root.value(QStringLiteral("selfSwitches")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString key = object.value(QStringLiteral("key")).toString();
        if (!key.isEmpty()) selfSwitches[key] = object.value(QStringLiteral("value")).toBool();
    }
    for (const QJsonValue& value : root.value(QStringLiteral("inventory")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) inventory[id] = qBound(0, object.value(QStringLiteral("amount")).toInt(), 9999);
    }
    for (const QJsonValue& value : root.value(QStringLiteral("quests")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        QuestState quest;
        quest.target = qBound(1, object.value(QStringLiteral("target")).toInt(1), 999999999);
        quest.progress = qBound(0, object.value(QStringLiteral("progress")).toInt(), quest.target);
        const QString status = object.value(QStringLiteral("status")).toString(QStringLiteral("active"));
        quest.status = status == QLatin1String("completed") || status == QLatin1String("failed")
            ? status : QStringLiteral("active");
        quests[id] = quest;
    }
    // `party` não existia no save v1. A ausência é aceita e o chamador pode
    // completar o grupo a partir do banco do projeto durante a migração.
    for (const QJsonValue& value : root.value(QStringLiteral("party")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        PartyMemberState member;
        member.actorId = object.value(QStringLiteral("actorId")).toString();
        if (member.actorId.isEmpty()) continue;
        bool duplicate = false;
        for (const PartyMemberState& existing : party)
            if (existing.actorId == member.actorId) { duplicate = true; break; }
        if (duplicate) continue;
        member.level = qMax(1, object.value(QStringLiteral("level")).toInt(1));
        member.experience = qMax(0, object.value(QStringLiteral("experience")).toInt());
        member.hp = qMax(0, object.value(QStringLiteral("hp")).toInt(1));
        member.mp = qMax(0, object.value(QStringLiteral("mp")).toInt());
        member.weaponId = object.value(QStringLiteral("weaponId")).toString();
        member.armorId = object.value(QStringLiteral("armorId")).toString();
        member.accessoryId = object.value(QStringLiteral("accessoryId")).toString();
        for (const QJsonValue& state : object.value(QStringLiteral("states")).toArray())
            if (state.isString() && !member.states.contains(state.toString()))
                member.states.push_back(state.toString());
        const QJsonObject stateTurns=object.value(QStringLiteral("stateTurns")).toObject();
        for(auto it=stateTurns.begin();it!=stateTurns.end();++it)
            if(member.states.contains(it.key()))member.stateTurns[it.key()]=qMax(0,it.value().toInt());
        if (party.size() < 4) party.push_back(member);
    }

    m_pureState.clear();
    for (auto it = switches.cbegin(); it != switches.cend(); ++it) m_pureState.setSwitch(it.key(), it.value());
    for (auto it = variables.cbegin(); it != variables.cend(); ++it) m_pureState.setVariable(it.key(), it.value());
    for (auto it = strings.cbegin(); it != strings.cend(); ++it) m_pureState.setStringValue(it.key(), it.value().toUtf8().toStdString());
    m_customDatabaseRuntime = std::move(customDatabaseRuntime);
    m_runtimeMaps = std::move(runtimeMaps);
    if (++m_runtimeMapRevision == 0) m_runtimeMapRevision = 1;
    for (auto it = selfSwitches.cbegin(); it != selfSwitches.cend(); ++it) m_pureState.setSelfSwitch(it.key().toUtf8().toStdString(), it.value());
    for (auto it = inventory.cbegin(); it != inventory.cend(); ++it) m_pureState.setItemCount(it.key().toUtf8().toStdString(), it.value());
    m_quests = std::move(quests);
    m_party = std::move(party);
    setGold(root.value(QStringLiteral("gold")).toInt());
    return true;
}

bool GameState::pageMatches(const QVariantMap& cond, const QString& eventoId) const
{
    if (cond.isEmpty()) return true;                 // sem condição = sempre vale
    const PageConditions c = PageConditions::fromJson(QJsonObject::fromVariantMap(cond));

    // TODAS as condições ativas precisam bater — é a regra do RPG Maker, e é o
    // que permite empilhar páginas cada vez mais específicas.
    if (c.useSwitchA && !switchOn(c.switchAId)) return false;
    if (c.useSwitchB && !switchOn(c.switchBId)) return false;
    if (c.useVariable && !compareWithOp(variable(c.variableId), c.variableOp, c.variableValue))
        return false;
    if (c.useSelfSwitch && !selfSwitch(eventoId, c.selfSwitchLetter)) return false;
    return true;
}

int GameState::choosePage(const MapEvent& ev) const
{
    // De trás para a frente: a última página válida vence. Assim a página 1 é
    // o estado inicial e as seguintes vão "sobrescrevendo" conforme o jogo
    // avança — mesma ordem mental do RPG Maker.
    for (int i = ev.pages.size() - 1; i >= 0; --i)
        if (pageMatches(ev.pages[i].conditions, ev.id)) return i;
    return -1;
}

} // namespace game
