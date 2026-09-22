#pragma once
#include <QString>

namespace ui {
enum class TeamConnectionState { Offline, Connecting, Online, Slow, Attention };
struct TeamSyncState {
    bool localChangesPending=false;
    bool operationsInFlight=false;
    bool structureDirty=false;
    bool checkpointRequired=false;
    bool roomConflict=false;
    bool resourceConflict=false;
    bool assetConflict=false;
    bool reconnectRequired=false;
    TeamConnectionState connection=TeamConnectionState::Offline;

    bool blocksPublication() const { return roomConflict||resourceConflict||assetConflict||reconnectRequired; }
    bool hasWork() const { return localChangesPending||operationsInFlight||structureDirty||checkpointRequired; }
    QString connectionId() const {
        switch(connection){
        case TeamConnectionState::Connecting:return QStringLiteral("reconnecting");
        case TeamConnectionState::Online:return QStringLiteral("online");
        case TeamConnectionState::Slow:return QStringLiteral("slow");
        case TeamConnectionState::Attention:return QStringLiteral("attention");
        default:return QStringLiteral("offline");
        }
    }
    void resetProject(){localChangesPending=false;operationsInFlight=false;structureDirty=false;checkpointRequired=false;roomConflict=false;resourceConflict=false;assetConflict=false;reconnectRequired=false;}
};
}
