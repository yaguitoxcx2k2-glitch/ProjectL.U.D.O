// ============================================================================
// RuntimeRenderState.h — contrato visual canônico do runtime GPU-first.
//
// Bloco 1 (fechamento 4.0): define os QUATRO espaços oficiais do runtime,
// a cadeia única World -> Camera -> Screen e a ordem semântica do frame.
// Nenhuma API gráfica entra aqui: QRhi consome a fotografia normalizada; o
// raster de referência pode validá-la sem virar um segundo runtime.
// ============================================================================
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTransform>

#include <array>

namespace game {

class ScreenToneState;

/// Espaços de coordenadas oficiais da LUDO.
///
/// World  = pixels absolutos do mapa.
/// Camera = pixels do mundo relativos ao topo-esquerdo da câmera, ANTES do zoom.
/// Screen = pixels da resolução lógica do jogo, DEPOIS da projeção da câmera.
/// Ui     = pixels lógicos da interface; semanticamente separado de Screen.
///
/// A única cadeia permitida para conteúdo do mapa é:
///     World -> Camera -> Screen
/// Screen/Ui nunca recebem câmera/zoom implicitamente.
enum class RuntimeCoordinateSpace : unsigned char {
    World = 0,
    Camera,
    Screen,
    Ui
};

const char* runtimeCoordinateSpaceName(RuntimeCoordinateSpace space);

/// Macro-estágios históricos do runtime. Eles continuam sendo a autoridade
/// para validação de espaço (World/Screen/Ui) e diagnóstico amplo. Desde RC2.66,
/// a ordem FINA das Picture Layers 0..9 é descrita por PictureLayerRenderGraph.h:
/// Pictures podem entrar entre estes macro-estágios e ScreenTone é aplicado por
/// etapa do mundo, não como um único passe global.
enum class RuntimeVisualStage : unsigned char {
    Panorama = 0,
    MapBelow,
    Actors,
    MapAbove,
    Fog,
    Weather,
    ScreenTone,
    PicturesBelowUi,
    ScreenEffects,
    Ui,
    PicturesAboveUi,
    Presentation
};

constexpr int runtimeVisualStageRank(RuntimeVisualStage stage)
{
    return static_cast<int>(stage);
}

constexpr bool runtimeVisualStageBefore(RuntimeVisualStage a, RuntimeVisualStage b)
{
    return runtimeVisualStageRank(a) < runtimeVisualStageRank(b);
}

const char* runtimeVisualStageName(RuntimeVisualStage stage);
const std::array<RuntimeVisualStage, 12>& runtimeVisualStageOrder();

/// Validação central de stage x espaço. O renderer usa isto como barreira de
/// segurança: um lote em espaço incompatível com sua etapa não é desenhado.
bool runtimeVisualStageAllowsSpace(RuntimeVisualStage stage, RuntimeCoordinateSpace space);

static_assert(runtimeVisualStageBefore(RuntimeVisualStage::Panorama, RuntimeVisualStage::MapBelow));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::MapBelow, RuntimeVisualStage::Actors));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::Actors, RuntimeVisualStage::MapAbove));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::MapAbove, RuntimeVisualStage::Fog));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::Fog, RuntimeVisualStage::Weather));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::Weather, RuntimeVisualStage::ScreenTone));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::ScreenTone, RuntimeVisualStage::PicturesBelowUi));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::PicturesBelowUi, RuntimeVisualStage::ScreenEffects));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::ScreenEffects, RuntimeVisualStage::Ui));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::Ui, RuntimeVisualStage::PicturesAboveUi));
static_assert(runtimeVisualStageBefore(RuntimeVisualStage::PicturesAboveUi, RuntimeVisualStage::Presentation));

struct ScreenToneUniforms {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float gray = 0.0f;

    bool active() const
    { return red != 0.0f || green != 0.0f || blue != 0.0f || gray != 0.0f; }
};

ScreenToneUniforms screenToneUniforms(const ScreenToneState& tone);

/// Zoom seguro para todos os caminhos do runtime. Nunca deixa NaN/Inf/zero
/// entrar na transformação de câmera.
double normalizedRuntimeZoom(double zoom);

struct RuntimeRenderState {
    QPointF camera;               ///< topo-esquerdo da câmera em World Space
    QPointF screenOffset;         ///< deslocamento World/Camera em pixels de tela (shake)
    QSize viewport = QSize(1, 1); ///< resolução lógica em Screen/Ui Space
    QSizeF mapSize;               ///< extensão do mapa em World Space
    double zoom = 1.0;
    ScreenToneUniforms tone;

    /// Retângulo inteiro da resolução lógica.
    QRectF screenRect() const;
    /// Viewport expresso em Camera Space (origem 0,0; unidades antes do zoom).
    QRectF cameraRect(double padding = 0.0) const;
    /// Área do mundo enxergada pela câmera antes do clipping no mapa.
    QRectF cameraWorldRect(double padding = 0.0) const;
    /// Limites físicos do mapa em World Space.
    QRectF mapWorldRect() const;
    /// Área efetivamente visível do mapa, já intersectada com seus limites.
    QRectF visibleWorldRect(double padding = 0.0) const;

    /// Etapa 1 oficial: World -> Camera (subtrai apenas a câmera).
    QTransform worldToCameraTransform() const;
    QPointF worldToCamera(const QPointF& point) const;
    QRectF worldToCamera(const QRectF& rect) const;
    QPointF cameraToWorld(const QPointF& point) const;
    QRectF cameraToWorld(const QRectF& rect) const;

    /// Etapa 2 oficial: Camera -> Screen (zoom + screenOffset/shake).
    QTransform cameraToScreenTransform() const;
    QPointF cameraToScreen(const QPointF& point) const;
    QRectF cameraToScreen(const QRectF& rect) const;
    QPointF screenToCamera(const QPointF& point) const;
    QRectF screenToCamera(const QRectF& rect) const;

    /// Composição oficial World -> Camera -> Screen. Recursos não devem
    /// reproduzir essa matemática manualmente.
    QTransform worldToScreenTransform() const;
    QPointF worldToScreen(const QPointF& point) const;
    QRectF worldToScreen(const QRectF& rect) const;
    QPointF screenToWorld(const QPointF& point) const;
    QRectF screenToWorld(const QRectF& rect) const;

    /// Projeta conforme o espaço DECLARADO pelo recurso. Este é o único ponto
    /// genérico de projeção usado pelo QRhi.
    QPointF projectPoint(const QPointF& point, RuntimeCoordinateSpace space) const;
    QRectF projectRect(const QRectF& rect, RuntimeCoordinateSpace space) const;
    double scaleFor(RuntimeCoordinateSpace space) const;
};

/// Fotografia canônica apresentada pelo runtime GPU. O serial muda uma única
/// vez por tick; todos os lotes, overlays e uniforms de um render usam a mesma
/// instância, impedindo estados temporais misturados dentro do quadro.
struct RuntimeVisualFrame {
    RuntimeRenderState renderState;
    quint64 serial = 0;

    bool valid() const { return renderState.viewport.isValid() && serial > 0; }
};

/// Factory única: normaliza viewport, mapa, câmera, zoom, tone e shake antes
/// de os renderizadores enxergarem o frame. Defaults ausentes/invalidos
/// degradam de forma previsível em vez de gerar divisão por zero ou NaN.
RuntimeRenderState makeRuntimeRenderState(const QPointF& camera,
                                          const QSize& viewport,
                                          const QSizeF& mapSize,
                                          double zoom,
                                          const ScreenToneUniforms& tone = {},
                                          const QPointF& screenOffset = {});

} // namespace game
