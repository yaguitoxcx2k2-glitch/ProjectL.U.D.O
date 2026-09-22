#include "game/RuntimeWindowUtils.h"
#include "core/TilesetOps.h"
#include "core/RuntimePreloadCache.h"
#include "RhiGameWindow.h"
#include "CameraChunkWindow.h"
#include "StarActorDepth.h"
#include "game/GameDebugDialog.h"
#include "game/debug/LiveEventInspector.h"
#include "game/RuntimeGpuPolicy.h"
#include "game/RuntimeShaderCache.h"

#include <limits>

#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMatrix4x4>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <rhi/qrhi.h>

using namespace core;

namespace game {

namespace {

bool g_verboseGpuLog = true;

/// Relatório de diagnóstico da GPU, gravado LINHA A LINHA com flush.
/// Se o programa morrer, a última linha diz exatamente onde ele estava — é a
/// única forma de investigar uma máquina que eu não tenho aqui.
void logGpu(const QString& linha)
{
    const bool critical=linha.contains(QLatin1String("FALHA"),Qt::CaseInsensitive)||
                        linha.contains(QLatin1String("EXCEDE"),Qt::CaseInsensitive)||
                        linha.contains(QLatin1String("erro"),Qt::CaseInsensitive);
    if(!g_verboseGpuLog&&!critical)return;
    static QFile arquivo(QDir::temp().filePath(QStringLiteral("tes_gpu.log")));
    static bool aberto = arquivo.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    qInfo("[gpu] %s", qPrintable(linha));
    if (!aberto) return;
    QTextStream ts(&arquivo);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
       << "  " << linha << Qt::endl;
    ts.flush();
    arquivo.flush();
}

/// Textura 1x1 branca: deixa o mesmo shader desenhar quads de cor sólida.
QImage imagemSolida()
{
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.fill(Qt::white);
    return img;
}

QImage imagemAusente()
{
    QImage img(16, 16, QImage::Format_RGBA8888);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixelColor(x, y, ((x / 4) + (y / 4)) % 2
                ? QColor(20, 20, 20) : QColor(255, 0, 180));
    return img;
}

} // namespace

RhiGameWindow::RhiGameWindow(Editor& editorRef, const QPointF& startPixel, QWidget* parent,
                             bool standaloneMenus, bool debugTools, const QString& backendOverride)
    : QRhiWidget(nullptr), m_s(editorRef, startPixel, font()), m_audio(this),
      m_standaloneMenus(standaloneMenus), m_debugTools(debugTools)
{
    // O playtest conserva diagnostico detalhado. No jogo exportado, gravar e
    // dar flush em disco para cada textura/pipeline atrasava o primeiro frame
    // e criava hitches; LUDO_GPU_LOG=1 permite reativar quando necessario.
    g_verboseGpuLog=debugTools||qEnvironmentVariableIsSet("LUDO_GPU_LOG");
    // 3.21.1: QRhiWidget::setApi() precisa acontecer ANTES de o widget entrar
    // na hierarquia de uma janela. Construir QRhiWidget(parent) e só depois
    // escolher Direct3D11 fazia Testar Jogo falhar na GPU, enquanto Testar
    // Mapa (parent=nullptr) funcionava. Configuramos o backend primeiro e
    // somente depois anexamos o renderer ao shell de janela única do Player.
    setWindowTitle(m_debugTools
        ? tr("Jogar (GPU) — %1").arg(editorRef.doc() ? editorRef.doc()->name : QString())
        : (editorRef.projectName.trimmed().isEmpty() ? tr("Ludo Player") : editorRef.projectName));
    m_s.setDebugPresentation(m_debugTools);
    m_s.setCutsceneSkipPromptVisible(m_debugTools);
    setFocusPolicy(Qt::StrongFocus);
    const QSize res = editorRef.gameResolution;
    resize(res);
    m_s.resize(res.width(), res.height());
    // Aqui as imagens de tela viram QUADS, não pixels na textura de interface:
    // é o que faz a mistura (somar/multiplicar/clarear) enxergar o mapa, como
    // acontece no renderizador de CPU.
    m_s.setPicturesInOverlay(false);
    m_s.setGpuOverlayCompositing(true);
    auto invalidateMapCache=[this]{ invalidarCacheDeMapa(); update(); };
    QObject::connect(&editorRef,&Editor::mapChanged,this,invalidateMapCache);
    QObject::connect(&editorRef,&Editor::layersChanged,this,invalidateMapCache);
    QObject::connect(&editorRef,&Editor::tilesetsChanged,this,invalidateMapCache);
    QObject::connect(&editorRef,&Editor::wangChanged,this,invalidateMapCache);
    // Hot Reload troca o Runtime Editor somente depois de uma preflight
    // atômica. O sinal de projeto é a fronteira para descartar também texturas
    // derivadas que mantêm a mesma chave, mas cujo conteúdo pode ter mudado.
    QObject::connect(&editorRef,&Editor::projectChanged,this,[this,&editorRef]{
        invalidarRecursosDoProjeto();
        m_audio.setPreloadCache(editorRef.runtimePreloadCache(), editorRef.projectRoot());
    });

    // A política central fornece D3D11 -> Vulkan no Windows. TES_RHI_API
    // continua sendo uma substituição exclusivamente diagnóstica.
    const QString environmentBackend = QString::fromLatin1(qgetenv("TES_RHI_API"));
    const QString requested = !backendOverride.trimmed().isEmpty() ? backendOverride
        : (!environmentBackend.trimmed().isEmpty() ? environmentBackend
                                                   : editorRef.runtimeGpuBackend);
    m_requestedBackend = runtimeGpuBackendFallbackChain(requested).value(0, QStringLiteral("auto"));
    if (m_requestedBackend == QLatin1String("vulkan")) setApi(QRhiWidget::Api::Vulkan);
    else if (m_requestedBackend == QLatin1String("opengl")) setApi(QRhiWidget::Api::OpenGL);
    else if (m_requestedBackend == QLatin1String("d3d11")) setApi(QRhiWidget::Api::Direct3D11);
    else if (m_requestedBackend == QLatin1String("d3d12")) setApi(QRhiWidget::Api::Direct3D12);
    else if (m_requestedBackend == QLatin1String("metal")) setApi(QRhiWidget::Api::Metal);

    // Só agora entra na hierarquia do LudoPlayer. Sem parent continua sendo
    // uma janela independente (Testar Mapa/F5), como antes.
    if (parent) setParent(parent);
    else setWindowFlag(Qt::Window, true);

    // RC2.63: reutiliza o cache real criado pelo preload. Áudio não ganha um
    // carregador paralelo: RuntimeAudio consome o mesmo RuntimePreloadCache.
    m_audio.setPreloadCache(editorRef.runtimePreloadCache(), editorRef.projectRoot());
    m_s.setAudioHook([this](const QString& f, int v) { m_audio.playEffect(f, v); });
    m_s.setEffectAudioHook([this](const QString& f, int v, int pitch, int pan) {
        m_audio.playEffect(f, v, pitch, pan);
    });
    m_s.setVoiceAudioHook([this](const QString& f,int v){m_audio.playVoice(f,v);},[this]{m_audio.stopVoice();},[this]{return m_audio.voicePlaying();});
    m_s.setChannelAudioHook([this](const QString& channel,const QString& file,int volume,bool loop,int fadeInMs,int transitionMs,int pitch,int pan){m_audio.playChannel(channel,file,volume,loop,fadeInMs,transitionMs,pitch,pan);},[this](const QString& channel,int fadeOutMs){m_audio.stopChannel(channel,fadeOutMs);});
    m_s.refreshMapAudio();

    // 60 fps de verdade (ver FrameClock.h).
    m_clock.start([this] { tick(); });
}

QString RhiGameWindow::caminhoDoLog()
{
    return QDir::temp().filePath(QStringLiteral("tes_gpu.log"));
}

RhiGameWindow::RhiGameWindow(std::unique_ptr<core::RuntimeProject> project, const QPointF& startPixel, QWidget* parent,
                             bool standaloneMenus, bool debugTools, const QString& backendOverride)
    : RhiGameWindow(project->legacyModel(), startPixel, parent, standaloneMenus, debugTools, backendOverride)
{
    // The delegating constructor may safely bind GameSession to the model: the
    // parameter owns it during delegation, then ownership moves into the window.
    m_runtimeProject = std::move(project);
}

RhiGameWindow::~RhiGameWindow()
{
    m_clock.stop();
    delete m_liveInspector;
    m_liveInspector = nullptr;
    // Recursos ligados ao QRhi devem desaparecer antes do dispositivo. Aqui já
    // não existe quadro futuro que precise da lixeira.
    descartarGpuMapMeshes();
    m_pipe.reset();m_pipeAdd.reset();m_pipeMul.reset();m_pipeScreen.reset();m_filterPipe.reset();m_filterPipeAdd.reset();m_filterPipeMul.reset();m_filterPipeScreen.reset();m_postPipe.reset();m_filterPostPipe.reset();
    m_texturas.clear();m_postSrbNearest.reset();m_postSrbLinear.reset();
    m_sceneTarget.reset();m_sceneRpd.reset();m_sceneTexture.reset();
    m_worldTarget.reset();m_worldRpd.reset();m_worldTexture.reset();
    m_postNearest.reset();m_postLinear.reset();m_worldToneSrb.reset();
    m_vbuf.reset();m_ubuf.reset();m_worldUbuf.reset();m_sampler.reset();m_linearSampler.reset();m_lixeira.clear();
    logGpu(QStringLiteral("janela do jogo fechada (quadros desenhados: %1)").arg(m_quadrosOk));
}

void RhiGameWindow::aposentar(std::shared_ptr<void> r)
{
    if (!r) return;
    // Quantos quadros o driver pode ter em voo. +1 de folga.
    const int emVoo = m_rhi ? m_rhi->resourceLimit(QRhi::FramesInFlight) : 3;
    m_lixeira.push_back({ std::move(r), qMax(2, emVoo) + 1 });
}

void RhiGameWindow::limparLixeira()
{
    for (int i = m_lixeira.size() - 1; i >= 0; --i)
        if (--m_lixeira[i].quadrosRestantes <= 0) m_lixeira.remove(i);
}

void RhiGameWindow::descartarGpuMapMeshes()
{
    m_currentMapCache = nullptr;
    for (MapBatchCacheEntry& entry : m_mapBatchCache)
        entry.gpuMesh.reset();
}

void RhiGameWindow::invalidarCacheDeMapa()
{
    m_currentMapCache = nullptr;
    // Em runtime normal, um mesh pode estar referenciado por um frame em voo.
    // Aposentamos o objeto inteiro (buffer + handles de textura) antes de
    // remover a entrada CPU do LRU.
    for (MapBatchCacheEntry& entry : m_mapBatchCache) {
        if (entry.gpuMesh) aposentar(std::static_pointer_cast<void>(entry.gpuMesh));
    }
    m_mapBatchCache.clear();
    m_mapBatchCacheStamp = 0;
    ++m_mapBatchCacheRevision;
    if (m_mapBatchCacheRevision == 0) m_mapBatchCacheRevision = 1;
}

void RhiGameWindow::invalidarRecursosDoProjeto()
{
    invalidarCacheDeMapa();

    // ImageProvider e versões são caches derivados do Runtime Editor. Eles
    // precisam ser reconstruídos mesmo quando a chave textual do asset não
    // mudou; caso contrário o modelo novo poderia continuar exibindo pixels
    // enviados antes do Hot Reload.
    m_imagens.clear();
    m_versaoPic.clear();
    m_versaoImagemDinamica.clear();
    m_uploadPendente.clear();
    m_missingTextureWarnings.clear();

    // `solid` e `missing` são recursos estruturais usados pelos pipelines e
    // não vêm do projeto. Todo o restante pode conter tileset/picture/UI do
    // snapshot anterior. Aposentamos em vez de destruir dentro do frame, pois
    // o driver pode ainda ter comandos em voo referenciando essas texturas.
    for (auto it = m_texturas.begin(); it != m_texturas.end();) {
        if (it.key() == QLatin1String("solid") || it.key() == QLatin1String("missing")) {
            ++it;
            continue;
        }
        aposentar(std::static_pointer_cast<void>(it.value()));
        it = m_texturas.erase(it);
    }

    m_overlayNovo = true;
    update();
}

void RhiGameWindow::resizeEvent(QResizeEvent* e)
{
    // A tela do jogo tem tamanho fixo (resolução do projeto): a janela só
    // decide o retângulo onde ela é ampliada.
    m_overlayNovo = true;
    QRhiWidget::resizeEvent(e);
}

void RhiGameWindow::closeEvent(QCloseEvent* e)
{
    m_clock.stop();
    QRhiWidget::closeEvent(e);
}

void RhiGameWindow::keyPressEvent(QKeyEvent* e)
{
    if (isRuntimeBorderlessToggleShortcut(e->key(), e->modifiers(), e->isAutoRepeat())) {
        QWidget* host = parentWidget() ? window() : this;
        toggleRuntimeBorderlessWindow(host, true);
        e->accept();
        return;
    }
    if(!e->isAutoRepeat())m_s.notifyKeyboardInput();
    if (m_debugTools && !e->isAutoRepeat() && e->key() == Qt::Key_F8) {
        m_clock.stop();
        GameDebugDialog debugger(m_s, this);
        debugger.exec();
        m_clock.start([this] { tick(); });
        setFocus();
        return;
    }
    if (m_debugTools && !e->isAutoRepeat() && e->key() == Qt::Key_F9) {
        if (!m_liveInspector) {
            m_liveInspector = new LiveEventInspector(m_s, nullptr);
            m_liveInspector->setFloating(true);
            m_liveInspector->resize(780, 720);
        }
        m_liveInspector->setVisible(!m_liveInspector->isVisible());
        if (m_liveInspector->isVisible()) { m_liveInspector->raise(); m_liveInspector->activateWindow(); }
        else setFocus();
        e->accept();
        return;
    }
    // F2: despeja no relatório o que está sendo mandado para a GPU neste
    // instante. É o que me permite investigar um problema visual numa máquina
    // que eu não tenho — sem isso, é adivinhação.
    if (e->key() == Qt::Key_F2) {
        logGpu(QStringLiteral("---- diagnóstico da cena (F2) ----"));
        logGpu(QStringLiteral("backend=%1 janela=%2x%3 zoom=%4 camera=(%5,%6)")
                   .arg(m_backend).arg(width()).arg(height()).arg(m_s.zoom())
                   .arg(m_s.cameraTopLeft().x()).arg(m_s.cameraTopLeft().y()));
        int weatherWorldBatches = 0, weatherNonWorldBatches = 0;
        for (const DrawBatch& b : m_weatherBatcher.batches())
            (b.space == RuntimeCoordinateSpace::World ? weatherWorldBatches : weatherNonWorldBatches)++;
        logGpu(QStringLiteral("clima type='%1' intensidade=%2%% quads=%3 space=World batches=%4 nonWorld=%5")
                   .arg(m_s.weatherType()).arg(m_s.weatherIntensity())
                   .arg(m_weatherBatcher.totalQuads()).arg(weatherWorldBatches).arg(weatherNonWorldBatches));
        const core::Editor& ed2 = m_s.editor();
        for (int i = 0; i < ed2.tilesets.size(); ++i) {
            const core::Tileset& t = ed2.tilesets[i];
            logGpu(QStringLiteral("tileset %1 '%2': imagem %3x%4, tile %5x%6, "
                                  "espacamento %7, margem %8, grade %9x%10")
                       .arg(i).arg(t.name).arg(t.image.width()).arg(t.image.height())
                       .arg(t.tilewidth).arg(t.tileheight).arg(t.spacing).arg(t.margin)
                       .arg(t.columns).arg(t.rows));
        }
        int n = 0;
        auto logStageBatches = [&](RuntimeVisualStage stage, const SpriteBatcher& batcher) {
            for (const DrawBatch& b : batcher.batches()) {
                logGpu(QStringLiteral("stage=%1 textura='%2' quads=%3 space=%4")
                           .arg(QString::fromLatin1(runtimeVisualStageName(stage)))
                           .arg(b.texture)
                           .arg(b.quads())
                           .arg(QString::fromLatin1(runtimeCoordinateSpaceName(b.space))));
                for (int q = 0; q + 5 < b.verts.size() && n < 24; q += 6, ++n) {
                    const QuadVertex& tl = b.verts[q];
                    const QuadVertex& br = b.verts[q + 4];
                    logGpu(QStringLiteral("   quad %1: coords(%2,%3)-(%4,%5)  uv(%6,%7)-(%8,%9) a=%10")
                               .arg(n).arg(tl.x).arg(tl.y).arg(br.x).arg(br.y)
                               .arg(tl.u, 0, 'f', 5).arg(tl.v, 0, 'f', 5)
                               .arg(br.u, 0, 'f', 5).arg(br.v, 0, 'f', 5).arg(tl.a));
                }
            }
        };
        logStageBatches(RuntimeVisualStage::Panorama, m_panoramaBatcher);
        logStageBatches(RuntimeVisualStage::MapBelow, m_mapBelowBatcher);
        logStageBatches(RuntimeVisualStage::Actors, m_actorBelowBatcher);
        logStageBatches(RuntimeVisualStage::Actors, m_actorSameBatcher);
        logStageBatches(RuntimeVisualStage::Actors, m_actorAboveBatcher);
        logStageBatches(RuntimeVisualStage::MapAbove, m_mapAboveBatcher);
        logStageBatches(RuntimeVisualStage::Fog, m_fogBatcher);
        logStageBatches(RuntimeVisualStage::Weather, m_weatherBatcher);
        logGpu(QStringLiteral("---- fim do diagnóstico ----"));
        return;
    }
    if(!e->isAutoRepeat()&&m_s.canOpenMenu()&&m_s.editor().inputMap.matches(GameAction::Cancel,e->key())){openGameMenu();return;}
    m_s.keyPress(e->key(), e->isAutoRepeat(), e->text());
    if (m_s.wantsClose()) close();
}

void RhiGameWindow::openGameMenu()
{
    m_s.openGameMenu(m_standaloneMenus);
    update();
    setFocus();
}

void RhiGameWindow::mousePressEvent(QMouseEvent* e)
{
    if(e->button()==Qt::RightButton&&m_s.canOpenMenu()){openGameMenu();return;}
    if(!m_destino.contains(e->position().toPoint())||m_destino.width()<=0||m_destino.height()<=0)return;
    const QPointF logical = logicalPointFromWindow(e->position(), m_destino, m_s.viewSize());
    m_s.mousePress(logical,e->button());update();
}

void RhiGameWindow::mouseMoveEvent(QMouseEvent* e)
{
    if(!e->buttons().testFlag(Qt::LeftButton)||!m_destino.contains(e->position().toPoint()))return;const QPointF logical=logicalPointFromWindow(e->position(),m_destino,m_s.viewSize());m_s.mouseMove(logical);update();
}

void RhiGameWindow::mouseReleaseEvent(QMouseEvent* e){m_s.mouseRelease(e->button());}

void RhiGameWindow::wheelEvent(QWheelEvent* e)
{
    if(!m_destino.contains(e->position().toPoint())||m_destino.width()<=0||m_destino.height()<=0){QRhiWidget::wheelEvent(e);return;}
    const int steps=e->angleDelta().y()/120;if(steps==0){QRhiWidget::wheelEvent(e);return;}
    const QPointF logical=logicalPointFromWindow(e->position(),m_destino,m_s.viewSize());
    m_s.mouseWheel(logical,steps);update();e->accept();
}

void RhiGameWindow::keyReleaseEvent(QKeyEvent* e)
{
    m_s.keyRelease(e->key(), e->isAutoRepeat());
}

void RhiGameWindow::tick()
{
    if(pollGamepad())return;
    m_s.setFrameCost(m_clock.lastFrameMs());
    const FramePacingSnapshot pacing = m_clock.pacingSnapshot();
    m_s.setFramePacingStats(pacing.lateFrames, pacing.totalFrames, pacing.lateRatio,
                            pacing.lastOverrunMs, pacing.worstOverrunMs);
    QElapsedTimer updateTimer;
    updateTimer.start();
    m_s.tick();
    m_s.setProfileStageTiming(QStringLiteral("gameUpdate"), updateTimer.nsecsElapsed() / 1e6);
    if (m_s.takeFullscreenToggleRequest()) {
        QWidget* host = parentWidget() ? window() : this;
        const bool requested = QSettings().value(QStringLiteral("game/fullscreen"), false).toBool();
        setRuntimeBorderlessWindow(host, requested, false);
    }
    if (m_s.takeAudioSettingsChangedRequest()) m_audio.refreshSettings();
    if (m_s.wantsClose()) { close(); return; }
    const QString title = m_debugTools
        ? tr("Jogar (GPU) — %1").arg(m_s.editor().doc() ? m_s.editor().doc()->name : QString())
        : (m_s.editor().projectName.trimmed().isEmpty() ? tr("Ludo Player") : m_s.editor().projectName);
    QWidget* host = parentWidget() ? window() : this;
    if (host->windowTitle() != title) host->setWindowTitle(title);
    update();
}

bool RhiGameWindow::pollGamepad()
{
    if (pollRuntimeGamepad(m_gamepad, m_gamepadHeld, m_s) == GamepadPollResult::OpenMenu) {
        openGameMenu();
        return true;
    }
    return false;
}

bool RhiGameWindow::ensureOffscreen()
{
    const QSize wanted=m_s.viewSize();
    if(!m_rhi||!wanted.isValid())return false;
    if(m_sceneTexture&&m_worldTexture&&m_postSrbNearest&&m_postSrbLinear&&m_worldToneSrb&&m_sceneSize==wanted)return true;

    m_postSrbNearest.reset();m_postSrbLinear.reset();m_worldToneSrb.reset();
    m_sceneTarget.reset();m_sceneRpd.reset();m_sceneTexture.reset();
    m_worldTarget.reset();m_worldRpd.reset();m_worldTexture.reset();
    m_sceneSize=wanted;

    auto makeTarget=[this,&wanted](std::unique_ptr<QRhiTexture>&texture,std::unique_ptr<QRhiTextureRenderTarget>&target,std::unique_ptr<QRhiRenderPassDescriptor>&rpd){
        texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8,wanted,1,QRhiTexture::RenderTarget));if(!texture->create())return false;
        QRhiTextureRenderTargetDescription desc(QRhiColorAttachment(texture.get()));target.reset(m_rhi->newTextureRenderTarget(desc));rpd.reset(target->newCompatibleRenderPassDescriptor());target->setRenderPassDescriptor(rpd.get());return target->create();
    };
    if(!makeTarget(m_sceneTexture,m_sceneTarget,m_sceneRpd)){logGpu(QStringLiteral("FALHA no render target composto %1x%2").arg(wanted.width()).arg(wanted.height()));return false;}
    if(!makeTarget(m_worldTexture,m_worldTarget,m_worldRpd)){logGpu(QStringLiteral("FALHA no render target WorldScene %1x%2").arg(wanted.width()).arg(wanted.height()));return false;}
    if(!m_postNearest){m_postNearest.reset(m_rhi->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));m_postNearest->create();m_postLinear.reset(m_rhi->newSampler(QRhiSampler::Linear,QRhiSampler::Linear,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));m_postLinear->create();}
    auto makeSrb=[this](QRhiTexture*texture,QRhiSampler*sampler){auto r=std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());r->setBindings({QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,m_ubuf.get()),QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::FragmentStage,texture,sampler)});return r->create()?std::move(r):nullptr;};
    m_postSrbNearest=makeSrb(m_sceneTexture.get(),m_postNearest.get());m_postSrbLinear=makeSrb(m_sceneTexture.get(),m_postLinear.get());
    m_worldToneSrb=makeSrb(m_worldTexture.get(),m_sampler.get());
    logGpu(QStringLiteral("pipeline visual GPU: Panorama/Map/Actors/Fog/Weather -> ScreenTone -> Pictures/UI -> Presentation %1x%2").arg(wanted.width()).arg(wanted.height()));
    return m_postSrbNearest&&m_postSrbLinear&&m_worldToneSrb;
}

void RhiGameWindow::initialize(QRhiCommandBuffer*)
{
    const bool newDevice = m_rhi != rhi();
    if (newDevice) {
        // Device loss/backend switch: os buffers de chunk pertencem ao QRhi
        // anterior e precisam morrer antes de os novos recursos serem criados.
        descartarGpuMapMeshes();
        m_pipe.reset();m_pipeAdd.reset();m_pipeMul.reset();m_pipeScreen.reset();m_filterPipe.reset();m_filterPipeAdd.reset();m_filterPipeMul.reset();m_filterPipeScreen.reset();
        m_worldPipe.reset();m_worldPipeAdd.reset();m_worldPipeMul.reset();m_worldPipeScreen.reset();m_worldFilterPipe.reset();m_worldFilterPipeAdd.reset();m_worldFilterPipeMul.reset();m_worldFilterPipeScreen.reset();
        m_postPipe.reset();m_filterPostPipe.reset();m_texturas.clear();m_vbuf.reset();m_ubuf.reset();m_worldUbuf.reset();m_sampler.reset();m_linearSampler.reset();
        m_sceneTarget.reset();m_sceneRpd.reset();m_sceneTexture.reset();
        m_worldTarget.reset();m_worldRpd.reset();m_worldTexture.reset();
        m_postNearest.reset();m_postLinear.reset();m_postSrbNearest.reset();m_postSrbLinear.reset();m_worldToneSrb.reset();
        m_lixeira.clear();m_uploadPendente.clear();m_capacidadeVbuf=0;
        m_rhi=rhi();m_backend=QString::fromLatin1(m_rhi->backendName());
        logGpu(QStringLiteral("backend=%1 textura máxima=%2").arg(m_backend).arg(m_rhi->resourceLimit(QRhi::TextureSizeMax)));
    }
    const auto preloadCache = m_s.editor().runtimePreloadCache();
    const bool wantFilterPipelines = !preloadCache ||
        preloadCache->requestsFeature(core::RuntimePreloadFeatures::filterSystem()) ||
        m_s.hasActiveFilters();
    QRhiRenderPassDescriptor* swapRpd=renderTarget()->renderPassDescriptor();const bool sceneChanged=m_sceneSize!=m_s.viewSize();
    const bool filterPipelinesReady = !wantFilterPipelines ||
        (m_filterPipe && m_worldFilterPipe && m_filterPostPipe);
    if(m_pipe&&m_worldPipe&&m_postPipe&&filterPipelinesReady&&swapRpd==m_rpdUsado&&!sceneChanged)return;
    m_pipe.reset();m_pipeAdd.reset();m_pipeMul.reset();m_pipeScreen.reset();m_filterPipe.reset();m_filterPipeAdd.reset();m_filterPipeMul.reset();m_filterPipeScreen.reset();m_worldPipe.reset();m_worldPipeAdd.reset();m_worldPipeMul.reset();m_worldPipeScreen.reset();m_worldFilterPipe.reset();m_worldFilterPipeAdd.reset();m_worldFilterPipeMul.reset();m_worldFilterPipeScreen.reset();m_postPipe.reset();m_filterPostPipe.reset();m_rpdUsado=swapRpd;
    if(!m_ubuf){m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,352));if(!m_ubuf->create())return;}
    if(!m_worldUbuf){m_worldUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,352));if(!m_worldUbuf->create())return;}
    if(!m_sampler){m_sampler.reset(m_rhi->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));m_sampler->create();}
    // Pictures com smooth=true usam filtragem bilinear (Linear min/mag). Pixel art continua em Nearest.
    if(!m_linearSampler){m_linearSampler.reset(m_rhi->newSampler(QRhiSampler::Linear,QRhiSampler::Linear,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));m_linearSampler->create();}
    if(!ensureOffscreen())return;
    garantirTextura(QStringLiteral("solid"),nullptr);garantirTextura(QStringLiteral("missing"),nullptr);
    if(!m_texturas.contains(QStringLiteral("solid"))||!m_texturas.contains(QStringLiteral("missing")))return;
    auto layout=[](){QRhiVertexInputLayout l;l.setBindings({{8*sizeof(float)}});l.setAttributes({{0,0,QRhiVertexInputAttribute::Float2,0},{0,1,QRhiVertexInputAttribute::Float2,2*sizeof(float)},{0,2,QRhiVertexInputAttribute::Float4,4*sizeof(float)}});return l;};
    auto makeScene=[this,&layout](std::unique_ptr<QRhiGraphicsPipeline>& pipe,
                                  QRhiRenderPassDescriptor* rpd,
                                  QRhiGraphicsPipeline::BlendFactor src,
                                  QRhiGraphicsPipeline::BlendFactor dst,
                                  const QString& name,
                                  bool filtered) {
        pipe.reset(m_rhi->newGraphicsPipeline());
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true; b.srcColor = src; b.dstColor = dst;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipe->setTargetBlends({b});
        pipe->setShaderStages({
            {QRhiShaderStage::Vertex, runtimeShader(QStringLiteral(":/shaders/sprite.vert.qsb"))},
            {QRhiShaderStage::Fragment, runtimeShader(filtered
                ? QStringLiteral(":/shaders/filter.frag.qsb")
                : QStringLiteral(":/shaders/sprite.frag.qsb"))}
        });
        pipe->setVertexInputLayout(layout());
        pipe->setShaderResourceBindings(m_texturas[QStringLiteral("solid")]->srb.get());
        pipe->setRenderPassDescriptor(rpd);
        if (!pipe->create()) {
            logGpu(QStringLiteral("FALHA pipeline cena %1").arg(name));
            pipe.reset();
        }
    };
    auto buildFamily = [&](QRhiRenderPassDescriptor* rpd,
                           std::unique_ptr<QRhiGraphicsPipeline>& normal,
                           std::unique_ptr<QRhiGraphicsPipeline>& add,
                           std::unique_ptr<QRhiGraphicsPipeline>& mul,
                           std::unique_ptr<QRhiGraphicsPipeline>& screen,
                           const QString& prefix, bool filtered) {
        makeScene(normal,rpd,QRhiGraphicsPipeline::SrcAlpha,QRhiGraphicsPipeline::OneMinusSrcAlpha,prefix+QStringLiteral("/normal"),filtered);
        makeScene(add,rpd,QRhiGraphicsPipeline::One,QRhiGraphicsPipeline::One,prefix+QStringLiteral("/somar"),filtered);
        makeScene(mul,rpd,QRhiGraphicsPipeline::DstColor,QRhiGraphicsPipeline::OneMinusSrcAlpha,prefix+QStringLiteral("/multiplicar"),filtered);
        makeScene(screen,rpd,QRhiGraphicsPipeline::One,QRhiGraphicsPipeline::OneMinusSrcColor,prefix+QStringLiteral("/clarear"),filtered);
    };
    buildFamily(m_sceneRpd.get(),m_pipe,m_pipeAdd,m_pipeMul,m_pipeScreen,QStringLiteral("composite"),false);
    if (wantFilterPipelines)
        buildFamily(m_sceneRpd.get(),m_filterPipe,m_filterPipeAdd,m_filterPipeMul,m_filterPipeScreen,QStringLiteral("composite/filter"),true);
    buildFamily(m_worldRpd.get(),m_worldPipe,m_worldPipeAdd,m_worldPipeMul,m_worldPipeScreen,QStringLiteral("world"),false);
    if (wantFilterPipelines)
        buildFamily(m_worldRpd.get(),m_worldFilterPipe,m_worldFilterPipeAdd,m_worldFilterPipeMul,m_worldFilterPipeScreen,QStringLiteral("world/filter"),true);
    m_postPipe.reset(m_rhi->newGraphicsPipeline());m_postPipe->setShaderStages({{QRhiShaderStage::Vertex,runtimeShader(QStringLiteral(":/shaders/sprite.vert.qsb"))},{QRhiShaderStage::Fragment,runtimeShader(QStringLiteral(":/shaders/sprite.frag.qsb"))}});m_postPipe->setVertexInputLayout(layout());m_postPipe->setShaderResourceBindings(m_postSrbLinear.get());m_postPipe->setRenderPassDescriptor(swapRpd);if(!m_postPipe->create()){logGpu(QStringLiteral("FALHA pipeline de ampliação final"));m_postPipe.reset();return;}
    if (wantFilterPipelines) {
        m_filterPostPipe.reset(m_rhi->newGraphicsPipeline());m_filterPostPipe->setShaderStages({{QRhiShaderStage::Vertex,runtimeShader(QStringLiteral(":/shaders/sprite.vert.qsb"))},{QRhiShaderStage::Fragment,runtimeShader(QStringLiteral(":/shaders/filter.frag.qsb"))}});m_filterPostPipe->setVertexInputLayout(layout());m_filterPostPipe->setShaderResourceBindings(m_postSrbLinear.get());m_filterPostPipe->setRenderPassDescriptor(swapRpd);if(!m_filterPostPipe->create()){logGpu(QStringLiteral("FALHA pipeline Ludo Filter System"));m_filterPostPipe.reset();return;}
        if (preloadCache) preloadCache->markFeaturePrepared(core::RuntimePreloadFeatures::filterGpuPipelines());
    }
    for(int i=0;i<m_s.editor().tilesets.size();++i){const core::Tileset&ts=m_s.editor().tilesets[i];if(ts.image.isNull())continue;m_imagens.insert(TextureKey::tileset(i),ts.image);garantirTextura(TextureKey::tileset(i),nullptr);}
    logGpu(wantFilterPipelines
        ? QStringLiteral("pipelines GPU criados: base + Ludo Filter System (preload planejado)")
        : QStringLiteral("pipelines GPU criados: base; Filter System omitido pelo RuntimePreloader"));
}

std::shared_ptr<RhiGameWindow::Tex> RhiGameWindow::garantirTextura(const QString& chave, QRhiResourceUpdateBatch* u)
{
    auto existente = m_texturas.constFind(chave);
    if (existente != m_texturas.constEnd()) return existente.value();
    QImage img = chave == QLatin1String("solid") ? imagemSolida()
               : chave == QLatin1String("missing") ? imagemAusente()
                                                    : m_imagens.value(chave);
    if (img.isNull()) {
        logGpu(QStringLiteral("textura '%1' ignorada: imagem vazia").arg(chave));
        return {};
    }
    // Nenhuma GPU aceita textura de qualquer tamanho. Passar do limite é erro
    // de criação — e, dependendo do driver, morte do processo.
    const int limite = m_rhi ? m_rhi->resourceLimit(QRhi::TextureSizeMax) : 4096;
    if (img.width() > limite || img.height() > limite) {
        logGpu(QStringLiteral("textura '%1' %2x%3 EXCEDE o limite %4: reduzindo")
                   .arg(chave).arg(img.width()).arg(img.height()).arg(limite));
        img = img.scaled(qMin(img.width(), limite), qMin(img.height(), limite),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (img.format() != QImage::Format_RGBA8888)
        img = img.convertToFormat(QImage::Format_RGBA8888);

    auto t = std::make_shared<Tex>();
    t->tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, img.size()));
    if (!t->tex->create()) {
        logGpu(QStringLiteral("FALHA ao criar textura '%1' %2x%3")
                   .arg(chave).arg(img.width()).arg(img.height()));
        return {};
    }
    auto makeTextureSrb = [this, &t](QRhiBuffer* uniform, QRhiSampler* sampler) {
        auto srb = std::unique_ptr<QRhiShaderResourceBindings>(m_rhi->newShaderResourceBindings());
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                                                     uniform),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      t->tex.get(), sampler) });
        return srb->create() ? std::move(srb) : nullptr;
    };
    t->srb = makeTextureSrb(m_ubuf.get(), m_sampler.get());
    t->srbLinear = makeTextureSrb(m_ubuf.get(), m_linearSampler.get());
    t->worldSrb = makeTextureSrb(m_worldUbuf.get(), m_sampler.get());
    t->worldSrbLinear = makeTextureSrb(m_worldUbuf.get(), m_linearSampler.get());
    if (!t->srb || !t->srbLinear || !t->worldSrb || !t->worldSrbLinear) {
        logGpu(QStringLiteral("FALHA ao criar bindings screen/world da textura '%1'").arg(chave));
        return {};
    }
    logGpu(QStringLiteral("textura '%1' criada %2x%3").arg(chave).arg(img.width()).arg(img.height()));
    if (u) {
        u->uploadTexture(t->tex.get(), img);
        ++m_frameTextureUploads;
        m_frameTextureUploadBytes += qint64(img.sizeInBytes());
    } else  m_uploadPendente.insert(chave);   // sobe no próximo render(), com batch válido
    m_texturas.insert(chave, t);
    return t;
}

std::shared_ptr<RhiGameWindow::GpuMapMesh>
RhiGameWindow::garantirGpuMapMesh(MapBatchCacheEntry& entry, QRhiResourceUpdateBatch* updates)
{
    if (entry.gpuMesh) return entry.gpuMesh;
    if (!m_rhi || !updates) return {};

    GpuMapMeshData packed;
    QString error;
    if (!buildGpuMapMeshData(entry.below, entry.stars, entry.above, &packed, &error)) {
        qWarning("RhiGameWindow: GPU map mesh rejeitado: %s", qPrintable(error));
        return {};
    }

    auto mesh = std::make_shared<GpuMapMesh>();
    mesh->vertexBytes = packed.vertexBytes();
    mesh->quads = packed.quads();

    auto resolve = [this, updates](const QVector<GpuMapSegmentData>& source,
                                  QVector<GpuMapSegment>& dest) -> bool {
        dest.reserve(source.size());
        for (const GpuMapSegmentData& item : source) {
            GpuMapSegment segment;
            segment.textureKey = item.texture;
            segment.texture = garantirTextura(item.texture, updates);
            if (!segment.texture) {
                segment.texture = garantirTextura(QStringLiteral("missing"), updates);
                if (!segment.texture) return false;
                if (!m_missingTextureWarnings.contains(item.texture)) {
                    m_missingTextureWarnings.insert(item.texture);
                    logGpu(QStringLiteral("FALHA textura de mapa '%1': usando placeholder GPU").arg(item.texture));
                }
            }
            segment.firstVertex = item.firstVertex;
            segment.vertexCount = item.vertexCount;
            segment.blend = item.blend;
            segment.depth = item.depth;
            dest.push_back(std::move(segment));
        }
        return true;
    };
    if (!resolve(packed.below, mesh->below) ||
        !resolve(packed.stars, mesh->stars) ||
        !resolve(packed.above, mesh->above)) {
        qWarning("RhiGameWindow: falha ao resolver textura do GPU map mesh; usando fallback dinamico");
        return {};
    }
    std::stable_sort(mesh->stars.begin(), mesh->stars.end(),
                     [](const GpuMapSegment& a, const GpuMapSegment& b) {
                         return a.depth < b.depth;
                     });

    if (mesh->vertexBytes > 0) {
        mesh->vertexBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Static,
                                                  QRhiBuffer::VertexBuffer,
                                                  mesh->vertexBytes));
        if (!mesh->vertexBuffer->create()) {
            qWarning("RhiGameWindow: falha ao criar GPU chunk mesh de %u bytes", mesh->vertexBytes);
            return {};
        }
        updates->uploadStaticBuffer(mesh->vertexBuffer.get(), 0, mesh->vertexBytes,
                                    packed.vertices.constData());
        m_frameStaticMapVertexUploadBytes += qint64(mesh->vertexBytes);
    }
    ++m_frameGpuMapMeshBuilds;
    entry.gpuMesh = mesh;
    logGpu(QStringLiteral("GPU chunk mesh residente: %1 quads / %2 bytes / %3 draws")
               .arg(mesh->quads).arg(mesh->vertexBytes)
               .arg(mesh->below.size() + mesh->stars.size() + mesh->above.size()));
    return mesh;
}

void RhiGameWindow::montarCena(const RuntimeVisualFrame& frame)
{
    const RuntimeRenderState& renderState = frame.renderState;
    m_panoramaBatcher.clear();
    m_mapBelowBatcher.clear();
    m_mapStarBatcher.clear();
    m_actorBelowBatcher.clear();
    m_actorSameBatcher.clear();
    m_actorAboveBatcher.clear();
    m_currentMapCache = nullptr;
    m_mapAboveBatcher.clear();
    m_fogBatcher.clear();
    const qint64 animationMs = m_s.visualElapsedMs();
    const double shakePadding = qMax(qAbs(renderState.screenOffset.x()),
                                     qAbs(renderState.screenOffset.y())) /
                                normalizedRuntimeZoom(renderState.zoom);
    const QRectF visible = renderState.cameraWorldRect(64.0 + shakePadding);
    const MapInfo& mi = m_s.editor().mapInfo();
    const int cw=qMax(1,mi.tileWidth*16), ch=qMax(1,mi.tileHeight*16);
    // Janela com guarda/histerese: a mesh não é reconstruída a cada borda de
    // chunk. O stride 2 reduz pela metade as trocas em movimento contínuo e a
    // guarda absorve shake e pequenas correções do follow sem cache miss.
    const QRect chunks = cameraChunkWindow(visible, QSize(cw, ch), 2, 1);
    const QRectF cachedVisible(chunks.x()*cw, chunks.y()*ch,
                               chunks.width()*cw, chunks.height()*ch);

    // O2: o cache não monta mais uma QString gigante por frame. Mudanças de
    // mapa/layers/tileset/Wang já invalidam o cache por signal; portanto a
    // chave quente precisa apenas da região, da geração e do frame efetivo dos
    // Autotiles. Isso remove flatLayers(), QString::arg() e concatenações do
    // caminho comum sem alterar a semântica de invalidação.
    const MapBatchCacheKey cacheKey{
        chunks.x(), chunks.y(), chunks.width(), chunks.height(),
        animatedTilesetFrameSignature(m_s.editor(), animationMs),
        m_mapBatchCacheRevision,
        m_s.state().runtimeMapRevision()
    };

    MapBatchCacheEntry* mapCache=nullptr;
    for(auto& entry:m_mapBatchCache) {
        if(entry.key==cacheKey) { mapCache=&entry; break; }
    }
    if(!mapCache) {
        ++m_frameMapCacheMisses;
        // 12 entradas cobrem três regiões de câmera × quatro frames de animação
        // sem deixar o cache crescer indefinidamente. Evicção LRU simples.
        if(m_mapBatchCache.size()>=12) {
            int oldest=0;
            for(int i=1;i<m_mapBatchCache.size();++i)
                if(m_mapBatchCache[i].stamp<m_mapBatchCache[oldest].stamp) oldest=i;
            if (m_mapBatchCache[oldest].gpuMesh)
                aposentar(std::static_pointer_cast<void>(m_mapBatchCache[oldest].gpuMesh));
            m_mapBatchCache.removeAt(oldest);
        }
        MapBatchCacheEntry entry;
        entry.key=cacheKey;
        const auto runtimeCell=[this](const core::LayerPtr& layer,int x,int y){return m_s.state().runtimeMapCell(m_s.editor(),m_s.currentMapId(),layer->id,x,y);};
        ScenePass below;below.visible=cachedVisible;below.animationTimeMs=animationMs;below.starTiles=false;below.aboveLayers=false;below.cellResolver=runtimeCell;
        buildSceneBatches(m_s.editor(),below,entry.below,m_imagens);
        ScenePass stars;stars.visible=cachedVisible;stars.animationTimeMs=animationMs;stars.starTiles=true;stars.aboveLayers=false;stars.depthSortedTiles=true;stars.cellResolver=runtimeCell;
        buildSceneBatches(m_s.editor(),stars,entry.stars,m_imagens);
        ScenePass above;above.visible=cachedVisible;above.animationTimeMs=animationMs;above.starTiles=false;above.aboveLayers=true;above.cellResolver=runtimeCell;
        buildSceneBatches(m_s.editor(),above,entry.above,m_imagens);
        m_mapBatchCache.push_back(std::move(entry));
        mapCache=&m_mapBatchCache.last();
        logGpu(QStringLiteral("cache de chunks %1,%2 %3x%4: %5 quads")
                   .arg(chunks.x()).arg(chunks.y()).arg(chunks.width()).arg(chunks.height())
                   .arg(mapCache->below.totalQuads()+mapCache->stars.totalQuads()+mapCache->above.totalQuads()));
    } else {
        ++m_frameMapCacheHits;
    }
    mapCache->stamp=++m_mapBatchCacheStamp;
    m_currentMapCache = mapCache;

    // Ordem semântica oficial do Bloco 1. Cada etapa recebe a MESMA fotografia
    // do RuntimeRenderState e cada batch continua em seu espaço declarado.
    m_s.appendPanoramaQuads(m_panoramaBatcher, m_imagens, renderState);
    // O7: mapa Below/★/Above não é mais copiado para o buffer dinâmico do
    // frame. Os batchers CPU permanecem na entrada de cache e viram um
    // GpuMapMesh estático na primeira utilização.
    m_s.appendActorQuads(m_actorBelowBatcher, m_imagens, -1);
    m_s.appendActorQuads(m_actorSameBatcher, m_imagens, 0);
    m_s.appendActorQuads(m_actorAboveBatcher, m_imagens, 1);
    m_s.appendFogQuads(m_fogBatcher, m_imagens, renderState);

    m_weatherBatcher.clear();
    m_s.appendRuntimeWeatherQuads(m_weatherBatcher, renderState);

    for (int i = 0; i < int(m_pictureLayerBatchers.size()); ++i) {
        m_pictureLayerBatchers[size_t(i)].clear();
        m_s.appendPictureQuads(m_pictureLayerBatchers[size_t(i)], m_imagens, core::PictureLayer(i), renderState);
    }
    m_screenEffectBatcher.clear();m_s.appendScreenEffectQuads(m_screenEffectBatcher);
    m_subtitleBatcher.clear();m_s.appendSubtitleQuads(m_subtitleBatcher,m_imagens);
    m_presentationEffectBatcher.clear();m_s.appendPresentationEffectQuads(m_presentationEffectBatcher);
    collectUnusedDynamicTextures();
}

void RhiGameWindow::collectUnusedDynamicTextures()
{
    QSet<QString> used;
    const auto collect = [&used](const SpriteBatcher& batcher) {
        for (const DrawBatch& batch : batcher.batches()) used.insert(batch.texture);
    };
    for (const SpriteBatcher& batcher : m_pictureLayerBatchers) collect(batcher);
    collect(m_subtitleBatcher);
    QStringList stale;
    for (auto it = m_imagens.cbegin(); it != m_imagens.cend(); ++it)
        if ((it.key().startsWith(QLatin1String("img:pic:")) ||
             it.key().startsWith(QLatin1String("img:subtitle:"))) && !used.contains(it.key()))
            stale.push_back(it.key());
    for (const QString& key : stale) {
        m_imagens.remove(key); m_versaoPic.remove(key); m_versaoImagemDinamica.remove(key);
        m_missingTextureWarnings.remove(key); m_uploadPendente.remove(key);
        if (m_texturas.contains(key)) aposentar(m_texturas.take(key));
    }
}

QRhiGraphicsPipeline* RhiGameWindow::pipelineDe(core::PictureBlend b) const
{
    switch (b) {
    case core::PictureBlend::Add:      return m_pipeAdd ? m_pipeAdd.get() : m_pipe.get();
    case core::PictureBlend::Multiply: return m_pipeMul ? m_pipeMul.get() : m_pipe.get();
    case core::PictureBlend::Screen:   return m_pipeScreen ? m_pipeScreen.get() : m_pipe.get();
    case core::PictureBlend::Normal:   break;
    }
    return m_pipe.get();
}

QRhiGraphicsPipeline* RhiGameWindow::worldPipelineDe(core::PictureBlend b) const
{
    switch (b) {
    case core::PictureBlend::Add:      return m_worldPipeAdd ? m_worldPipeAdd.get() : m_worldPipe.get();
    case core::PictureBlend::Multiply: return m_worldPipeMul ? m_worldPipeMul.get() : m_worldPipe.get();
    case core::PictureBlend::Screen:   return m_worldPipeScreen ? m_worldPipeScreen.get() : m_worldPipe.get();
    case core::PictureBlend::Normal:   break;
    }
    return m_worldPipe.get();
}

QRhiGraphicsPipeline* RhiGameWindow::filterPipelineDe(core::PictureBlend b) const
{
    // RuntimePreloader pode omitir toda a família filtrada quando o snapshot
    // não usa filtros. Mesmo se uma chamada inesperada chegar aqui, nunca
    // devolvemos nullptr: a família base é o fallback seguro.
    if (!m_filterPipe) return pipelineDe(b);
    switch (b) {
    case core::PictureBlend::Add:      return m_filterPipeAdd ? m_filterPipeAdd.get() : m_filterPipe.get();
    case core::PictureBlend::Multiply: return m_filterPipeMul ? m_filterPipeMul.get() : m_filterPipe.get();
    case core::PictureBlend::Screen:   return m_filterPipeScreen ? m_filterPipeScreen.get() : m_filterPipe.get();
    case core::PictureBlend::Normal:   break;
    }
    return m_filterPipe.get();
}

QRhiGraphicsPipeline* RhiGameWindow::worldFilterPipelineDe(core::PictureBlend b) const
{
    if (!m_worldFilterPipe) return worldPipelineDe(b);
    switch (b) {
    case core::PictureBlend::Add:      return m_worldFilterPipeAdd ? m_worldFilterPipeAdd.get() : m_worldFilterPipe.get();
    case core::PictureBlend::Multiply: return m_worldFilterPipeMul ? m_worldFilterPipeMul.get() : m_worldFilterPipe.get();
    case core::PictureBlend::Screen:   return m_worldFilterPipeScreen ? m_worldFilterPipeScreen.get() : m_worldFilterPipe.get();
    case core::PictureBlend::Normal:   break;
    }
    return m_worldFilterPipe.get();
}

void RhiGameWindow::render(QRhiCommandBuffer* cb)
{
    // A família filtrada é opcional por contrato do RuntimePreloader. Somente
    // os pipelines base/targets são requisitos universais para o primeiro frame.
    if (!m_pipe || !m_worldPipe || !m_postPipe ||
        !m_sceneTarget || !m_worldTarget || !m_worldToneSrb) return;
    QElapsedTimer totalRenderTimer;
    totalRenderTimer.start();
    m_frameTextureUploadBytes = 0;
    m_frameStaticMapVertexUploadBytes = 0;
    m_frameGpuMapMeshBuilds = 0;
    m_frameTextureUploads = 0;
    m_frameMapCacheHits = 0;
    m_frameMapCacheMisses = 0;
    int pipelineChanges = 0;
    int shaderResourceChanges = 0;
    qint64 uploadPrepNs = 0;
    qint64 mapMeshBuildNs = 0;
    limparLixeira();
    // A resolução lógica é fixada ao abrir o teste. Alterações no editor valem
    // no próximo F5, evitando destruir render targets ainda usados pela GPU.
    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();
    // Texturas criadas antes de existir um batch (no initialize) sobem agora.
    QElapsedTimer uploadTimer;
    uploadTimer.start();
    for (const QString& chave : std::as_const(m_uploadPendente)) {
        auto it = m_texturas.constFind(chave);
        if (it == m_texturas.constEnd()) continue;
        QImage img = chave == QLatin1String("solid") ? imagemSolida()
                   : chave == QLatin1String("missing") ? imagemAusente()
                                                        : m_imagens.value(chave);
        if (img.isNull()) continue;
        if (img.format() != QImage::Format_RGBA8888)
            img = img.convertToFormat(QImage::Format_RGBA8888);
        u->uploadTexture((*it)->tex.get(), img);
        ++m_frameTextureUploads;
        m_frameTextureUploadBytes += qint64(img.sizeInBytes());
    }
    m_uploadPendente.clear();
    uploadPrepNs += uploadTimer.nsecsElapsed();

    // Uma única fotografia do frame: montar geometria, projetar, desenhar
    // overlay e atualizar uniforms usam exatamente o mesmo estado lógico.
    QElapsedTimer stageTimer;
    stageTimer.start();
    const RuntimeVisualFrame visualFrame = m_s.visualFrame();
    const RuntimeRenderState& frameState = visualFrame.renderState;
    m_s.setProfileStageTiming(QStringLiteral("renderState"), stageTimer.nsecsElapsed() / 1e6);
    stageTimer.restart();
    montarCena(visualFrame);
    for(auto it=m_texturas.begin();it!=m_texturas.end();){
        if(it.key().startsWith(QLatin1String("img:subtitle:"))&&!m_imagens.contains(it.key())){
            m_versaoImagemDinamica.remove(it.key());aposentar(std::static_pointer_cast<void>(it.value()));it=m_texturas.erase(it);
        }else ++it;
    }
    m_s.setProfileStageTiming(QStringLiteral("batchBuild"), stageTimer.nsecsElapsed() / 1e6);

    // O7: a primeira utilização desta região/frame de Autotile transforma os
    // batchers CPU do mapa em um QRhiBuffer::Static. Nas utilizações seguintes
    // este bloco é apenas um pointer lookup: câmera não reempacota mapa.
    std::shared_ptr<GpuMapMesh> mapMesh;
    if (m_currentMapCache) {
        QElapsedTimer mapMeshTimer;
        mapMeshTimer.start();
        mapMesh = garantirGpuMapMesh(*m_currentMapCache, u);
        mapMeshBuildNs = mapMeshTimer.nsecsElapsed();
        uploadPrepNs += mapMeshBuildNs;
        if (!mapMesh) {
            // Fallback seguro: uma falha de recurso GPU não pode fazer o mapa
            // desaparecer. Reusa exatamente o caminho dinâmico pré-O7.
            m_mapBelowBatcher.append(m_currentMapCache->below);
            m_mapStarBatcher.append(m_currentMapCache->stars);
            m_mapAboveBatcher.append(m_currentMapCache->above);
        }
    }

    stageTimer.restart();
    // ---- vértices ---------------------------------------------------------
    // Cada DrawBatch DECLARA World/Camera/Screen/Ui e RuntimeRenderState aplica
    // a projeção uma única vez. A matriz uniforme final só converte pixels
    // lógicos -> clip.
    QVector<float>& dados = m_frameVertices;
    QVector<RenderSegment>& preTone = m_preToneSegments;
    QVector<RenderSegment>& depthSegments = m_depthSegments;
    auto& pictureLayers = m_pictureLayerSegments;
    QVector<RenderSegment>& screenEffects = m_screenEffectSegments;
    QVector<RenderSegment>& subtitles = m_subtitleSegments;
    QVector<RenderSegment>& presentationEffects = m_presentationEffectSegments;
    dados.clear();
    preTone.clear();
    depthSegments.clear();
    for (auto& segments : pictureLayers) segments.clear();
    screenEffects.clear(); subtitles.clear(); presentationEffects.clear();

    auto totalPictureQuads = [&]() {
        int n = 0; for (const SpriteBatcher& b : m_pictureLayerBatchers) n += b.totalQuads(); return n;
    };
    auto totalPictureBatches = [&]() {
        qsizetype n = 0; for (const SpriteBatcher& b : m_pictureLayerBatchers) n += b.batches().size(); return n;
    };
    const int actorQuads = m_actorBelowBatcher.totalQuads() + m_actorSameBatcher.totalQuads()
                         + m_actorAboveBatcher.totalQuads();
    const qsizetype actorBatches = m_actorBelowBatcher.batches().size() + m_actorSameBatcher.batches().size()
                                 + m_actorAboveBatcher.batches().size();
    const int pictureQuads = totalPictureQuads();
    const qsizetype pictureBatches = totalPictureBatches();

    const int sceneQuads = m_panoramaBatcher.totalQuads() + m_mapBelowBatcher.totalQuads()
                         + m_mapStarBatcher.totalQuads() + actorQuads
                         + m_mapAboveBatcher.totalQuads()
                         + m_fogBatcher.totalQuads() + m_weatherBatcher.totalQuads()
                         + pictureQuads
                         + m_screenEffectBatcher.totalQuads()+m_subtitleBatcher.totalQuads()+m_presentationEffectBatcher.totalQuads();
    const qsizetype wantedFloats = (qsizetype(sceneQuads) * 6 + 18) * 8;
    if (dados.capacity() < wantedFloats)
        dados.reserve(qMax(wantedFloats, qMax<qsizetype>(4096, dados.capacity() * 2)));
    const qsizetype wantedSegments = m_panoramaBatcher.batches().size() + m_mapBelowBatcher.batches().size()
                                  + m_mapStarBatcher.batches().size() + actorBatches
                                  + m_mapAboveBatcher.batches().size()
                                  + m_fogBatcher.batches().size() + m_weatherBatcher.batches().size()
                                  + pictureBatches
                                  + m_screenEffectBatcher.batches().size()+m_subtitleBatcher.batches().size()+m_presentationEffectBatcher.batches().size();
    if (preTone.capacity() < wantedSegments) preTone.reserve(wantedSegments);
    if (depthSegments.capacity() < m_mapStarBatcher.batches().size() + m_actorSameBatcher.batches().size())
        depthSegments.reserve(m_mapStarBatcher.batches().size() + m_actorSameBatcher.batches().size());
    for (int i = 0; i < int(pictureLayers.size()); ++i) {
        const qsizetype wanted = m_pictureLayerBatchers[size_t(i)].batches().size();
        if (pictureLayers[size_t(i)].capacity() < wanted) pictureLayers[size_t(i)].reserve(wanted);
    }

    // ÚNICO ponto do QRhi que transforma coordenadas. Além da projeção, esta
    // função valida stage x space; lote na camada errada é rejeitado em vez de
    // ser silenciosamente transformado/desenhado duas vezes.
    auto appendProjected = [&](const SpriteBatcher& batcher, QVector<RenderSegment>& destino,
                               RuntimeVisualStage stage, bool encodePerBatchTone,
                               int filterDomain) {
        for (const DrawBatch& b : batcher.batches()) {
            if (!runtimeVisualStageAllowsSpace(stage, b.space)) {
                qWarning("RhiGameWindow: stage %s rejeitou batch em %s Space",
                         runtimeVisualStageName(stage), runtimeCoordinateSpaceName(b.space));
                continue;
            }
            RenderSegment t;
            t.textureKey = b.texture;
            t.inicio = quint32(dados.size() / 8);
            t.contagem = quint32(b.verts.size());
            t.blend = b.blend;
            t.screenTone = b.affectedByScreenTone;
            t.smooth = b.smooth;
            t.space = b.space;
            t.depth = b.depth;
            t.filterDomain = filterDomain;
            for (const QuadVertex& v : b.verts) {
                const QPointF projected = frameState.projectPoint(QPointF(v.x, v.y), b.space);
                const bool applyTone = encodePerBatchTone && t.screenTone;
                const float encoded = qAbs(v.a) + float(filterDomain * 2);
                const float a = applyTone ? -encoded : encoded;
                dados << float(projected.x()) << float(projected.y())
                      << v.u << v.v << v.r << v.g << v.b << a;
            }
            destino.push_back(t);
        }
    };

    // RC2.66: cada etapa do mundo recebe tone por batch. Isso permite inserir
    // PictureLayer 0..6 entre etapas físicas sem um ScreenTone global posterior
    // destruir a opção PictureDef::affectedByTone.
    appendProjected(m_panoramaBatcher, preTone, RuntimeVisualStage::Panorama, true, 1);
    const int panoramaEnd = preTone.size();
    appendProjected(m_mapBelowBatcher, preTone, RuntimeVisualStage::MapBelow, true, 1); // fallback O7
    const int mapBelowEnd = preTone.size();
    appendProjected(m_actorBelowBatcher, preTone, RuntimeVisualStage::Actors, true, 1);
    const int actorBelowEnd = preTone.size();

    appendProjected(m_actorSameBatcher, depthSegments, RuntimeVisualStage::Actors, true, 1);
    appendProjected(m_mapStarBatcher, depthSegments, RuntimeVisualStage::MapAbove, true, 1); // fallback O7
    std::stable_sort(depthSegments.begin(), depthSegments.end(),
                     [](const RenderSegment& a, const RenderSegment& b) { return a.depth < b.depth; });

    appendProjected(m_mapAboveBatcher, preTone, RuntimeVisualStage::MapAbove, true, 1); // fallback O7
    const int mapAboveEnd = preTone.size();
    appendProjected(m_actorAboveBatcher, preTone, RuntimeVisualStage::Actors, true, 1);
    const int actorAboveEnd = preTone.size();
    appendProjected(m_fogBatcher, preTone, RuntimeVisualStage::Fog, true, 1);
    const int fogEnd = preTone.size();
    appendProjected(m_weatherBatcher, preTone, RuntimeVisualStage::Weather, true, 1);
    const int weatherEnd = preTone.size();

    for (int i = 0; i < int(m_pictureLayerBatchers.size()); ++i) {
        appendProjected(m_pictureLayerBatchers[size_t(i)], pictureLayers[size_t(i)],
                        i <= int(core::PictureLayer::AboveAnimations)
                            ? RuntimeVisualStage::PicturesBelowUi
                            : RuntimeVisualStage::PicturesAboveUi,
                        true, 2);
    }
    appendProjected(m_screenEffectBatcher,screenEffects,RuntimeVisualStage::ScreenEffects,false,1);
    appendProjected(m_subtitleBatcher,subtitles,RuntimeVisualStage::Ui,false,3);
    appendProjected(m_presentationEffectBatcher,presentationEffects,RuntimeVisualStage::Presentation,false,3);

    const int staticMapDrawCalls = mapMesh ? (mapMesh->below.size() + mapMesh->stars.size() + mapMesh->above.size()) : 0;
    const int staticMapQuads = mapMesh ? mapMesh->quads : 0;
    int pictureDrawCalls = 0; for (const auto& v : pictureLayers) pictureDrawCalls += v.size();
    const int gpuDrawCalls = preTone.size() + depthSegments.size() + staticMapDrawCalls + pictureDrawCalls
                           + screenEffects.size() + subtitles.size()
                           + presentationEffects.size() + 3;
    const int gpuQuads = m_panoramaBatcher.totalQuads() + m_mapBelowBatcher.totalQuads()
                       + m_mapStarBatcher.totalQuads() + actorQuads
                       + m_mapAboveBatcher.totalQuads() + staticMapQuads
                       + m_fogBatcher.totalQuads() + m_weatherBatcher.totalQuads()
                       + pictureQuads + m_screenEffectBatcher.totalQuads()
                       + m_subtitleBatcher.totalQuads()
                       + m_presentationEffectBatcher.totalQuads() + 3;
    m_s.setRenderStats(QStringLiteral("QRhi/%1").arg(m_backend), gpuDrawCalls, gpuQuads);
    m_s.setProfileStageTiming(QStringLiteral("vertexPacking"), stageTimer.nsecsElapsed() / 1e6);
    m_s.setProfileStageTiming(QStringLiteral("gpuMapMeshBuild"), mapMeshBuildNs / 1e6);

    // ---- interface: uma textura do tamanho da janela ----------------------
    const QSize tela = renderTarget()->pixelSize();
    // Tudo é desenhado na RESOLUÇÃO DO JOGO; o viewport amplia por um fator
    // inteiro e centraliza (o resto da janela fica preto).
    const QSize resJogo = frameState.viewport;
    m_destino = GameSession::letterboxRect(resJogo, tela);
    qint64 overlayNs = 0;
    static const QString overlayKey = QStringLiteral("overlay");
    std::shared_ptr<Tex> overlayTexture = m_texturas.value(overlayKey);
    if (m_overlay.size() != resJogo || m_s.overlayDirty() || m_overlayNovo) {
        QElapsedTimer overlayTimer;
        overlayTimer.start();
        if (m_overlay.size() != resJogo)
            m_overlay = QImage(resJogo, QImage::Format_RGBA8888_Premultiplied);
        m_overlay.fill(Qt::transparent);
        QPainter p(&m_overlay);
        m_s.drawOverlay(p, frameState);
        p.end();
        m_s.clearOverlayDirty();
        m_overlayNovo = false;
        m_imagens.insert(overlayKey, m_overlay);
        if (!overlayTexture || overlayTexture->tex->pixelSize() != m_overlay.size()) {
            // Mesma regra do buffer: a textura velha não pode ser destruída
            // agora — vai para a lixeira e uma nova entra no lugar.
            if (overlayTexture) aposentar(m_texturas.take(overlayKey));
            overlayTexture = garantirTextura(overlayKey, u);
        } else {
            QImage uploadImage = m_overlay;
            if (uploadImage.format() != QImage::Format_RGBA8888)
                uploadImage = uploadImage.convertToFormat(QImage::Format_RGBA8888);
            QElapsedTimer localUpload; localUpload.start();
            u->uploadTexture(overlayTexture->tex.get(), uploadImage);
            uploadPrepNs += localUpload.nsecsElapsed();
            ++m_frameTextureUploads;
            m_frameTextureUploadBytes += qint64(uploadImage.sizeInBytes());
        }
        overlayNs += overlayTimer.nsecsElapsed();
    }
    m_s.setProfileStageTiming(QStringLiteral("overlay"), overlayNs / 1e6);

    // Quad que copia a WorldScene já composta. A tonalidade foi aplicada por
    // batch no PASSO 1. O alpha 3 codifica domínio World + opacidade 1; o
    // pipeline normal apenas decodifica a opacidade, enquanto o filter.frag
    // usa o domínio para aplicar escopos sem voltar a amostrar cada tile.
    const quint32 inicioWorldCopy = quint32(dados.size() / 8);
    {
        const float x1=float(resJogo.width()), y1=float(resJogo.height());
        auto vtx=[&](float x,float y,float u2,float v2){dados<<x<<y<<u2<<v2<<1<<1<<1<<3;};
        vtx(0,0,0,0);vtx(x1,0,1,0);vtx(0,y1,0,1);vtx(x1,0,1,0);vtx(x1,y1,1,1);vtx(0,y1,0,1);
    }

    // quad da interface, em pixels de TELA (por isso vai com outra matriz)
    const quint32 inicioOverlay = quint32(dados.size() / 8);
    {
        const float x0 = 0, y0 = 0, x1 = float(resJogo.width()), y1 = float(resJogo.height());
        const float c[4] = { 1, 1, 1, 1 };
        auto vtx = [&](float x, float y, float u2, float v2) {
            dados << x << y << u2 << v2 << c[0] << c[1] << c[2] << (6.0f + c[3]);
        };
        vtx(x0, y0, 0, 0); vtx(x1, y0, 1, 0); vtx(x0, y1, 0, 1);
        vtx(x1, y0, 1, 0); vtx(x1, y1, 1, 1); vtx(x0, y1, 0, 1);
    }
    // Quad final: lê a textura offscreen e a amplia para a janela.
    const quint32 inicioPost=quint32(dados.size()/8);
    {const float x1=float(resJogo.width()),y1=float(resJogo.height());auto vtx=[&](float x,float y,float u2,float v2){dados<<x<<y<<u2<<v2<<1<<1<<1<<1;};vtx(0,0,0,0);vtx(x1,0,1,0);vtx(0,y1,0,1);vtx(x1,0,1,0);vtx(x1,y1,1,1);vtx(0,y1,0,1);}
    const quint32 bytes = quint32(dados.size() * sizeof(float));
    if (!m_vbuf || m_capacidadeVbuf < bytes) {
        // ESTE ERA O BUG QUE FECHAVA O JOGO COM O MAPA PINTADO:
        // o buffer antigo era destruído aqui, no meio do quadro, enquanto o
        // driver ainda podia estar lendo dele. Com o mapa vazio nunca dava,
        // porque os 64 KB iniciais bastavam e o buffer nunca era trocado.
        // Agora o antigo vai para a lixeira e só é apagado depois que os
        // quadros em voo terminam. E crescemos com folga (2×), para não ficar
        // trocando de buffer a cada passo do jogador.
        if (m_vbuf) {
            std::shared_ptr<QRhiBuffer> velho(m_vbuf.release());
            aposentar(velho);
        }
        logGpu(QStringLiteral("buffer de vertices: precisa de %1 bytes (capacidade %2) - crescendo")
                   .arg(bytes).arg(m_capacidadeVbuf));
        m_capacidadeVbuf = qMax(bytes * 2, quint32(256 * 1024));
        m_vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                      m_capacidadeVbuf));
        if (!m_vbuf->create()) {
            qWarning("RhiGameWindow: nao consegui criar o buffer de %u bytes", m_capacidadeVbuf);
            u->release();      // o pool de batches é limitado: devolver sempre
            m_vbuf.reset();
            m_capacidadeVbuf = 0;
            return;
        }
    }
    if (bytes) {
        QElapsedTimer localUpload; localUpload.start();
        u->updateDynamicBuffer(m_vbuf.get(), 0, bytes, dados.constData());
        uploadPrepNs += localUpload.nsecsElapsed();
    }

    // As texturas são resolvidas uma única vez por segmento. O submit usa o
    // handle compartilhado diretamente e não volta ao QHash<QString,...> para
    // cada draw call.

    // Imagens de tela com efeito animado: a composição muda a cada quadro, e
    // a textura precisa acompanhar. Reenviamos SÓ as que mudaram de versão —
    // reenviar todas seria jogar megabytes por segundo no barramento à toa.
    for (auto it = m_imagens.constBegin(); it != m_imagens.constEnd(); ++it) {
        const QString& chave = it.key();
        if (!chave.startsWith(QLatin1String("img:pic:"))) continue;
        const int numero = chave.section(QLatin1Char(':'), 2, 2).toInt();
        const quint64 v = m_s.pictureVersion(numero);
        if (m_versaoPic.value(chave, std::numeric_limits<quint64>::max()) == v) continue;
        m_versaoPic[chave] = v;
        auto tex = m_texturas.constFind(chave);
        if (tex == m_texturas.constEnd()) continue;
        if ((*tex)->tex->pixelSize() != it.value().size()) {
            // Mudou de tamanho (a onda alarga a imagem): textura nova, e a
            // velha vai para a lixeira — destruir agora seria puxar o tapete
            // de um quadro que ainda pode estar em voo.
            aposentar(m_texturas.take(chave));
            garantirTextura(chave, u);
        } else {
            QImage uploadImage = it.value();
            if (uploadImage.format() != QImage::Format_RGBA8888)
                uploadImage = uploadImage.convertToFormat(QImage::Format_RGBA8888);
            QElapsedTimer localUpload; localUpload.start();
            u->uploadTexture((*tex)->tex.get(), uploadImage);
            uploadPrepNs += localUpload.nsecsElapsed();
            ++m_frameTextureUploads;
            m_frameTextureUploadBytes += qint64(uploadImage.sizeInBytes());
        }
    }

    // Legendas sao texturas pequenas e estaveis durante fade/slide/zoom. A
    // geometria e o alfa mudam na GPU; so typewriter/efeito de letra altera
    // os pixels e dispara este upload localizado.
    for(auto it=m_imagens.constBegin();it!=m_imagens.constEnd();++it){
        const QString&key=it.key();if(!key.startsWith(QLatin1String("img:subtitle:")))continue;
        const quint64 version=quint64(it.value().cacheKey());
        if(m_versaoImagemDinamica.value(key,std::numeric_limits<quint64>::max())==version)continue;
        m_versaoImagemDinamica[key]=version;auto tex=m_texturas.constFind(key);if(tex==m_texturas.constEnd())continue;
        QImage uploadImage=it.value();if(uploadImage.format()!=QImage::Format_RGBA8888)uploadImage=uploadImage.convertToFormat(QImage::Format_RGBA8888);
        u->uploadTexture((*tex)->tex.get(),uploadImage);++m_frameTextureUploads;m_frameTextureUploadBytes+=qint64(uploadImage.sizeInBytes());
    }

    auto resolveSegments = [&](QVector<RenderSegment>& segments) {
        for (RenderSegment& segment : segments) {
            segment.texture = garantirTextura(segment.textureKey, u);
            if (!segment.texture) {
                segment.texture = garantirTextura(QStringLiteral("missing"), u);
                if (!m_missingTextureWarnings.contains(segment.textureKey)) {
                    m_missingTextureWarnings.insert(segment.textureKey);
                    logGpu(QStringLiteral("FALHA textura '%1': usando placeholder GPU").arg(segment.textureKey));
                }
            }
        }
    };
    resolveSegments(preTone);
    // Atores/eventos participam da ordenação dinâmica com tiles ★. Este lote
    // também precisa resolver seus handles antes do submit; sem isso o QRhi
    // descartava todos os segmentos por textura nula, embora o CPU funcionasse.
    resolveSegments(depthSegments);
    for (auto& segments : pictureLayers) resolveSegments(segments);
    resolveSegments(screenEffects);resolveSegments(subtitles);resolveSegments(presentationEffects);
    if (!overlayTexture) overlayTexture = garantirTextura(overlayKey, u);

    // ---- uniforms ----------------------------------------------------------
    // Screen matrix: geometria dinâmica já projetada em pixels lógicos.
    QMatrix4x4 logicalToClip;
    logicalToClip.ortho(0, float(resJogo.width()), float(resJogo.height()), 0, -1.0f, 1.0f);
    const QMatrix4x4 telaM = m_rhi->clipSpaceCorrMatrix() * logicalToClip;

    // O7: mapa estático permanece em World Space. Somente esta matriz pequena
    // muda quando a câmera/zoom/shake mudam; o vertex buffer do chunk fica
    // intocado na GPU.
    const QMatrix4x4 worldToScreen(frameState.worldToScreenTransform());
    const QMatrix4x4 worldM = m_rhi->clipSpaceCorrMatrix() * logicalToClip * worldToScreen;

    const float tone[4] = {
        frameState.tone.red, frameState.tone.green,
        frameState.tone.blue, frameState.tone.gray
    };
    {
        QElapsedTimer localUpload; localUpload.start();
        u->updateDynamicBuffer(m_ubuf.get(), 0, 64, telaM.constData());
        u->updateDynamicBuffer(m_ubuf.get(), 64, 16, tone);
        u->updateDynamicBuffer(m_worldUbuf.get(), 0, 64, worldM.constData());
        u->updateDynamicBuffer(m_worldUbuf.get(), 64, 16, tone);
        // Blur 2.0 + Tilt-Shift 2.0: um único bloco de 17 vec4 atende todo o Filter Stack v3.
        // Aberração + Noise + Scanlines + Vignette + Blur + Tilt-Shift continuam
        // sem framebuffer/passe adicional; só os 272 bytes de parâmetros mudam.
        if (m_s.hasActiveFilters() || m_s.filterSystem().isTransitioning()) {
            const LudoFilterUniforms f = m_s.filterUniforms();
            const float params[68] = {
                f.chromaticIntensityPx, f.chromaticEdgeStart, f.chromaticFalloff, f.chromaticMix,
                f.chromaticMode, f.vignetteScopeMask, f.blurScopeMask, f.activeMask,
                f.noiseIntensity, f.noiseGrainSizePx, f.noiseSpeed, f.timeSeconds,
                f.scanlineIntensity, f.scanlineSpacingPx, f.scanlineWhiteSweep, f.scanlineTimeSeconds,
                f.sweepSpeed, f.sweepWidth, f.sweepIntensity, f.sweepDelaySeconds,
                f.vignetteIntensity, f.vignetteRadius, f.vignetteSoftness, f.noisePackedModeSeed,
                f.blurRadiusPx, f.blurDirection, f.noiseContrast, f.noiseColorAmount,
                f.tiltBlurPx, f.tiltCenterY, f.tiltFocusWidth, f.tiltFalloff,
                f.chromaticScopeMask, f.noiseScopeMask, f.scanlineScopeMask, f.tiltScopeMask,
                f.viewportWidth, f.viewportHeight, f.invViewportWidth, f.invViewportHeight,
                f.scanlineStyle, f.scanlineThickness, f.scanlineSoftness, f.scanlineScrollSpeed,
                f.scanlinePhasePx, f.scanlineInterlaceAmount, f.scanlineInterlaceSpeed, f.scanlineSweepSoftness,
                f.scanlineSweepRed, f.scanlineSweepGreen, f.scanlineSweepBlue, f.scanlineReserved,
                f.blurStyle, f.blurStrength, f.blurQuality, f.blurEdgePreservation,
                f.blurAngleDegrees, f.blurReserved1, f.blurReserved2, f.blurReserved3,
                f.tiltStyle, f.tiltStrength, f.tiltQuality, f.tiltEdgePreservation,
                f.tiltAngleDegrees, f.tiltUpperBlur, f.tiltLowerBlur, f.tiltReserved
            };
            u->updateDynamicBuffer(m_ubuf.get(), 80, 272, params);
            u->updateDynamicBuffer(m_worldUbuf.get(), 80, 272, params);
        }
        uploadPrepNs += localUpload.nsecsElapsed();
    }
    m_s.setProfileStageTiming(QStringLiteral("gpuUploadPrep"), uploadPrepNs / 1e6);

    QElapsedTimer submitTimer;
    submitTimer.start();
    const bool filtersActive = m_s.hasActiveFilters();
    const bool filterPipelinesAvailable = m_filterPipe && m_worldFilterPipe && m_filterPostPipe;
    // Se o preload decidiu corretamente que filtros não eram necessários, o
    // render segue 100% pelo pipeline base. A proteção também impede nullptr
    // caso uma feature dinâmica não prevista tente ativar filtro neste frame.
    const bool filtersRenderable = filtersActive && filterPipelinesAvailable;
    const bool fullFrameFilter = filtersRenderable && m_s.filterSystem().fullFrameEligible();
    // Escopo parcial é resolvido no draw que pertence ao domínio. Não criamos
    // máscara, framebuffer ou pass adicional: os mesmos dois targets e o mesmo
    // número de passes continuam valendo.
    const bool scopedFilters = filtersRenderable && !fullFrameFilter;
    auto domainAffected = [&](int domain) {
        if (!scopedFilters) return false;
        switch (domain) {
        case 1: return m_s.filterSystem().affects(core::FilterRenderDomain::World);
        case 2: return m_s.filterSystem().affects(core::FilterRenderDomain::Pictures);
        case 3: return m_s.filterSystem().affects(core::FilterRenderDomain::Hud);
        default: return false;
        }
    };
    // Aberração Cromática, Blur e Tilt-Shift deslocam amostras vizinhas.
    // Filtrá-los em cada tile/sprite do World faz o deslocamento atravessar
    // a célula UV do atlas e cria costuras periódicas (ex.: linhas a cada
    // 32 px). Quando um desses efeitos está ativo no World, a WorldScene é
    // desenhada crua no PASSO 1 e filtrada uma única vez no quad composto do
    // PASSO 2. Não há framebuffer nem pass novo: reutilizamos worldTexture,
    // sceneTarget e o mesmo shader fundido.
    const bool composedWorldFilter = scopedFilters &&
        m_s.filterSystem().requiresComposedSampling(core::FilterRenderDomain::World);
    auto worldDrawAffected = [&](int domain) {
        if (domain == 1 && composedWorldFilter) return false;
        return domainAffected(domain);
    };

    // ------------------------------------------------------------------
    // PASSO 1 — WorldScene com ScreenTone POR BATCH e PictureLayer 0..6
    // realmente intercaladas entre as etapas físicas do mundo.
    // ------------------------------------------------------------------
    cb->beginPass(m_worldTarget.get(), QColor::fromRgbF(0.0f, 0.0f, 0.0f), { 1.0f, 0 }, u);
    cb->setGraphicsPipeline(m_worldPipe.get());
    ++pipelineChanges;
    cb->setViewport({0,0,float(m_sceneSize.width()),float(m_sceneSize.height())});

    QRhiGraphicsPipeline* worldPipeline = m_worldPipe.get();
    QRhiShaderResourceBindings* worldResources = nullptr;
    auto prepararWorldPipeline = [&](core::PictureBlend blend, bool filtered) {
        QRhiGraphicsPipeline* wanted = filtered ? worldFilterPipelineDe(blend) : worldPipelineDe(blend);
        if (wanted == worldPipeline) return;
        cb->setGraphicsPipeline(wanted);
        ++pipelineChanges;
        cb->setViewport({0,0,float(m_sceneSize.width()),float(m_sceneSize.height())});
        worldPipeline = wanted;
        worldResources = nullptr;
    };
    auto bindWorldResources = [&](QRhiShaderResourceBindings* resources) {
        if (!resources || resources == worldResources) return;
        cb->setShaderResources(resources);
        ++shaderResourceChanges;
        worldResources = resources;
    };
    auto desenharWorld = [&](const RenderSegment& segment) {
        if (!segment.texture || segment.contagem == 0) return;
        prepararWorldPipeline(segment.blend, worldDrawAffected(segment.filterDomain));
        bindWorldResources(segment.smooth ? segment.texture->srbLinear.get() : segment.texture->srb.get());
        const QRhiCommandBuffer::VertexInput vb(m_vbuf.get(), segment.inicio * 8 * sizeof(float));
        cb->setVertexInput(0, 1, &vb);
        cb->draw(segment.contagem);
    };
    auto desenharMapa = [&](const GpuMapSegment& segment) {
        if (!mapMesh || !mapMesh->vertexBuffer || !segment.texture || segment.vertexCount == 0) return;
        prepararWorldPipeline(segment.blend, worldDrawAffected(1));
        bindWorldResources(segment.texture->worldSrb.get());
        const QRhiCommandBuffer::VertexInput vb(mapMesh->vertexBuffer.get(),
                                                 segment.firstVertex * 8 * sizeof(float));
        cb->setVertexInput(0, 1, &vb);
        cb->draw(segment.vertexCount);
    };
    auto desenharDinamicos = [&](int begin, int end) {
        begin = qBound(0, begin, preTone.size());
        end = qBound(begin, end, preTone.size());
        for (int i = begin; i < end; ++i) desenharWorld(preTone[i]);
    };
    auto desenharPictureWorld = [&](int layer) {
        if (layer < 0 || layer >= int(pictureLayers.size())) return;
        for (const RenderSegment& segment : pictureLayers[size_t(layer)]) desenharWorld(segment);
    };

    desenharDinamicos(0, panoramaEnd);
    desenharPictureWorld(0);
    if (mapMesh) for (const GpuMapSegment& t : mapMesh->below) desenharMapa(t);
    else desenharDinamicos(panoramaEnd, mapBelowEnd);
    desenharPictureWorld(1);
    desenharDinamicos(mapBelowEnd, actorBelowEnd);
    desenharPictureWorld(2);
    if (mapMesh) {
        int starIndex = 0;
        for (const RenderSegment& actor : depthSegments) {
            while (starIndex < mapMesh->stars.size() &&
                   starDrawsBeforeActor(mapMesh->stars[starIndex].depth, actor.depth))
                desenharMapa(mapMesh->stars[starIndex++]);
            desenharWorld(actor);
        }
        while (starIndex < mapMesh->stars.size()) desenharMapa(mapMesh->stars[starIndex++]);
    } else {
        for (const RenderSegment& segment : depthSegments) desenharWorld(segment);
    }
    desenharPictureWorld(3);
    if (mapMesh) for (const GpuMapSegment& t : mapMesh->above) desenharMapa(t);
    else desenharDinamicos(actorBelowEnd, mapAboveEnd);
    desenharPictureWorld(4);
    desenharDinamicos(mapAboveEnd, actorAboveEnd);
    desenharPictureWorld(5);
    desenharDinamicos(actorAboveEnd, fogEnd);
    desenharDinamicos(fogEnd, weatherEnd);
    desenharPictureWorld(6);
    cb->endPass();

    // ------------------------------------------------------------------
    // PASSO 2 — copia WorldScene e compõe Screen/UI/Pictures superiores.
    // Em escopo parcial, cada domínio escolhe a variante normal ou filtrada.
    // ------------------------------------------------------------------
    cb->beginPass(m_sceneTarget.get(), QColor::fromRgbF(0.0f, 0.0f, 0.0f), { 1.0f, 0 });
    cb->setGraphicsPipeline(composedWorldFilter ? m_filterPipe.get() : m_pipe.get());
    ++pipelineChanges;
    cb->setViewport({0,0,float(m_sceneSize.width()),float(m_sceneSize.height())});
    cb->setShaderResources(m_worldToneSrb.get());
    ++shaderResourceChanges;
    {
        const QRhiCommandBuffer::VertexInput vb(m_vbuf.get(), inicioWorldCopy * 8 * sizeof(float));
        cb->setVertexInput(0, 1, &vb);
        cb->draw(6);
    }

    QRhiGraphicsPipeline* scenePipeline = composedWorldFilter ? m_filterPipe.get() : m_pipe.get();
    QRhiShaderResourceBindings* sceneResources = m_worldToneSrb.get();
    auto desenhar = [&](const std::shared_ptr<Tex>& texture, quint32 inicio, quint32 contagem,
                        core::PictureBlend mistura, bool smooth, int domain) {
        if (!texture || contagem == 0) return;
        const bool filtered = domainAffected(domain);
        QRhiGraphicsPipeline* wanted = filtered ? filterPipelineDe(mistura) : pipelineDe(mistura);
        if (wanted != scenePipeline) {
            cb->setGraphicsPipeline(wanted);
            ++pipelineChanges;
            cb->setViewport({0,0,float(m_sceneSize.width()),float(m_sceneSize.height())});
            scenePipeline = wanted;
            sceneResources = nullptr;
        }
        QRhiShaderResourceBindings* resources = smooth ? texture->srbLinear.get() : texture->srb.get();
        if (resources != sceneResources) {
            cb->setShaderResources(resources);
            ++shaderResourceChanges;
            sceneResources = resources;
        }
        const QRhiCommandBuffer::VertexInput vb(m_vbuf.get(), inicio * 8 * sizeof(float));
        cb->setVertexInput(0, 1, &vb);
        cb->draw(contagem);
    };
    auto desenharSegment = [&](const RenderSegment& t) {
        desenhar(t.texture, t.inicio, t.contagem, t.blend, t.smooth, t.filterDomain);
    };
    auto desenharPictureScreen = [&](int layer) {
        if (layer < 0 || layer >= int(pictureLayers.size())) return;
        for (const RenderSegment& t : pictureLayers[size_t(layer)]) desenharSegment(t);
    };

    for (const RenderSegment& t : screenEffects) desenharSegment(t);
    for (const RenderSegment& t : subtitles) desenharSegment(t);
    desenharPictureScreen(7);
    desenhar(overlayTexture, inicioOverlay, 6, core::PictureBlend::Normal, false, 3);
    desenharPictureScreen(8);
    for (const RenderSegment& t : presentationEffects) desenharSegment(t);
    desenharPictureScreen(9);
    cb->endPass();

    const QRhiCommandBuffer::VertexInput postVb(m_vbuf.get(),inicioPost*8*sizeof(float));
    cb->beginPass(renderTarget(),QColor::fromRgbF(0,0,0),{1,0});
    cb->setGraphicsPipeline(fullFrameFilter ? m_filterPostPipe.get() : m_postPipe.get());
    ++pipelineChanges;
    cb->setViewport({float(m_destino.x()),float(m_destino.y()),float(m_destino.width()),float(m_destino.height())});
    cb->setShaderResources(m_s.smoothScaleFilter()?m_postSrbLinear.get():m_postSrbNearest.get());
    ++shaderResourceChanges;
    cb->setVertexInput(0,1,&postVb);
    cb->draw(6);
    cb->endPass();
    m_s.setProfileStageTiming(QStringLiteral("renderSubmit"), submitTimer.nsecsElapsed() / 1e6);
    m_s.setProfileStageTiming(QStringLiteral("qrhiRenderCpu"), totalRenderTimer.nsecsElapsed() / 1e6);
    const qint64 dynamicVertexBytes = qint64(bytes);
    const qint64 totalVertexBytes = dynamicVertexBytes + m_frameStaticMapVertexUploadBytes;
    m_s.setRenderWorkStats(m_frameTextureUploadBytes, totalVertexBytes, m_frameTextureUploads,
                           m_frameMapCacheHits, m_frameMapCacheMisses,
                           pipelineChanges, shaderResourceChanges,
                           m_frameStaticMapVertexUploadBytes, dynamicVertexBytes,
                           m_frameGpuMapMeshBuilds);
    if (++m_quadrosOk == 1)
        logGpu(QStringLiteral("primeiro quadro desenhado com sucesso (%1 quads, %2 lotes, clima=%3 quads, GPU-map=%4 bytes)")
                   .arg(m_panoramaBatcher.totalQuads() + m_mapBelowBatcher.totalQuads() +
                        m_mapStarBatcher.totalQuads() + actorQuads +
                        m_mapAboveBatcher.totalQuads() + staticMapQuads +
                        m_fogBatcher.totalQuads() + m_weatherBatcher.totalQuads() + pictureQuads)
                   .arg(preTone.size() + depthSegments.size() + staticMapDrawCalls + pictureDrawCalls)
                   .arg(m_weatherBatcher.totalQuads())
                   .arg(m_frameStaticMapVertexUploadBytes));
}

} // namespace game
