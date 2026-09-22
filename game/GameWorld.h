// ============================================================================
//  GameWorld.h — Núcleo do runtime: o mundo jogável construído a partir do
//  documento aberto no editor.
//
//  Esta classe NÃO conhece interface nenhuma (nem widgets, nem janela): só
//  lê o `core::Editor` e resolve colisão e movimento. É assim que ela pode ser
//  testada sem abrir tela — e é o que garante que o runtime não interfere no
//  editor: aqui nada é escrito, só lido.
//
//  Movimento no estilo editores de RPG: o personagem anda de célula em célula, com
//  interpolação suave entre elas, e "vira" no lugar quando esbarra.
//
//  A partir da "Configuração de Personagem" tudo isto é ajustável:
//    · passo de 1 célula ou de 0,5 célula (meia-célula);
//    · caminhada em 4 ou em 8 direções (com diagonais);
//    · hitbox de 1×1 célula ou de 1×0,5 (só os pés);
//    · animação de 3 ou 5 quadros, em vaivém ou em ciclo.
//
//  Para dar conta do meio-passo, TODA a posição interna é medida em
//  MEIAS-CÉLULAS (a unidade "hu" daqui em diante): a célula 3 começa em 6 hu.
//  Assim não existe posição fracionária escondida e a colisão continua sendo
//  aritmética inteira — nada de erro de arredondamento atravessando parede.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/RuntimePreloadCache.h"
#include "game/MoveRouteRuntime.h"

#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QHash>
#include <QJsonObject>

#include <functional>
#include <optional>
#include <utility>

namespace game {

/// Direções. As quatro primeiras são as clássicas (a ordem das linhas do
/// charset do editores de RPG); as diagonais vêm depois para não mexer no que já
/// existia.
enum class Dir { Down, Left, Right, Up, DownLeft, DownRight, UpLeft, UpRight };

/// Direção a partir de um par de entradas -1/0/1 (dy > 0 = para baixo).
Dir  dirFromDelta(int dx, int dy);
/// Vetor -1/0/1 correspondente à direção.
QPoint deltaFromDir(Dir d);
bool isDiagonal(Dir d);
/// Direção cardeal equivalente (diagonal → o componente horizontal, como faz
/// o editores de RPG quando o charset só tem 4 linhas).
Dir  cardinalOf(Dir d);
/// Linha do charset para a direção, conforme o tipo de padrão de sprite.
/// 4 direções: 0 baixo, 1 esquerda, 2 direita, 3 cima.
/// 8 direções em 8 linhas: as mesmas 4 e, em seguida,
///             4 baixo-esquerda, 5 baixo-direita, 6 cima-esquerda, 7 cima-direita.
int  charsetRow(Dir d, core::Editor::PlayerSettings::SpriteDirs dirs);

/// Onde ficam os quadros de uma direção dentro da folha.
struct CharsetCell {
    int row = 0;         ///< linha da folha
    int colOffset = 0;   ///< primeira coluna dos quadros desta direção
};

/// Resolve linha e coluna inicial para qualquer arranjo de folha, inclusive o
/// **3 normais + 3 diagonais na mesma linha**, em que cada linha guarda a
/// direção cardeal e a diagonal que fica entre ela e a próxima do ciclo
/// baixo → esquerda → cima → direita:
///
/// | linha | quadros 0..n-1 | quadros n..2n-1 |
/// |---|---|---|
/// | 0 | baixo    | baixo-esquerda |
/// | 1 | esquerda | cima-esquerda  |
/// | 2 | direita  | baixo-direita  |
/// | 3 | cima     | cima-direita   |
CharsetCell charsetCellFor(Dir d, const core::Editor::PlayerSettings& ps);
/// Quadro da animação para um "progresso" em meias-células caminhadas.
/// `cols` é 3 ou 5; `order` decide entre vaivém (0,1,2,1) e ciclo (0,1,2).
int  animFrameForPhase(double halfStepsWalked, int cols,
                       core::Editor::PlayerSettings::AnimOrder order);
/// Quadro usado quando o personagem está parado (o do meio).
int  idleFrameFor(int cols);

struct PlayerConfig {
    /// Quantas células o personagem percorre por segundo.
    double tilesPerSecond = 5.0;
    /// Caminhada em 8 direções (permite diagonal).
    bool   diagonal = false;
    /// Passo de meia célula.
    bool   halfStep = false;
    /// Hitbox 1×0,5 (metade de baixo da célula). Só vale com meio-passo.
    bool   hitboxHalf = false;
    /// Colunas de animação (3, 5, 6…) e a ordem em que são percorridas.
    int    animCols = 3;
    /// Coluna usada com o personagem parado.
    int    idleCol = 1;
    core::Editor::PlayerSettings::AnimOrder animOrder =
        core::Editor::PlayerSettings::PingPong;

    /// Copia tudo o que o runtime precisa da configuração do projeto.
    void from(const core::Editor::PlayerSettings& ps);
};


struct MoveRouteDebugEntry {
    QString target;              ///< player, event:<id> ou event:<id>:custom
    QString eventId;             ///< vazio para jogador
    QString state;               ///< idle/running/paused/autonomous/...
    MoveRouteTicket ticket = 0;
    int commandIndex = 0;
    QString commandType;
    int queuedCount = 0;
    int blockedAttempts = 0;
    QString lastFailure;
    QPoint actorPositionHU;
    bool hasPathTarget = false;
    QPoint pathTargetHU;
    QVector<QPoint> pathNodesHU;
    int pathReplans = 0;
    int pathExpandedNodes = 0;
    int pathExpandedThisTick = 0;
    QString pathStatus;
};

class World
{
public:
    explicit World(const core::Editor& ed);

    /// Recomeça com o personagem na célula indicada (ou na livre mais próxima).
    void reset(const QPoint& startCell);
    /// Restaura a posição exata de um save (inclusive meio passo) e a direção.
    /// Se o mapa mudou e a posição ficou inválida, usa a célula livre mais próxima.
    void restorePlayer(const QPoint& halfCell, Dir facing);

    // ---- consultas do mundo ---------------------------------------------
    /// União das máscaras de colisão de todos os tiles visíveis na célula.
    /// Fora do mapa devolve "bloqueado dos quatro lados".
    int cellMask(int gx, int gy) const;

    /// A célula é intransponível por qualquer lado? (usado para escolher o
    /// ponto de partida)
    bool isBlocked(int gx, int gy) const;

    /// Dá para sair de (gx,gy) na direção `d` e entrar na célula vizinha?
    /// Segue a regra de aresta do editores de RPG: a passagem é barrada se o lado de
    /// saída da célula atual OU o lado de entrada da vizinha estiver bloqueado.
    /// (Só aceita direções cardeais; diagonal devolve a combinação das duas.)
    bool canMove(int gx, int gy, Dir d) const;

    /// Célula livre mais próxima de `wanted` (busca em espiral).
    QPoint findFreeCellNear(const QPoint& wanted) const;

    // ---- eventos ---------------------------------------------------------
    /// Existe nesta célula um evento que barra o jogador?
    const core::MapEvent* blockingEventAt(int gx, int gy) const;
    /// Células que a hitbox ocupa agora (tiles inteiros que ela toca).
    QVector<QPoint> occupiedCells() const;
    /// Células logo à frente do personagem — TODAS as que a hitbox toca na
    /// direção do olhar. Com passo de meia célula o personagem pode estar
    /// entre duas colunas/linhas: sem isso, ficar de frente para o NPC e
    /// "não acontecer nada" seria a regra, não a exceção.
    QVector<QPoint> cellsInFront() const;
    /// Primeiro evento à frente cujo gatilho é a tecla de ação.
    const core::MapEvent* eventInFront() const;
    /// Retira o próximo gatilho de contato detectado pelo movimento do jogador
    /// ou por um evento com "Toque do evento".
    QString takeTouchEvent() { const QString id=m_pendingTouchEvent; m_pendingTouchEvent.clear(); return id; }

    /// Resolve a página ativa de cada evento. Sem resolver, usa página 0 para
    /// manter World utilizável nos testes isolados; GameSession liga isto ao
    /// GameState (condições, switches e variáveis).
    void setEventPageResolver(std::function<int(const core::MapEvent&)> resolver)
    { m_pageResolver = std::move(resolver); }
    void setEventSwitchHook(std::function<void(int,bool)> hook) { m_switchHook = std::move(hook); }
    void setEventSoundHook(std::function<void(const QString&,int)> hook) { m_soundHook = std::move(hook); }
    /// Disparado quando o ator completa um stride caminhável de 1 tile.
    /// Em half-step, dois movimentos de 0,5 tile compõem um único footstep.
    /// eventId vazio = jogador. O World não conhece áudio nem superfícies.
    using FootstepHook = std::function<void(const QString&, const QPointF&)>;
    void setFootstepHook(FootstepHook hook) { m_footstepHook = std::move(hook); }
    void setRememberEventPositionHook(std::function<void(const QString&,const QPoint&)> hook)
    { m_rememberEventPositionHook = std::move(hook); }
    /// Bloco I: o World continua sem possuir o GameState. A sessão injeta as
    /// consultas de mapa Runtime para colisão sem acoplar as duas classes.
    void setRuntimeMapCellResolver(std::function<core::Cell(const QString&,int,int)> resolver)
    { m_runtimeMapCellResolver = std::move(resolver); }
    void setRuntimePassageResolver(std::function<int(int,int,int)> resolver)
    { m_runtimePassageResolver = std::move(resolver); }
    void setRuntimeMapRevisionResolver(std::function<quint64()> resolver)
    { m_runtimeMapRevisionResolver = std::move(resolver); }
    /// Preferência de acessibilidade já resolvida pela sessão. Mantê-la aqui
    /// evita consultar QSettings/registro sempre que calculamos a posição visual.
    void setReduceShake(bool on) { m_reduceShake = on; }

    struct EventActorView {
        QPoint cell;
        QPointF pixel;
        core::EventGraphic graphic;
        int page = -1;
        int opacity = 255;
        QString blend = QStringLiteral("normal");
        bool transparent = false;
        bool through = false;
        bool blocksPlayer = true;
        core::EventPriority priority = core::EventPriority::Same;
    };

    /// Diagnóstico somente-leitura dos caches derivados introduzidos no O4.
    /// Nenhum destes valores faz parte do save/projeto.
    struct SpatialCacheDiagnostics {
        int collisionCells = 0;
        int indexedEventCells = 0;
        int indexedEventEntries = 0;
        quint64 collisionRebuilds = 0;
        quint64 eventIndexRebuilds = 0;
        quint64 eventIndexMoves = 0;
    };
    SpatialCacheDiagnostics spatialCacheDiagnostics() const;
    /// RC2.53: exporta somente caches derivados do mapa atual para o preload
    /// do Editor. Nenhum estado de gameplay/persistência atravessa esta fronteira.
    core::RuntimeMapDerivedCache derivedCacheSnapshot() const;

    bool eventView(const core::MapEvent& e, EventActorView* out) const;
    /// Faz o evento ativo olhar para o jogador, a menos que Direcao fixa esteja ativa.
    bool faceEventTowardPlayer(const QString& eventId);
    /// Lê direção/frame vivos do evento sem alterar sua posição. Usado pela
    /// interação RC2.66 para restaurar a pose original ao terminar.
    bool eventFacing(const QString& eventId, int* direction, int* frame) const;
    /// Restaura somente direção/frame de um evento vivo. A célula/rota não é
    /// alterada; se o evento não existir mais (ex.: troca de mapa), retorna false.
    bool restoreEventFacing(const QString& eventId, int direction, int frame);
    QPoint eventCell(const QString& eventId) const;
    /// Posição precisa do evento vivo em meias-células (HU).
    QPoint eventHalfCell(const QString& eventId) const;
    /// Aplica uma posição persistida em meias-células e atualiza o índice espacial.
    bool restoreEventPosition(const QString& eventId, const QPoint& halfCell);
    /// Estado mutável do mapa usado pelo save: posições/direções, aparência e
    /// runtimes de rota. Movimento físico é estabilizado e o comando corrente
    /// é retomado de modo determinístico após o load.
    QJsonObject runtimeState() const;
    void restoreRuntimeState(const QJsonObject& object);

    QSize mapSizeInCells() const;

    // ---- laço do jogo ----------------------------------------------------
    /// Avança o mundo em `dt` segundos. `inputDx/inputDy` são -1, 0 ou 1.
    void update(double dt, int inputDx, int inputDy);

    // ---- estado do personagem -------------------------------------------
    /// Célula (inteira) onde está o canto superior-esquerdo do personagem.
    QPoint  playerCell()  const;
    /// Posição em meias-células — precisa quando o passo é 0,5.
    QPoint  playerHalfCell() const { return QPoint(m_hx, m_hy); }
    /// Posição em pixels do mapa (canto superior-esquerdo do personagem).
    QPointF playerPixel() const;
    QPointF playerVisualPixel() const;
    /// Inicia uma rota e devolve um ticket estável. Ticket 0 = alvo/rota inválido.
    /// O ticket permite que “Esperar terminar” acompanhe exatamente a rota
    /// criada, mesmo quando existem filas ou substituições posteriores.
    MoveRouteTicket startMoveRoute(const QString& target, const core::MoveRoute& route,
                                   const QString& sourceEventId = QString());
    bool setMoveRoute(const QString& target, const core::MoveRoute& route,
                      const QString& sourceEventId = QString())
    { return startMoveRoute(target, route, sourceEventId) != 0; }
    MoveRouteTicketState moveRouteTicketState(const QString& target, MoveRouteTicket ticket) const;
    bool moveRouteRunning(const QString& target) const;
    bool controlMoveRoute(const QString& target, MoveRouteControlAction action);
    void cancelAllMoveRoutes();
    /// Estado resumido das rotas para diagnóstico e depuração in-game.
    QJsonObject moveRouteDiagnostics() const;
    /// Snapshot tipado para Preview/Debugger. É somente leitura e expõe o
    /// mesmo MoveRouteRuntime usado IN-GAME; não existe simulador paralelo.
    QVector<MoveRouteDebugEntry> moveRouteDebugEntries() const;
    /// Retângulo de colisão atual, em meias-células (x, y, largura, altura).
    QRect   hitboxHalfCells() const;
    Dir     facing()      const { return m_dir; }
    void    setFacing(Dir d) { m_dir = d; }
    bool    isMoving()    const { return m_moving; }
    int     stepsTaken()  const { return m_steps; }
    bool    playerTransparent() const { return m_playerTransparent; }
    int     playerOpacity() const { return m_playerOpacity; }
    QString playerBlend() const { return m_playerBlend; }
    const core::EventGraphic* playerGraphicOverride() const
    { return m_playerHasGraphicOverride ? &m_playerGraphicOverride : nullptr; }
    /// Quadro da animação de caminhada, já respeitando padrão e ordem.
    int     animFrame() const;
    /// Progresso 0..1 dentro do passo atual.
    double  stepProgress() const { return m_moving ? m_t : 0.0; }

    PlayerConfig config;

private:
    /// Altura da hitbox em meias-células (2 = 1 célula, 1 = meia célula).
    int  hbHeight() const { return (config.hitboxHalf && config.halfStep) ? 1 : 2; }
    /// Deslocamento vertical da hitbox dentro do quadro do personagem.
    int  hbOffsetY() const { return 2 - hbHeight(); }   // meia hitbox fica nos pés
    /// Passo em meias-células (1 = 0,5 célula, 2 = célula inteira).
    int  stepHU() const { return config.halfStep ? 1 : 2; }

    /// Dá para sair de (hx,hy) andando (dhx,dhy) meias-células? Só cardeal.
    bool canStepFrom(int hx, int hy, int dhx, int dhy) const;
    /// Versão que aceita diagonal (exige que os dois caminhos em "L" estejam
    /// livres — é o que impede o personagem de cortar quina de parede).
    bool canWalk(int hx, int hy, int dhx, int dhy) const;
    bool tryStartMove(int dx, int dy);
    /// Acumula distância de caminhada e informa quando um novo "stride" de
    /// 1 tile foi completado. Isso mantém a mesma cadência sonora tanto em
    /// movimento de 1 tile quanto em meio-passo de 0,5 tile.
    static bool advanceFootstepCadence(double& accumulatedTiles, double walkedTiles);

    struct LiveEvent {
        QPointF cell, from, to;              ///< posição em tiles; aceita 0,5
        double progress = 0.0;
        bool moving = false;
        bool jumping = false;
        int page = -2;
        int dir = 0, frame = 1;
        double animClock = 0.0;              ///< relógio da animação parada/stepping
        double animPhase = 0.0;              ///< fase acumulada da caminhada, em meias-células
        double footstepDistanceTiles = 0.0;  ///< distância caminhada desde o último som de passo
        /// Rota da própria página e rota forçada são independentes. Isso evita
        /// que uma rota de comando destrua o cursor da movimentação autônoma.
        MoveRouteRuntime customRouteRuntime;
        MoveRouteRuntime forcedRouteRuntime;
        enum class MovementOwner { None, Autonomous, CustomRoute, ForcedRoute } movementOwner = MovementOwner::None;
        double autonomousCooldown = 0.0;
        bool walkingAnimation = true, steppingAnimation = false;
        bool directionFixed = false, through = false, transparent = false;
        bool blocksPlayer = true;
        double speed = 3.0;
        int frequency = 3, opacity = 255;
        QString blend = QStringLiteral("normal");
        bool hasGraphicOverride = false;
        core::EventGraphic graphicOverride;
        double shakeX = 0.0, shakeY = 0.0, shakeRemaining = 0.0, shakePhase = 0.0;
    };

    void resetEvents();
    void updateEvents(double dt);

    // O4 — caches espaciais derivados. A colisão estática vira lookup O(1) e
    // consultas locais de eventos deixam de percorrer o vetor inteiro.
    int  computeCellMaskUncached(int gx, int gy) const;
    void ensureCollisionGridCurrent() const;
    void rebuildCollisionGrid() const;

    void ensureEventSpatialIndexCurrent() const;
    void rebuildEventSpatialIndex() const;
    bool seedCollisionCacheFromPreload() const;
    bool seedEventIndexFromPreload() const;
    int  eventSpatialKey(const QPoint& cell) const;
    void syncEventSpatialCell(const QString& eventId, const QPoint& cell);
    void removeEventFromSpatialIndex(const QString& eventId);
    const QVector<QString>* eventIdsAtCell(const QPoint& cell) const;
    const core::MapEvent* eventDefinition(const QString& eventId) const;
    bool startEventMove(const core::MapEvent& ev, LiveEvent& s, const core::EventPage& pg,
                        int dx, int dy);
    bool canEventPathMoveFrom(const core::MapEvent& ev, const LiveEvent& s,
                              const core::EventPage& pg, const QPoint& fromHU,
                              int dx, int dy) const;
    std::optional<QPoint> resolveMovePathTargetHU(const core::MoveRoutePathOptions& options,
                                                  const QString& sourceEventId) const;
    void runEventRoute(const core::MapEvent& ev, LiveEvent& s, const core::EventPage& pg,
                       MoveRouteRuntime& runtime, LiveEvent::MovementOwner owner);
    double eventDelay(int frequency) const;
    MoveRouteTicket allocateMoveRouteTicket();

    const core::Editor& ed;
    std::function<int(const core::MapEvent&)> m_pageResolver;
    std::function<void(int,bool)> m_switchHook;
    std::function<void(const QString&,int)> m_soundHook;
    FootstepHook m_footstepHook;
    std::function<void(const QString&,const QPoint&)> m_rememberEventPositionHook;
    std::function<core::Cell(const QString&,int,int)> m_runtimeMapCellResolver;
    std::function<int(int,int,int)> m_runtimePassageResolver;
    std::function<quint64()> m_runtimeMapRevisionResolver;
    bool m_reduceShake = false;
    QHash<QString, LiveEvent> m_events;

    // Caches derivados: nunca serializados. `mutable` permite que consultas
    // const façam lazy rebuild quando Editor::mapRevision() mudar.
    mutable QSize m_collisionGridSize;
    mutable QVector<quint8> m_collisionGrid;
    mutable quint64 m_collisionRevision = 0;
    mutable quint64 m_collisionRuntimeRevision = 0;
    mutable quint64 m_collisionRebuilds = 0;

    mutable QHash<int, QVector<QString>> m_eventSpatialIndex;
    mutable QHash<QString, QPoint> m_eventIndexedCells;
    mutable QHash<QString, int> m_eventDefinitionIndex;
    mutable quint64 m_eventIndexRevision = 0;
    mutable quint64 m_eventIndexRebuilds = 0;
    quint64 m_eventIndexMoves = 0;

    QString m_pendingTouchEvent;
    int activePage(const core::MapEvent& e) const
    { return m_pageResolver ? m_pageResolver(e) : (e.pages.isEmpty() ? -1 : 0); }
    int     m_hx = 0, m_hy = 0;         ///< posição atual em meias-células
    int     m_fromHx = 0, m_fromHy = 0;
    int     m_toHx = 0, m_toHy = 0;
    double  m_t = 0.0;          ///< progresso 0..1 entre `from` e `to`
    bool    m_moving = false;
    Dir     m_dir = Dir::Down;
    int     m_steps = 0;
    double  m_animPhase = 0.0;  ///< meias-células caminhadas (acumulado)
    double  m_playerFootstepDistanceTiles = 0.0; ///< distância desde o último footstep

    MoveRouteRuntime m_playerRouteRuntime;
    MoveRouteTicket m_nextMoveRouteTicket = 1;
    double m_playerShakeX = 0.0, m_playerShakeY = 0.0;
    double m_playerShakeRemaining = 0.0, m_playerShakePhase = 0.0;
    double m_playerAnimClock = 0.0;
    int m_playerRouteFrequency = 3;
    int m_playerOpacity = 255;
    bool m_playerWalkingAnimation = true, m_playerSteppingAnimation = false;
    bool m_playerDirectionFixed = false, m_playerThrough = false;
    bool m_playerTransparent = false, m_playerJumping = false;
    QString m_playerBlend = QStringLiteral("normal");
    bool m_playerHasGraphicOverride = false;
    core::EventGraphic m_playerGraphicOverride;
    void updatePlayerRoute(double dt, int* dx, int* dy);
};

} // namespace game
