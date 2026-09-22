// ============================================================================
// RuntimeWeather.h — estado e geometria únicos do clima IN-GAME.
//
// O clima nasce sempre em World Space. CPU e GPU consomem o mesmo
// RuntimeWeatherFrame; nenhum backend pode projetar/ancorar partículas por
// conta própria. Isso evita regressões de câmera, zoom, teleporte e mapas
// menores que a viewport.
// ============================================================================
#pragma once

#include "core/Weather.h"
#include "game/RuntimeRenderState.h"

#include <QColor>
#include <QRectF>
#include <QPolygonF>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace game {

class SpriteBatcher;

// Compatibilidade de API: o runtime usa exatamente o mesmo WeatherState que
// MapEnvironment/Propriedades do Mapa. Estes aliases evitam churn nos módulos
// de render sem manter uma segunda representação de clima.
using RuntimeWeatherKind = core::WeatherKind;
using RuntimeWeatherState = core::WeatherState;

inline RuntimeWeatherKind runtimeWeatherKindFromString(QString type)
{ return core::weatherKindFromString(type); }
inline QString runtimeWeatherKindId(RuntimeWeatherKind kind)
{ return core::weatherKindId(kind); }

struct RuntimeWeatherPrimitive {
    /// Bounding box em World Space; permanece para diagnóstico/clipping/testes.
    QRectF worldRect;
    QColor color;
    /// Quando contém quatro pontos, representa um quad inclinado (chuva/neve)
    /// consumido igualmente por QPainter e SpriteBatcher. Vazio = worldRect.
    QPolygonF worldQuad;

    bool isQuad() const { return worldQuad.size() == 4; }
};

/// Fotografia imutável de um frame climático. Todos os retângulos estão em
/// World Space e já foram recortados ao mapa + área visível.
struct RuntimeWeatherFrame {
    QRectF visibleWorld;       ///< parte do mapa realmente visível
    QRectF generationWorld;    ///< faixa mundial consultada ao redor da câmera
    QVector<RuntimeWeatherPrimitive> primitives;
    int particleCount = 0;
    int fieldCellsVisited = 0; ///< prova de que não percorremos o mapa inteiro

    bool isEmpty() const { return primitives.isEmpty(); }
};

/// Gera a geometria oficial do clima. Determinística para
/// (estado, tempo, câmera): CPU e GPU recebem exatamente a mesma regra.
RuntimeWeatherFrame buildRuntimeWeatherFrame(const RuntimeWeatherState& weather,
                                              const RuntimeRenderState& renderState);

/// Consumidores oficiais do mesmo RuntimeWeatherFrame. CPU/QPainter e
/// QRhi/SpriteBatch não mantêm loops/clipping paralelos.
void drawRuntimeWeatherFrame(QPainter& painter, const RuntimeWeatherFrame& frame,
                             const RuntimeRenderState& renderState);
void appendRuntimeWeatherFrame(SpriteBatcher& out, const RuntimeWeatherFrame& frame);

} // namespace game
