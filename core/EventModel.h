// ============================================================================
//  EventModel.h — Eventos do mapa (NPCs, baús, teleportes…).
//
//  Estrutura inspirada no formato clássico de 256 cores: um evento fica numa célula do
//  mapa e tem uma ou mais PÁGINAS; cada página traz um gráfico, um gatilho,
//  um tipo de movimento e uma lista de comandos.
//
//  A interface edita páginas completas lado a lado: cada uma possui gráfico,
//  gatilho, condições e comandos próprios, todos persistidos no projeto.
// ============================================================================
#pragma once

#include "Model.h"
#include "EventExecutionContext.h"

#include <QImage>
#include <QPoint>
#include <QVariantMap>
#include <QStringList>

namespace core {

/// Quando a página do evento dispara.
enum class EventTrigger {
    ActionKey,     ///< o jogador aperta a tecla de ação de frente para o evento
    PlayerTouch,   ///< o jogador encosta no evento
    EventTouch,    ///< o evento encosta no jogador
    DirectionalSensor, ///< dispara ao entrar no alcance configurado por direção
    Autorun,       ///< roda sozinho e trava o jogo até terminar
    Parallel       ///< roda sozinho em paralelo
};

/// Índices canônicos do sensor: baixo, esquerda, direita, cima e diagonais.
/// Editor e runtime devem usar estas funções para que o preview represente
/// exatamente a área que dispara o evento.
int directionalSensorIndexForDelta(int dx, int dy);
bool directionalSensorContains(const QVector<int>& ranges, int dx, int dy);

/// Prioridade visual/colisao da pagina em relacao ao personagem.
enum class EventPriority {
    Below,   ///< abaixo do personagem; nao bloqueia passagem
    Same,    ///< mesmo nivel do personagem; participa da colisao
    Above    ///< acima do personagem; nao bloqueia passagem
};

/// Como o evento se move sozinho.
enum class EventMove { Fixed, Random, Approach, Custom };

/// Um passo de uma rota de movimento. `type` é fechado pela interface — não
/// existe comando de script arbitrário: a engine continua 100% no-code.
struct MoveCommand {
    QString     type;
    QVariantMap params;
};

/// Política quando um passo da rota não pode ser executado.
/// `Wait` preserva o comportamento histórico da LUDO/editores de RPG: tenta de novo
/// depois do intervalo da frequência. `Skip` corresponde ao antigo
/// “Ignorar se intransponível”. `Cancel` encerra a rota com diagnóstico.
enum class MoveRouteBlockedPolicy { Wait, Skip, Cancel };

/// O que fazer quando uma nova rota é enviada para um alvo que já possui rota.
/// Replace mantém a compatibilidade histórica. Queue permite sequenciar rotas
/// sem depender de eventos paralelos ou temporização manual.
enum class MoveRouteStartMode { Replace, Queue };

/// Comportamento do pathfinding estrutural da rota.
/// Reach chega a um destino e conclui; Follow permanece acompanhando o alvo;
/// Flee se afasta até a distância mínima; KeepDistance permanece dentro de
/// uma faixa. Todos compartilham o mesmo planejador/runtime.
enum class MoveRoutePathBehavior { Reach, Follow, Flee, KeepDistance };

/// Tipo de alvo de um comando pathfind.
enum class MoveRoutePathTargetKind { Cell, Player, SourceEvent, Event };

struct MoveRoutePathOptions {
    MoveRoutePathBehavior behavior = MoveRoutePathBehavior::Reach;
    MoveRoutePathTargetKind targetKind = MoveRoutePathTargetKind::Cell;
    QString eventId;
    QPoint cell{0,0};
    int minDistance = 0;      ///< tiles; usado por Flee/KeepDistance
    int maxDistance = 0;      ///< tiles; tolerância de Reach/Follow e teto de KeepDistance
    bool diagonal = true;
    bool continuous = false;  ///< Follow/KeepDistance usam true por padrão
    int maxSearchNodes = 4096;
};

QString moveRoutePathBehaviorId(MoveRoutePathBehavior behavior);
MoveRoutePathBehavior moveRoutePathBehaviorFromId(const QString& id);
QString moveRoutePathTargetKindId(MoveRoutePathTargetKind kind);
MoveRoutePathTargetKind moveRoutePathTargetKindFromId(const QString& id);
MoveRoutePathOptions moveRoutePathOptionsFromCommand(const MoveCommand& command);
QVariantMap moveRoutePathOptionsToParams(const MoveRoutePathOptions& options);

/// Rota personalizada no estilo editores de RPG.
/// Todos os defaults ficam aqui para que editor, ProjectIO e runtime não
/// inventem comportamentos diferentes quando um campo não existir.
struct MoveRoute {
    QString target = QStringLiteral("self");   ///< self | player | event:<id>
    QVector<MoveCommand> commands;
    bool repeat = true;
    MoveRouteBlockedPolicy blockedPolicy = MoveRouteBlockedPolicy::Wait;
    MoveRouteStartMode startMode = MoveRouteStartMode::Replace;
    bool waitForCompletion = false;
};

/// Aparência do evento no mapa.
struct EventGraphic {
    enum Kind {
        None,      ///< sem gráfico (evento invisível: gatilho de área, teleporte…)
        Tile,      ///< um tile do tileset
        Charset    ///< sprite/personagem em folha configurável (3x4, 4x4, multi-personagem…)
    };
    Kind    kind = None;
    TileRef tile;                        ///< kind == Tile
    QImage  charset;                     ///< kind == Charset
    /// Caminho relativo à pasta do projeto, quando veio do navegador Assets.
    QString sourcePath;
    /// Grade de UM personagem: quadros por direção × direções.
    int     charsetCols = 3, charsetRows = 4;
    /// Quantos personagens existem horizontal/verticalmente na mesma folha.
    int     characterCols = 1, characterRows = 1;
    int     characterIndex = 0;
    int     dir = 0;                     ///< 0=baixo, 1=esquerda, 2=direita, 3=cima
    int     frame = 1;                   ///< quadro parado (o do meio)
    int     opacity = 255;               ///< opacidade base da página (0..255)
    QString blend = QStringLiteral("normal"); ///< normal | add | multiply | screen

    bool isVisible() const { return kind != None; }
    int totalCols() const { return qMax(1, charsetCols) * qMax(1, characterCols); }
    int totalRows() const { return qMax(1, charsetRows) * qMax(1, characterRows); }
    int characterCount() const { return qMax(1, characterCols) * qMax(1, characterRows); }
    /// Retângulo do quadro dentro da folha, incluindo a escolha do personagem.
    QRect charsetFrameRect() const;
};

/// Um comando de página (mostrar mensagem, teleportar…).
/// Só o invólucro por enquanto: o interpretador vem na etapa seguinte.
struct EventCommand {
    QString     type;
    QVariantMap params;
    EventExecutionMode executionMode = EventExecutionMode::Normal;
    /// Metadados exclusivos do editor (grupo, cor e collapse). O runtime não
    /// interpreta este mapa e CommandSchema continua descrevendo só params.
    QVariantMap editorMetadata;
};

/// Uma página do evento.
struct EventPage {
    EventGraphic          graphic;
    /// Quando esta página vale (ver core::PageConditions em GameData.h).
    /// Fica como QVariantMap aqui para EventModel nao depender de GameData;
    /// quem interpreta é o runtime.
    QVariantMap           conditions;
    EventTrigger          trigger = EventTrigger::ActionKey;
    /// Alcance: baixo, esquerda, direita, cima e quatro diagonais. Zero desativa.
    QVector<int>          sensorRanges = QVector<int>(8, 0);
    EventPriority         priority = EventPriority::Same;
    EventMove             move = EventMove::Fixed;
    double                moveSpeed = 3.0;       ///< tiles por segundo
    int                   moveFrequency = 3;     ///< 1=lenta … 5=muito frequente
    MoveRoute             route;
    bool                  walkingAnimation = true;
    bool                  steppingAnimation = false;
    bool                  directionFixed = false;
    bool                  throughWall = false;   ///< atravessa paredes/eventos
    bool                  blocksPlayer = true;   ///< o jogador não passa por cima
    /// Cada deslocamento automático/rota anda meia célula em vez de uma.
    /// Mantém a velocidade em tiles/s: dois meios passos levam o mesmo tempo
    /// que um passo inteiro.
    bool                  halfStepMovement = false;
    /// Deslocamento visual vertical persistente em meias células: -1 = cima, 0 = normal, +1 = baixo.
    int                   halfTileVerticalOffset = 0;
    /// Sons de passos desta página. Vazio = resolver automaticamente pelo chão.
    bool                  footstepsEnabled = true;
    QString               footstepSurfaceId;
    int                   footstepVolume = 100;
    /// Compatibilidade histórica: controla se o JOGADOR mantém a direção usada
    /// na interação. Mantido no formato para não quebrar projetos antigos.
    bool                  keepPlayerFacingAfterInteract = true;
    /// RC2.68: "Voltar à posição inicial" pertence ao EVENTO.
    /// O runtime guarda posição em meia-célula, direção e frame antes da
    /// interação e restaura tudo ao terminar. Direção Fixa continua soberana.
    bool                  restoreEventFacingAfterInteract = true;
    QVector<EventCommand> commands;
};

/// Um evento posicionado no mapa.
struct MapEvent {
    QString            id = idGen();
    QString            name;
    QPoint             cell{ 0, 0 };
    QVector<EventPage> pages;            ///< sempre com ao menos uma

    MapEvent() { pages.push_back(EventPage()); }

    EventPage&       page(int i = 0);
    const EventPage& page(int i = 0) const;
};

/// Nomes legíveis, usados na interface e no arquivo de projeto.
QString eventTriggerId(EventTrigger t);
EventTrigger eventTriggerFromId(const QString& id);
QString eventTriggerLabel(EventTrigger t);

QString eventPriorityId(EventPriority p);
EventPriority eventPriorityFromId(const QString& id);
QString eventPriorityLabel(EventPriority p);

QString eventMoveId(EventMove m);
EventMove eventMoveFromId(const QString& id);
QString eventMoveLabel(EventMove m);

QString moveRouteBlockedPolicyId(MoveRouteBlockedPolicy policy);
MoveRouteBlockedPolicy moveRouteBlockedPolicyFromId(const QString& id);
QString moveRouteStartModeId(MoveRouteStartMode mode);
MoveRouteStartMode moveRouteStartModeFromId(const QString& id);

/// Lista canônica dos comandos que uma rota aceita. Runtime, testes e
/// validações usam esta mesma fonte para que um botão novo no editor não seja
/// esquecido no executor (ou vice-versa).
QStringList moveRouteCommandTypes();
bool isKnownMoveRouteCommandType(const QString& type);

QVariantMap moveRouteToMap(const MoveRoute& route);
MoveRoute moveRouteFromMap(const QVariantMap& map);

} // namespace core
