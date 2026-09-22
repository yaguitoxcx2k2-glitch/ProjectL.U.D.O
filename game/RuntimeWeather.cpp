#include "RuntimeWeather.h"
#include "SpriteBatch.h"

#include <QPainter>

#include <QtGlobal>
#include <cmath>

namespace game {

namespace {

quint32 weatherHash(quint32 value)
{
    value += 0x9e3779b9u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

double weatherUnit(quint32 value)
{
    return double(weatherHash(value)) / 4294967296.0;
}

quint32 weatherSeed(int fieldX, int fieldY, int slot, int channel)
{
    quint32 value = quint32(fieldX) * 0x9e3779b1u;
    value ^= quint32(fieldY) * 0x85ebca6bu;
    value ^= quint32(slot + 1) * 0xc2b2ae35u;
    value ^= quint32(channel + 1) * 0x27d4eb2du;
    return weatherHash(value);
}

void appendClippedRect(RuntimeWeatherFrame& frame, const QRectF& rect, const QColor& color,
                       bool particle)
{
    const QRectF clipped = rect.intersected(frame.visibleWorld);
    if (clipped.isEmpty()) return;
    RuntimeWeatherPrimitive primitive;
    primitive.worldRect = clipped;
    primitive.color = color;
    frame.primitives.push_back(primitive);
    if (particle) ++frame.particleCount;
}

// Liang-Barsky sobre a linha central. O quad final é construído depois do
// clipping para garantir que Weather nunca invada letterbox/fora do mapa.
bool clipSegmentToRect(QPointF* a, QPointF* b, const QRectF& rect)
{
    if (!a || !b || rect.isEmpty()) return false;
    const double x0 = a->x(), y0 = a->y();
    const double dx = b->x() - x0, dy = b->y() - y0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0 - rect.left(), rect.right() - x0,
                         y0 - rect.top(), rect.bottom() - y0};
    double u0 = 0.0, u1 = 1.0;
    for (int i = 0; i < 4; ++i) {
        if (qFuzzyIsNull(p[i])) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) u0 = qMax(u0, t);
        else u1 = qMin(u1, t);
        if (u0 > u1) return false;
    }
    *a = QPointF(x0 + dx * u0, y0 + dy * u0);
    *b = QPointF(x0 + dx * u1, y0 + dy * u1);
    return true;
}

void appendRainStreak(RuntimeWeatherFrame& frame, QPointF tail, QPointF head,
                      double width, const QColor& color)
{
    const double half = qMax(0.35, width * 0.5);
    const QRectF safe = frame.visibleWorld.adjusted(half, half, -half, -half);
    if (safe.isEmpty() || !clipSegmentToRect(&tail, &head, safe)) return;
    const QPointF d = head - tail;
    const double len = std::hypot(d.x(), d.y());
    if (len < 0.5) return;
    const QPointF n(-d.y() / len * half, d.x() / len * half);
    QPolygonF quad;
    quad << tail - n << tail + n << head + n << head - n;
    const QRectF bounds = quad.boundingRect();
    if (bounds.isEmpty() || !frame.visibleWorld.contains(bounds)) return;
    RuntimeWeatherPrimitive primitive;
    primitive.worldRect = bounds;
    primitive.color = color;
    primitive.worldQuad = quad;
    frame.primitives.push_back(primitive);
    ++frame.particleCount;
}

void appendSnowDiamond(RuntimeWeatherFrame& frame, const QPointF& center,
                       double side, const QColor& color)
{
    const double radius = qMax(1.0, side * 0.5);
    const QRectF safe = frame.visibleWorld.adjusted(radius, radius, -radius, -radius);
    if (safe.isEmpty() || !safe.contains(center)) return;
    QPolygonF quad;
    quad << QPointF(center.x(), center.y() - radius)
         << QPointF(center.x() + radius, center.y())
         << QPointF(center.x(), center.y() + radius)
         << QPointF(center.x() - radius, center.y());
    RuntimeWeatherPrimitive primitive;
    primitive.worldRect = quad.boundingRect();
    primitive.color = color;
    primitive.worldQuad = quad;
    frame.primitives.push_back(primitive);
    ++frame.particleCount;
}

int stormFlashAlpha(double elapsed, int intensity)
{
    // Dois clarões curtos no início do mesmo ciclo usado pelo SE de trovão.
    // A curva evita o antigo frame branco seco e continua totalmente
    // determinística para Save/Load/CPU/QRhi.
    double phase = std::fmod(qMax(0.0, elapsed), 6.5);
    if (phase < 0.0) phase += 6.5;
    double pulse = 0.0;
    if (phase < 0.055) pulse = 1.0 - phase / 0.055;
    else if (phase >= 0.12 && phase < 0.19) pulse = 0.55 * (1.0 - (phase - 0.12) / 0.07);
    return qBound(0, qRound(pulse * (90.0 + intensity * 0.75)), 190);
}

} // namespace

RuntimeWeatherFrame buildRuntimeWeatherFrame(const RuntimeWeatherState& weather,
                                              const RuntimeRenderState& renderState)
{
    RuntimeWeatherFrame frame;
    if (!weather.active()) return frame;

    frame.visibleWorld = renderState.visibleWorldRect();
    if (frame.visibleWorld.isEmpty()) return frame;

    const bool snow = weather.kind == RuntimeWeatherKind::Snow;
    const bool storm = weather.kind == RuntimeWeatherKind::Storm;

    // Tempestade colore apenas o mundo visível. ScreenTone continua sendo a
    // próxima stage oficial, e Pictures/UI continuam acima do Weather.
    if (storm) {
        appendClippedRect(frame, frame.visibleWorld,
                          QColor(10, 16, 31, 24 + weather.intensity / 2), false);
        const int flashAlpha = stormFlashAlpha(weather.elapsed, weather.intensity);
        if (flashAlpha > 0)
            appendClippedRect(frame, frame.visibleWorld,
                              QColor(225, 235, 255, flashAlpha), false);
    }

    // Campo procedural em World Space. Cada partícula recebe pequenas variações
    // de velocidade/comprimento/ângulo; a câmera somente escolhe as células que
    // serão consultadas e nunca entra na seed.
    constexpr double fieldW = 192.0;
    constexpr double fieldH = 256.0;
    const int perField = snow ? (3 + weather.intensity / 7)
                              : (storm ? 6 + weather.intensity / 4
                                       : 4 + weather.intensity / 6);
    // Snow pode oscilar ~30 px para fora da célula-base; a margem precisa
    // cobrir esse sway para que flocos não apareçam/sumam ao cruzar fronteiras
    // de field/câmera. Rain usa ~40 px e Storm precisa de streaks maiores.
    const double pad = snow ? 40.0 : (storm ? 64.0 : 40.0);
    frame.generationWorld = frame.visibleWorld.adjusted(-pad, -pad, pad, pad)
                                .intersected(renderState.mapWorldRect());
    const QRectF& query = frame.generationWorld;
    if (query.isEmpty()) return frame;

    const int fx0 = int(std::floor(query.left() / fieldW));
    const int fy0 = int(std::floor(query.top() / fieldH));
    const int fx1 = int(std::floor(qMax(query.left(), query.right() - 0.000001) / fieldW));
    const int fy1 = int(std::floor(qMax(query.top(), query.bottom() - 0.000001) / fieldH));
    const int visibleFields = qMax(0, fx1 - fx0 + 1) * qMax(0, fy1 - fy0 + 1);
    frame.primitives.reserve(visibleFields * perField + (storm ? 2 : 0));

    for (int fy = fy0; fy <= fy1; ++fy) {
        for (int fx = fx0; fx <= fx1; ++fx) {
            ++frame.fieldCellsVisited;
            const double originX = fx * fieldW;
            const double originY = fy * fieldH;
            for (int slot = 0; slot < perField; ++slot) {
                const double baseX = weatherUnit(weatherSeed(fx, fy, slot, 0)) * fieldW;
                const double baseY = weatherUnit(weatherSeed(fx, fy, slot, 1)) * fieldH;
                const double phase = weatherUnit(weatherSeed(fx, fy, slot, 2)) * 6.283185307179586;
                const double variation = weatherUnit(weatherSeed(fx, fy, slot, 3));
                const double variation2 = weatherUnit(weatherSeed(fx, fy, slot, 4));

                if (snow) {
                    const double speed = 22.0 + variation * 32.0;
                    double localY = std::fmod(baseY + weather.elapsed * speed, fieldH);
                    if (localY < 0.0) localY += fieldH;
                    const double swaySpeed = 0.55 + variation2 * 1.35;
                    const double sway = 8.0 + variation * 22.0;
                    double localX = std::fmod(baseX + std::sin(weather.elapsed * swaySpeed + phase) * sway,
                                              fieldW);
                    if (localX < 0.0) localX += fieldW;
                    const double side = 2.5 + variation2 * 4.5;
                    appendSnowDiamond(frame, QPointF(originX + localX, originY + localY), side,
                                      QColor(244, 250, 255,
                                             qBound(125, 145 + weather.intensity / 2 + qRound(variation * 25.0), 225)));
                    continue;
                }

                const double speedBase = storm ? 500.0 : 350.0;
                const double speed = speedBase * (0.82 + variation * 0.34);
                double localY = std::fmod(baseY + weather.elapsed * speed, fieldH);
                if (localY < 0.0) localY += fieldH;
                const double drift = storm ? (0.42 + variation2 * 0.18)
                                           : (0.24 + variation2 * 0.14);
                double localX = std::fmod(baseX + localY * drift, fieldW);
                if (localX < 0.0) localX += fieldW;

                const double length = (storm ? 34.0 : 20.0) * (0.72 + variation * 0.62);
                const double width = (storm ? 1.9 : 1.25) * (0.82 + variation2 * 0.42);
                const double slant = length * (storm ? (0.42 + variation2 * 0.20)
                                                     : (0.28 + variation2 * 0.16));
                const QPointF head(originX + localX, originY + localY);
                const QPointF tail(head.x() - slant, head.y() - length);
                appendRainStreak(frame, tail, head, width,
                                 QColor(storm ? 205 : 188, storm ? 226 : 216, 255,
                                        qBound(115, 140 + weather.intensity / 2 + qRound(variation * 20.0), 220)));
            }
        }
    }

    // Fail-safe para intensidades baixas/câmeras muito pequenas. A fallback
    // usa a MESMA forma oficial do clima em vez de um retângulo especial.
    if (frame.particleCount == 0) {
        const QPointF center = frame.visibleWorld.center();
        if (snow) {
            appendSnowDiamond(frame, center, 5.0, QColor(248, 252, 255, 210));
        } else {
            const double length = storm ? 34.0 : 22.0;
            const double slant = length * (storm ? 0.5 : 0.34);
            appendRainStreak(frame, QPointF(center.x() - slant, center.y() - length * 0.5),
                             QPointF(center.x(), center.y() + length * 0.5),
                             storm ? 2.0 : 1.4,
                             QColor(storm ? 210 : 190, storm ? 230 : 218, 255, 205));
        }
    }

    return frame;
}

void drawRuntimeWeatherFrame(QPainter& painter, const RuntimeWeatherFrame& frame,
                             const RuntimeRenderState& renderState)
{
    if (frame.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setTransform(renderState.worldToScreenTransform(), false);
    painter.setClipRect(frame.visibleWorld);
    painter.setPen(Qt::NoPen);
    for (const RuntimeWeatherPrimitive& primitive : frame.primitives) {
        painter.setBrush(primitive.color);
        if (primitive.isQuad()) painter.drawPolygon(primitive.worldQuad);
        else painter.fillRect(primitive.worldRect, primitive.color);
    }
    painter.restore();
}

void appendRuntimeWeatherFrame(SpriteBatcher& out, const RuntimeWeatherFrame& frame)
{
    for (const RuntimeWeatherPrimitive& primitive : frame.primitives) {
        // RC2.66: ScreenTone é aplicado por etapa/batch para permitir Picture Layers
        // realmente intercaladas com Weather sem dupla tonalização.
        if (primitive.isQuad())
            out.addSolidQuad(primitive.worldQuad, primitive.color, true,
                             RuntimeCoordinateSpace::World);
        else
            out.addSolid(primitive.worldRect, primitive.color, true,
                         RuntimeCoordinateSpace::World);
    }
}

} // namespace game
