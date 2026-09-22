// ============================================================================
//  RhiGameWindow.h — runtime oficial GPU-first da LUDO.
//
//  Usa o **QRhi** do Qt: escrevemos UM renderizador e o Qt o traduz para a API
//  nativa de cada sistema —
//      Windows: Direct3D 11 (padrão daqui) ou Direct3D 12
//      Linux:   Vulkan ou OpenGL
//      macOS:   Metal
//  Os shaders são escritos uma vez em GLSL e compilados pelo `qsb` para HLSL,
//  MSL e SPIR-V (ver shaders/README.md).
//
//  Divisão de trabalho:
//    · mapa, objetos e personagens -> quads texturizados na GPU;
//    · mensagens, legendas e HUD -> QPainter gera pixels somente quando o
//      overlay fica dirty; a composição/apresentação continua no QRhi.
//
//  `GameSession` fornece um RuntimeVisualFrame canônico. O raster QPainter
//  remanescente é referência de testes/gerador de texturas, não outro runtime.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/RuntimeProject.h"
#include "game/GameSession.h"
#include "game/GamepadInput.h"
#include "game/RuntimeAudio.h"
#include "game/SpriteBatch.h"
#include "game/GpuMapMeshData.h"

#include <QHash>
#include <QSet>
#include <QVector>
#include <QImage>
#include "game/FrameClock.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <memory>
#include <array>

QT_BEGIN_NAMESPACE
class QTimer;
class QMouseEvent;
class QWheelEvent;
QT_END_NAMESPACE

namespace game { class LiveEventInspector; }

namespace game {

class RhiGameWindow : public QRhiWidget
{
    Q_OBJECT
public:
    RhiGameWindow(core::Editor& ed, const QPointF& startPixel, QWidget* parent = nullptr,
                  bool standaloneMenus = false, bool debugTools = true,
                  const QString& backendOverride = QString());
    RhiGameWindow(std::unique_ptr<core::RuntimeProject> project, const QPointF& startPixel, QWidget* parent = nullptr,
                  bool standaloneMenus = false, bool debugTools = true,
                  const QString& backendOverride = QString());
    ~RhiGameWindow() override;
    bool loadGame(int slot,QString* error=nullptr) { return m_s.loadGame(slot,error); }
    void startEntryFade(int frames = 24) { m_s.startEntryFade(frames); update(); }
    void setHotReloadSource(core::Editor* source) { m_s.setHotReloadSource(source); }

    /// Nome do backend em uso ("D3D11", "Vulkan"…) — vai para o HUD e para a
    /// mensagem de diagnóstico.
    QString backendName() const { return m_backend; }
    QString requestedBackendId() const { return m_requestedBackend; }
    /// Já conseguimos desenhar pelo menos um quadro? O vigia usa este sinal
    /// para tentar o próximo backend GPU da plataforma quando necessário.
    bool    primeiroQuadroOk() const { return m_quadrosOk > 0; }
    /// Contadores somente-leitura usados pelo benchmark/diagnóstico. Não alteram
    /// o loop do jogo nem o estado persistido.
    int renderedFrameCount() const { return m_quadrosOk; }
    RuntimeProfilerSnapshot performanceSnapshot() const { return m_s.profilerSnapshot(); }
    /// Onde está o relatório de diagnóstico desta sessão.
    static QString caminhoDoLog();

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void tick();
    void openGameMenu();
    bool pollGamepad();
    void montarCena(const RuntimeVisualFrame& frame);
    LiveEventInspector* m_liveInspector = nullptr;
    void collectUnusedDynamicTextures();
    bool ensureOffscreen();

    std::unique_ptr<core::RuntimeProject> m_runtimeProject;
    GameSession m_s;
    RuntimeAudio m_audio;
    QString m_backend;
    QString m_requestedBackend;
    int     m_quadrosOk = 0;
    const QRhiRenderPassDescriptor* m_rpdUsado = nullptr;

    // ---- recursos de GPU --------------------------------------------------
    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_vbuf, m_ubuf, m_worldUbuf;
    std::unique_ptr<QRhiSampler> m_sampler, m_linearSampler;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipe;
    // Variantes de cena que executam o Ludo Filter System no próprio draw.
    // São usadas somente quando o escopo exclui alguma camada (Pictures/HUD),
    // preservando a ordem de composição sem framebuffer/máscara extra.
    std::unique_ptr<QRhiGraphicsPipeline> m_filterPipe;
    /// Primeiro passe: WorldScene SEM tone. O tone é aplicado uma única vez
    /// sobre a cena já composta antes de Weather/Overlay, igual ao CPU.
    std::unique_ptr<QRhiGraphicsPipeline> m_worldPipe;
    std::unique_ptr<QRhiGraphicsPipeline> m_worldFilterPipe;
    // Cena lógica -> textura -> janela. O antigo Ludo Screen Filters usava
    // dois passes e um shader grande; a ampliação agora é um único passe.
    std::unique_ptr<QRhiTexture> m_sceneTexture;
    std::unique_ptr<QRhiTexture> m_worldTexture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_sceneRpd, m_worldRpd;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneTarget, m_worldTarget;
    std::unique_ptr<QRhiSampler> m_postNearest,m_postLinear;
    std::unique_ptr<QRhiShaderResourceBindings> m_postSrbNearest,m_postSrbLinear;
    std::unique_ptr<QRhiShaderResourceBindings> m_worldToneSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_postPipe;
    // Ludo Filter System: pipeline alternativo do MESMO post-pass final.
    // Não há framebuffer nem draw pass extra quando um filtro está ativo.
    std::unique_ptr<QRhiGraphicsPipeline> m_filterPostPipe;
    QSize m_sceneSize;
    /// Um pipeline por mistura (somar, multiplicar, clarear): na GPU a
    /// mistura é ESTADO, não um parâmetro do desenho.
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeAdd, m_pipeMul, m_pipeScreen;
    std::unique_ptr<QRhiGraphicsPipeline> m_filterPipeAdd, m_filterPipeMul, m_filterPipeScreen;
    std::unique_ptr<QRhiGraphicsPipeline> m_worldPipeAdd, m_worldPipeMul, m_worldPipeScreen;
    std::unique_ptr<QRhiGraphicsPipeline> m_worldFilterPipeAdd, m_worldFilterPipeMul, m_worldFilterPipeScreen;
    QRhiGraphicsPipeline* pipelineDe(core::PictureBlend b) const;
    QRhiGraphicsPipeline* filterPipelineDe(core::PictureBlend b) const;
    QRhiGraphicsPipeline* worldPipelineDe(core::PictureBlend b) const;
    QRhiGraphicsPipeline* worldFilterPipelineDe(core::PictureBlend b) const;
    struct Tex {
        std::unique_ptr<QRhiTexture> tex;
        /// SRB de geometria já projetada em pixels de Screen.
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        std::unique_ptr<QRhiShaderResourceBindings> srbLinear;
        /// O7: mesma textura, mas com matriz World->Screen no uniform.
        std::unique_ptr<QRhiShaderResourceBindings> worldSrb;
        std::unique_ptr<QRhiShaderResourceBindings> worldSrbLinear;
    };
    /// Garante que a textura desta chave existe na GPU (sobe se for nova) e
    /// devolve o handle resolvido para evitar novo lookup textual no submit.
    std::shared_ptr<Tex> garantirTextura(const QString& chave, QRhiResourceUpdateBatch* u);
    struct RenderSegment {
        QString textureKey;
        std::shared_ptr<Tex> texture;
        quint32 inicio = 0, contagem = 0;
        core::PictureBlend blend = core::PictureBlend::Normal;
        double depth = 0.0;
        bool screenTone = true;
        bool smooth = false;
        RuntimeCoordinateSpace space = RuntimeCoordinateSpace::World;
        int filterDomain = 0; ///< 1=World, 2=Pictures, 3=HUD; codificado também no alpha.
    };
    // QHash exige valor copiável; os recursos de GPU são move-only.
    QHash<QString, std::shared_ptr<Tex>> m_texturas;
    ImageProvider m_imagens;      ///< imagens ainda não enviadas / fonte da verdade
    /// Bloco 1: um batcher por etapa semântica relevante. A ordem não depende
    /// mais de convenções implícitas dentro de um único vetor.
    SpriteBatcher m_panoramaBatcher;
    SpriteBatcher m_mapBelowBatcher;
    SpriteBatcher m_mapStarBatcher;
    SpriteBatcher m_actorBelowBatcher;
    SpriteBatcher m_actorSameBatcher;
    SpriteBatcher m_actorAboveBatcher;
    SpriteBatcher m_mapAboveBatcher;
    SpriteBatcher m_fogBatcher;
    SpriteBatcher m_weatherBatcher;
    /// RC2.66: um batcher por PictureLayer. Layers 0..6 são intercaladas
    /// com o mundo; 7/8/9 ficam na composição de UI/apresentação.
    std::array<SpriteBatcher, 10> m_pictureLayerBatchers;
    // O frame de um Autotile animado não deve obrigar a remontar todos os
    // quads visíveis a cada ciclo. Guardamos algumas variantes recentes do
    // mesmo conjunto de chunks (tipicamente 4 frames) e apenas as reutilizamos.
    struct MapBatchCacheKey {
        int x = 0, y = 0, width = 0, height = 0;
        quint64 animationSignature = 0;
        quint64 revision = 0;
        quint64 runtimeRevision = 0;
        bool operator==(const MapBatchCacheKey& other) const noexcept {
            return x == other.x && y == other.y && width == other.width && height == other.height
                && animationSignature == other.animationSignature && revision == other.revision
                && runtimeRevision == other.runtimeRevision;
        }
    };
    struct GpuMapSegment {
        QString textureKey;
        std::shared_ptr<Tex> texture;
        quint32 firstVertex = 0;
        quint32 vertexCount = 0;
        core::PictureBlend blend = core::PictureBlend::Normal;
        double depth = 0.0;
    };
    struct GpuMapMesh {
        std::unique_ptr<QRhiBuffer> vertexBuffer;
        QVector<GpuMapSegment> below, stars, above;
        quint32 vertexBytes = 0;
        int quads = 0;
    };
    struct MapBatchCacheEntry {
        MapBatchCacheKey key;
        SpriteBatcher below, stars, above;
        /// O7: criado sob demanda na primeira utilização desta região/frame.
        /// Depois fica residente na GPU até a entrada LRU ser invalidada/evictada.
        std::shared_ptr<GpuMapMesh> gpuMesh;
        quint64 stamp = 0;
    };
    QVector<MapBatchCacheEntry> m_mapBatchCache;
    quint64 m_mapBatchCacheStamp = 0;
    quint64 m_mapBatchCacheRevision = 1;
    MapBatchCacheEntry* m_currentMapCache = nullptr;
    SpriteBatcher m_screenEffectBatcher;
    SpriteBatcher m_subtitleBatcher;
    SpriteBatcher m_presentationEffectBatcher;
    /// Versão já enviada de cada textura de imagem de tela: efeitos animados
    /// (onda, brilho deslizante) refazem a composição e precisam reenviar.
    QHash<QString, quint64> m_versaoPic;
    QHash<QString, quint64> m_versaoImagemDinamica;
    // O2: scratch do frame reaproveitado. clear() zera o tamanho, mas preserva
    // a capacidade, eliminando churn de heap depois do warmup.
    QVector<float> m_frameVertices;
    QVector<RenderSegment> m_preToneSegments;
    QVector<RenderSegment> m_depthSegments;
    QVector<RenderSegment> m_screenEffectSegments;
    QVector<RenderSegment> m_subtitleSegments;
    QVector<RenderSegment> m_presentationEffectSegments;
    std::array<QVector<RenderSegment>, 10> m_pictureLayerSegments;
    FrameClock m_clock;
    /// Tela do jogo dentro da janela (escala proporcional + barras pretas).
    QRect m_destino;      ///< cena do quadro atual
    QImage  m_overlay;            ///< interface desenhada com QPainter
    bool    m_overlayNovo = true;
    bool    m_standaloneMenus = false;
    bool    m_debugTools = true;
    GamepadInput m_gamepad;
    QSet<core::GameAction> m_gamepadHeld;
    quint32 m_capacidadeVbuf = 0;
    // O1 / Performance Baseline: contadores do quadro corrente. Não afetam
    // renderização nem persistência; existem apenas para diagnóstico/CI.
    qint64 m_frameTextureUploadBytes = 0;
    qint64 m_frameStaticMapVertexUploadBytes = 0;
    int m_frameGpuMapMeshBuilds = 0;
    int m_frameTextureUploads = 0;
    int m_frameMapCacheHits = 0;
    int m_frameMapCacheMisses = 0;
    /// Texturas criadas mas ainda sem conteúdo enviado (ver garantirTextura).
    QSet<QString> m_uploadPendente;
    QSet<QString> m_missingTextureWarnings;

    // ---- "lixeira" de recursos -------------------------------------------
    // NUNCA destruir um recurso de GPU dentro do quadro: o driver ainda pode
    // estar lendo dele nos quadros em voo. Recurso aposentado espera algumas
    // rodadas aqui antes de ser apagado de verdade.
    struct Aposentado { std::shared_ptr<void> recurso; int quadrosRestantes; };
    QVector<Aposentado> m_lixeira;
    void aposentar(std::shared_ptr<void> r);
    void limparLixeira();
    void invalidarCacheDeMapa();
    void invalidarRecursosDoProjeto();
    void descartarGpuMapMeshes();
    std::shared_ptr<GpuMapMesh> garantirGpuMapMesh(MapBatchCacheEntry& entry,
                                                   QRhiResourceUpdateBatch* updates);

};

} // namespace game
