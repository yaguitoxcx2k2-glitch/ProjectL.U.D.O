#include "Fog.h"
#include "core/ProjectIO.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <cmath>

using namespace core;

namespace game {

namespace {

QPainter::CompositionMode composition(FogBlend b)
{
    switch (b) {
    case FogBlend::Add:      return QPainter::CompositionMode_Plus;
    case FogBlend::Multiply: return QPainter::CompositionMode_Multiply;
    case FogBlend::Screen:   return QPainter::CompositionMode_Screen;
    case FogBlend::Overlay:  return QPainter::CompositionMode_Overlay;
    case FogBlend::Normal:   return QPainter::CompositionMode_SourceOver;
    }
    return QPainter::CompositionMode_SourceOver;
}

PictureBlend gpuBlend(FogBlend b)
{
    switch (b) {
    case FogBlend::Add:      return PictureBlend::Add;
    case FogBlend::Multiply: return PictureBlend::Multiply;
    case FogBlend::Screen:
    case FogBlend::Overlay:  return PictureBlend::Screen; // melhor equivalente no pipeline fixo
    case FogBlend::Normal:   return PictureBlend::Normal;
    }
    return PictureBlend::Normal;
}

double wrapped(double v, double size)
{
    if (size <= 0) return 0;
    v = std::fmod(v, size);
    if (v > 0) v -= size;
    return v;
}

} // namespace

void FogManager::resetFrom(const Editor& ed)
{
    m_live.clear();
    const MapDoc* d = ed.doc();
    if (!d) return;
    for (const FogDef& f : d->environment.fogs)
        if (f.enabled && !f.image.isNull()) show(f);
}

void FogManager::show(const FogDef& def)
{
    if (def.slot < 1 || def.slot > 5 || def.image.isNull()) return;
    Live l;
    l.def = def;
    l.opacity = def.fadeInFrames > 0 ? 0.0 : def.opacity;
    if (def.fadeInFrames > 0) {
        l.fadeStart = 0.0;
        l.fadeTarget = def.opacity;
        l.fadeDuration = def.fadeInFrames / 60.0;
    }
    m_live[def.slot] = l;
}

void FogManager::remove(int slot, int fadeFrames)
{
    auto it = m_live.find(slot);
    if (it == m_live.end()) return;
    if (fadeFrames <= 0) { m_live.erase(it); return; }
    it->fadeStart = it->opacity;
    it->fadeTarget = 0;
    it->fadeElapsed = 0;
    it->fadeDuration = fadeFrames / 60.0;
    it->removeAfterFade = true;
}

void FogManager::setOpacity(int slot, int opacity, int durationFrames)
{
    auto it = m_live.find(slot);
    if (it == m_live.end()) return;
    const int target = qBound(0, opacity, 255);
    if (durationFrames <= 0) {
        it->opacity = target;
        it->fadeDuration = 0;
        return;
    }
    it->fadeStart = it->opacity;
    it->fadeTarget = target;
    it->fadeElapsed = 0;
    it->fadeDuration = durationFrames / 60.0;
    it->removeAfterFade = false;
}

void FogManager::setBlend(int slot, FogBlend blend)
{
    if (auto it = m_live.find(slot); it != m_live.end()) it->def.blend = blend;
}

void FogManager::setScroll(int slot, double x, double y)
{
    if (auto it = m_live.find(slot); it != m_live.end()) {
        it->def.scrollX = x; it->def.scrollY = y;
    }
}

void FogManager::clear() { m_live.clear(); }

void FogManager::update(double dt)
{
    for (auto it = m_live.begin(); it != m_live.end();) {
        Live& l = it.value();
        l.originX += l.def.scrollX * 60.0 * dt;
        l.originY += l.def.scrollY * 60.0 * dt;
        if (l.fadeDuration > 0) {
            l.fadeElapsed += dt;
            const double t = qBound(0.0, l.fadeElapsed / l.fadeDuration, 1.0);
            l.opacity = l.fadeStart + (l.fadeTarget - l.fadeStart) * t;
            if (t >= 1.0) l.fadeDuration = 0;
        }
        if (l.removeAfterFade && l.fadeDuration <= 0 && l.opacity <= 0.01)
            it = m_live.erase(it);
        else ++it;
    }
}

QJsonArray FogManager::toJson() const
{
    QJsonArray result;
    QList<int> slotIds = m_live.keys();
    std::sort(slotIds.begin(), slotIds.end());
    for (int slot : slotIds) {
        const Live& live = m_live.value(slot);
        QJsonObject object{
            {QStringLiteral("slot"), live.def.slot},
            {QStringLiteral("enabled"), live.def.enabled},
            {QStringLiteral("source"), live.def.sourcePath},
            {QStringLiteral("blend"), fogBlendId(live.def.blend)},
            {QStringLiteral("definitionOpacity"), live.def.opacity},
            {QStringLiteral("opacity"), live.opacity},
            {QStringLiteral("scrollX"), live.def.scrollX},
            {QStringLiteral("scrollY"), live.def.scrollY},
            {QStringLiteral("zoom"), live.def.zoom},
            {QStringLiteral("tileRepeat"), live.def.tileRepeat},
            {QStringLiteral("fadeStart"), live.fadeStart},
            {QStringLiteral("fadeTarget"), live.fadeTarget},
            {QStringLiteral("fadeElapsed"), live.fadeElapsed},
            {QStringLiteral("fadeDuration"), live.fadeDuration},
            {QStringLiteral("originX"), live.originX},
            {QStringLiteral("originY"), live.originY},
            {QStringLiteral("removeAfterFade"), live.removeAfterFade}
        };
        if (!live.def.image.isNull())
            object[QStringLiteral("image")] = io::imageToDataUri(live.def.image);
        result.append(object);
    }
    return result;
}

void FogManager::restoreFromJson(const QJsonArray& array, const Editor& ed)
{
    m_live.clear();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        Live live;
        live.def.slot = qBound(1, object.value(QStringLiteral("slot")).toInt(1), 5);
        live.def.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        live.def.sourcePath = object.value(QStringLiteral("source")).toString();
        live.def.image = io::dataUriToImage(object.value(QStringLiteral("image")).toString());
        if (live.def.image.isNull() && !live.def.sourcePath.isEmpty()) {
            live.def.image = ed.preloadedRuntimeImage(live.def.sourcePath);
            if (live.def.image.isNull()) live.def.image.load(QDir(ed.projectRoot()).filePath(live.def.sourcePath));
        }
        if (live.def.image.isNull() && ed.doc()) {
            for (const FogDef& mapFog : ed.doc()->environment.fogs)
                if (mapFog.slot == live.def.slot) { live.def.image = mapFog.image; break; }
        }
        if (live.def.image.isNull()) continue;
        live.def.blend = fogBlendFromId(object.value(QStringLiteral("blend")).toString());
        live.def.opacity = qBound(0, object.value(QStringLiteral("definitionOpacity")).toInt(180), 255);
        live.def.scrollX = object.value(QStringLiteral("scrollX")).toDouble();
        live.def.scrollY = object.value(QStringLiteral("scrollY")).toDouble();
        live.def.zoom = qBound(0.1, object.value(QStringLiteral("zoom")).toDouble(1.0), 5.0);
        live.def.tileRepeat = object.value(QStringLiteral("tileRepeat")).toBool(true);
        live.opacity = qBound(0.0, object.value(QStringLiteral("opacity")).toDouble(live.def.opacity), 255.0);
        live.fadeStart = qBound(0.0, object.value(QStringLiteral("fadeStart")).toDouble(live.opacity), 255.0);
        live.fadeTarget = qBound(0.0, object.value(QStringLiteral("fadeTarget")).toDouble(live.opacity), 255.0);
        live.fadeDuration = qMax(0.0, object.value(QStringLiteral("fadeDuration")).toDouble());
        live.fadeElapsed = qBound(0.0, object.value(QStringLiteral("fadeElapsed")).toDouble(), live.fadeDuration);
        live.originX = object.value(QStringLiteral("originX")).toDouble();
        live.originY = object.value(QStringLiteral("originY")).toDouble();
        live.removeAfterFade = object.value(QStringLiteral("removeAfterFade")).toBool(false);
        m_live[live.def.slot] = live;
    }
}

void FogManager::draw(QPainter& p, const RuntimeRenderState& state) const
{
    const QSize screen = state.viewport;
    // A névoa preserva o tamanho histórico em pixels de tela, mas a origem
    // ligada ao mapa vem EXCLUSIVAMENTE da cadeia World -> Camera -> Screen.
    const QPointF worldOriginOnScreen = state.worldToScreen(QPointF(0.0, 0.0));
    p.save();
    p.setClipRect(state.screenRect());
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QList<int> slotIds = m_live.keys();
    std::sort(slotIds.begin(), slotIds.end());
    for (int slot : slotIds) {
        const auto it=m_live.constFind(slot); if(it==m_live.constEnd())continue;
        const Live& l = it.value();
        if (l.def.image.isNull() || l.opacity <= 0.01) continue;
        const double tw = qMax(1.0, l.def.image.width() * l.def.zoom);
        const double th = qMax(1.0, l.def.image.height() * l.def.zoom);
        const double sx = wrapped(worldOriginOnScreen.x() + l.originX, tw);
        const double sy = wrapped(worldOriginOnScreen.y() + l.originY, th);
        p.setOpacity(qBound(0.0, l.opacity / 255.0, 1.0));
        p.setCompositionMode(composition(l.def.blend));
        if (l.def.tileRepeat) {
            for (double y=sy; y<screen.height(); y+=th)
                for (double x=sx; x<screen.width(); x+=tw)
                    p.drawImage(QRectF(x,y,tw,th), l.def.image);
        } else {
            p.drawImage(QRectF(sx,sy,tw,th), l.def.image);
        }
    }
    p.restore();
}

void FogManager::appendQuads(SpriteBatcher& out, ImageProvider& provider,
                             const RuntimeRenderState& state) const
{
    const QSize screen = state.viewport;
    const QPointF worldOriginOnScreen = state.worldToScreen(QPointF(0.0, 0.0));
    QList<int> slotIds = m_live.keys();
    std::sort(slotIds.begin(), slotIds.end());
    for (int slot : slotIds) {
        const auto it=m_live.constFind(slot); if(it==m_live.constEnd())continue;
        const Live& l = it.value();
        if (l.def.image.isNull() || l.opacity <= 0.01) continue;
        const QString key = TextureKey::image(QStringLiteral("fog:%1:%2")
                                                   .arg(slot).arg(l.def.image.cacheKey()));
        if (!provider.contains(key)) provider.insert(key, l.def.image);
        const double twScreen = qMax(1.0, l.def.image.width() * l.def.zoom);
        const double thScreen = qMax(1.0, l.def.image.height() * l.def.zoom);
        const double sx = wrapped(worldOriginOnScreen.x() + l.originX, twScreen);
        const double sy = wrapped(worldOriginOnScreen.y() + l.originY, thScreen);
        auto add = [&](double x, double y) {
            const QRectF dst(x, y, twScreen, thScreen);
            out.add(key, dst, QRectF(l.def.image.rect()), l.def.image.size(),
                    l.opacity/255.0, QColor(), gpuBlend(l.def.blend), true,
                    RuntimeCoordinateSpace::Screen);
        };
        if (l.def.tileRepeat) {
            for (double y=sy; y<screen.height(); y+=thScreen)
                for (double x=sx; x<screen.width(); x+=twScreen) add(x,y);
        } else add(sx,sy);
    }
}

} // namespace game
