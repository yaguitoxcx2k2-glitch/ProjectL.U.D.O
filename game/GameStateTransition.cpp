#include "GameStateTransition.h"

#include <QCoreApplication>

namespace game {

InvalidMapPolicy invalidMapPolicyFor(GameStateTransitionKind kind)
{
    switch (kind) {
    case GameStateTransitionKind::NewGame:
    case GameStateTransitionKind::Restart:
        return InvalidMapPolicy::FallbackToProjectStartThenFirst;
    case GameStateTransitionKind::Save:
    case GameStateTransitionKind::Load:
    case GameStateTransitionKind::MapTransfer:
        return InvalidMapPolicy::Reject;
    }
    return InvalidMapPolicy::Reject;
}

static QPoint clampCell(const core::MapDoc& map, const QPoint& cell)
{
    return QPoint(qBound(0, cell.x(), qMax(0, map.map.width - 1)),
                  qBound(0, cell.y(), qMax(0, map.map.height - 1)));
}

QPoint clampHalfCellToMap(const core::MapDoc& map, const QPoint& halfCell)
{
    // Mantem exatamente o mesmo dominio aceito por World::restorePlayer:
    // coordenadas em meia celula, limitadas ao ultimo tile inteiro do mapa.
    return QPoint(qBound(0, halfCell.x(), qMax(0, map.map.width * 2 - 2)),
                  qBound(0, halfCell.y(), qMax(0, map.map.height * 2 - 2)));
}

bool resolveGameTransitionTarget(const core::Editor& editor,
                                 GameStateTransitionKind kind,
                                 const QString& requestedMapId,
                                 const QPoint& requestedCell,
                                 GameTransitionTarget* out,
                                 QString* error)
{
    if (!out) {
        if (error) *error = QCoreApplication::translate("GameStateTransition", "Destino de transição ausente.");
        return false;
    }
    *out = GameTransitionTarget();

    QString mapId = requestedMapId.trimmed();
    const core::MapDoc* map = editor.mapById(mapId);
    bool fallback = false;

    if (!map && invalidMapPolicyFor(kind) == InvalidMapPolicy::FallbackToProjectStartThenFirst) {
        fallback = true;
        mapId = editor.startMapId;
        map = editor.mapById(mapId);
        if (!map && !editor.docs.isEmpty()) {
            mapId = editor.docs.first().id;
            map = &editor.docs.first();
        }
    }

    if (!map) {
        if (error) {
            *error = requestedMapId.trimmed().isEmpty()
                ? QCoreApplication::translate("GameStateTransition", "Não há mapa válido para esta transição.")
                : QCoreApplication::translate("GameStateTransition", "O mapa solicitado não existe mais no projeto.");
        }
        return false;
    }

    out->mapId = mapId;
    // Se o mapa solicitado precisou de fallback, coordenadas do mapa antigo
    // nao sao semanticamente transferiveis. O fallback seguro e (0,0), salvo
    // quando o destino recuperado e exatamente o Start Map do projeto.
    QPoint cell = requestedCell;
    if (fallback && requestedMapId != mapId) {
        if (mapId == editor.startMapId) cell = editor.startPosition;
        else cell = QPoint(0, 0);
    }
    out->cell = clampCell(*map, cell);
    out->usedFallback = fallback;
    return true;
}

} // namespace game
