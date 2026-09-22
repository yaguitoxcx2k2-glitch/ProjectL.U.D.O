// ============================================================================
//  Interpreter.h — Executa a lista de comandos de uma página de evento.
//
//  Por que uma máquina de estados e não uma função que "mostra a mensagem e
//  espera": porque esperar dentro do laço travaria o jogo inteiro — nada de
//  repintar, nada de 60 fps, e o sistema acusando aplicativo travado. Aqui o
//  interpretador é cutucado a cada quadro com `update(dt, confirmou)` e diz o
//  que a tela deve mostrar.
//
//  Como todo o resto do runtime, NÃO conhece interface: dá para rodar uma
//  página inteira num teste sem abrir janela.
// ============================================================================
#pragma once

#include "game/InterpreterCommandDispatcher.h"

#include "game/MoveRouteRuntime.h"
#include "game/RuntimeDiagnostics.h"

#include "GameState.h"
#include "Pictures.h"
#include "Subtitle.h"
#include "TextBox.h"
#include "core/EventModel.h"
#include "core/EventExecutionContext.h"
#include "core/GameData.h"

#include <QFont>
#include <QImage>
#include <QColor>
#include <QString>
#include <QSet>
#include <QVector>
#include <functional>

namespace game {

class FogManager;

/// Resultado canônico de um comando encaminhado à GameSession.
/// Completed      = efeito já está observável; pode continuar neste mesmo tick.
/// DeferredCommit = aceito/enfileirado; o próximo comando só pode começar
///                  depois que a GameSession aplicar sua fila.
/// Waiting        = operação assíncrona/modal ainda está ativa.
/// Failed         = comando conhecido terminou com erro de dados/estado.
/// Rejected       = comando não pôde ser tratado; o Interpreter segue sem travar.
enum class CommandResult { Completed, DeferredCommit, Waiting, Failed, Rejected };

/// Espera canônica do Interpreter. Antes cada subsistema mantinha seu próprio
/// boolean/campo específico de subsistema, o que tornava
/// cancelamento, debug e novos comandos de espera inconsistentes. Agora toda
/// espera explícita de Event Command ocupa um único slot tipado.
enum class AwaitableKind { None, Frames, Picture, MoveRoute, Runtime, Subtitles, Condition, ParallelBlock };

/// Motivo de cancelamento usado por Interpreter e GameSession. A mesma razão
/// acompanha rotas, blocos paralelos e mudanças de contexto para que cada
/// subsistema possa finalizar/abortar de forma determinística sem flags locais.
enum class CancellationReason {
    Stop, CutsceneSkip, MapTransfer, LoadGame, RestartGame, ReturnTitle, GameOver, ContextRemoved
};

QString awaitableKindId(AwaitableKind kind);
QString cancellationReasonId(CancellationReason reason);

struct CommandAwaitable {
    AwaitableKind kind = AwaitableKind::None;
    int framesRemaining = 0;
    double frameAccumulator = 0.0;
    int pictureTarget = 0; // -1 = qualquer Picture; >0 = slot
    QString routeTarget;
    MoveRouteTicket routeTicket = 0;
    core::EventCommand command; // Runtime/Condition preservam o contrato original
    int timeoutFramesRemaining = 0; // Condition: 0 = sem timeout
    double timeoutAccumulator = 0.0;
    quint64 parallelTicket = 0;       // ParallelBlock: ticket da GameSession
    int resumeCommandIndex = -1;      // comando estrutural de fechamento

    bool active() const { return kind != AwaitableKind::None; }
    void clear() { *this = CommandAwaitable(); }
};

/// Onde a caixa aparece.
enum class BoxPosition { Top, Middle, Bottom };

BoxPosition boxPositionFromId(const QString& id);
QString     boxPositionId(BoxPosition p);

/// Aparência/medidas da caixa — o interpretador precisa delas para quebrar o
/// texto no lugar certo.
struct MessageStyle {
    QFont  font;
    double innerWidth = 300.0;   ///< largura útil do texto, em px lógicos
    int    maxLines = 4;
    double charsPerSecond = 40.0;
};

/// O que a janela precisa saber para desenhar a mensagem agora.
struct MessageView {
    bool        visible = false;
    TextPage    page;
    int         revealed = 0;      ///< cursor da máquina de reveal (inclui controles inline)
    int         drawableRevealed = 0; ///< glyphs realmente visíveis para o renderer
    QVector<double> revealTimesSec;   ///< relógio de nascimento de cada glyph
    bool        waitingKey = false;///< terminou de digitar (ou \! no meio)
    bool        lastPage = true;
    QString     speaker;            ///< Name Box in-game (vazio = oculto)
    QString     speakerId;
    QString     portrait;
    QImage      portraitImage;
    QString     portraitPosition = QStringLiteral("left");
    QString     expression;
    QString     fontFamily;
    QColor      nameColor;
    QColor      textColor;
    BoxPosition position = BoxPosition::Bottom;
    int         offsetX = 0;
    int         offsetY = 0;
    int         fontSize = 0;      ///< 0 = tamanho global da caixa de texto
    core::TextGradientSpec gradient;
    core::TextEffectStack effects;
    double      effectTimeSec = 0.0;
    /// RC2.55: relógio separado da fase de saída. -1 = ainda não saindo.
    double      exitTimeSec = -1.0;
    bool        exiting = false;
};

/// Menu de escolhas exibido por um comando de evento. O resultado é 1..N;
/// cancelar pode ser proibido (-1) ou gravar 0.
struct ChoiceView {
    bool visible = false;
    QStringList options;
    int selected = 0;
    int cancelValue = 0;
    int resultVariable = 0;
    QString position = QStringLiteral("center-right");
    int offsetX = 0;
    int offsetY = 0;
    // RC2.57 / Choices 2.0: layout e apresentação pertencem ao comando,
    // enquanto aparência base/estados vêm do Theme compartilhado.
    QString layout = QStringLiteral("vertical"); // vertical/horizontal/grid
    int columns = 1;
    int spacingX = 6;
    int spacingY = 4;
    QString alignment = QStringLiteral("left"); // left/center/right
    QString boxMode = QStringLiteral("theme"); // theme/transparent
    QString fontFamily;
    int fontSize = 0;
    QVector<bool> disabled;
    QVector<bool> staticDisabled;          ///< bloqueios definidos diretamente pelo comando
    QVector<QVariantMap> conditions;       ///< árvore de condição por opção
    QStringList disabledLabels;            ///< texto alternativo enquanto bloqueada
    QStringList disabledReasons;           ///< explicação visível do bloqueio
    bool showDisabledReason = true;
    double timeLimit = 0.0;                ///< segundos; 0 desativa o cronômetro
    double timeRemaining = 0.0;
    int defaultChoice = -1;                ///< índice 0..N-1; -1 cancela ao expirar
    QVector<QVector<core::EventCommand>> branches; ///< comandos executados diretamente por escolha
    QVector<TextPage> richPages;              ///< mesmo parser de Mensagens/Legendas/Pictures
    core::TextGradientSpec gradient;
    core::TextEffectStack effects;
    double effectTimeSec = 0.0;
    /// RC2.55: Choices usam a mesma fase de saída do motor unificado.
    double exitTimeSec = -1.0;
    bool exiting = false;
    int pendingResult = 0;
};

class Interpreter
{
public:
    explicit Interpreter(const MessageStyle& style = MessageStyle());

    void setStyle(const MessageStyle& s) { m_style = s; }
    /// Liga o interpretador ao gerenciador de legendas (opcional: sem ele os
    /// comandos de legenda são ignorados, e o jogo não trava por isso).
    void setSubtitles(SubtitleManager* m) { m_subs = m; }
    /// Liga a memória da partida (interruptores/variáveis) e o projeto, de
    /// onde saem os eventos comuns. Sem eles, os comandos de lógica são
    /// ignorados em vez de travar.
    void setState(GameState* st, const core::Editor* ed) { m_state = st; m_ed = ed; }
    /// Dispositivo ativo desta partida para resolver \KEY[acao]. Quando não
    /// informado, preserva o comportamento antigo e usa a opção do projeto.
    void setInputController(const QString* controller) { m_inputController = controller; }
    /// Liga o gerenciador de imagens de tela. Sem ele os comandos `picture.*`
    /// são ignorados — nunca travam a cutscene.
    void setPictures(PictureManager* pm) { m_pics = pm; }
    void setFogs(FogManager* fm) { m_fogs = fm; }
    using MessageVoiceHook = std::function<void(const QString&, int)>;
    void setMessageVoiceHook(MessageVoiceHook hook) { m_messageVoiceHook = std::move(hook); }
    using DialogueHistoryHook = std::function<void(const QString&, const QString&, const QString&,
                                                   const QString&, const QString&)>;
    void setDialogueHistoryHook(DialogueHistoryHook hook) { m_dialogueHistoryHook = std::move(hook); }
    /// Contrato por ticket: iniciar, consultar e controlar são operações
    /// distintas. Isso impede “Esperar terminar” de confundir uma rota com
    /// outra quando há fila/substituição no mesmo alvo.
    using MoveRouteStartHook = std::function<MoveRouteTicket(const QString&, const core::MoveRoute&)>;
    using MoveRouteStateHook = std::function<MoveRouteTicketState(const QString&, MoveRouteTicket)>;
    using MoveRouteControlHook = std::function<bool(const QString&, MoveRouteControlAction)>;
    void setMoveRouteStartHook(MoveRouteStartHook hook) { m_moveRouteStartHook = std::move(hook); }
    void setMoveRouteStateHook(MoveRouteStateHook hook) { m_moveRouteStateHook = std::move(hook); }
    void setMoveRouteControlHook(MoveRouteControlHook hook) { m_moveRouteControlHook = std::move(hook); }
    /// Ponte para os sistemas Runtime. O resultado distingue efeito já
    /// concluído, commit adiado para a GameSession e espera assíncrona.
    /// Isso forma a barreira de ordenação entre comandos visuais/lógicos.
    using RuntimeCommandHook = std::function<CommandResult(const core::EventCommand&, bool)>;
    void setRuntimeCommandHook(RuntimeCommandHook hook) { m_runtimeCommandHook=std::move(hook); }
    /// Notifica a sessão quando um comando No-Code troca o idioma. Isso
    /// permite reconstruir menus/listas que já estavam materializados antes
    /// da mudança, além dos Widgets que são resolvidos a cada draw.
    using LocaleChangedHook = std::function<void(const QString&)>;
    void setLocaleChangedHook(LocaleChangedHook hook) { m_localeChangedHook = std::move(hook); }
    using ConditionHook=std::function<bool(const core::EventCommand&)>;void setConditionHook(ConditionHook hook){m_conditionHook=std::move(hook);}
    /// RC2.43 / Bloco C: consulta valores vivos que pertencem à GameSession
    /// (mundo, mapa, áudio, input e sistema) sem acoplar o Interpreter a ela.
    using GameValueHook = std::function<QVariant(const QVariantMap&)>;
    void setGameValueHook(GameValueHook hook) { m_gameValueHook = std::move(hook); }
    using TraceHook = std::function<void(const QString&, int, int, const core::EventCommand&)>;
    void setTraceHook(TraceHook hook) { m_traceHook = std::move(hook); }
    using DiagnosticTraceHook = std::function<void(const QString&, const QVariantMap&)>;
    void setDiagnosticTraceHook(DiagnosticTraceHook hook) { m_diagnosticTraceHook = std::move(hook); }
    /// Gate de depuração: false mantém o comando atual bloqueado até Pause/Step/Continue liberar.
    using DebugGateHook = std::function<bool(const QString&, const QString&, int, int, const core::EventCommand&)>;
    void setDebugGateHook(DebugGateHook hook) { m_debugGateHook = std::move(hook); }
    /// M / RC2.49: Reserva um Common Event no scheduler real da GameSession.
    /// Os argumentos já chegam resolvidos/tipados pelo mesmo Value Resolver.
    using ReserveCommonEventHook = std::function<bool(const QString&, const QVariantMap&, int)>;
    void setReserveCommonEventHook(ReserveCommonEventHook hook) { m_reserveCommonEventHook = std::move(hook); }
    /// Blocos Parallel reutilizam os Interpreters paralelos da GameSession.
    /// Cada vetor interno é uma tarefa independente iniciada no mesmo tick.
    using ParallelBlockStartHook = std::function<quint64(const QVector<QVector<core::EventCommand>>&, const core::EventExecutionContext&)>;
    using ParallelBlockStateHook = std::function<bool(quint64)>;
    using ParallelBlockCancelHook = std::function<void(quint64, CancellationReason)>;
    void setParallelBlockStartHook(ParallelBlockStartHook hook) { m_parallelBlockStartHook = std::move(hook); }
    void setParallelBlockStateHook(ParallelBlockStateHook hook) { m_parallelBlockStateHook = std::move(hook); }
    void setParallelBlockCancelHook(ParallelBlockCancelHook hook) { m_parallelBlockCancelHook = std::move(hook); }
    void setDebugSource(QString source) { m_debugSource = std::move(source); }
    const QString& debugSource() const { return m_debugSource; }
    /// Diagnóstico de comandos que não constam no schema central. O runtime
    /// continua tolerante para compatibilidade, mas o erro deixa de ser silencioso.
    using UnknownCommandHook = std::function<void(const QString&, const QString&, int)>;
    void setUnknownCommandHook(UnknownCommandHook hook) { m_unknownCommandHook = std::move(hook); }
    const MessageStyle& style() const { return m_style; }

    /// Começa a executar os comandos desta página. Ignora páginas sem comando.
    void start(const core::MapEvent& ev, int pageIndex = 0);
    /// Executa um Evento Comum pelo número. `arguments` usa IDs estáveis da
    /// assinatura e recebe valores já resolvidos (útil para UI/testes).
    bool startCommonEvent(int numero, const QVariantMap& arguments = {});
    void start(const QVector<core::EventCommand>& cmds);
    /// Executa uma lista vinculada a um contexto já conhecido (por exemplo,
    /// metadados automáticos de uma página). Não cria um segundo conceito de
    /// "self": o contexto usa a mesma identidade de Map Event do Interpreter.
    void start(const QVector<core::EventCommand>& cmds, const core::EventExecutionContext& context);
    void stop(CancellationReason reason = CancellationReason::Stop);
    /// Cancela somente o trabalho assíncrono pertencente a este Interpreter.
    /// Não limpa estado global de Pictures/Camera/Subtitles: isso pertence à
    /// GameSession, que conhece o escopo correto da transição.
    void cancelActiveWork(CancellationReason reason);
    /// Região estrutural de cutscene ativa no ponto atual. O frame pode ser
    /// um Common Event chamado por uma região aberta no Map Event; por isso a
    /// informação pertence à pilha do Interpreter, não à GameSession.
    struct CutsceneRegionInfo {
        bool active = false;
        int frameIndex = -1;
        int beginIndex = -1;
        int endIndex = -1;
        int nestingDepth = 0;
        QString source;
        bool skipAllowed = true;
        QString skipAction = QStringLiteral("skipCutscene");
        QString nestingPolicy = QStringLiteral("allow");
    };
    CutsceneRegionInfo cutsceneRegion() const;
    bool cutsceneActive() const { return cutsceneRegion().active; }
    int cutsceneDepth() const { return cutsceneRegion().nestingDepth; }
    /// Regiões podem conter Choices/modais/input. Esses pontos são barreiras
    /// lógicas: permanecem interativos e não podem ser descartados pelo skip.
    bool cutsceneSkipReady() const;
    /// Acelera somente a região Begin/End correspondente. Esperas/efeitos
    /// temporais são suprimidos pelo CommandRegistry, mas comandos lógicos
    /// continuam rodando até o End pareado. Retorna false fora de uma região.
    bool skipCutscene();

    bool running() const { return m_running; }
    /// Id do evento que está falando (vazio quando veio de uma lista solta).
    QString currentEventId() const;
    QString currentMapId() const;
    core::EventExecutionContext executionContext() const;
    core::EventTargetResolution resolveEventTarget(const QString& requested,
                                                    bool allowPosition = false,
                                                    bool verifyExplicitEvent = true) const;

    /// Avança um quadro. `confirmEdge` = a tecla de confirmar ACABOU de ser
    /// apertada (borda, não estado) — senão a mensagem passaria voando.
    void update(double dt, bool confirmEdge);
    InterpreterCommandDispatcher& commandDispatcher() { return m_commandDispatcher; }
    const InterpreterCommandDispatcher& commandDispatcher() const { return m_commandDispatcher; }
    AwaitableKind awaitableKind() const { return m_awaitable.kind; }
    EventRuntimeDebugState runtimeDebugState() const;

    const MessageView& message() const { return m_view; }
    const ChoiceView& choice() const { return m_choice; }
    void moveChoice(int delta);
    void cancelChoice();

    /// Só para os testes/depuração: qual comando está rodando (do quadro atual).
    int commandIndex() const { return m_stack.isEmpty() ? -1 : m_stack.last().index; }
    /// Profundidade da pilha (evento comum/map event chamando outro frame).
    int callDepth() const { return m_stack.size(); }

    struct DebugFrameInfo {
        QString source;
        QString mapId;
        QString eventId;
        QString commonEventId;
        int commonEventNumber = 0;
        int pageIndex = -1;
        int commandIndex = -1;
        bool commonFrame = false;
        QVariantMap values;
    };
    QVector<DebugFrameInfo> debugCallStack() const;

    /// Valida, sem mutar a pilha, se todos os frames vivos podem ser
    /// reassociados a outro modelo executável. Usado pelo Hot Reload para
    /// garantir a troca atômica do Runtime Snapshot.
    bool canHotReloadTo(const core::Editor& target, QStringList* diagnostics = nullptr) const;

    /// L / RC2.49: remapeia frames ativos para o modelo atual usando IDs
    /// estáveis. GameState permanece intacto; frames cujo alvo desapareceu
    /// falham explicitamente em vez de continuar com uma cópia obsoleta.
    bool hotReload(QStringList* diagnostics = nullptr);
    /// G / RC2.46: o scheduler de Eventos Comuns usa o MESMO Value Resolver
    /// dos comandos. Não há uma segunda linguagem de condições no runtime.
    QVariant resolveSchedulerSource(const QVariantMap& spec, core::CommonValueType type) const
    { return resolveValueSpec(spec, type); }

private:
    void beginCommand();          ///< prepara o comando atual
    bool handleTextCommand(const core::EventCommand& c);
    bool handleRuntimeCommand(const core::EventCommand& c);
    bool handleFogCommand(const core::EventCommand& c);
    bool handleMoveRouteCommand(const core::EventCommand& c);
    bool handleFlowCommand(const core::EventCommand& c);
    bool handlePluginCommand(const core::EventCommand& c);
    void nextCommand();
    const core::EventCommand* current() const;
    /// Pula do `if` falso até o `senão`/`fim` correspondente (contando aninhamento).
    void skipToElseOrEnd();
    void skipToEnd();
    bool evaluateCondition(const core::EventCommand& c) const;
    bool evaluateConditionPredicate(const QVariantMap& params) const;
    void refreshChoiceConditions();
    void expireChoiceTimer();
    core::EventCommand resolveCommandValueFields(const core::EventCommand& command) const;
    int commonFrameIndex() const;
    QVariant commonValue(const QString& id) const;
    core::CommonValueType commonValueType(const QString& id, core::CommonValueType fallback = core::CommonValueType::Number) const;
    QVariant resolveValueSpec(const QVariantMap& spec, core::CommonValueType type) const;
    QVariant resolveValueSpecDepth(const QVariantMap& spec, core::CommonValueType type, int depth) const;
    QVariant resolveCommonSource(const QVariantMap& spec, core::CommonValueType type) const
    { return resolveValueSpec(spec, type); }
    QVariant coerceCommonValue(const QVariant& value, core::CommonValueType type) const;
    void applyValueTarget(const QVariantMap& target, const QVariant& value, core::CommonValueType type);
    bool pushCommonEvent(const core::CommonEvent& ce, const QVariantMap& argumentSpecs,
                         const QVariantMap& returnTarget, bool specsAreResolved = false);
    bool pushMapEvent(const core::MapEvent& ev, int pageIndex, const QString& mapId);
    bool pushInlineCommands(const QVector<core::EventCommand>& commands);
    QString activeEventId() const;
    QString activeMapId() const;
    void applyCommonReturn(const QVariantMap& target, const QVariant& value);
    void returnFromCommon(const QVariant& value);
    /// Resolve \v[n] no texto usando a memória da partida.
    QString resolveVariable(int id) const;
    QString resolveInputPrompts(QString text) const;
    QString resolveDataTokens(QString text) const;
    QString resolveInlineLocalization(QString text) const;
    void updateMessage(double dt, bool confirmEdge);
    void beginSubtitle(const core::EventCommand& c);
    /// Executa um comando `picture.*`. Devolve false se o tipo não for de
    /// imagem (aí o interpretador segue procurando).
    bool runPictureCommand(const core::EventCommand& c);
    void updateSubtitleWait(bool confirmEdge);
    void beginPage(int i);
    void finishChoice(int value);
    void completeChoice(int value);

    struct DatabaseEachState {
        QStringList recordIds;
        int position = 0;
        QVariantMap target;
    };
    struct RepeatState {
        int remaining = 0;
        int iteration = 0;
        QVariantMap indexTarget;
    };

    /// Um "quadro" de execução: uma lista de comandos e onde estamos nela.
    /// Existe pilha porque um evento comum pode chamar outro.
    struct Frame {
        QVector<core::EventCommand> cmds;
        int index = 0;
        QString commonEventId;
        int commonEventNumber = 0;
        QVariantMap commonValues;     ///< parâmetros + locais, indexados pelo ID estável
        QVariantMap returnTarget;     ///< destino no chamador/global
        QVariant returnValue;
        core::CommonValueType returnType = core::CommonValueType::Number;
        bool returnEnabled = false;
        QString source;                ///< origem de debug estável deste frame
        QString mapId;
        QString eventId;
        int pageIndex = -1;
        core::EventExecutionMode executionMode = core::EventExecutionMode::Normal;
        bool commonFrame = false;
        QHash<int, DatabaseEachState> databaseEachStates;
        QHash<int, RepeatState> repeatStates;
    };

    MessageStyle m_style;
    QVector<Frame> m_stack;
    QString m_eventId;
    InterpreterCommandDispatcher m_commandDispatcher;
    bool m_running = false;
    bool m_debugBlocked = false;
    QString m_debugSource;
    // Comandos instantâneos eram executados por recursão até a lista terminar.
    // Um salto para o próprio rótulo podia bloquear o jogo ou estourar a pilha.
    // O orçamento interrompe a cadeia e a retoma no próximo update.
    int  m_instantBudget = 0;
    bool m_yieldPending = false;
    bool m_skippingCutscene = false;
    int m_cutsceneSkipFrameIndex = -1;
    int m_cutsceneSkipEndIndex = -1;
    QString m_cutsceneSkipSource;
    static constexpr int kMaxInstantCommands = 128;
    GameState* m_state = nullptr;
    const core::Editor* m_ed = nullptr;
    const QString* m_inputController = nullptr;

    // estado do comando "message"
    QVector<TextPage> m_pages;
    int    m_pageIdx = 0;
    double m_charAcc = 0.0;       ///< letras acumuladas (fração)
    double m_waitSec = 0.0;       ///< pausa pendente (\. e \|)
    double m_dialogueFastForwardSpeed = 1.0;
    bool m_dialogueSkipMode = false;
    MessageView m_view;
    ChoiceView m_choice;

    // estado dos comandos de legenda
    SubtitleManager* m_subs = nullptr;
    PictureManager*  m_pics = nullptr;
    FogManager*      m_fogs = nullptr;
    MoveRouteStartHook m_moveRouteStartHook;
    MoveRouteStateHook m_moveRouteStateHook;
    MoveRouteControlHook m_moveRouteControlHook;
    RuntimeCommandHook m_runtimeCommandHook;
    LocaleChangedHook m_localeChangedHook;
    ConditionHook m_conditionHook;
    GameValueHook m_gameValueHook;
    TraceHook m_traceHook;
    DiagnosticTraceHook m_diagnosticTraceHook;
    DebugGateHook m_debugGateHook;
    ReserveCommonEventHook m_reserveCommonEventHook;
    ParallelBlockStartHook m_parallelBlockStartHook;
    ParallelBlockStateHook m_parallelBlockStateHook;
    ParallelBlockCancelHook m_parallelBlockCancelHook;
    UnknownCommandHook m_unknownCommandHook;
    MessageVoiceHook m_messageVoiceHook;
    DialogueHistoryHook m_dialogueHistoryHook;
    QSet<QString> m_reportedUnknownTypes;
    CommandAwaitable m_awaitable;
    QString m_lastCancellationReason = QStringLiteral("none");

    void awaitFrames(int frames);
    void awaitPicture(int target);
    void awaitMoveRoute(QString target, MoveRouteTicket ticket);
    void awaitRuntime(core::EventCommand command);
    void awaitSubtitles();
    void awaitCondition(core::EventCommand command, int timeoutFrames);
    void awaitParallelBlock(quint64 ticket, int resumeCommandIndex);
    bool updateAwaitable(double dt, bool confirmEdge);
    void clearAwaitable();
    void emitDiagnosticTrace(const QString& phase, const QVariantMap& details = {}) const;

    QVector<TypedChar> flatten(const TextPage& p) const;
};

} // namespace game
