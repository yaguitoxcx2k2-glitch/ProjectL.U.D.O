#pragma once

#include "core/Editor.h"
#include "game/SpriteBatch.h"

#include <QHash>
#include <QJsonArray>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace game {

/// Estado das névoas durante a partida. Não altera o projeto do editor.
class FogManager
{
public:
    void resetFrom(const core::Editor& ed);
    void update(double dt);

    void show(const core::FogDef& def);
    void remove(int slot, int fadeFrames = 0);
    void setOpacity(int slot, int opacity, int durationFrames = 0);
    void setBlend(int slot, core::FogBlend blend);
    void setScroll(int slot, double x, double y);
    void clear();
    bool contains(int slot) const { return m_live.contains(slot); }
    int count() const { return m_live.size(); }
    QJsonArray toJson() const;
    void restoreFromJson(const QJsonArray& array, const core::Editor& ed);

    /// CPU: desenha usando o mesmo RuntimeRenderState do restante do frame.
    void draw(QPainter& p, const RuntimeRenderState& state) const;
    /// GPU: fog nasce explicitamente em Screen Space; não precisa mais
    /// falsificar coordenadas de mundo para cancelar câmera/zoom depois.
    void appendQuads(SpriteBatcher& out, ImageProvider& provider,
                     const RuntimeRenderState& state) const;

private:
    struct Live {
        core::FogDef def;
        double opacity = 0.0;
        double fadeStart = 0.0, fadeTarget = 0.0;
        double fadeElapsed = 0.0, fadeDuration = 0.0;
        double originX = 0.0, originY = 0.0;
        bool removeAfterFade = false;
    };
    QHash<int, Live> m_live;
};

} // namespace game
