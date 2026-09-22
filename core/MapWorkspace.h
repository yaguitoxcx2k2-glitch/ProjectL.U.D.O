#pragma once

#include "EditorSession.h"

#include <QSet>
#include <QString>

namespace core {
class Editor;

/// Contrato do workspace do editor. Não altera a estrutura persistida de mapas;
/// opera exclusivamente sobre EditorSession usando IDs estáveis.
namespace mapworkspace {

struct MapCloseResult {
    bool closed = false;
    QString fallbackMapId;
};

void normalize(Editor& editor);
bool openMap(Editor& editor, const QString& mapId);
/// Fecha somente a entrada volátil do workspace e devolve, por valor, o mapa
/// que deve ocupar a mesma posição visual (ou a anterior quando não houver).
MapCloseResult closeMap(Editor& editor, const QString& mapId, int preferredFallbackIndex = -1);
void setOpenOrder(Editor& editor, const QVector<QString>& orderedMapIds);
void forgetRemovedMaps(Editor& editor, const QSet<QString>& removedIds);
void recordViewport(Editor& editor, const QString& mapId, double zoom, const QPointF& center);
bool viewport(const Editor& editor, const QString& mapId, MapViewportState* state);
QString takeRecentlyClosed(Editor& editor);

} // namespace mapworkspace
} // namespace core
