#pragma once

#include <QString>

namespace core {

class Editor;

enum class EventExecutionOrigin {
    DetachedList,
    MapEvent,
    CommonEvent
};

/// Intenção formal da execução. Normal é o fluxo explícito; OnPageActivated
/// pertence ao lifecycle da página; Autorun/Parallel descrevem o scheduler
/// existente que iniciou o Interpreter (não criam um scheduler novo).
enum class EventExecutionMode { Normal, OnPageActivated, Autorun, Parallel };

QString eventExecutionModeId(EventExecutionMode mode);
EventExecutionMode eventExecutionModeFromId(const QString& id);

/// Identidade imutável do ponto em que um Event Command está sendo executado.
/// Centraliza o que antes era inferido separadamente por movimento, câmera,
/// Sprite FX, condições e comandos Ludo.
struct EventExecutionContext {
    EventExecutionOrigin origin = EventExecutionOrigin::DetachedList;
    EventExecutionMode mode = EventExecutionMode::Normal;
    QString source;
    QString mapId;
    QString mapEventId;
    QString commonEventId;
    int commonEventNumber = 0;
    int pageIndex = -1;
    int commandIndex = -1;
    int callDepth = 0;

    QString callerSource;
    QString callerMapId;
    QString callerMapEventId;
    QString callerCommonEventId;

    bool hasMapEvent() const { return !mapEventId.isEmpty(); }
    bool hasCommonEvent() const { return !commonEventId.isEmpty() || commonEventNumber > 0; }
    QString thisEventTarget() const {
        return hasMapEvent() ? QStringLiteral("event:") + mapEventId : QString();
    }
};

enum class EventTargetKind {
    Invalid,
    Player,
    MapEvent,
    Position
};

enum class EventTargetError {
    None,
    Empty,
    ThisEventUnavailable,
    InvalidSyntax,
    EventNotFound
};

struct EventTargetResolveOptions {
    bool allowPlayer = true;
    bool allowThisEvent = true;
    bool allowExplicitEvent = true;
    bool allowPosition = false;
    bool verifyExplicitEvent = true;
};

/// Resultado canônico compartilhado pelos consumidores de alvos de mundo.
/// `canonical` usa apenas "player", "event:<id>" ou "position".
struct EventTargetResolution {
    EventTargetKind kind = EventTargetKind::Invalid;
    EventTargetError error = EventTargetError::InvalidSyntax;
    QString requested;
    QString canonical;
    QString eventId;

    bool valid() const { return error == EventTargetError::None && kind != EventTargetKind::Invalid; }
};

EventTargetResolution resolveEventTarget(const QString& requested,
                                         const EventExecutionContext& context,
                                         const Editor* editor = nullptr,
                                         const EventTargetResolveOptions& options = {});

QString eventTargetErrorId(EventTargetError error);

} // namespace core
