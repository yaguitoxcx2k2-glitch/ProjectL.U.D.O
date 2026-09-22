#include "MapWorkspace.h"

#include "Editor.h"

#include <utility>

namespace core::mapworkspace {

void normalize(Editor& editor)
{
    QVector<QString> valid;
    valid.reserve(editor.session.openMapIds.size() + 1);
    QSet<QString> seen;
    for (const QString& id : std::as_const(editor.session.openMapIds)) {
        if (editor.mapIndexById(id) < 0 || seen.contains(id)) continue;
        valid.push_back(id);
        seen.insert(id);
    }

    const MapDoc* active = editor.doc();
    if (active && !seen.contains(active->id)) {
        valid.push_back(active->id);
        seen.insert(active->id);
    }
    if (valid.isEmpty() && !editor.docs.isEmpty()) valid.push_back(editor.docs.first().id);
    editor.session.openMapIds = valid;

    for (auto it = editor.session.mapViewports.begin(); it != editor.session.mapViewports.end(); ) {
        if (editor.mapIndexById(it.key()) < 0) it = editor.session.mapViewports.erase(it);
        else ++it;
    }
    for (int i = editor.session.recentlyClosedMapIds.size() - 1; i >= 0; --i)
        if (editor.mapIndexById(editor.session.recentlyClosedMapIds.at(i)) < 0)
            editor.session.recentlyClosedMapIds.removeAt(i);
}

bool openMap(Editor& editor, const QString& mapId)
{
    if (editor.mapIndexById(mapId) < 0) return false;
    if (!editor.session.openMapIds.contains(mapId)) editor.session.openMapIds.push_back(mapId);
    editor.session.recentlyClosedMapIds.removeAll(mapId);
    return true;
}

MapCloseResult closeMap(Editor& editor, const QString& mapId, int preferredFallbackIndex)
{
    normalize(editor);

    MapCloseResult result;
    const int closingIndex = editor.session.openMapIds.indexOf(mapId);
    if (editor.session.openMapIds.size() <= 1 || closingIndex < 0) return result;

    editor.session.openMapIds.removeAt(closingIndex);
    editor.session.recentlyClosedMapIds.removeAll(mapId);
    editor.session.recentlyClosedMapIds.prepend(mapId);
    while (editor.session.recentlyClosedMapIds.size() > 12)
        editor.session.recentlyClosedMapIds.removeLast();

    const int requestedIndex = preferredFallbackIndex >= 0 ? preferredFallbackIndex : closingIndex;
    const int fallbackIndex = qBound(0, requestedIndex, editor.session.openMapIds.size() - 1);
    result.closed = true;
    result.fallbackMapId = editor.session.openMapIds.at(fallbackIndex);
    return result;
}

void setOpenOrder(Editor& editor, const QVector<QString>& orderedMapIds)
{
    QVector<QString> ordered;
    ordered.reserve(editor.session.openMapIds.size());
    for (const QString& id : orderedMapIds) {
        if (!editor.session.openMapIds.contains(id) || ordered.contains(id)) continue;
        ordered.push_back(id);
    }
    for (const QString& id : std::as_const(editor.session.openMapIds))
        if (!ordered.contains(id)) ordered.push_back(id);
    editor.session.openMapIds = ordered;
    normalize(editor);
}

void forgetRemovedMaps(Editor& editor, const QSet<QString>& removedIds)
{
    for (const QString& id : removedIds) {
        editor.session.openMapIds.removeAll(id);
        editor.session.recentlyClosedMapIds.removeAll(id);
        editor.session.mapViewports.remove(id);
    }
    normalize(editor);
}

void recordViewport(Editor& editor, const QString& mapId, double zoom, const QPointF& center)
{
    if (editor.mapIndexById(mapId) < 0) return;
    MapViewportState state;
    state.zoom = zoom;
    state.center = center;
    state.initialized = true;
    editor.session.mapViewports.insert(mapId, state);
}

bool viewport(const Editor& editor, const QString& mapId, MapViewportState* state)
{
    const auto it = editor.session.mapViewports.constFind(mapId);
    if (it == editor.session.mapViewports.constEnd() || !it->initialized) return false;
    if (state) *state = it.value();
    return true;
}

QString takeRecentlyClosed(Editor& editor)
{
    while (!editor.session.recentlyClosedMapIds.isEmpty()) {
        const QString id = editor.session.recentlyClosedMapIds.takeFirst();
        if (editor.mapIndexById(id) >= 0) return id;
    }
    return QString();
}

} // namespace core::mapworkspace
