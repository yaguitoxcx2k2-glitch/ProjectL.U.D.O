#include "RuntimeRenderState.h"

#include "ScreenTone.h"

#include <QtGlobal>
#include <cmath>

namespace game {

namespace {

float finiteTone(float value, float lo, float hi)
{
    if (!std::isfinite(double(value))) return 0.0f;
    return qBound(lo, value, hi);
}

double finiteCoord(double value)
{
    return std::isfinite(value) ? value : 0.0;
}

double safePadding(double padding)
{
    return std::isfinite(padding) ? qMax(0.0, padding) : 0.0;
}

} // namespace

const char* runtimeCoordinateSpaceName(RuntimeCoordinateSpace space)
{
    switch (space) {
    case RuntimeCoordinateSpace::World:  return "World";
    case RuntimeCoordinateSpace::Camera: return "Camera";
    case RuntimeCoordinateSpace::Screen: return "Screen";
    case RuntimeCoordinateSpace::Ui:     return "Ui";
    }
    return "Unknown";
}

const char* runtimeVisualStageName(RuntimeVisualStage stage)
{
    switch (stage) {
    case RuntimeVisualStage::Panorama:         return "Panorama";
    case RuntimeVisualStage::MapBelow:         return "MapBelow";
    case RuntimeVisualStage::Actors:           return "Actors";
    case RuntimeVisualStage::MapAbove:         return "MapAbove";
    case RuntimeVisualStage::Fog:              return "Fog";
    case RuntimeVisualStage::Weather:          return "Weather";
    case RuntimeVisualStage::ScreenTone:       return "ScreenTone";
    case RuntimeVisualStage::PicturesBelowUi:  return "PicturesBelowUi";
    case RuntimeVisualStage::ScreenEffects:    return "ScreenEffects";
    case RuntimeVisualStage::Ui:               return "Ui";
    case RuntimeVisualStage::PicturesAboveUi:  return "PicturesAboveUi";
    case RuntimeVisualStage::Presentation:     return "Presentation";
    }
    return "Unknown";
}

const std::array<RuntimeVisualStage, 12>& runtimeVisualStageOrder()
{
    static const std::array<RuntimeVisualStage, 12> stages = {
        RuntimeVisualStage::Panorama,
        RuntimeVisualStage::MapBelow,
        RuntimeVisualStage::Actors,
        RuntimeVisualStage::MapAbove,
        RuntimeVisualStage::Fog,
        RuntimeVisualStage::Weather,
        RuntimeVisualStage::ScreenTone,
        RuntimeVisualStage::PicturesBelowUi,
        RuntimeVisualStage::ScreenEffects,
        RuntimeVisualStage::Ui,
        RuntimeVisualStage::PicturesAboveUi,
        RuntimeVisualStage::Presentation
    };
    return stages;
}

bool runtimeVisualStageAllowsSpace(RuntimeVisualStage stage, RuntimeCoordinateSpace space)
{
    switch (stage) {
    case RuntimeVisualStage::Panorama:
        // Panorama normal nasce em World; "Fixar na tela" nasce em Screen.
        return space == RuntimeCoordinateSpace::World || space == RuntimeCoordinateSpace::Screen;
    case RuntimeVisualStage::MapBelow:
    case RuntimeVisualStage::Actors:
    case RuntimeVisualStage::MapAbove:
        return space == RuntimeCoordinateSpace::World;
    case RuntimeVisualStage::Fog:
        // Fog histórico preserva tamanho em pixels de tela, mas sua origem é
        // derivada exclusivamente pela projeção oficial do RuntimeRenderState.
        return space == RuntimeCoordinateSpace::Screen;
    case RuntimeVisualStage::Weather:
        return space == RuntimeCoordinateSpace::World;
    case RuntimeVisualStage::ScreenTone:
    case RuntimeVisualStage::ScreenEffects:
        return space == RuntimeCoordinateSpace::Screen;
    case RuntimeVisualStage::PicturesBelowUi:
    case RuntimeVisualStage::PicturesAboveUi:
        return space == RuntimeCoordinateSpace::World || space == RuntimeCoordinateSpace::Screen;
    case RuntimeVisualStage::Ui:
        return space == RuntimeCoordinateSpace::Screen || space == RuntimeCoordinateSpace::Ui;
    case RuntimeVisualStage::Presentation:
        return space == RuntimeCoordinateSpace::Screen || space == RuntimeCoordinateSpace::Ui;
    }
    return false;
}

ScreenToneUniforms screenToneUniforms(const ScreenToneState& tone)
{
    ScreenToneUniforms out;
    out.red = float(tone.red()) / 255.0f;
    out.green = float(tone.green()) / 255.0f;
    out.blue = float(tone.blue()) / 255.0f;
    out.gray = float(tone.gray()) / 255.0f;
    return out;
}

double normalizedRuntimeZoom(double zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0.0) return 1.0;
    return qBound(0.01, zoom, 64.0);
}

RuntimeRenderState makeRuntimeRenderState(const QPointF& camera,
                                          const QSize& viewport,
                                          const QSizeF& mapSize,
                                          double zoom,
                                          const ScreenToneUniforms& tone,
                                          const QPointF& screenOffset)
{
    RuntimeRenderState out;
    out.camera = QPointF(finiteCoord(camera.x()), finiteCoord(camera.y()));
    out.screenOffset = QPointF(finiteCoord(screenOffset.x()), finiteCoord(screenOffset.y()));
    out.viewport = QSize(qMax(1, viewport.width()), qMax(1, viewport.height()));
    out.mapSize = QSizeF(qMax(0.0, finiteCoord(mapSize.width())),
                         qMax(0.0, finiteCoord(mapSize.height())));
    out.zoom = normalizedRuntimeZoom(zoom);
    out.tone.red = finiteTone(tone.red, -1.0f, 1.0f);
    out.tone.green = finiteTone(tone.green, -1.0f, 1.0f);
    out.tone.blue = finiteTone(tone.blue, -1.0f, 1.0f);
    out.tone.gray = finiteTone(tone.gray, 0.0f, 1.0f);
    return out;
}

QRectF RuntimeRenderState::screenRect() const
{
    return QRectF(QPointF(0.0, 0.0), QSizeF(viewport));
}

QRectF RuntimeRenderState::cameraRect(double padding) const
{
    const double z = normalizedRuntimeZoom(zoom);
    const double p = safePadding(padding);
    return QRectF(-p, -p,
                  viewport.width() / z + p * 2.0,
                  viewport.height() / z + p * 2.0);
}

QRectF RuntimeRenderState::cameraWorldRect(double padding) const
{
    return cameraToWorld(cameraRect(padding));
}

QRectF RuntimeRenderState::mapWorldRect() const
{
    return QRectF(QPointF(0.0, 0.0), mapSize);
}

QRectF RuntimeRenderState::visibleWorldRect(double padding) const
{
    const QRectF map = mapWorldRect();
    if (map.isEmpty()) return {};
    return cameraWorldRect(padding).intersected(map);
}

QTransform RuntimeRenderState::worldToCameraTransform() const
{
    return QTransform(1.0, 0.0, 0.0, 1.0, -camera.x(), -camera.y());
}

QPointF RuntimeRenderState::worldToCamera(const QPointF& point) const
{
    return QPointF(point.x() - camera.x(), point.y() - camera.y());
}

QRectF RuntimeRenderState::worldToCamera(const QRectF& rect) const
{
    return rect.translated(-camera.x(), -camera.y());
}

QPointF RuntimeRenderState::cameraToWorld(const QPointF& point) const
{
    return QPointF(point.x() + camera.x(), point.y() + camera.y());
}

QRectF RuntimeRenderState::cameraToWorld(const QRectF& rect) const
{
    return rect.translated(camera.x(), camera.y());
}

QTransform RuntimeRenderState::cameraToScreenTransform() const
{
    const double z = normalizedRuntimeZoom(zoom);
    return QTransform(z, 0.0, 0.0, z, screenOffset.x(), screenOffset.y());
}

QPointF RuntimeRenderState::cameraToScreen(const QPointF& point) const
{
    const double z = normalizedRuntimeZoom(zoom);
    return QPointF(point.x() * z + screenOffset.x(),
                   point.y() * z + screenOffset.y());
}

QRectF RuntimeRenderState::cameraToScreen(const QRectF& rect) const
{
    const double z = normalizedRuntimeZoom(zoom);
    return QRectF(QPointF(rect.x() * z + screenOffset.x(),
                          rect.y() * z + screenOffset.y()),
                  QSizeF(rect.width() * z, rect.height() * z));
}

QPointF RuntimeRenderState::screenToCamera(const QPointF& point) const
{
    const double z = normalizedRuntimeZoom(zoom);
    return QPointF((point.x() - screenOffset.x()) / z,
                   (point.y() - screenOffset.y()) / z);
}

QRectF RuntimeRenderState::screenToCamera(const QRectF& rect) const
{
    const double z = normalizedRuntimeZoom(zoom);
    return QRectF(screenToCamera(rect.topLeft()), QSizeF(rect.width() / z, rect.height() / z));
}

QTransform RuntimeRenderState::worldToScreenTransform() const
{
    const double z = normalizedRuntimeZoom(zoom);
    // Forma matricial da cadeia World -> Camera -> Screen. O shake permanece
    // em pixels de tela e nunca é multiplicado pelo zoom.
    return QTransform(z, 0.0, 0.0, z,
                      -camera.x() * z + screenOffset.x(),
                      -camera.y() * z + screenOffset.y());
}

QPointF RuntimeRenderState::worldToScreen(const QPointF& point) const
{
    return cameraToScreen(worldToCamera(point));
}

QRectF RuntimeRenderState::worldToScreen(const QRectF& rect) const
{
    return cameraToScreen(worldToCamera(rect));
}

QPointF RuntimeRenderState::screenToWorld(const QPointF& point) const
{
    return cameraToWorld(screenToCamera(point));
}

QRectF RuntimeRenderState::screenToWorld(const QRectF& rect) const
{
    return cameraToWorld(screenToCamera(rect));
}

QPointF RuntimeRenderState::projectPoint(const QPointF& point, RuntimeCoordinateSpace space) const
{
    switch (space) {
    case RuntimeCoordinateSpace::World:  return worldToScreen(point);
    case RuntimeCoordinateSpace::Camera: return cameraToScreen(point);
    case RuntimeCoordinateSpace::Screen:
    case RuntimeCoordinateSpace::Ui:     return point;
    }
    return point;
}

QRectF RuntimeRenderState::projectRect(const QRectF& rect, RuntimeCoordinateSpace space) const
{
    switch (space) {
    case RuntimeCoordinateSpace::World:  return worldToScreen(rect);
    case RuntimeCoordinateSpace::Camera: return cameraToScreen(rect);
    case RuntimeCoordinateSpace::Screen:
    case RuntimeCoordinateSpace::Ui:     return rect;
    }
    return rect;
}

double RuntimeRenderState::scaleFor(RuntimeCoordinateSpace space) const
{
    return (space == RuntimeCoordinateSpace::World || space == RuntimeCoordinateSpace::Camera)
        ? normalizedRuntimeZoom(zoom) : 1.0;
}

} // namespace game
