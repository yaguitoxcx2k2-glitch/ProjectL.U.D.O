#include "GameWorld.h"

#include "core/ProjectIO.h"
#include "game/MoveRouteExecutor.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRect>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace core;

namespace game {

static QPoint eventAnchorCell(const QPointF& pos)
{
    // A âncora permanece na célula de origem durante o meio-passo e só muda
    // quando cruza de fato a próxima borda inteira.
    return QPoint(int(std::floor(pos.x() + 1e-9)), int(std::floor(pos.y() + 1e-9)));
}

// ---------------------------------------------------------------- direções --
Dir dirFromDelta(int dx, int dy)
{
    dx = clampi(dx, -1, 1);
    dy = clampi(dy, -1, 1);
    if (dx < 0 && dy < 0) return Dir::UpLeft;
    if (dx > 0 && dy < 0) return Dir::UpRight;
    if (dx < 0 && dy > 0) return Dir::DownLeft;
    if (dx > 0 && dy > 0) return Dir::DownRight;
    if (dx < 0) return Dir::Left;
    if (dx > 0) return Dir::Right;
    if (dy < 0) return Dir::Up;
    return Dir::Down;
}

QPoint deltaFromDir(Dir d)
{
    switch (d) {
    case Dir::Down:      return QPoint( 0,  1);
    case Dir::Left:      return QPoint(-1,  0);
    case Dir::Right:     return QPoint( 1,  0);
    case Dir::Up:        return QPoint( 0, -1);
    case Dir::DownLeft:  return QPoint(-1,  1);
    case Dir::DownRight: return QPoint( 1,  1);
    case Dir::UpLeft:    return QPoint(-1, -1);
    case Dir::UpRight:   return QPoint( 1, -1);
    }
    return QPoint(0, 1);
}

bool isDiagonal(Dir d)
{
    return d == Dir::DownLeft || d == Dir::DownRight
        || d == Dir::UpLeft   || d == Dir::UpRight;
}

Dir cardinalOf(Dir d)
{
    // Diagonal vira o componente HORIZONTAL: é o que o editores de RPG faz quando o
    // charset só tem as quatro linhas clássicas (e é o que fica menos estranho
    // na tela, porque o sprite de perfil serve para os dois lados).
    switch (d) {
    case Dir::DownLeft:
    case Dir::UpLeft:    return Dir::Left;
    case Dir::DownRight:
    case Dir::UpRight:   return Dir::Right;
    default:             return d;
    }
}

int charsetRow(Dir d, Editor::PlayerSettings::SpriteDirs dirs)
{
    if (dirs == Editor::PlayerSettings::Sprite4Dir) d = cardinalOf(d);
    switch (d) {
    case Dir::Down:      return 0;
    case Dir::Left:      return 1;
    case Dir::Right:     return 2;
    case Dir::Up:        return 3;
    case Dir::DownLeft:  return 4;
    case Dir::DownRight: return 5;
    case Dir::UpLeft:    return 6;
    case Dir::UpRight:   return 7;
    }
    return 0;
}

CharsetCell charsetCellFor(Dir d, const Editor::PlayerSettings& ps)
{
    CharsetCell cell;
    if (ps.spriteDirs == Editor::PlayerSettings::Sprite4Dir) {
        cell.row = charsetRow(cardinalOf(d), Editor::PlayerSettings::Sprite4Dir);
        return cell;
    }
    if (!ps.diagonalsInSameRow) {               // 8 linhas, uma por direção
        cell.row = charsetRow(d, Editor::PlayerSettings::Sprite8Dir);
        return cell;
    }
    // 4 linhas: cardeal + a diagonal "seguinte" na mesma linha.
    switch (d) {
    case Dir::Down:      cell.row = 0; break;
    case Dir::DownLeft:  cell.row = 0; cell.colOffset = ps.diagonalColumnOffset(); break;
    case Dir::Left:      cell.row = 1; break;
    case Dir::UpLeft:    cell.row = 1; cell.colOffset = ps.diagonalColumnOffset(); break;
    case Dir::Right:     cell.row = 2; break;
    case Dir::DownRight: cell.row = 2; cell.colOffset = ps.diagonalColumnOffset(); break;
    case Dir::Up:        cell.row = 3; break;
    case Dir::UpRight:   cell.row = 3; cell.colOffset = ps.diagonalColumnOffset(); break;
    }
    return cell;
}

int idleFrameFor(int cols)
{
    cols = qMax(1, cols);
    // formato 4×4 usa quatro padrões por direção e volta ao primeiro quando
    // o personagem para. Mantemos o centro para os formatos ímpares antigos.
    if (cols == 4) return 0;
    return qMax(0, cols / 2);                 // 3 quadros -> 1, 5 quadros -> 2
}

int animFrameForPhase(double halfStepsWalked, int cols,
                      Editor::PlayerSettings::AnimOrder order)
{
    cols = qMax(1, cols);
    if (cols == 1) return 0;

    // Charsets 4x4 seguem a semântica do formato 4×4: quatro padrões em
    // sequência 0-1-2-3 e repouso no padrão 0. Eventos não têm um seletor de
    // ordem próprio, então mesmo a rota "PingPong" interna deve usar esse ciclo.
    if (cols == 4) {
        double ph = std::fmod(halfStepsWalked, 4.0);
        if (ph < 0) ph += 4.0;
        return qBound(0, int(std::floor(ph)), 3);
    }

    // formato clássico de 3 quadros começa pelo quadro parado (o central), pisa para um
    // lado, volta ao centro, pisa para o outro e volta.
    const int len = (order == Editor::PlayerSettings::PingPong) ? (cols - 1) * 2 : cols;
    double ph = std::fmod(halfStepsWalked, double(len));
    if (ph < 0) ph += len;
    int i = qBound(0, int(std::floor(ph)), len - 1);
    if (order == Editor::PlayerSettings::LoopOrder) return i;
    const int idle = idleFrameFor(cols);
    i = (i + len - idle) % len;
    return (i < cols) ? i : (len - i);
}

void PlayerConfig::from(const Editor::PlayerSettings& ps)
{
    tilesPerSecond = ps.tilesPerSecond;
    diagonal   = ps.walksDiagonally();
    halfStep   = ps.halfStep();
    hitboxHalf = ps.effectiveHitbox() == Editor::PlayerSettings::HitboxHalf;
    animCols   = ps.framesPerDirection();
    idleCol    = ps.effectiveIdleFrame();
    animOrder  = ps.animOrder;
}

// ------------------------------------------------------------------- mundo --
World::World(const Editor& editorRef) : ed(editorRef)
{
    reset(QPoint(0, 0));
}

QSize World::mapSizeInCells() const
{
    const MapInfo& info = ed.mapInfo();
    return QSize(qMax(1, info.width), qMax(1, info.height));
}

int World::computeCellMaskUncached(int gx, int gy) const
{
    const MapInfo& info = ed.mapInfo();
    if (gx < 0 || gy < 0 || gx >= info.width || gy >= info.height) return Editor::SideAll;

    const int baseW = qMax(1, info.tileWidth);
    const int baseH = qMax(1, info.tileHeight);
    const int px0 = gx * baseW, py0 = gy * baseH;

    int mask = 0;
    for (const LayerPtr& l : ed.flatLayers()) {
        if (!l || !l->visible || l->type != LayerType::Tile) continue;
        const int lw = qMax(1, l->tileWidth), lh = qMax(1, l->tileHeight);
        const int lx0 = px0 / lw, ly0 = py0 / lh;
        const int lx1 = (px0 + baseW - 1) / lw, ly1 = (py0 + baseH - 1) / lh;
        for (int ly = ly0; ly <= ly1; ++ly)
            for (int lx = lx0; lx <= lx1; ++lx) {
                if (!l->inBounds(lx, ly)) continue;
                const Cell effective = m_runtimeMapCellResolver
                    ? m_runtimeMapCellResolver(l->id, lx, ly) : l->data2D[ly][lx];
                for (const TileRef& t : effective)
                    mask |= ed.collisionMask(t.tilesetIdx, t.tx, t.ty);
            }
    }
    if (m_runtimePassageResolver) mask = m_runtimePassageResolver(gx, gy, mask);
    return mask;
}

void World::rebuildCollisionGrid() const
{
    const MapInfo& info = ed.mapInfo();
    const int width = qMax(0, info.width);
    const int height = qMax(0, info.height);
    m_collisionGridSize = QSize(width, height);
    m_collisionGrid.resize(width * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            m_collisionGrid[y * width + x] = quint8(computeCellMaskUncached(x, y) & Editor::SideAll);
    m_collisionRevision = ed.mapRevision();
    m_collisionRuntimeRevision = m_runtimeMapRevisionResolver ? m_runtimeMapRevisionResolver() : 0;
    ++m_collisionRebuilds;
}

void World::ensureCollisionGridCurrent() const
{
    const MapInfo& info = ed.mapInfo();
    const QSize expected(qMax(0, info.width), qMax(0, info.height));
    const quint64 runtimeRevision = m_runtimeMapRevisionResolver ? m_runtimeMapRevisionResolver() : 0;
    if (m_collisionRevision == ed.mapRevision() && m_collisionRuntimeRevision == runtimeRevision && m_collisionGridSize == expected &&
        m_collisionGrid.size() == expected.width() * expected.height()) return;
    rebuildCollisionGrid();
}

bool World::seedCollisionCacheFromPreload() const
{
    const auto preload = ed.runtimePreloadCache();
    const MapDoc* doc = ed.doc();
    if (!preload || !doc) return false;
    const core::RuntimeMapDerivedCache* prepared = preload->mapCache(doc->id);
    if (!prepared || !prepared->hasCollisionGrid()) return false;
    const QSize expected(qMax(0, doc->map.width), qMax(0, doc->map.height));
    if (prepared->collisionSize != expected) return false;
    m_collisionGridSize = prepared->collisionSize;
    m_collisionGrid = prepared->collisionGrid;
    m_collisionRevision = ed.mapRevision();
    m_collisionRuntimeRevision = 0; // antes de Runtime Map Management entrar em cena
    ++m_collisionRebuilds;
    return true;
}

bool World::seedEventIndexFromPreload() const
{
    const auto preload = ed.runtimePreloadCache();
    const MapDoc* doc = ed.doc();
    if (!preload || !doc) return false;
    const core::RuntimeMapDerivedCache* prepared = preload->mapCache(doc->id);
    if (!prepared) return false;
    // O cache preparado representa as posições iniciais. Durante resetEvents()
    // todos os LiveEvent ainda estão exatamente nessas âncoras.
    m_eventSpatialIndex = prepared->eventSpatialIndex;
    m_eventIndexedCells = prepared->eventIndexedCells;
    m_eventDefinitionIndex = prepared->eventDefinitionIndex;
    m_eventIndexRevision = ed.mapRevision();
    ++m_eventIndexRebuilds;
    return true;
}

int World::eventSpatialKey(const QPoint& cell) const
{
    const QSize size = mapSizeInCells();
    if (cell.x() < 0 || cell.y() < 0 || cell.x() >= size.width() || cell.y() >= size.height()) return -1;
    return cell.y() * size.width() + cell.x();
}

void World::rebuildEventSpatialIndex() const
{
    m_eventSpatialIndex.clear();
    m_eventIndexedCells.clear();
    m_eventDefinitionIndex.clear();
    const MapDoc* doc = ed.doc();
    if (doc) {
        for (int i = 0; i < doc->events.size(); ++i) {
            const MapEvent& ev = doc->events.at(i);
            m_eventDefinitionIndex.insert(ev.id, i);
            const auto live = m_events.constFind(ev.id);
            const QPoint cell = live == m_events.constEnd() ? ev.cell : eventAnchorCell(live->cell);
            const int key = eventSpatialKey(cell);
            if (key < 0) continue;
            m_eventSpatialIndex[key].push_back(ev.id); // ordem do documento preservada
            m_eventIndexedCells.insert(ev.id, cell);
        }
    }
    m_eventIndexRevision = ed.mapRevision();
    ++m_eventIndexRebuilds;
}

void World::ensureEventSpatialIndexCurrent() const
{
    if (m_eventIndexRevision == ed.mapRevision()) return;
    rebuildEventSpatialIndex();
}

const MapEvent* World::eventDefinition(const QString& eventId) const
{
    ensureEventSpatialIndexCurrent();
    const MapDoc* doc = ed.doc();
    if (!doc || eventId.isEmpty()) return nullptr;
    const int idx = m_eventDefinitionIndex.value(eventId, -1);
    if (idx >= 0 && idx < doc->events.size() && doc->events.at(idx).id == eventId)
        return &doc->events.at(idx);
    // Caminho raro: documento foi alterado por acesso direto sem emitir
    // mapChanged(). Mantemos correção e reparamos o índice localmente.
    for (int i = 0; i < doc->events.size(); ++i) {
        if (doc->events.at(i).id == eventId) {
            m_eventDefinitionIndex.insert(eventId, i);
            return &doc->events.at(i);
        }
    }
    return nullptr;
}

const QVector<QString>* World::eventIdsAtCell(const QPoint& cell) const
{
    ensureEventSpatialIndexCurrent();
    const int key = eventSpatialKey(cell);
    if (key < 0) return nullptr;
    const auto it = m_eventSpatialIndex.constFind(key);
    return it == m_eventSpatialIndex.constEnd() ? nullptr : &it.value();
}

void World::removeEventFromSpatialIndex(const QString& eventId)
{
    ensureEventSpatialIndexCurrent();
    const auto oldIt = m_eventIndexedCells.constFind(eventId);
    if (oldIt == m_eventIndexedCells.constEnd()) return;
    const int key = eventSpatialKey(oldIt.value());
    if (key >= 0) {
        auto bucket = m_eventSpatialIndex.find(key);
        if (bucket != m_eventSpatialIndex.end()) {
            bucket->removeAll(eventId);
            if (bucket->isEmpty()) m_eventSpatialIndex.erase(bucket);
        }
    }
    m_eventIndexedCells.remove(eventId);
}

void World::syncEventSpatialCell(const QString& eventId, const QPoint& cell)
{
    ensureEventSpatialIndexCurrent();
    const auto oldIt = m_eventIndexedCells.constFind(eventId);
    if (oldIt != m_eventIndexedCells.constEnd() && oldIt.value() == cell) return;
    if (oldIt != m_eventIndexedCells.constEnd()) removeEventFromSpatialIndex(eventId);

    const int key = eventSpatialKey(cell);
    if (key < 0) return;
    QVector<QString>& bucket = m_eventSpatialIndex[key];
    if (!bucket.contains(eventId)) bucket.push_back(eventId);
    // A implementação antiga escolhia o primeiro evento na ordem do documento.
    // Preservamos esse contrato mesmo depois de um evento se mover de célula.
    std::stable_sort(bucket.begin(), bucket.end(), [this](const QString& a, const QString& b) {
        return m_eventDefinitionIndex.value(a, std::numeric_limits<int>::max()) <
               m_eventDefinitionIndex.value(b, std::numeric_limits<int>::max());
    });
    m_eventIndexedCells.insert(eventId, cell);
    ++m_eventIndexMoves;
}

World::SpatialCacheDiagnostics World::spatialCacheDiagnostics() const
{
    ensureCollisionGridCurrent();
    ensureEventSpatialIndexCurrent();
    SpatialCacheDiagnostics out;
    out.collisionCells = m_collisionGrid.size();
    out.indexedEventCells = m_eventSpatialIndex.size();
    for (auto it = m_eventSpatialIndex.constBegin(); it != m_eventSpatialIndex.constEnd(); ++it)
        out.indexedEventEntries += it.value().size();
    out.collisionRebuilds = m_collisionRebuilds;
    out.eventIndexRebuilds = m_eventIndexRebuilds;
    out.eventIndexMoves = m_eventIndexMoves;
    return out;
}

core::RuntimeMapDerivedCache World::derivedCacheSnapshot() const
{
    ensureCollisionGridCurrent();
    ensureEventSpatialIndexCurrent();
    core::RuntimeMapDerivedCache out;
    out.collisionSize = m_collisionGridSize;
    out.collisionGrid = m_collisionGrid;
    out.eventSpatialIndex = m_eventSpatialIndex;
    out.eventIndexedCells = m_eventIndexedCells;
    out.eventDefinitionIndex = m_eventDefinitionIndex;
    return out;
}

void World::reset(const QPoint& startCell)
{
    // No playtest RC2.53 o Editor pode entregar a grade derivada já preparada.
    // Fora desse caminho (Player exportado, testes, modelo alterado) preservamos
    // a reconstrução defensiva histórica.
    if (!seedCollisionCacheFromPreload()) rebuildCollisionGrid();
    const QPoint cell = findFreeCellNear(startCell);
    m_hx = cell.x() * 2;
    m_hy = cell.y() * 2;
    m_fromHx = m_toHx = m_hx;
    m_fromHy = m_toHy = m_hy;
    m_t = 0.0;
    m_moving = false;
    m_dir = Dir::Down;
    m_steps = 0;
    m_animPhase = 0.0;
    m_playerFootstepDistanceTiles = 0.0;
    m_playerAnimClock = 0.0;
    m_playerRouteFrequency = 3;
    m_playerOpacity = 255;
    m_playerWalkingAnimation = true;
    m_playerSteppingAnimation = false;
    m_playerDirectionFixed = false;
    m_playerThrough = false;
    m_playerTransparent = false;
    m_playerJumping = false;
    m_playerBlend = QStringLiteral("normal");
    m_playerHasGraphicOverride = false;
    m_playerRouteRuntime.cancelAll();
    m_pendingTouchEvent.clear();
    resetEvents();
}

void World::restorePlayer(const QPoint& halfCell, Dir facing)
{
    const QSize size = mapSizeInCells();
    const int wantedHx = qBound(0, halfCell.x(), qMax(0, size.width() * 2 - 2));
    const int wantedHy = qBound(0, halfCell.y(), qMax(0, size.height() * 2 - 2));
    const QPoint wantedCell(wantedHx / 2, wantedHy / 2);

    reset(wantedCell);
    if (config.halfStep && playerCell() == wantedCell) {
        m_hx = m_fromHx = m_toHx = wantedHx;
        m_hy = m_fromHy = m_toHy = wantedHy;
        bool valid = true;
        for (const QPoint& cell : occupiedCells()) {
            if (isBlocked(cell.x(), cell.y())) { valid = false; break; }
        }
        if (!valid) reset(wantedCell);
    }
    const int rawDirection = qBound(0, int(facing), int(Dir::UpRight));
    m_dir = static_cast<Dir>(rawDirection);
    cancelAllMoveRoutes();
}

void World::resetEvents()
{
    m_events.clear();
    const MapDoc* d = ed.doc();
    if (!d) { rebuildEventSpatialIndex(); return; }
    for (const MapEvent& ev : d->events) {
        LiveEvent s;
        s.cell = s.from = s.to = ev.cell;
        const int pi = activePage(ev);
        s.page = pi;
        if (pi >= 0) {
            const EventPage& pg = ev.page(pi);
            s.dir = pg.graphic.dir;
            s.frame = pg.graphic.frame;
            s.walkingAnimation = pg.walkingAnimation;
            s.steppingAnimation = pg.steppingAnimation;
            s.directionFixed = pg.directionFixed;
            s.through = pg.throughWall;
            s.blocksPlayer = pg.blocksPlayer;
            s.speed = pg.moveSpeed;
            s.frequency = pg.moveFrequency;
            s.opacity = qBound(0, pg.graphic.opacity, 255);
            s.blend = pg.graphic.blend;
            if (pg.move == EventMove::Custom && !pg.route.commands.isEmpty())
                s.customRouteRuntime.startAutonomous(pg.route);
        }
        m_events.insert(ev.id, s);
    }
    if (!seedEventIndexFromPreload()) rebuildEventSpatialIndex();
}

bool World::advanceFootstepCadence(double& accumulatedTiles, double walkedTiles)
{
    // Footstep é uma cadência por DISTÂNCIA, não por quantidade de micro-passos.
    // Em half-step: 0,5 + 0,5 = 1,0 tile -> um som. Em passo normal: 1,0 -> um som.
    constexpr double strideTiles = 1.0;
    constexpr double epsilon = 1e-9;
    if (walkedTiles <= 0.0) return false;
    accumulatedTiles = qMax(0.0, accumulatedTiles) + walkedTiles;
    if (accumulatedTiles + epsilon < strideTiles) return false;
    accumulatedTiles = std::fmod(accumulatedTiles, strideTiles);
    if (accumulatedTiles < epsilon) accumulatedTiles = 0.0;
    return true;
}

double World::eventDelay(int frequency) const
{
    static const double delays[] = { 0.0, 1.0, 0.65, 0.38, 0.18, 0.04 };
    return delays[qBound(1, frequency, 5)];
}

QPoint World::eventCell(const QString& eventId) const
{
    auto it = m_events.constFind(eventId);
    return it == m_events.constEnd() ? QPoint() : eventAnchorCell(it->cell);
}

QPoint World::eventHalfCell(const QString& eventId) const
{
    auto it = m_events.constFind(eventId);
    return it == m_events.constEnd() ? QPoint()
                                      : QPoint(qRound(it->cell.x() * 2.0), qRound(it->cell.y() * 2.0));
}

bool World::restoreEventPosition(const QString& eventId, const QPoint& halfCell)
{
    auto it = m_events.find(eventId);
    if (it == m_events.end()) return false;
    const QSize size = mapSizeInCells();
    QPointF cell(qBound(0, halfCell.x(), qMax(0, size.width()*2-2))/2.0,
                 qBound(0, halfCell.y(), qMax(0, size.height()*2-2))/2.0);
    it->cell = it->from = it->to = cell;
    it->moving = false; it->jumping = false; it->progress = 0.0;
    syncEventSpatialCell(eventId, eventAnchorCell(cell));
    return true;
}

QJsonObject World::runtimeState() const
{
    QJsonArray events;
    QStringList ids = m_events.keys();
    ids.sort();
    for (const QString& id : ids) {
        const LiveEvent& state = m_events.value(id);
        const bool routeMovement = state.moving &&
            (state.movementOwner == LiveEvent::MovementOwner::CustomRoute ||
             state.movementOwner == LiveEvent::MovementOwner::ForcedRoute);
        // Se o movimento pertence a uma rota, salva na origem e repete o
        // comando ao carregar. Para autonomia aleatória/aproximação, a
        // fronteira mais próxima continua sendo a escolha visual mais estável.
        const QPointF settled = routeMovement ? state.from
            : (state.moving && state.progress >= 0.5 ? state.to : state.cell);
        const QPoint anchor = eventAnchorCell(settled);
        events.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("x"), anchor.x()},
            {QStringLiteral("y"), anchor.y()},
            {QStringLiteral("xHalf"), qRound(settled.x() * 2.0)},
            {QStringLiteral("yHalf"), qRound(settled.y() * 2.0)},
            {QStringLiteral("direction"), state.dir},
            {QStringLiteral("frame"), state.frame},
            {QStringLiteral("opacity"), state.opacity},
            {QStringLiteral("transparent"), state.transparent},
            {QStringLiteral("through"), state.through},
            {QStringLiteral("blocksPlayer"), state.blocksPlayer},
            {QStringLiteral("blend"), state.blend},
            {QStringLiteral("walkingAnimation"), state.walkingAnimation},
            {QStringLiteral("steppingAnimation"), state.steppingAnimation},
            {QStringLiteral("directionFixed"), state.directionFixed},
            {QStringLiteral("speed"), state.speed},
            {QStringLiteral("frequency"), state.frequency},
            {QStringLiteral("footstepDistanceTiles"), state.footstepDistanceTiles},
            {QStringLiteral("customRouteRuntime"), state.customRouteRuntime.toJson()},
            {QStringLiteral("forcedRouteRuntime"), state.forcedRouteRuntime.toJson()}
        });
    }
    return QJsonObject{
        {QStringLiteral("steps"), m_steps},
        {QStringLiteral("animationPhase"), m_animPhase},
        {QStringLiteral("playerFootstepDistanceTiles"), m_playerFootstepDistanceTiles},
        {QStringLiteral("playerOpacity"), m_playerOpacity},
        {QStringLiteral("playerTransparent"), m_playerTransparent},
        {QStringLiteral("playerThrough"), m_playerThrough},
        {QStringLiteral("playerBlend"), m_playerBlend},
        {QStringLiteral("playerWalkingAnimation"), m_playerWalkingAnimation},
        {QStringLiteral("playerSteppingAnimation"), m_playerSteppingAnimation},
        {QStringLiteral("playerDirectionFixed"), m_playerDirectionFixed},
        {QStringLiteral("playerRouteRuntime"), m_playerRouteRuntime.toJson()},
        {QStringLiteral("nextMoveRouteTicket"), QString::number(m_nextMoveRouteTicket)},
        {QStringLiteral("events"), events}
    };
}

void World::restoreRuntimeState(const QJsonObject& object)
{
    // Restore e cancelamento de cutscene têm contratos diferentes: aqui todo
    // runtime de rota precisa começar limpo antes de receber o snapshot.
    m_playerRouteRuntime.reset();
    for (auto it = m_events.begin(); it != m_events.end(); ++it) {
        it->forcedRouteRuntime.reset();
        it->customRouteRuntime.reset();
        it->movementOwner = LiveEvent::MovementOwner::None;
    }
    m_steps = qMax(0, object.value(QStringLiteral("steps")).toInt());
    m_animPhase = qMax(0.0, object.value(QStringLiteral("animationPhase")).toDouble());
    m_playerFootstepDistanceTiles = qBound(0.0, object.value(QStringLiteral("playerFootstepDistanceTiles")).toDouble(0.0), 0.999999);
    m_playerOpacity = qBound(0, object.value(QStringLiteral("playerOpacity")).toInt(255), 255);
    m_playerTransparent = object.value(QStringLiteral("playerTransparent")).toBool(false);
    m_playerThrough = object.value(QStringLiteral("playerThrough")).toBool(false);
    m_playerBlend = object.value(QStringLiteral("playerBlend")).toString(QStringLiteral("normal"));
    m_playerWalkingAnimation = object.value(QStringLiteral("playerWalkingAnimation")).toBool(true);
    m_playerSteppingAnimation = object.value(QStringLiteral("playerSteppingAnimation")).toBool(false);
    m_playerDirectionFixed = object.value(QStringLiteral("playerDirectionFixed")).toBool(false);
    if (object.value(QStringLiteral("playerRouteRuntime")).isObject())
        m_playerRouteRuntime.fromJson(object.value(QStringLiteral("playerRouteRuntime")).toObject());

    MoveRouteTicket highestTicket = m_playerRouteRuntime.highestTicket();
    const QSize bounds = mapSizeInCells();
    for (const QJsonValue& value : object.value(QStringLiteral("events")).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject saved = value.toObject();
        auto it = m_events.find(saved.value(QStringLiteral("id")).toString());
        if (it == m_events.end()) continue;
        LiveEvent& state = it.value();
        QPointF cell;
        if (saved.contains(QStringLiteral("xHalf")) || saved.contains(QStringLiteral("yHalf"))) {
            const int hx=qBound(0,saved.value(QStringLiteral("xHalf")).toInt(),qMax(0,bounds.width()*2-1));
            const int hy=qBound(0,saved.value(QStringLiteral("yHalf")).toInt(),qMax(0,bounds.height()*2-1));
            cell=QPointF(hx/2.0,hy/2.0);
        } else {
            cell=QPointF(qBound(0,saved.value(QStringLiteral("x")).toInt(),qMax(0,bounds.width()-1)),
                         qBound(0,saved.value(QStringLiteral("y")).toInt(),qMax(0,bounds.height()-1)));
        }
        state.cell = state.from = state.to = cell;
        state.progress = 0.0;
        state.moving = state.jumping = false;
        state.dir = qBound(0, saved.value(QStringLiteral("direction")).toInt(), 7);
        state.frame = qMax(0, saved.value(QStringLiteral("frame")).toInt(1));
        state.opacity = qBound(0, saved.value(QStringLiteral("opacity")).toInt(255), 255);
        state.transparent = saved.value(QStringLiteral("transparent")).toBool(false);
        state.through = saved.value(QStringLiteral("through")).toBool(state.through);
        state.blocksPlayer = saved.value(QStringLiteral("blocksPlayer")).toBool(state.blocksPlayer);
        state.blend = saved.value(QStringLiteral("blend")).toString(QStringLiteral("normal"));
        state.walkingAnimation = saved.value(QStringLiteral("walkingAnimation")).toBool(state.walkingAnimation);
        state.steppingAnimation = saved.value(QStringLiteral("steppingAnimation")).toBool(state.steppingAnimation);
        state.directionFixed = saved.value(QStringLiteral("directionFixed")).toBool(state.directionFixed);
        state.speed = qBound(0.25, saved.value(QStringLiteral("speed")).toDouble(state.speed), 20.0);
        state.frequency = qBound(1, saved.value(QStringLiteral("frequency")).toInt(state.frequency), 5);
        state.footstepDistanceTiles = qBound(0.0, saved.value(QStringLiteral("footstepDistanceTiles")).toDouble(0.0), 0.999999);
        if (saved.value(QStringLiteral("customRouteRuntime")).isObject())
            state.customRouteRuntime.fromJson(saved.value(QStringLiteral("customRouteRuntime")).toObject(), true);
        else {
            state.customRouteRuntime.reset();
            if(const MapEvent* ev=eventDefinition(saved.value(QStringLiteral("id")).toString())){
                const int pageIndex=activePage(*ev);
                if(pageIndex>=0){const EventPage& page=ev->page(pageIndex);if(page.move==EventMove::Custom&&!page.route.commands.isEmpty())state.customRouteRuntime.startAutonomous(page.route);}
            }
        }
        if (saved.value(QStringLiteral("forcedRouteRuntime")).isObject())
            state.forcedRouteRuntime.fromJson(saved.value(QStringLiteral("forcedRouteRuntime")).toObject());
        highestTicket=qMax(highestTicket,state.forcedRouteRuntime.highestTicket());
        state.movementOwner=LiveEvent::MovementOwner::None;
    }
    const QJsonValue savedNextValue=object.value(QStringLiteral("nextMoveRouteTicket"));
    bool savedNextOk=false;
    MoveRouteTicket savedNext=savedNextValue.isString()
        ? MoveRouteTicket(savedNextValue.toString().toULongLong(&savedNextOk))
        : MoveRouteTicket(savedNextValue.toDouble(1.0));
    if(!savedNextValue.isString())savedNextOk=savedNext>0;
    if(!savedNextOk||savedNext==0)savedNext=1;
    const MoveRouteTicket afterHighest = highestTicket == std::numeric_limits<MoveRouteTicket>::max()
        ? MoveRouteTicket(1) : highestTicket + 1;
    m_nextMoveRouteTicket=qMax<MoveRouteTicket>(1,qMax(savedNext,afterHighest));
    rebuildEventSpatialIndex();
}

MoveRouteTicket World::allocateMoveRouteTicket()
{
    for (;;) {
        MoveRouteTicket candidate=m_nextMoveRouteTicket++;
        if(candidate==0)continue; // zero é reservado para “sem ticket”/autônomo
        bool used=m_playerRouteRuntime.ticketState(candidate)!=MoveRouteTicketState::Unknown;
        if(!used)for(auto it=m_events.constBegin();it!=m_events.constEnd();++it)
            if(it->forcedRouteRuntime.ticketState(candidate)!=MoveRouteTicketState::Unknown){used=true;break;}
        if(!used)return candidate;
    }
}

MoveRouteTicket World::startMoveRoute(const QString& target, const MoveRoute& route,
                                          const QString& sourceEventId)
{
    if (route.commands.isEmpty()) return 0;
    MoveRouteRequest request;
    request.ticket = allocateMoveRouteTicket();
    request.route = route;
    request.sourceEventId = sourceEventId;

    if (target == QLatin1String("player")) {
        request.route.target = QStringLiteral("player");
        if (route.startMode == core::MoveRouteStartMode::Replace &&
            m_playerRouteRuntime.awaitingMovement() && m_moving) {
            m_hx=m_toHx;m_hy=m_toHy;m_fromHx=m_toHx;m_fromHy=m_toHy;
            m_t=0.0;m_moving=false;m_playerJumping=false;
        }
        return m_playerRouteRuntime.start(request, route.startMode) ? request.ticket : 0;
    }
    QString id = target.trimmed();
    if (id.startsWith(QLatin1String("event:"))) id.remove(0,6);
    auto it = m_events.find(id);
    if (it == m_events.end()) return 0;
    request.route.target = QStringLiteral("event:") + id;
    if (route.startMode == core::MoveRouteStartMode::Replace &&
        it->forcedRouteRuntime.awaitingMovement() && it->moving &&
        it->movementOwner == LiveEvent::MovementOwner::ForcedRoute) {
        it->cell=it->to;it->from=it->to;it->progress=0.0;it->moving=false;it->jumping=false;
        syncEventSpatialCell(id,eventAnchorCell(it->cell));
        it->movementOwner=LiveEvent::MovementOwner::None;
    }
    return it->forcedRouteRuntime.start(request, route.startMode) ? request.ticket : 0;
}

MoveRouteTicketState World::moveRouteTicketState(const QString& target, MoveRouteTicket ticket) const
{
    if (ticket == 0) return MoveRouteTicketState::Unknown;
    if (target == QLatin1String("player")) return m_playerRouteRuntime.ticketState(ticket);
    QString id = target;
    if (id.startsWith(QLatin1String("event:"))) id.remove(0,6);
    const auto it = m_events.constFind(id);
    return it == m_events.constEnd() ? MoveRouteTicketState::Unknown
                                     : it->forcedRouteRuntime.ticketState(ticket);
}

bool World::moveRouteRunning(const QString& target) const
{
    // A máquina de estado é a única fonte de verdade de “rota rodando”.
    // Shake e movimento físico pertencentes à rota já mantêm o runtime em
    // wait/awaitingMovement; incluí-los separadamente criaria falsos positivos.
    if (target == QLatin1String("player")) return m_playerRouteRuntime.busy();
    QString id=target;if(id.startsWith(QLatin1String("event:")))id.remove(0,6);
    const auto it=m_events.constFind(id);
    return it!=m_events.constEnd() && it->forcedRouteRuntime.busy();
}

bool World::controlMoveRoute(const QString& target, MoveRouteControlAction action)
{
    MoveRouteRuntime* runtime = nullptr;
    LiveEvent* event = nullptr;
    QString eventId;
    const bool player = target == QLatin1String("player");
    if (player) runtime = &m_playerRouteRuntime;
    else {
        eventId=target;if(eventId.startsWith(QLatin1String("event:")))eventId.remove(0,6);
        auto it=m_events.find(eventId);if(it==m_events.end())return false;
        event=&it.value();runtime=&event->forcedRouteRuntime;
    }
    switch (action) {
    case MoveRouteControlAction::Invalid:
        return false;
    case MoveRouteControlAction::Pause:
        return runtime->pause();
    case MoveRouteControlAction::Resume:
        return runtime->resume();
    case MoveRouteControlAction::ClearQueue:
        runtime->clearQueue(); return true;
    case MoveRouteControlAction::Cancel: {
        // Cancelar nunca deixa o ator no meio de um passo que já não pertence a
        // runtime algum. Fecha o passo na fronteira de destino e então cancela
        // o ticket; a próxima rota da fila pode começar de uma posição estável.
        const bool routeMovement = runtime->awaitingMovement();
        if (routeMovement && player && m_moving) {
            m_hx=m_toHx;m_hy=m_toHy;m_fromHx=m_toHx;m_fromHy=m_toHy;
            m_t=0.0;m_moving=false;m_playerJumping=false;
        } else if (routeMovement && event && event->moving &&
                   event->movementOwner == LiveEvent::MovementOwner::ForcedRoute) {
            event->cell=event->to;event->from=event->to;event->progress=0.0;
            event->moving=false;event->jumping=false;event->movementOwner=LiveEvent::MovementOwner::None;
            syncEventSpatialCell(eventId,eventAnchorCell(event->cell));
        }
        return runtime->cancelCurrent();
    }
    }
    return false;
}

void World::cancelAllMoveRoutes()
{
    // Este contrato significa “cancelar rotas FORÇADAS/em fila”. A rota Custom
    // da página é movimento autônomo do evento e precisa sobreviver a um
    // skip/cancel global, como sobrevivia antes da Rota A.
    m_playerRouteRuntime.cancelAll();
    m_playerShakeRemaining=0;m_playerJumping=false;
    if(m_moving){m_hx=m_toHx;m_hy=m_toHy;m_fromHx=m_toHx;m_fromHy=m_toHy;m_t=0;m_moving=false;}
    for(auto it=m_events.begin();it!=m_events.end();++it){
        LiveEvent& s=it.value();
        const bool customWasMoving = s.moving &&
            s.movementOwner == LiveEvent::MovementOwner::CustomRoute;
        s.forcedRouteRuntime.cancelAll();
        s.shakeRemaining=0;
        if(s.moving){
            s.cell=s.to;s.from=s.to;s.progress=0;s.moving=false;s.jumping=false;
            // Se um skip encerrou fisicamente um passo da rota autônoma, avance
            // o cursor explicitamente. Caso contrário o mesmo passo seria
            // repetido depois do skip e a rota ficaria visualmente incoerente.
            if(customWasMoving && s.customRouteRuntime.awaitingMovement())
                s.customRouteRuntime.notifyMovementFinished(0.0);
        }
        s.movementOwner=LiveEvent::MovementOwner::None;
    }
    rebuildEventSpatialIndex();
}

QVector<MoveRouteDebugEntry> World::moveRouteDebugEntries() const
{
    QVector<MoveRouteDebugEntry> out;
    auto appendRuntime=[&out](const MoveRouteRuntime& runtime,const QString& target,const QString& eventId,
                              const QPoint& actorHU,bool autonomous){
        if(!runtime.busy())return;
        MoveRouteDebugEntry entry;
        entry.target=target;entry.eventId=eventId;entry.ticket=runtime.currentTicket();
        entry.commandIndex=runtime.commandIndex();entry.queuedCount=runtime.queuedCount();
        entry.blockedAttempts=runtime.blockedAttempts();entry.lastFailure=runtime.lastFailure();
        entry.actorPositionHU=actorHU;
        if(const core::MoveCommand* command=runtime.currentCommand())entry.commandType=command->type;
        if(autonomous)entry.state=runtime.paused()?QStringLiteral("paused"):QStringLiteral("autonomous");
        else entry.state=moveRouteTicketStateId(runtime.ticketState(runtime.currentTicket()));
        const MoveRoutePathCache& path=runtime.pathCache();
        if(path.commandIndex>=0){
            entry.hasPathTarget=true;entry.pathTargetHU=path.targetHU;entry.pathNodesHU=path.nodesHU;
            entry.pathReplans=path.replans;entry.pathExpandedNodes=path.expandedNodes;entry.pathExpandedThisTick=runtime.pathExpandedThisTick();entry.pathStatus=path.status;
        }
        out.push_back(entry);
    };
    appendRuntime(m_playerRouteRuntime,QStringLiteral("player"),QString(),QPoint(m_hx,m_hy),false);
    QStringList ids=m_events.keys();ids.sort();
    for(const QString& id:ids){
        const LiveEvent& state=m_events.value(id);
        const QPointF actorCell = state.moving
            ? state.from + (state.to - state.from) * qBound(0.0, state.progress, 1.0)
            : state.cell;
        const QPoint actorHU(qRound(actorCell.x()*2.0),qRound(actorCell.y()*2.0));
        appendRuntime(state.forcedRouteRuntime,QStringLiteral("event:")+id,id,actorHU,false);
        appendRuntime(state.customRouteRuntime,QStringLiteral("event:")+id+QStringLiteral(":custom"),id,actorHU,true);
    }
    return out;
}

QJsonObject World::moveRouteDiagnostics() const
{
    QJsonArray events;
    QStringList ids=m_events.keys();ids.sort();
    for(const QString& id:ids){
        const LiveEvent& state=m_events.value(id);
        if(!state.forcedRouteRuntime.busy()&&!state.customRouteRuntime.busy())continue;
        events.append(QJsonObject{
            {QStringLiteral("eventId"),id},
            {QStringLiteral("forced"),state.forcedRouteRuntime.diagnostics()},
            {QStringLiteral("custom"),state.customRouteRuntime.diagnostics()}
        });
    }
    QJsonArray debugPaths;
    for(const MoveRouteDebugEntry& entry:moveRouteDebugEntries()){
        QJsonArray nodes;
        for(const QPoint& point:entry.pathNodesHU)nodes.append(QJsonArray{point.x(),point.y()});
        debugPaths.append(QJsonObject{
            {QStringLiteral("target"),entry.target},
            {QStringLiteral("state"),entry.state},
            {QStringLiteral("ticket"),QString::number(entry.ticket)},
            {QStringLiteral("commandIndex"),entry.commandIndex},
            {QStringLiteral("commandType"),entry.commandType},
            {QStringLiteral("actorXHU"),entry.actorPositionHU.x()},
            {QStringLiteral("actorYHU"),entry.actorPositionHU.y()},
            {QStringLiteral("pathTargetXHU"),entry.pathTargetHU.x()},
            {QStringLiteral("pathTargetYHU"),entry.pathTargetHU.y()},
            {QStringLiteral("pathStatus"),entry.pathStatus},
            {QStringLiteral("pathReplans"),entry.pathReplans},
            {QStringLiteral("pathExpandedNodes"),entry.pathExpandedNodes},
            {QStringLiteral("pathExpandedThisTick"),entry.pathExpandedThisTick},
            {QStringLiteral("pathNodesHU"),nodes}
        });
    }
    return QJsonObject{
        {QStringLiteral("player"),m_playerRouteRuntime.diagnostics()},
        {QStringLiteral("events"),events},
        {QStringLiteral("debugPaths"),debugPaths},
        {QStringLiteral("nextTicket"),QString::number(m_nextMoveRouteTicket)}
    };
}

bool World::eventView(const MapEvent& ev, EventActorView* out) const
{
    if (!out) return false;
    const int pi = activePage(ev);
    if (pi < 0) return false;
    const EventPage& pg = ev.page(pi);
    const auto it = m_events.constFind(ev.id);
    LiveEvent fallback;
    fallback.cell = fallback.from = fallback.to = ev.cell;
    fallback.page = pi; fallback.dir = pg.graphic.dir; fallback.frame = pg.graphic.frame;
    fallback.through = pg.throughWall; fallback.blocksPlayer = pg.blocksPlayer;
    fallback.opacity = qBound(0, pg.graphic.opacity, 255);
    fallback.blend = pg.graphic.blend;
    const LiveEvent& s = (it == m_events.constEnd()) ? fallback : it.value();
    const bool synced = s.page == pi;
    const double x = s.moving ? s.from.x() + (s.to.x()-s.from.x())*s.progress : s.cell.x();
    const double y = s.moving ? s.from.y() + (s.to.y()-s.from.y())*s.progress : s.cell.y();
    out->cell = eventAnchorCell(s.cell);
    const double salto = (s.moving && s.jumping)
                             ? std::sin(3.14159265358979323846 * s.progress) * ed.mapInfo().tileHeight * 0.55
                             : 0.0;
    const bool suppressShake = m_reduceShake;
    const double shakeX = (!suppressShake && s.shakeRemaining > 0) ? std::sin(s.shakePhase * .93) * s.shakeX : 0.0;
    const double shakeY = (!suppressShake && s.shakeRemaining > 0) ? std::sin(s.shakePhase * 1.17) * s.shakeY : 0.0;
    out->pixel = QPointF(x * ed.mapInfo().tileWidth + shakeX,
                         y * ed.mapInfo().tileHeight - salto + shakeY);
    out->graphic = (synced && s.hasGraphicOverride) ? s.graphicOverride : pg.graphic;
    out->graphic.dir = synced ? s.dir : pg.graphic.dir;
    out->graphic.frame = synced ? s.frame : pg.graphic.frame;
    out->page = pi;
    out->opacity = synced ? s.opacity : qBound(0, pg.graphic.opacity, 255);
    out->blend = synced ? s.blend : pg.graphic.blend;
    out->transparent = synced && s.transparent;
    out->through = synced ? s.through : pg.throughWall;
    out->blocksPlayer = synced ? s.blocksPlayer : pg.blocksPlayer;
    out->priority = pg.priority;
    return true;
}

bool World::faceEventTowardPlayer(const QString& eventId)
{
    const MapEvent* ev = eventDefinition(eventId);
    if (!ev) return false;
    const int pi = activePage(*ev);
    if (pi < 0) return false;
    const EventPage& pg = ev->page(pi);
    if (pg.directionFixed) return false;
    auto it = m_events.find(eventId);
    if (it == m_events.end()) return false;
    LiveEvent& state = it.value();
    if (state.directionFixed) return false;
    const QPointF delta = QPointF(playerCell()) - state.cell;
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())) return false;
    if (qAbs(delta.x()) > qAbs(delta.y())) state.dir = delta.x() < 0 ? 1 : 2;
    else state.dir = delta.y() < 0 ? 3 : 0;
    state.frame = pg.graphic.frame;
    return true;
}

bool World::eventFacing(const QString& eventId, int* direction, int* frame) const
{
    const auto it = m_events.constFind(eventId);
    if (it == m_events.cend()) return false;
    if (direction) *direction = it->dir;
    if (frame) *frame = it->frame;
    return true;
}

bool World::restoreEventFacing(const QString& eventId, int direction, int frame)
{
    auto it = m_events.find(eventId);
    if (it == m_events.end()) return false;
    it->dir = qBound(0, direction, 7);
    it->frame = qMax(0, frame);
    return true;
}

bool World::startEventMove(const MapEvent& ev, LiveEvent& s, const EventPage& pg,
                           int dx, int dy)
{
    dx = clampi(dx, -1, 1); dy = clampi(dy, -1, 1);
    if (!dx && !dy) return false;
    const int oldDir = s.dir;
    if (!s.directionFixed) {
        if (dx < 0) s.dir = 1; else if (dx > 0) s.dir = 2;
        else if (dy < 0) s.dir = 3; else s.dir = 0;
    }
    const double step = pg.halfStepMovement ? 0.5 : 1.0;
    const QPointF target = s.cell + QPointF(dx * step, dy * step);
    const QSize sz = mapSizeInCells();
    if (target.x() < 0.0 || target.y() < 0.0 ||
        target.x() >= sz.width() || target.y() >= sz.height()) return false;

    const QPoint currentCell=eventAnchorCell(s.cell), targetCell=eventAnchorCell(target);
    // Em meio passo o evento pode mudar de posição sem trocar de célula.
    // A antiga checagem baseada só em targetCell deixava o primeiro 0,5 tile
    // atravessar o jogador ou outro evento. Colisão dinâmica usa a mesma
    // unidade inteira (meias-células) do jogador.
    if (!s.through && pg.priority == EventPriority::Same) {
        const QRect targetBody(qRound(target.x()*2.0), qRound(target.y()*2.0), 2, 2);
        if (targetBody.intersects(hitboxHalfCells())) {
            if (pg.trigger == EventTrigger::EventTouch && m_pendingTouchEvent.isEmpty())
                m_pendingTouchEvent = ev.id;
            return false;
        }
        for (auto otherIt=m_events.cbegin(); otherIt!=m_events.cend(); ++otherIt) {
            if (otherIt.key()==ev.id || otherIt->through || !otherIt->blocksPlayer) continue;
            const MapEvent* other=eventDefinition(otherIt.key());
            if (!other) continue;
            const int otherPage=activePage(*other);
            if (otherPage<0 || other->page(otherPage).priority!=EventPriority::Same) continue;
            const QPointF otherPos=otherIt->moving ? otherIt->to : otherIt->cell;
            const QRect otherBody(qRound(otherPos.x()*2.0),qRound(otherPos.y()*2.0),2,2);
            if (targetBody.intersects(otherBody)) return false;
        }
    }
    if (!s.through && targetCell != currentCell) {
        const Dir d = dirFromDelta(dx,dy);
        if (!canMove(currentCell.x(), currentCell.y(), d)) return false;
        if (targetCell == playerCell()) {
            if (pg.trigger == EventTrigger::EventTouch && m_pendingTouchEvent.isEmpty())
                m_pendingTouchEvent = ev.id;
            if (pg.priority == EventPriority::Same) return false;
        }
        if (const QVector<QString>* ids = eventIdsAtCell(targetCell)) {
            for (const QString& id : *ids) {
                if (id == ev.id) continue;
                const MapEvent* other = eventDefinition(id);
                if (!other) continue;
                EventActorView ov;
                if (eventView(*other,&ov) && ov.priority==EventPriority::Same && ov.blocksPlayer && !ov.through) return false;
            }
        }
    }
    s.from=s.cell; s.to=target; s.progress=0; s.moving=true; s.jumping=false;
    if(s.walkingAnimation)s.frame=(s.hasGraphicOverride?s.graphicOverride:pg.graphic).frame;
    if (s.directionFixed) s.dir=oldDir;
    return true;
}

bool World::canEventPathMoveFrom(const MapEvent& ev,const LiveEvent& s,const EventPage& pg,
                                 const QPoint& fromHU,int dx,int dy) const
{
    dx=clampi(dx,-1,1);dy=clampi(dy,-1,1);
    if(!dx&&!dy)return false;
    const int stepHU=pg.halfStepMovement?1:2;
    const QPoint toHU=fromHU+QPoint(dx*stepHU,dy*stepHU);
    const QSize sz=mapSizeInCells();
    if(toHU.x()<0||toHU.y()<0||toHU.x()>=sz.width()*2||toHU.y()>=sz.height()*2)return false;
    if(s.through)return true;
    const QPointF from(fromHU.x()/2.0,fromHU.y()/2.0);
    const QPointF to(toHU.x()/2.0,toHU.y()/2.0);
    if(pg.priority==EventPriority::Same){
        const QRect targetBody(toHU.x(),toHU.y(),2,2);
        if(targetBody.intersects(hitboxHalfCells()))return false;
        for(auto otherIt=m_events.cbegin();otherIt!=m_events.cend();++otherIt){
            if(otherIt.key()==ev.id||otherIt->through||!otherIt->blocksPlayer)continue;
            const MapEvent* other=eventDefinition(otherIt.key());if(!other)continue;
            const int otherPage=activePage(*other);
            if(otherPage<0||other->page(otherPage).priority!=EventPriority::Same)continue;
            const QPointF otherPos=otherIt->moving?otherIt->to:otherIt->cell;
            if(targetBody.intersects(QRect(qRound(otherPos.x()*2.0),qRound(otherPos.y()*2.0),2,2)))return false;
        }
    }
    const QPoint currentCell=eventAnchorCell(from),targetCell=eventAnchorCell(to);
    if(targetCell==currentCell)return true;
    if(!canMove(currentCell.x(),currentCell.y(),dirFromDelta(dx,dy)))return false;
    if(targetCell==playerCell()&&pg.priority==EventPriority::Same)return false;
    if(const QVector<QString>* ids=eventIdsAtCell(targetCell))for(const QString& id:*ids){
        if(id==ev.id)continue;
        const MapEvent* other=eventDefinition(id);if(!other)continue;
        EventActorView ov;
        if(eventView(*other,&ov)&&ov.priority==EventPriority::Same&&ov.blocksPlayer&&!ov.through)return false;
    }
    return true;
}

std::optional<QPoint> World::resolveMovePathTargetHU(const MoveRoutePathOptions& options,
                                                     const QString& sourceEventId) const
{
    const QSize size=mapSizeInCells();
    switch(options.targetKind){
    case MoveRoutePathTargetKind::Cell:
        if(options.cell.x()<0||options.cell.y()<0||options.cell.x()>=size.width()||options.cell.y()>=size.height())
            return std::nullopt;
        return QPoint(options.cell.x()*2,options.cell.y()*2);
    case MoveRoutePathTargetKind::Player:
        return QPoint(m_hx,m_hy);
    case MoveRoutePathTargetKind::SourceEvent: {
        if(sourceEventId.trimmed().isEmpty())return std::nullopt;
        const auto it=m_events.constFind(sourceEventId.trimmed());
        if(it==m_events.constEnd())return std::nullopt;
        return QPoint(qRound(it->cell.x()*2.0),qRound(it->cell.y()*2.0));
    }
    case MoveRoutePathTargetKind::Event: {
        const auto it=m_events.constFind(options.eventId.trimmed());
        if(it==m_events.constEnd())return std::nullopt;
        return QPoint(qRound(it->cell.x()*2.0),qRound(it->cell.y()*2.0));
    }
    }
    return std::nullopt;
}

void World::runEventRoute(const MapEvent& ev, LiveEvent& s, const EventPage& pg,
                          MoveRouteRuntime& runtime, LiveEvent::MovementOwner owner)
{
    runtime.update(0.0); // mantém a fronteira explícita; timers são atualizados em updateEvents.
    if (!runtime.ready() || s.moving) return;

    MoveRouteActorOps actor;
    actor.move = [this,&ev,&s,&pg,owner](int dx,int dy,bool keepDirection) {
        const int oldDir=s.dir;
        const bool ok=startEventMove(ev,s,pg,dx,dy);
        if(keepDirection)s.dir=oldDir;
        if(ok){s.movementOwner=owner;return MoveRouteMoveResult::Started;}
        return MoveRouteMoveResult::Blocked;
    };
    actor.jump = [this,&ev,&s,owner](int x,int y) {
        const QPointF target=s.cell+QPointF(x,y);
        const QPointF delta=target-s.cell;
        if(!s.directionFixed&&(!qFuzzyIsNull(delta.x())||!qFuzzyIsNull(delta.y())))
            s.dir=qAbs(delta.x())>qAbs(delta.y())?(delta.x()<0?1:2):(delta.y()<0?3:0);
        const QSize sz=mapSizeInCells();const QPoint targetCell=eventAnchorCell(target);
        const bool inside=target.x()>=0&&target.y()>=0&&target.x()<sz.width()&&target.y()<sz.height();
        bool clear=inside&&(s.through||(!isBlocked(targetCell.x(),targetCell.y())&&targetCell!=playerCell()));
        if(clear&&!s.through)if(const QVector<QString>* ids=eventIdsAtCell(targetCell))for(const QString& id:*ids){
            if(id==ev.id)continue;const MapEvent* other=eventDefinition(id);if(!other)continue;EventActorView ov;
            if(eventView(*other,&ov)&&ov.priority==EventPriority::Same&&ov.blocksPlayer&&!ov.through){clear=false;break;}
        }
        if(!clear)return MoveRouteMoveResult::Blocked;
        s.from=s.cell;s.to=target;s.progress=0;s.moving=true;s.jumping=true;s.movementOwner=owner;
        return MoveRouteMoveResult::Started;
    };
    actor.referenceDelta = [this,&s]() -> std::optional<QPoint> {
        const QPointF delta=QPointF(playerCell())-s.cell;
        if(qFuzzyIsNull(delta.x())&&qFuzzyIsNull(delta.y()))return QPoint();
        return QPoint(qRound(delta.x()),qRound(delta.y()));
    };
    actor.pathPositionHU=[&s]{return QPoint(qRound(s.cell.x()*2.0),qRound(s.cell.y()*2.0));};
    actor.pathStepHU=[&pg]{return pg.halfStepMovement?1:2;};
    actor.pathCanMoveFrom=[this,&ev,&s,&pg](const QPoint& fromHU,int dx,int dy){
        return canEventPathMoveFrom(ev,s,pg,fromHU,dx,dy);
    };
    actor.resolvePathTargetHU=[this](const MoveRoutePathOptions& options,const QString& sourceEventId){
        return resolveMovePathTargetHU(options,sourceEventId);
    };
    actor.direction=[&s]{return s.dir;};
    actor.setDirection=[&s](int d){s.dir=qBound(0,d,3);};
    actor.directionFixed=[&s]{return s.directionFixed;};
    actor.setWalkingAnimation=[&s](bool on){s.walkingAnimation=on;};
    actor.setSteppingAnimation=[&s](bool on){s.steppingAnimation=on;};
    actor.setDirectionFixed=[&s](bool on){s.directionFixed=on;};
    actor.setThrough=[&s](bool on){s.through=on;};
    actor.setTransparent=[&s](bool on){s.transparent=on;};
    actor.setSpeed=[&s](double v){s.speed=qBound(.25,v,20.0);};
    actor.setFrequency=[&s](int v){s.frequency=qBound(1,v,5);};
    actor.setOpacity=[&s](int v){s.opacity=qBound(0,v,255);};
    actor.setBlend=[&s](const QString& value){s.blend=value;};
    actor.setSwitch=[this](int id,bool on){if(m_switchHook)m_switchHook(id,on);};
    actor.playSound=[this](const QString& source,int volume){if(m_soundHook)m_soundHook(source,volume);};
    actor.changeGraphic=[this,&s](const EventGraphic& source){
        EventGraphic g=source;
        if(g.charset.isNull()&&!g.sourcePath.isEmpty()){g.charset=ed.preloadedRuntimeImage(g.sourcePath);if(g.charset.isNull())g.charset.load(QDir(ed.projectRoot()).filePath(g.sourcePath));}
        s.graphicOverride=g;s.hasGraphicOverride=true;s.dir=qBound(0,g.dir,3);s.frame=g.frame;
    };
    actor.startShake=[&s](double x,double y,double seconds){s.shakeX=x;s.shakeY=y;s.shakeRemaining=seconds;s.shakePhase=0;};
    actor.rememberPosition=[this,&ev,&s]{
        if(m_rememberEventPositionHook)
            m_rememberEventPositionHook(ev.id,QPoint(qRound(s.cell.x()*2.0),qRound(s.cell.y()*2.0)));
    };
    // Frequencia controla apenas a cadencia AUTONOMA e a tentativa quando o
    // caminho esta bloqueado. Uma rota explicita ja possui o comando "wait";
    // inserir eventDelay entre todos os seus comandos criava a pausa oculta
    // andar -> parar -> andar e tambem atrasava changeGraphic/switch/etc.
    actor.commandCooldown=[] { return 0.0; };
    actor.blockedRetryDelay=[this,&s]{return eventDelay(s.frequency);};
    // Drena comandos instantaneos no mesmo tick. O limite protege projetos
    // corrompidos/repetitivos; movimento, wait, shake ou bloqueio tiram o
    // runtime de ready() e encerram naturalmente o burst.
    for(int budget=0;budget<64&&runtime.ready()&&!s.moving;++budget)
        MoveRouteExecutor::executeOne(runtime,actor);
}

void World::updateEvents(double dt)
{
    const MapDoc* doc=ed.doc(); if(!doc)return;
    ensureEventSpatialIndexCurrent();
    QSet<QString> alive;
    for(const MapEvent& ev:doc->events){
        alive.insert(ev.id);
        if(!m_events.contains(ev.id)){
            LiveEvent state;state.cell=state.from=state.to=ev.cell;m_events.insert(ev.id,state);
            syncEventSpatialCell(ev.id,eventAnchorCell(state.cell));
        }
        LiveEvent& s=m_events[ev.id];
        const bool forcedPaused = s.forcedRouteRuntime.paused();
        if(s.shakeRemaining>0 && !forcedPaused){s.shakeRemaining=qMax(0.0,s.shakeRemaining-dt);s.shakePhase+=dt*60.0;}
        const int pi=activePage(ev);
        if(pi<0){
            if(s.page!=pi){s.customRouteRuntime.cancelAll();s.autonomousCooldown=0;s.page=pi;}
            // Evento sem página ativa não pode continuar uma rota forçada: o
            // ticket vira Cancelled em vez de deixar o Interpreter esperando.
            if(s.forcedRouteRuntime.busy())s.forcedRouteRuntime.cancelAll();
            if(s.moving){s.cell=s.from;s.to=s.from;s.progress=0;s.moving=false;s.jumping=false;s.movementOwner=LiveEvent::MovementOwner::None;}
            continue;
        }
        const EventPage& pg=ev.page(pi);
        if(s.page!=pi){
            // Uma página nova substitui imediatamente autonomia/rota Custom.
            // Rota forçada, porém, é independente da página e continua.
            if(s.moving && s.movementOwner != LiveEvent::MovementOwner::ForcedRoute){
                const QPointF stable = s.progress >= 0.5 ? s.to : s.from;
                s.cell=s.from=s.to=stable;s.progress=0;s.moving=false;s.jumping=false;
                syncEventSpatialCell(ev.id,eventAnchorCell(s.cell));
                s.movementOwner=LiveEvent::MovementOwner::None;
            }
            s.page=pi;s.customRouteRuntime.cancelAll();s.autonomousCooldown=0;s.hasGraphicOverride=false;s.animClock=0;s.animPhase=0;s.footstepDistanceTiles=0.0;
            s.dir=pg.graphic.dir;s.frame=pg.graphic.frame;s.walkingAnimation=pg.walkingAnimation;
            s.steppingAnimation=pg.steppingAnimation;s.directionFixed=pg.directionFixed;s.through=pg.throughWall;
            s.blocksPlayer=pg.blocksPlayer;s.speed=pg.moveSpeed;s.frequency=pg.moveFrequency;
            s.opacity=qBound(0,pg.graphic.opacity,255);s.transparent=false;s.blend=pg.graphic.blend;
            if(pg.move==EventMove::Custom&&!pg.route.commands.isEmpty())s.customRouteRuntime.startAutonomous(pg.route);
        }

        // A rota forçada suspende completamente a rota/autonomia da página.
        // Seus relógios não correm em segundo plano, evitando saltos ao retomar.
        if(s.forcedRouteRuntime.busy())s.forcedRouteRuntime.update(dt);
        else {
            if(pg.move==EventMove::Custom)s.customRouteRuntime.update(dt);
            if(s.autonomousCooldown>0)s.autonomousCooldown=qMax(0.0,s.autonomousCooldown-dt);
        }

        double movementOverflowSec = 0.0;
        const bool movementPaused = s.moving &&
            s.movementOwner == LiveEvent::MovementOwner::ForcedRoute &&
            s.forcedRouteRuntime.paused();
        if(s.moving && movementPaused) continue;
        if(s.moving){
            const double movementRate=qMax(.25,s.speed)*(pg.halfStepMovement?2.0:1.0);
            const double stepTiles=pg.halfStepMovement?0.5:1.0;
            const double deltaProgress=dt*movementRate;
            const double consumedProgress=qMin(1.0-s.progress,deltaProgress);
            s.animPhase+=qMax(0.0,consumedProgress)*stepTiles*2.0;
            s.progress+=deltaProgress;
            if(s.walkingAnimation){const int cols=qMax(1,(s.hasGraphicOverride?s.graphicOverride:pg.graphic).charsetCols);s.frame=animFrameForPhase(s.animPhase,cols,Editor::PlayerSettings::PingPong);}
            if(s.progress<1.0)continue;
            movementOverflowSec=(s.progress-1.0)/movementRate;
            const bool completedJump = s.jumping;
            s.cell=s.to;s.from=s.to;s.progress=0;s.moving=false;s.jumping=false;
            syncEventSpatialCell(ev.id,eventAnchorCell(s.cell));
            if (completedJump) s.footstepDistanceTiles = 0.0;
            else if (advanceFootstepCadence(s.footstepDistanceTiles, stepTiles) && m_footstepHook)
                m_footstepHook(ev.id, s.cell);
            s.frame=(s.hasGraphicOverride?s.graphicOverride:pg.graphic).frame;
            if(s.movementOwner==LiveEvent::MovementOwner::ForcedRoute)
                s.forcedRouteRuntime.notifyMovementFinished(0.0);
            else if(s.movementOwner==LiveEvent::MovementOwner::CustomRoute)
                s.customRouteRuntime.notifyMovementFinished(0.0);
            s.movementOwner=LiveEvent::MovementOwner::None;
        }
        if(s.steppingAnimation&&!s.moving){s.animClock+=dt;const int cols=qMax(1,(s.hasGraphicOverride?s.graphicOverride:pg.graphic).charsetCols);s.frame=animFrameForPhase(s.animClock*6,cols,Editor::PlayerSettings::PingPong);}

        if(s.forcedRouteRuntime.busy())runEventRoute(ev,s,pg,s.forcedRouteRuntime,LiveEvent::MovementOwner::ForcedRoute);
        else if(pg.move==EventMove::Random){
            if(s.autonomousCooldown<=0){int d=QRandomGenerator::global()->bounded(4);QPoint v=deltaFromDir(Dir(d));if(startEventMove(ev,s,pg,v.x(),v.y()))s.movementOwner=LiveEvent::MovementOwner::Autonomous;else s.autonomousCooldown=eventDelay(s.frequency);}
        }
        else if(pg.move==EventMove::Approach){
            if(s.autonomousCooldown<=0){QPointF delta=QPointF(playerCell())-s.cell;if(qFuzzyIsNull(delta.x())&&qFuzzyIsNull(delta.y()))s.autonomousCooldown=eventDelay(s.frequency);else{QPoint v=qAbs(delta.x())>qAbs(delta.y())?QPoint(delta.x()<0?-1:1,0):QPoint(0,delta.y()<0?-1:1);if(startEventMove(ev,s,pg,v.x(),v.y()))s.movementOwner=LiveEvent::MovementOwner::Autonomous;else s.autonomousCooldown=eventDelay(s.frequency);}}
        }
        else if(pg.move==EventMove::Custom)runEventRoute(ev,s,pg,s.customRouteRuntime,LiveEvent::MovementOwner::CustomRoute);

        if(movementOverflowSec>0.0&&s.moving){
            const double rate=qMax(.25,s.speed)*(pg.halfStepMovement?2.0:1.0);
            const double stepTiles=pg.halfStepMovement?0.5:1.0;
            s.progress=qMin(0.999999,movementOverflowSec*rate);
            s.animPhase+=s.progress*stepTiles*2.0;
            if(s.walkingAnimation){const int cols=qMax(1,(s.hasGraphicOverride?s.graphicOverride:pg.graphic).charsetCols);s.frame=animFrameForPhase(s.animPhase,cols,Editor::PlayerSettings::PingPong);}
        }
    }
    for(auto it=m_events.begin();it!=m_events.end();)if(!alive.contains(it.key())){
        const QString id=it.key();removeEventFromSpatialIndex(id);it=m_events.erase(it);
    }else++it;
}

int World::cellMask(int gx, int gy) const
{
    ensureCollisionGridCurrent();
    if (gx < 0 || gy < 0 || gx >= m_collisionGridSize.width() || gy >= m_collisionGridSize.height())
        return Editor::SideAll;
    return int(m_collisionGrid.at(gy * m_collisionGridSize.width() + gx));
}

bool World::isBlocked(int gx, int gy) const
{
    return cellMask(gx, gy) == Editor::SideAll;
}

bool World::canMove(int gx, int gy, Dir d) const
{
    if (isDiagonal(d)) {
        // Diagonal só passa se os dois caminhos em "L" passarem: assim não dá
        // para cortar a quina entre duas paredes.
        const QPoint v = deltaFromDir(d);
        const Dir h = (v.x() < 0) ? Dir::Left : Dir::Right;
        const Dir w = (v.y() < 0) ? Dir::Up   : Dir::Down;
        return canMove(gx, gy, h) && canMove(gx + v.x(), gy, w)
            && canMove(gx, gy, w) && canMove(gx, gy + v.y(), h);
    }
    int outSide = 0, dx = 0, dy = 0;
    switch (d) {
    case Dir::Up:    outSide = Editor::SideTop;    dy = -1; break;
    case Dir::Down:  outSide = Editor::SideBottom; dy =  1; break;
    case Dir::Left:  outSide = Editor::SideLeft;   dx = -1; break;
    case Dir::Right: outSide = Editor::SideRight;  dx =  1; break;
    default: break;
    }
    if (cellMask(gx, gy) & outSide) return false;                       // sair
    const int inSide = Editor::oppositeSide(outSide);
    if (cellMask(gx + dx, gy + dy) & inSide) return false;              // entrar
    // Uma celula totalmente bloqueada nunca e atravessavel, mesmo que os
    // lados relevantes estejam livres por algum motivo.
    return cellMask(gx + dx, gy + dy) != Editor::SideAll;
}

QPoint World::findFreeCellNear(const QPoint& wanted) const
{
    const QSize size = mapSizeInCells();
    const QPoint start(clampi(wanted.x(), 0, size.width() - 1),
                       clampi(wanted.y(), 0, size.height() - 1));
    if (!isBlocked(start.x(), start.y())) return start;

    // Busca em anéis crescentes ao redor do ponto desejado.
    const int maxR = qMax(size.width(), size.height());
    for (int r = 1; r <= maxR; ++r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (qMax(qAbs(dx), qAbs(dy)) != r) continue;   // só a borda do anel
                const int x = start.x() + dx, y = start.y() + dy;
                if (!isBlocked(x, y)) return QPoint(x, y);
            }
    }
    return start;   // mapa inteiro bloqueado: fica onde está
}

const MapEvent* World::blockingEventAt(int gx, int gy) const
{
    const QVector<QString>* ids = eventIdsAtCell(QPoint(gx, gy));
    if (!ids) return nullptr;
    for (const QString& id : *ids) {
        const MapEvent* e = eventDefinition(id);
        if (!e) continue;
        EventActorView view;
        if (!eventView(*e, &view)) continue;
        if (view.priority == EventPriority::Same && view.blocksPlayer && !view.through) return e;
    }
    return nullptr;
}

QVector<QPoint> World::occupiedCells() const
{
    const QRect hb = hitboxHalfCells();
    QVector<QPoint> cells;
    for (int y = hb.top(); y <= hb.bottom(); ++y)
        for (int x = hb.left(); x <= hb.right(); ++x) {
            const QPoint c(x >> 1, y >> 1);
            if (!cells.contains(c)) cells.push_back(c);
        }
    return cells;
}

QVector<QPoint> World::cellsInFront() const
{
    const QPoint v = deltaFromDir(m_dir);
    QVector<QPoint> cells;
    for (const QPoint& c : occupiedCells()) {
        const QPoint f = c + v;
        if (!cells.contains(f)) cells.push_back(f);
    }
    // Tira as células que o próprio personagem já ocupa (acontece quando ele
    // está no meio de duas): quem está embaixo dos pés não está "à frente".
    const QVector<QPoint> aqui = occupiedCells();
    QVector<QPoint> out;
    for (const QPoint& c : cells)
        if (!aqui.contains(c)) out.push_back(c);
    return out.isEmpty() ? cells : out;
}

const MapEvent* World::eventInFront() const
{
    // Eventos abaixo/acima podem compartilhar a celula do jogador. Eles sao
    // acionados primeiro quando possuem gatilho Tecla de acao.
    const QVector<QPoint> aqui = occupiedCells();
    for (const QPoint& c : aqui) {
        const QVector<QString>* ids = eventIdsAtCell(c);
        if (!ids) continue;
        for (const QString& id : *ids) {
            const MapEvent* e = eventDefinition(id);
            if (!e) continue;
            EventActorView view;
            if (!eventView(*e, &view) || view.priority == EventPriority::Same) continue;
            if (view.page >= 0 && e->page(view.page).trigger == EventTrigger::ActionKey) return e;
        }
    }

    const QVector<QPoint> frente = cellsInFront();
    for (const QPoint& c : frente) {
        const QVector<QString>* ids = eventIdsAtCell(c);
        if (!ids) continue;
        for (const QString& id : *ids) {
            const MapEvent* e = eventDefinition(id);
            if (!e) continue;
            EventActorView view;
            if (!eventView(*e, &view)) continue;
            if (view.page >= 0 && e->page(view.page).trigger == EventTrigger::ActionKey) return e;
        }
    }
    return nullptr;
}

QRect World::hitboxHalfCells() const
{
    return QRect(m_hx, m_hy + hbOffsetY(), 2, hbHeight());
}

// Um passo cardeal de `dhx`/`dhy` meias-células a partir de (hx,hy).
//
// A ideia: só interessa a ARESTA que a hitbox atravessa. Como o passo é de no
// máximo uma célula, no máximo uma fronteira de tiles é cruzada — e ela é
// verificada para cada faixa de meia-célula que a hitbox ocupa. É isso que faz
// a hitbox 1×0,5 passar por vãos onde a 1×1 não cabe.
bool World::canStepFrom(int hx, int hy, int dhx, int dhy) const
{
    if (dhx == 0 && dhy == 0) return false;
    if (dhx != 0 && dhy != 0) return false;            // aqui, só cardeal

    const QSize sz = mapSizeInCells();
    const int nx = hx + dhx, ny = hy + dhy;
    // O personagem ocupa sempre 1×1 célula de espaço; a hitbox é um recorte.
    if (nx < 0 || ny < 0 || nx + 2 > sz.width() * 2 || ny + 2 > sz.height() * 2)
        return false;

    const int oy = hy + hbOffsetY(), bh = hbHeight();

    // Evento que barra o jogador: qualquer célula que a hitbox passe a ocupar
    // no destino já impede o passo (o NPC é sólido).
    {
        const int bx = nx, by = ny + hbOffsetY();
        for (int r = by; r < by + bh; ++r)
            for (int c = bx; c < bx + 2; ++c)
                if (blockingEventAt(c >> 1, r >> 1)) return false;
    }

    if (dhx != 0) {
        const Dir d = (dhx > 0) ? Dir::Right : Dir::Left;
        const int oldLead = (dhx > 0) ? (hx + 1) : hx;          // meia-célula da frente
        const int newLead = oldLead + dhx;
        const int oldTx = oldLead >> 1, newTx = newLead >> 1;
        if (oldTx == newTx) return true;                        // nem sai do tile
        for (int r = oy; r < oy + bh; ++r) {
            const int ty = r >> 1;
            if (!canMove(oldTx, ty, d)) return false;
        }
    } else {
        const Dir d = (dhy > 0) ? Dir::Down : Dir::Up;
        const int oldLead = (dhy > 0) ? (oy + bh - 1) : oy;
        const int newLead = oldLead + dhy;
        const int oldTy = oldLead >> 1, newTy = newLead >> 1;
        if (oldTy == newTy) return true;
        for (int c = hx; c < hx + 2; ++c) {
            const int tx = c >> 1;
            if (!canMove(tx, oldTy, d)) return false;
        }
    }
    return true;
}

bool World::canWalk(int hx, int hy, int dhx, int dhy) const
{
    if (dhx == 0 && dhy == 0) return false;
    if (dhx == 0 || dhy == 0) return canStepFrom(hx, hy, dhx, dhy);
    // Diagonal: exige os dois contornos em "L" livres.
    return canStepFrom(hx, hy, dhx, 0) && canStepFrom(hx + dhx, hy, 0, dhy)
        && canStepFrom(hx, hy, 0, dhy) && canStepFrom(hx, hy + dhy, dhx, 0);
}

bool World::tryStartMove(int dx, int dy)
{
    if (dx == 0 && dy == 0) return false;
    // Vira para o lado pedido mesmo que não consiga andar (igual ao editores de RPG),
    // exceto durante "fixar direção" da rota.
    if (!m_playerDirectionFixed) m_dir = dirFromDelta(dx, dy);

    const int s = stepHU();
    const int dhx = dx * s, dhy = dy * s;

    // Detecta o contato pela hitbox de destino, inclusive quando o evento é
    // sólido e por isso o passo não chega a começar.
    if (m_pendingTouchEvent.isEmpty()) {
        const int nx=m_hx+dhx, ny=m_hy+dhy+hbOffsetY(), bh=hbHeight();
        QSet<QPoint> destino;
        for(int r=ny;r<ny+bh;++r)for(int c=nx;c<nx+2;++c)destino.insert(QPoint(c>>1,r>>1));
        const MapEvent* touch=nullptr;int bestOrder=std::numeric_limits<int>::max();
        for(const QPoint& cell:destino)if(const QVector<QString>* ids=eventIdsAtCell(cell))for(const QString& id:*ids){
            const MapEvent* ev=eventDefinition(id);if(!ev)continue;EventActorView view;
            if(!eventView(*ev,&view)||view.page<0||ev->page(view.page).trigger!=EventTrigger::PlayerTouch)continue;
            const int order=m_eventDefinitionIndex.value(id,std::numeric_limits<int>::max());
            if(order<bestOrder){bestOrder=order;touch=ev;}
        }
        if(touch)m_pendingTouchEvent=touch->id;
    }
    const QSize sz = mapSizeInCells();
    const int nx = m_hx + dhx, ny = m_hy + dhy;
    const bool inside = nx >= 0 && ny >= 0 && nx + 2 <= sz.width() * 2 && ny + 2 <= sz.height() * 2;
    if (!inside || (!m_playerThrough && !canWalk(m_hx, m_hy, dhx, dhy))) return false;

    m_fromHx = m_hx; m_fromHy = m_hy;
    m_toHx = m_hx + dhx; m_toHy = m_hy + dhy;
    m_t = 0.0;
    m_moving = true;
    return true;
}

void World::updatePlayerRoute(double dt, int* dx, int* dy)
{
    if(m_playerSteppingAnimation&&!m_moving)m_playerAnimClock+=dt;
    const bool routePaused=m_playerRouteRuntime.paused();
    if(m_playerShakeRemaining>0 && !routePaused){m_playerShakeRemaining=qMax(0.0,m_playerShakeRemaining-dt);m_playerShakePhase+=dt*60.0;}
    m_playerRouteRuntime.update(dt);
    if(!m_playerRouteRuntime.busy())return;
    *dx=*dy=0; // rota forçada tem posse do movimento enquanto estiver ativa/pausada
    if(m_moving || !m_playerRouteRuntime.ready())return;

    MoveRouteActorOps actor;
    actor.move=[this](int x,int y,bool keepDirection){
        const Dir old=m_dir;const bool ok=tryStartMove(x,y);if(keepDirection)m_dir=old;
        return ok?MoveRouteMoveResult::Started:MoveRouteMoveResult::Blocked;
    };
    actor.jump=[this](int x,int y){
        if(!m_playerDirectionFixed&&(x||y))m_dir=dirFromDelta(x<0?-1:x>0?1:0,y<0?-1:y>0?1:0);
        const QSize sz=mapSizeInCells();const int nx=m_hx+x*2,ny=m_hy+y*2;
        const bool inside=nx>=0&&ny>=0&&nx+2<=sz.width()*2&&ny+2<=sz.height()*2;
        bool clear=inside;if(clear&&!m_playerThrough){const QPoint cell(nx/2,ny/2);clear=!isBlocked(cell.x(),cell.y())&&!blockingEventAt(cell.x(),cell.y());}
        if(!clear)return MoveRouteMoveResult::Blocked;
        m_fromHx=m_hx;m_fromHy=m_hy;m_toHx=nx;m_toHy=ny;m_t=0;m_moving=true;m_playerJumping=true;
        return MoveRouteMoveResult::Started;
    };
    actor.referenceDelta=[this]() -> std::optional<QPoint> {
        const QString id=m_playerRouteRuntime.sourceEventId();
        if(id.isEmpty())return std::nullopt;
        const auto it=m_events.constFind(id);if(it==m_events.constEnd())return std::nullopt;
        return eventAnchorCell(it->cell)-playerCell();
    };
    actor.pathPositionHU=[this]{return QPoint(m_hx,m_hy);};
    actor.pathStepHU=[this]{return stepHU();};
    actor.pathCanMoveFrom=[this](const QPoint& fromHU,int dx,int dy){
        const int s=stepHU();const int dhx=dx*s,dhy=dy*s;
        const QSize sz=mapSizeInCells();const int nx=fromHU.x()+dhx,ny=fromHU.y()+dhy;
        const bool inside=nx>=0&&ny>=0&&nx+2<=sz.width()*2&&ny+2<=sz.height()*2;
        return inside&&(m_playerThrough||canWalk(fromHU.x(),fromHU.y(),dhx,dhy));
    };
    actor.resolvePathTargetHU=[this](const MoveRoutePathOptions& options,const QString& sourceEventId){
        return resolveMovePathTargetHU(options,sourceEventId);
    };
    actor.direction=[this]{return int(m_dir);};
    actor.setDirection=[this](int d){m_dir=Dir(qBound(0,d,7));};
    actor.directionFixed=[this]{return m_playerDirectionFixed;};
    actor.setWalkingAnimation=[this](bool on){m_playerWalkingAnimation=on;};
    actor.setSteppingAnimation=[this](bool on){m_playerSteppingAnimation=on;};
    actor.setDirectionFixed=[this](bool on){m_playerDirectionFixed=on;};
    actor.setThrough=[this](bool on){m_playerThrough=on;};
    actor.setTransparent=[this](bool on){m_playerTransparent=on;};
    actor.setSpeed=[this](double v){config.tilesPerSecond=qBound(.25,v,20.0);};
    actor.setFrequency=[this](int v){m_playerRouteFrequency=qBound(1,v,5);};
    actor.setOpacity=[this](int v){m_playerOpacity=qBound(0,v,255);};
    actor.setBlend=[this](const QString& value){m_playerBlend=value;};
    actor.setSwitch=[this](int id,bool on){if(m_switchHook)m_switchHook(id,on);};
    actor.playSound=[this](const QString& source,int volume){if(m_soundHook)m_soundHook(source,volume);};
    actor.changeGraphic=[this](const EventGraphic& source){
        EventGraphic g=source;if(g.charset.isNull()&&!g.sourcePath.isEmpty()){g.charset=ed.preloadedRuntimeImage(g.sourcePath);if(g.charset.isNull())g.charset.load(QDir(ed.projectRoot()).filePath(g.sourcePath));}
        m_playerGraphicOverride=g;m_playerHasGraphicOverride=true;if(!m_playerDirectionFixed)m_dir=Dir(qBound(0,g.dir,7));
    };
    actor.startShake=[this](double x,double y,double seconds){m_playerShakeX=x;m_playerShakeY=y;m_playerShakeRemaining=seconds;m_playerShakePhase=0;};
    actor.commandCooldown=[] { return 0.0; };
    actor.blockedRetryDelay=[this]{return eventDelay(m_playerRouteFrequency);};
    for(int budget=0;budget<64&&m_playerRouteRuntime.ready()&&!m_moving;++budget)
        MoveRouteExecutor::executeOne(m_playerRouteRuntime,actor);
}

void World::update(double dt, int inputDx, int inputDy)
{
    if (dt <= 0) return;
    updateEvents(dt);
    updatePlayerRoute(dt,&inputDx,&inputDy);
    // Pausar rota congela também o passo físico já iniciado; timers e shake
    // associados à rota ficam congelados pelo mesmo contrato.
    if(m_playerRouteRuntime.paused() && m_playerRouteRuntime.awaitingMovement() && m_moving) return;
    inputDx = clampi(inputDx, -1, 1);
    inputDy = clampi(inputDy, -1, 1);
    // Caminhada em 4 direções: o eixo horizontal tem prioridade, como no
    // editores de RPG. Em 8 direções a diagonal é mantida.
    if (!config.diagonal && inputDx != 0) inputDy = 0;

    if (!m_moving) {
        tryStartMove(inputDx, inputDy);
        if (!m_moving) return;
    }

    // Um passo pode valer meia célula: a velocidade continua em células/s, por
    // isso o progresso do passo corre mais rápido quando o passo é menor.
    const double stepTiles = config.halfStep ? 0.5 : 1.0;
    double remaining = dt * qMax(0.1, config.tilesPerSecond) / stepTiles;
    // O laço permite atravessar mais de uma célula num quadro lento sem
    // "engasgar" nem pular parede: cada passo é validado individualmente.
    while (m_moving && remaining > 0.0) {
        const double need = 1.0 - m_t;
        if (remaining < need) {
            m_t += remaining;
            m_animPhase += remaining * stepTiles * 2.0;   // fase em meias-células
            remaining = 0.0;
            break;
        }
        remaining -= need;
        m_animPhase += need * stepTiles * 2.0;
        m_hx = m_toHx; m_hy = m_toHy;
        m_fromHx = m_hx; m_fromHy = m_hy;
        m_t = 0.0;
        m_moving = false;
        const bool completedJump = m_playerJumping;
        m_playerJumping = false;
        ++m_steps;
        if (completedJump) m_playerFootstepDistanceTiles = 0.0;
        else if (advanceFootstepCadence(m_playerFootstepDistanceTiles, stepTiles) && m_footstepHook)
            m_footstepHook(QString(), QPointF(m_hx / 2.0, m_hy / 2.0));
        if (m_playerRouteRuntime.awaitingMovement())
            m_playerRouteRuntime.notifyMovementFinished(0.0);
        // Uma sequencia de passos da rota usa o restante deste mesmo frame.
        // Isso elimina a bolha de um tick entre tiles inclusive quando houve
        // um frame lento, sem atravessar paredes (tryStartMove ainda valida).
        if (m_playerRouteRuntime.busy() && !m_moving) {
            int routeDx=0, routeDy=0;
            updatePlayerRoute(0.0, &routeDx, &routeDy);
        }
        // Tecla ainda pressionada: emenda o próximo passo (andar contínuo).
        if (inputDx || inputDy) tryStartMove(inputDx, inputDy);
    }
}

int World::animFrame() const
{
    // Parado: o quadro escolhido na configuracao (por padrao, o do meio).
    if (!m_moving) return m_playerSteppingAnimation
        ? animFrameForPhase(m_playerAnimClock*6.0,config.animCols,config.animOrder)
        : qBound(0, config.idleCol, qMax(1, config.animCols) - 1);
    if (!m_playerWalkingAnimation) return qBound(0, config.idleCol, qMax(1, config.animCols) - 1);
    return animFrameForPhase(m_animPhase, config.animCols, config.animOrder);
}

QPoint World::playerCell() const
{
    return QPoint(m_hx >> 1, m_hy >> 1);
}

QPointF World::playerVisualPixel() const
{
    QPointF p=playerPixel();
    if(m_moving&&m_playerJumping)p.ry()-=std::sin(3.14159265358979323846*m_t)*ed.mapInfo().tileHeight*.55;
    if(!m_reduceShake && m_playerShakeRemaining>0){p.rx()+=std::sin(m_playerShakePhase*.93)*m_playerShakeX;p.ry()+=std::sin(m_playerShakePhase*1.17)*m_playerShakeY;}
    return p;
}

QPointF World::playerPixel() const
{
    const MapInfo& info = ed.mapInfo();
    const double hw = qMax(1, info.tileWidth) / 2.0;     // largura de meia célula
    const double hh = qMax(1, info.tileHeight) / 2.0;
    if (!m_moving) return QPointF(m_hx * hw, m_hy * hh);
    const double x = m_fromHx + (m_toHx - m_fromHx) * m_t;
    const double y = m_fromHy + (m_toHy - m_fromHy) * m_t;
    return QPointF(x * hw, y * hh);
}

} // namespace game
