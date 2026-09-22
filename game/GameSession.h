// ============================================================================
//  GameSession.h — TODA a lógica e o desenho do jogo, sem ser uma janela.
//
//  Existe por um motivo concreto: agora há DOIS renderizadores — o de CPU
//  GPU (`RhiGameWindow`, Direct3D/Vulkan/Metal
//  via QRhi). Se cada um tivesse a sua cópia do laço do jogo, do interpretador
//  e do desenho das legendas, eles iriam divergir em uma semana.
//
//  A classe continua sendo a fonte única do estado do jogo, mas a implementação
//  foi dividida por responsabilidade: GameSession.cpp (ciclo/mundo/desenho),
//  GameSessionCommands.cpp (comandos de runtime) e GameSessionEffects.cpp
//  (câmera, Screen Tone, efeitos Ludo e panoramas). A janela só decide COMO a
//  cena vai para a tela; CPU e GPU consomem a mesma sessão.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/DialogueHistory.h"
#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/Fog.h"
#include "game/Interpreter.h"
#include "game/PictureFx.h"
#include "game/ScreenTone.h"
#include "game/LudoFilterSystem.h"
#include "game/RuntimeRenderState.h"
#include "game/RuntimeScreenEffects.h"
#include "game/RuntimePersistence.h"
#include "game/RuntimeWeather.h"
#include "game/RuntimeDiagnostics.h"
#include "game/RuntimeClock.h"
#include "game/RuntimeRandom.h"
#include "game/EventScheduler.h"
#include "game/RuntimeReplay.h"
#include "game/RuntimeTrace.h"
#include "game/ui/GameUiLayer.h"
#include "game/ui/UiModalController.h"
#include "game/ui/UiBattleController.h"
#include "game/ui/UiShopController.h"
#include "game/Pictures.h"
#include "game/SpriteBatch.h"
#include "game/Subtitle.h"
#include "game/SpeechBubble.h"

#include <QElapsedTimer>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QMap>
#include <QQueue>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace core { class RuntimeProject; }

namespace game {

class GameSession
{
public:
    struct DebugTraceEntry {
        qint64 sequence = 0;
        QString phase = QStringLiteral("command.execute");
        QString source;
        QString mapId;
        QString eventId;
        QString commandType;
        int commandIndex = -1;
        int callDepth = 0;
        QVariantMap details;
    };
    /// `startPixel` é onde o editor está olhando: o jogo começa por ali.
    GameSession(core::Editor& ed, const QPointF& startPixel, const QFont& fonteBase);
    /// Preferred runtime boundary. Editor overload remains as a compatibility adapter.
    GameSession(core::RuntimeProject& project, const QPointF& startPixel, const QFont& fonteBase);

    // ---- ligações opcionais da janela ------------------------------------
    using AudioHook = std::function<void(const QString&, int)>;
    void setAudioHook(AudioHook tocar) { m_audio = std::move(tocar); }
    /// Efeitos que precisam de pitch/pan (Footstep System, e futuramente landing/splash).
    using EffectAudioHook = std::function<void(const QString&, int, int, int)>;
    void setEffectAudioHook(EffectAudioHook tocar) { m_effectAudio = std::move(tocar); }
    void setVoiceAudioHook(AudioHook tocar, std::function<void()> parar, std::function<bool()> tocando={})
    { m_voiceAudio = std::move(tocar); m_stopVoiceAudio = std::move(parar); m_voicePlaying = std::move(tocando); }
    using BattleHook = std::function<int(const QString&, bool)>;
    void setBattleHook(BattleHook hook) { m_battle = std::move(hook); }
    using ChannelAudioHook = std::function<void(const QString&, const QString&, int, bool, int, int, int, int)>;
    using StopChannelAudioHook = std::function<void(const QString&, int)>;
    void setChannelAudioHook(ChannelAudioHook play, StopChannelAudioHook stop)
    { m_channelAudio = std::move(play); m_stopChannelAudio = std::move(stop); }
    using ShopHook = std::function<void(const QStringList&, bool)>;
    void setShopHook(ShopHook hook) { m_shop = std::move(hook); }
    void refreshMapAudio();
    /// Reaplica preferências locais do jogador que afetam a sessão (ex.: velocidade do texto).
    void refreshPlayerPreferences();
    /// Filtro da ampliação final em cache. Evita consultar QSettings/registro
    /// do Windows em todo frame nos dois renderizadores.
    bool smoothScaleFilter() const { return m_smoothScaleFilter; }
    /// Idioma efetivo da partida e troca centralizada. A troca força rebuild
    /// dos controladores que mantêm textos/listas em cache.
    QString playerLocale() const;
    QString setPlayerLocale(const QString& requestedLocale);

    // ---- ciclo de vida ----------------------------------------------------
    void resize(int w, int h);
    /// RC2.85: entrada do Player direto no mapa com fade-in preto, iniciada
    /// depois de qualquer Load para que o snapshot salvo nao sobrescreva a
    /// transicao de abertura desta execucao.
    void startEntryFade(int frames = 24);
    void tick();                       ///< avança um quadro (usa o relógio interno)
    /// Deterministic/headless step. Uses the fixed 60 Hz simulation quantum.
    void tickFixedStep();
    void setDeterministicMode(bool enabled, quint64 seed = 0x4c55444fULL);
    bool deterministicMode() const { return m_runtimeClock.deterministic(); }
    QByteArray deterministicStateHash() const;
    const RuntimeReplay& replay() const { return m_replay; }
    RuntimeReplay& replay() { return m_replay; }
    RuntimeTraceRecorder& traceRecorder() { return m_trace; }
    void setTraceRecording(bool enabled) { m_trace.setEnabled(enabled); }
    void keyPress(int key, bool autoRepeat, const QString& text = QString());
    void keyRelease(int key, bool autoRepeat);
    void actionPress(core::GameAction action);
    void actionRelease(core::GameAction action);
    void mousePress(const QPointF& logicalPosition, Qt::MouseButton button);
    void mouseMove(const QPointF& logicalPosition);
    void mouseRelease(Qt::MouseButton button);
    void mouseWheel(const QPointF& logicalPosition, int steps);
    /// O jogo pediu para fechar (tecla de sair)?
    bool wantsClose() const { return m_wantClose; }

    // ---- mapa e persistência ---------------------------------------------
    /// `useMapSpawn` existe apenas para compatibilidade binária interna; a UI
    /// no-code sempre grava um destino visual explícito.
    bool transferToMap(const QString& mapId, const QPoint& cell, bool useMapSpawn,
                       Dir facing = Dir::Down, QString* error = nullptr);
    bool saveGame(int slot, QString* error = nullptr) const;
    bool loadGame(int slot, QString* error = nullptr);
    /// Reinicia a partida atual usando os defaults do projeto e o Start Map.
    bool restartGame(QString* error = nullptr);
    bool startUiCommonEvent(int number);
    bool canOpenMenu() const { return !m_gameOver&&!anyInterpreterRunning()&&!m_mapTransition.active&&!m_uiModal.active()&&!m_uiMenu.active()&&!m_uiMenu.customActive()&&!m_uiBattle.active()&&!m_uiShop.active(); }
    bool inGameUiModalActive() const { return m_uiModal.active(); }
    bool inGameMenuActive() const { return m_uiMenu.active(); }
    bool inGameBattleActive() const { return m_uiBattle.active(); }
    bool inGameShopActive() const { return m_uiShop.active(); }
    void openGameMenu(bool standalone);
    bool takeFullscreenToggleRequest() { return m_uiMenu.takeFullscreenToggleRequest(); }
    bool takeAudioSettingsChangedRequest() { return m_uiMenu.takeAudioSettingsChangedRequest(); }
    qint64 playTimeSeconds() const { return qint64(m_playTimeSeconds); }
    qint64 visualElapsedMs() const { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    QString currentMapId() const;
    QString weatherType() const { return m_weather.typeId(); }
    int weatherIntensity() const { return m_weather.intensity; }
    const RuntimeWeatherState& weatherState() const { return m_weather; }

    // ---- raster de referência/geração de texturas ------------------------
    /// Cena inteira: mapa + atores + interface. Mantido para compatibilidade.
    void draw(QPainter& p);
    /// Renderizador de CPU: compõe a cena, aplica a tonalidade pixel a pixel
    /// e só então desenha a interface. Evita a falsa "camada cinza" que
    /// apagava a imagem quando Gray chegava a 255.
    void drawCpuFrame(QImage& frame);
    /// Aplica a mesma equação de tonalidade usada pelo shader da GPU.
    void applyScreenTone(QImage& image) const;
    /// Só a interface (mensagens, legendas, HUD), em pixels de tela.
    /// O renderizador de GPU desenha isto numa imagem e sobe como textura.
    void drawOverlay(QPainter& p);
    /// Variante usada pelo renderer para garantir UMA única fotografia do
    /// RuntimeRenderState durante todo o frame.
    void drawOverlay(QPainter& p, const RuntimeRenderState& state);
    /// Só o clima. As partículas são ancoradas em coordenadas do MAPA e
    /// projetadas pela câmera; assim não parecem coladas ao monitor.
    void drawWeatherLayer(QPainter& p) const;
    void drawWeatherLayer(QPainter& p, const RuntimeRenderState& state) const;
    /// Só os atores (jogador e eventos), em pixels do mapa.
    void drawActors(QPainter& p);
    void drawActorsByPriority(QPainter& p, core::EventPriority priority, bool includePlayer = false);
    /// Tiles ★ e atores intercalados pelo Y dos pés (regra formato 4×4).
    void drawStarActors(QPainter& p);
    /// Mesma cena de atores, mas em QUADS — é o que o renderizador de GPU usa.
    /// Fica aqui de propósito: a regra de "como o herói aparece" (sombra,
    /// charset, ordenação por Y) é uma só para os dois caminhos.
    void appendActorQuads(SpriteBatcher& out, ImageProvider& prov);
    void appendActorQuads(SpriteBatcher& out, ImageProvider& prov, int priorityFilter);
    void appendPanoramaQuads(SpriteBatcher& out, ImageProvider& prov);
    void appendPanoramaQuads(SpriteBatcher& out, ImageProvider& prov, const RuntimeRenderState& state);
    void appendFogQuads(SpriteBatcher& out, ImageProvider& prov);
    void appendFogQuads(SpriteBatcher& out, ImageProvider& prov, const RuntimeRenderState& state);
    /// Entrada oficial do clima para batches. Todos os vértices permanecem em
    /// World Space; somente RuntimeRenderState pode aplicar câmera/zoom.
    void appendWeatherQuads(SpriteBatcher& out);
    void appendWeatherQuads(SpriteBatcher& out, const RuntimeRenderState& state);
    void appendRuntimeWeatherQuads(SpriteBatcher& out);
    void appendRuntimeWeatherQuads(SpriteBatcher& out, const RuntimeRenderState& state);
    /// Pictures de UMA camada em quads. Cada lote declara World ou Screen;
    /// a janela apenas executa a projeção definida pelo RuntimeRenderState.
    void appendPictureQuads(SpriteBatcher& out, ImageProvider& prov, core::PictureLayer camada);
    void appendPictureQuads(SpriteBatcher& out, ImageProvider& prov, core::PictureLayer camada,
                            const RuntimeRenderState& state);
    /// O renderizador de GPU desenha as imagens como quads, então precisa que
    /// elas NÃO saiam também na textura de interface (senão aparecem duas
    /// vezes, e a mistura da de cima fica errada).
    void setPicturesInOverlay(bool on) { m_picturesNoOverlay = on; }
    /// QRhi compoe fades/flash, legendas e transicoes finais como quads. O
    /// O raster de referência preserva a composição para testes e geração de
    /// texturas editoriais; não existe mais runtime CPU selecionável.
    void setGpuOverlayCompositing(bool on) { m_gpuOverlayCompositing = on; m_overlayDirty = true; }
    void appendScreenEffectQuads(SpriteBatcher& out) const;
    void appendSubtitleQuads(SpriteBatcher& out, ImageProvider& provider);
    void appendPresentationEffectQuads(SpriteBatcher& out) const;

    // ---- consultas para o renderizador de GPU -----------------------------
    QPointF cameraTopLeft() const;
    RuntimeRenderState renderState() const;
    RuntimeVisualFrame visualFrame() const { return {renderState(), m_visualFrameSerial}; }
    double  zoom() const { return m_zoom; }
    QSize   viewSize() const { return QSize(m_viewW, m_viewH); }
    /// Retângulo onde a tela lógica ocupa o máximo possível da janela sem
    /// deformar. Aceita escala fracionária (por exemplo 1,7×); as janelas CPU
    /// e GPU aplicam filtragem suave nessa ampliação.
    static QRect letterboxRect(const QSize& logica, const QSize& janela);
    const World& world() const { return m_world; }
    const GameState& state() const { return m_state; }
    core::DialogueHistory& dialogueHistory() { return m_dialogueHistory; }
    const core::DialogueHistory& dialogueHistory() const { return m_dialogueHistory; }
    /// Imagens de tela vivas (o editor de pré-visualização também usa).
    PictureManager& pictures() { return m_pics; }
    FogManager& fogs() { return m_fogs; }
    const FogManager& fogs() const { return m_fogs; }
    /// Versão da imagem composta de um slot: muda quando os efeitos a
    /// refazem. O renderizador de GPU usa isto para reenviar só o que mudou.
    quint64 pictureVersion(int numero) const { return m_picFx.version(numero); }
    const PictureManager& pictures() const { return m_pics; }
    GameState& state() { return m_state; }
    core::Editor& editor() { return ed; }
    const core::Editor& editor() const { return ed; }
    const core::InputSystemSettings& inputSettings() const { return ed.inputSystem; }
    // RC2.46 / H — estado de Input de primeira classe para Value Resolver,
    // condições, Common Events e comandos de espera.
    bool inputHeld(core::GameAction a) const { return held(a); }
    bool inputPressed(core::GameAction a) const;
    bool inputReleased(core::GameAction a) const;
    int inputHoldFrames(core::GameAction a) const;
    void setGamepadAnalog(bool connected, double x, double y, double leftTrigger=0.0, double rightTrigger=0.0);
    void notifyKeyboardInput()
    { if (ed.inputSystem.adaptivePrompts) m_promptController.clear(); }
    void notifyControllerInput()
    { if (ed.inputSystem.adaptivePrompts) m_promptController = QStringLiteral("generic"); }
    /// A interface mudou desde o último quadro? (evita subir textura à toa)
    bool overlayDirty() const { return m_overlayDirty; }
    void clearOverlayDirty() { m_overlayDirty = false; }
    double fps() const { return m_fps; }
    int screenToneRed() const { return m_screenTone.red(); }
    int screenToneGreen() const { return m_screenTone.green(); }
    int screenToneBlue() const { return m_screenTone.blue(); }
    int screenToneGray() const { return m_screenTone.gray(); }
    bool hasScreenTone() const { return m_screenTone.hasTone(); }
    const LudoFilterSystem& filterSystem() const { return m_filters; }
    LudoFilterUniforms filterUniforms() const { return m_filters.uniforms(viewSize()); }
    bool hasActiveFilters() const { return m_filters.hasActiveFilters(); }
    const QVector<DebugTraceEntry>& debugTrace() const { return m_debugTrace; }
    void clearDebugTrace() { m_debugTrace.clear(); }
    // ---- Bloco E: debugger visual / profiler / diagnóstico ---------------
    void debugPause();
    void debugContinue();
    void debugStep();
    void debugStepInto();
    void debugStepOver();
    void debugStepOut();
    void debugPumpOneCommand();
    void debugPumpStepOver();
    void debugPumpStepOut();
    QVector<Interpreter::DebugFrameInfo> debugCallStack() const;
    /// No playtest, aponta para o Editor vivo. O Player exportado deixa vazio,
    /// portanto Hot Reload nunca cria uma dependência de desenvolvimento no jogo final.
    void setHotReloadSource(core::Editor* source) { m_hotReloadSource = source; }
    bool hotReload(QStringList* diagnostics = nullptr);
    bool debugPaused() const { return m_eventDebugger.paused(); }
    bool debugBlocked() const { return m_eventDebugger.blocked(); }
    const DebugCommandLocation& debugLocation() const { return m_eventDebugger.current(); }
    void addDebugBreakpoint(const QString& source, int commandIndex) { m_eventDebugger.addBreakpoint(source, commandIndex); }
    void removeDebugBreakpoint(const QString& source, int commandIndex) { m_eventDebugger.removeBreakpoint(source, commandIndex); }
    bool hasDebugBreakpoint(const QString& source, int commandIndex) const { return m_eventDebugger.hasBreakpoint(source, commandIndex); }
    QStringList debugBreakpoints() const { return m_eventDebugger.breakpoints(); }
    void setDebugBreakpointCondition(const QString& source, int commandIndex,
                                     const QVariantMap& condition)
    { m_eventDebugger.setBreakpointCondition(source, commandIndex, condition); }
    QVariantMap debugBreakpointCondition(const QString& source, int commandIndex) const
    { return m_eventDebugger.breakpointCondition(source, commandIndex); }
    EventRuntimeDebugState eventRuntimeDebugState() const;
    quint64 addDebugWatch(DebugWatchKind kind, const QString& reference,
                          const QString& label = QString());
    bool removeDebugWatch(quint64 serial) { return m_debugWatches.remove(serial); }
    bool setDebugWatchBreakOnChange(quint64 serial, bool enabled)
    { return m_debugWatches.setBreakOnChange(serial, enabled); }
    bool setDebugWatchCondition(quint64 serial, const QString& op, const QVariant& value)
    { return m_debugWatches.setCondition(serial, op, value); }
    QVector<DebugWatch> debugWatches() const { return m_debugWatches.watches(); }
    QVector<DebugWatchSample> debugWatchSamples() const { return m_debugWatches.samples(); }
    QString debugTraceText(const QString& filter = QString()) const;
    QJsonArray debugTraceJson(const QString& filter = QString()) const;
    QStringList activeCommonEvents() const;
    QString debugUiScreen() const;
    QString debugUiFocus() const;
    RuntimeProfilerSnapshot profilerSnapshot() const { return m_profiler.snapshot(); }
    void setFrameCost(double ms);
    void setRenderStats(const QString& renderer, int drawCalls, int quads);
    void setProfileStageTiming(const QString& stage, double ms);
    void setFramePacingStats(int lateFrames, int totalFrames, double lateRatio, double lastOverrunMs, double worstOverrunMs);
    void setRenderWorkStats(qint64 textureUploadBytes, qint64 vertexUploadBytes,
                            int textureUploads, int mapCacheHits, int mapCacheMisses,
                            int pipelineChanges, int shaderResourceChanges,
                            qint64 staticMapVertexUploadBytes = 0,
                            qint64 dynamicVertexUploadBytes = -1,
                            int gpuMapMeshBuilds = 0);
    QJsonObject diagnosticSnapshot() const;
    bool writeDiagnosticBundle(const QString& zipPath, QString* error = nullptr) const;
    /// Ativa/desativa apenas a interface técnica de playtest. O Player
    /// exportado deixa isto desligado; mensagens normais do jogo continuam.
    void setDebugPresentation(bool on)
    { m_debugPresentation = on; m_showHud = on; m_overlayDirty = true; }
    /// O atalho de pular cutscene continua funcionando, mas o Player exportado
    /// pode esconder a dica visual para não exibir UI de desenvolvimento.
    void setCutsceneSkipPromptVisible(bool on)
    { m_cutsceneSkipPromptVisible = on; m_overlayDirty = true; }
    /// Rota C: overlay técnico de pathfinding/estado. Só é exposto pelo playtest
    /// (F8); o LudoPlayer exportado nunca o ativa automaticamente.
    bool moveRouteDebugVisible() const { return m_showMoveRouteDebug; }
    void setMoveRouteDebugVisible(bool on) { m_showMoveRouteDebug = on; m_overlayDirty = true; }

private:
    void refreshLocalizationState();
    bool held(core::GameAction a) const;
    bool inputActionMatches(core::GameAction a, const QString& state) const;
    void updateInputFrameState();
    void tryInteract();
    void prepareInteractionFacing(const core::MapEvent& event, int page);
    void restoreInteractionFacingIfNeeded();
    void drawGameUi(QPainter& p);
    void drawMoveRouteDebugOverlay(QPainter& p, const RuntimeRenderState& state) const;
    QPair<QRect,int> choiceGeometry(const Interpreter* interpreter) const;
    void drawWeather(QPainter& p, const RuntimeRenderState& state) const;
    RuntimeWeatherFrame weatherFrame() const;
    RuntimeWeatherFrame weatherFrame(const RuntimeRenderState& state) const;
    /// Desenha as imagens de tela de UMA camada, em pixels de tela.
    void drawPictures(QPainter& p, core::PictureLayer camada, const RuntimeRenderState& state);
    /// Etapas Panorama -> MapBelow -> Actors -> ★/MapAbove -> Fog.
    /// Weather e ScreenTone são etapas posteriores e não entram aqui.
    void drawScene(QPainter& p, const RuntimeRenderState& state);
    /// Imagem com a opacidade JÁ multiplicada nos pixels (com cache).
    /// Existe por causa de uma diferença medida entre CPU e GPU: com mistura
    /// aditiva, o QPainter satura a soma ANTES de aplicar a opacidade, e a
    /// GPU multiplica antes de somar. Multiplicando aqui, os dois caminhos
    /// passam a fazer a mesma conta — e a conta certa (a do editores de RPG).
    const QImage& imagemComAlfa(const QString& chave, const QImage& original, int alfa);
    void drawSubtitles(QPainter& p);
    void drawSpeechBubbles(QPainter& p, const RuntimeRenderState& state);
    QPointF speechBubbleAnchor(const SpeechBubble& bubble,const RuntimeRenderState& state) const;
    QPointF subtitleAnchorPixel(const Subtitle& s) const;
    void drawPlayer(QPainter& p);
    void drawEvent(QPainter& p, const core::MapEvent& e);
    void drawPanorama(QPainter& p,const QPointF& cam,bool fixedPass, const RuntimeRenderState& state);
    void drawCutsceneSkip(QPainter& p);
    void updateLudo(double dt);
    void updateStormThunder();
    void syncWeatherFromCurrentMap();
    void updateMapTransition(double dt);
    void drawMapTransition(QPainter& p) const;
    /// Flash/Fade globais em Screen Space. Ficam acima de cena/clima/Pictures
    /// e abaixo da UI normal; transição de mapa permanece Presentation final.
    void drawScreenEffects(QPainter& p) const;
    void drawGameOver(QPainter& p) const;
    void updateOverlayDirty(double dt);
    CommandResult runRuntimeCommand(const core::EventCommand& command, bool start);
    bool applyStateMutationCommand(const core::EventCommand& command, QString* error = nullptr);
    bool runLudoCommand(const core::EventCommand& command,bool start);
    bool processPendingRuntimeActions();
    QJsonObject runtimeSnapshot() const;
    void restoreRuntimeSnapshot(const QJsonObject& snapshot);
    double spriteOpacity(const QString& target,const QPointF& pixel) const;
    QPointF spriteOffset(const QString& target) const;
    QPointF eventPageVisualOffset(const core::MapEvent& event) const;
    QPointF spriteScale(const QString& target) const;
    QRectF transformedSpriteRect(const QString& target,const QRectF& rect) const;
    QPointF cameraTargetPoint(const QString& target,const QVariantMap& params) const;
    void configureInterpreter(Interpreter& interpreter);
    QVariant resolveDebugWatch(const DebugWatch& watch, const Interpreter* context = nullptr) const;
    bool evaluateDebugBreakpointCondition(const QVariantMap& condition,
                                          const Interpreter& context) const;
    void updateDebugWatches();
    void appendDebugTrace(QString phase, QString source, QString eventId,
                          int commandIndex, int callDepth, QString commandType = {},
                          QVariantMap details = {});
    quint64 startInlineParallelBlock(const QVector<QVector<core::EventCommand>>& tasks,
                                     const core::EventExecutionContext& context);
    bool inlineParallelBlockActive(quint64 ticket);
    void cancelInlineParallelBlock(quint64 ticket, CancellationReason reason);
    void stopParallelInterpreters(CancellationReason reason);
    void stopAllInterpreters(CancellationReason reason);
    void finishCutsceneTransientState();
    void syncParallelInterpreters();
    void updateCommonEventScheduler();
    bool reserveCommonEvent(const QString& commonId, const QVariantMap& arguments, int priority);
    bool dispatchReservedCommonEvent();
    void updateCutsceneRegions();
    bool commonEventCondition(const core::CommonEvent& ce) const;
    bool commonEventReady(const core::CommonEvent& ce) const;
    void consumeCommonEventSchedule(const core::CommonEvent& ce);
    bool startAutorunIfNeeded();
    void startTouchEventIfNeeded();
    bool startDirectionalSensorIfNeeded();
    void applyRememberedEventPositions();
    Interpreter* visibleInterpreter();
    const Interpreter* visibleInterpreter() const;
    /// Resolve de forma determinística qual Interpreter possui a região de
    /// cutscene ativa que receberá o input de Skip. Reutiliza o pool atual de
    /// Main/Common/Parallel; não existe scheduler de cutscene separado.
    Interpreter* activeCutsceneInterpreter();
    const Interpreter* activeCutsceneInterpreter() const;
    bool canSkipCutscene() const;
    core::GameAction cutsceneSkipAction() const;
    bool anyInterpreterRunning() const;
    void updateParallelInterpreters(double dt,bool confirmEdge);
    void maybeRandomEncounter();
    int randomBounded(int upperExclusive);
    void finishUiModal();
    void playUiSound(const QString& relativePath);
    bool handleUiAction(core::GameAction action);
    bool handleMenuAction(core::GameAction action);
    bool handleBattleAction(core::GameAction action);
    bool handleShopAction(core::GameAction action);
    void finishBattleCommand(const core::EventCommand& command);
    void playFootstep(const QString& eventId, const QPointF& actorCell,
                      const QString& forcedSurfaceId = QString(), int commandVolume = 100);

    core::Editor& ed; // transitional implementation bridge; callers should use RuntimeProject
    QPointer<core::Editor> m_hotReloadSource;
    RuntimeClock m_runtimeClock;
    RuntimeRandom m_runtimeRandom;
    EventScheduler m_eventScheduler;
    RuntimeReplay m_replay;
    RuntimeTraceRecorder m_trace;
    double m_forcedDeltaSeconds = -1.0;
    /// Estado exclusivamente da partida: detectar um controle não deve
    /// modificar nem marcar como alterada a configuração salva do projeto.
    QString m_promptController;
    World    m_world;
    GameState m_state;
    core::DialogueHistory m_dialogueHistory;
    Interpreter m_interp;
    bool m_restoreFacingAfterInteraction = false;
    Dir m_facingBeforeInteraction = Dir::Down;
    bool m_restoreEventFacingAfterInteraction = false;
    QString m_interactionEventId;
    QString m_interactionMapId;
    int m_eventDirectionBeforeInteraction = 0;
    int m_eventFrameBeforeInteraction = 0;
    QPoint m_eventHalfCellBeforeInteraction;
    // Um paralelo não pode usar o interpretador principal: isso congelaria o
    // mapa e faria um evento interromper outro. Cada origem mantém seu estado.
    // Ordem determinística sem reconstruir/ordenar uma QStringList a cada frame.
    QMap<QString,std::shared_ptr<Interpreter>> m_parallel;
    struct InlineParallelBlockRuntime {
        QStringList interpreterKeys;
    };
    QHash<quint64, InlineParallelBlockRuntime> m_inlineParallelBlocks;
    quint64 m_inlineParallelSerial = 0;
    struct CommonScheduleRuntime {
        bool initialized=false;
        bool previousCondition=false;
        bool active=false;
        bool pending=false;
        int framesUntilRun=0;
    };
    QHash<QString,CommonScheduleRuntime> m_commonSchedule;
    struct ReservedCommonCall {
        QString commonId;
        QVariantMap arguments;
        int priority = 0;
        quint64 serial = 0;
    };
    QVector<ReservedCommonCall> m_reservedCommonEvents;
    quint64 m_reservedCommonSerial = 0;
    quint64 m_commonScheduleTickSerial=0;
    QSet<QString> m_cutsceneRegionInside;
    QSet<QString> m_cutsceneRegionVisitFired;
    QSet<QString> m_cutsceneRegionPersistentFired;
    QSet<QString> m_autoLudoExecuted;
    QSet<QString> m_sensorLatched;
    struct RememberedEventPosition { QString mapId; QPoint halfCell; };
    QHash<QString,RememberedEventPosition> m_rememberedEventPositions;
    QHash<QString,QVector<core::EventCommand>> m_autoLudoCache;
    SubtitleManager m_subs;
    SpeechBubbleManager m_bubbles;
    PictureManager  m_pics;
    FogManager      m_fogs;
    VisualFxCache  m_picFx;
    ImageProvider m_cpuDepthImages;
    QJsonObject m_subsStyleCache;
    AudioHook m_audio;
    EffectAudioHook m_effectAudio;
    quint64 m_footstepSerial = 0;
    struct FootstepVariantHistory { int previous = -1; int last = -1; };
    QHash<QString,FootstepVariantHistory> m_footstepVariantHistory;
    AudioHook m_voiceAudio;
    std::function<void()> m_stopVoiceAudio;
    std::function<bool()> m_voicePlaying;
    BattleHook m_battle;
    ChannelAudioHook m_channelAudio;
    StopChannelAudioHook m_stopChannelAudio;
    struct AudioChannelState { QString source; int volume = 90; bool loop = false; int pitch=100; int pan=0; };
    QHash<QString,AudioChannelState> m_audioChannels;
    ShopHook m_shop;

    /// A tecla de confirmar foi apertada desde o último quadro?
    /// Marcada no evento de tecla (e NÃO por varredura): um toque rápido pode
    /// começar e terminar entre dois quadros e simplesmente sumir.
    RuntimeCameraState m_camera;
    struct SpriteFx {
        QString type, mode=QStringLiteral("visibleNear");
        double opacity=1, from=1, to=1, elapsed=0, duration=0;
        double distance=6, nearDistance=1, minimum=.15, maximum=1, smoothness=1;
        QPointF offset, offsetFrom, offsetTo;
        QPointF scale=QPointF(1,1), scaleFrom=QPointF(1,1), scaleTo=QPointF(1,1);
        double transformElapsed=0, transformDuration=0;
        double shakeX=0, shakeY=0, shakeFrequency=12, shakeRemaining=0, shakePhase=0;
        bool active=false, transformActive=false, fadeEnabled=false, phantomEnabled=false;
    };
    QHash<QString,SpriteFx> m_spriteFx;
    ScreenToneState m_screenTone;
    LudoFilterSystem m_filters;
    QImage m_filterCpuScratch;
    RuntimeScreenEffectsState m_screenEffects;
    struct MapTransition {
        bool active=false, transferred=false;
        double elapsed=0.0, halfDuration=0.3, opacity=0.0;
        core::EventCommand command;
    } m_mapTransition;
    QVector<QPointF> m_panoramaOffsets;
    double m_panoramaAge = 0.0;
    RuntimeWeatherState m_weather;
    qint64 m_lastThunderCycle = -1;
    bool m_cutsceneOverride=true;
    bool m_skipHeld=false,m_skipTriggered=false;
    double m_skipHeldSec=0,m_skipFade=0;

    bool     m_confirmPending = false;
    bool     m_wantClose = false;
    bool     m_gameOver = false;
    bool     m_loadingGame = false;
    ui::GameUiLayer m_gameUi;
    ui::UiModalController m_uiModal;
    ui::UiMenuController m_uiMenu;
    ui::UiBattleController m_uiBattle;
    ui::UiShopController m_uiShop;
    core::EventCommand m_runtimeModalCommand;
    bool     m_overlayDirty = true;
    quint64  m_visualFrameSerial = 1;
    bool     m_gpuOverlayCompositing = false;
    QHash<QString,QImage> m_subtitleGpuImages;
    QHash<QString,QString> m_subtitleGpuSignatures;
    bool     m_smoothScaleFilter = false;
    int      m_uiScalePercent = 100;
    bool     m_strongFocus = false;
    bool     m_reduceShake = false;
    bool     m_reduceFlash = false;
    double   m_overlayRefreshAcc = 0.0;
    QElapsedTimer m_clock;
    QSet<int> m_keys;
    QSet<core::GameAction> m_actions;
    QHash<int,quint64> m_actionPressedFrame;
    QHash<int,quint64> m_actionReleasedFrame;
    QHash<int,int> m_actionHoldFrames;
    bool m_gamepadConnected=false;
    double m_gamepadAxisX=0.0,m_gamepadAxisY=0.0,m_gamepadLeftTrigger=0.0,m_gamepadRightTrigger=0.0;
    Qt::MouseButtons m_mouseButtons=Qt::NoButton;
    quint64 m_mouseLeftPressedFrame=0,m_mouseLeftReleasedFrame=0;
    quint64 m_mouseRightPressedFrame=0,m_mouseRightReleasedFrame=0;
    QPixmap  m_charsetPm;
    QFont    m_font;
    int      m_viewW = 800, m_viewH = 600;
    double   m_zoom = 2.0;
    double   m_fps = 0.0;
    double   m_frameMs = 0.0;
    QPointF  m_mouseLogical;
    QPointF  m_mouseDelta;
    int      m_mouseWheelDelta = 0;
    double   m_playTimeSeconds = 0.0;
    QVector<DebugTraceEntry> m_debugTrace;
    qint64 m_debugSequence = 0;
    EventDebugController m_eventDebugger;
    DebugWatchStore m_debugWatches;
    RuntimeProfiler m_profiler;
    bool     m_showHud = true;
    bool     m_debugPresentation = true;
    bool     m_showMoveRouteDebug = false;
    bool     m_cutsceneSkipPromptVisible = true;
    bool     m_picturesNoOverlay = true;
    QHash<QString, QPair<int, QImage>> m_picAlfaCache;
    QString  m_hint;
    qint64   m_hintUntil = 0;
    qint64   m_lastNs = 0;
    QQueue<core::EventCommand> m_pendingRuntimeActions;
    int m_stepsAtLastEncounter = 0;
    int m_nextEncounterSteps = 0;
    bool m_randomEncounterBattle = false;
};

} // namespace game
