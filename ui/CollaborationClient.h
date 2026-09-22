#pragma once
#include "core/Editor.h"
#include "TeamSyncState.h"
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>
class QWidget;
class QProgressDialog;
class QNetworkReply;
namespace ui {
class TeamRasterTransport;
class TeamConnection;
class CollaborationClient : public QObject {
    Q_OBJECT
public:
    explicit CollaborationClient(core::Editor& editor, QWidget* parent);
    bool attached() const;
    bool hasPending() const;
    bool connected() const { return !token.isEmpty(); }
    QString currentServerName() const { return serverName; }
    QString currentServerUrl() const { return server.toString(); }
    QString currentServerId() const { return serverId; }
    QString currentRole() const { return role; }
    QString teamProjectsRoot() const;
    bool loginToServer(const QUrl& url, const QString& user, const QString& password);
    bool listRemoteProjects(QJsonArray& projects);
    void listRemoteProjectsAsync(QObject* context,
                                 std::function<void(bool,const QJsonArray&,const QString&)> done);
    QJsonObject localProjectInfo(const QString& projectId, const QString& projectName,
                                 const QJsonObject& remote = QJsonObject()) const;
    bool openRemoteProject(const QString& projectId, const QString& projectName,
                           const QJsonObject& remote = QJsonObject());
    void connectDialog();
    void connectToServerDialog(const QString& suggestedAddress);
    void publish();
    void openRemote();
    bool synchronize();
    bool preparePublication(int& version);
    bool verifyPublication(int version);
    void history();
    void people();
    void disconnectServer();
    void detachProject();
signals:
    void projectReloaded();
    void connectionStateChanged(const QString& state);
    void statusChanged(const QString& text);
private:
    void setConnectionState(const QString& state);
    mutable TeamSyncState syncState;
    bool request(const QString& path, const QJsonObject& body, QJsonObject& result,
                 bool get = false, bool quiet = false,
                 const QString& progressText = QString(), int progressStart = -1,
                 int progressEnd = -1);
    QJsonObject snapshot() const;
    QJsonObject outgoing(const QJsonObject& current) const;
    bool apply(const QJsonObject& response, bool chooseFolder);
    void offerResumeCurrentProject();
    QString teamProjectPath(const QString& projectId, const QString& projectName) const;
    bool openLocalTeamCopy(const QString& path, const QJsonObject& link, const QJsonObject& remote);
    bool applyIncremental(const QJsonObject& delta, const QJsonObject& authoritativePayload,
                          const QJsonObject& response);
    bool saveLocalCopy();
    void persistLink();
    void heartbeat();
    void roomTick();
    void scheduleRoomFlush(int delayMs = 60);
    void ensureRoomWatch();
    void restartRoomWatch();
    void stopRoomWatch();
    bool exchangeRoomNow(bool showProgress = false);
    bool drainRealtimeForPublication(QString* error = nullptr, int timeoutMs = 120000);
    void captureHistoryChange();
    void scheduleMapStructureReconcile(int delayMs = 0);
    void reconcileMapStructure();
    bool mapKnownToServer(const QString& mapId) const;
    void scheduleStructureRefresh(int delayMs = 80);
    void syncStructureAsync();
    void initializeRoomTracking(const QJsonObject& response = QJsonObject());
    void queueRoomOperation(QJsonObject operation);
    bool applyRoomResponse(const QJsonObject& response);
    bool applyRoomOperation(const QJsonObject& envelope);
    void updateTeamBaseForMap(const QString& mapId);
    void showStatus(const QJsonObject& response);
    void beginProgress(const QString& title, const QString& text);
    void updateProgress(int value, const QString& text = QString());
    void endProgress();
    void markTeamPending();
    void postJsonAsync(const QString& path, const QJsonObject& body, int timeoutMs,
                       std::function<void(bool,int,const QJsonObject&,const QString&)> done);
    void scheduleAssetSync(const QStringList& changedPaths = QStringList(), int delayMs = 140);
    void syncAssetsAsync();
    void uploadNextAssetBlob();
    void sendAssetExchange();
    void applyAssetExchangeResponse(const QJsonObject& response);
    void finishAssetDownloads();
    void downloadNextAssetBlob();
    void scheduleResourceSync(int delayMs = 180);
    void syncResourcesAsync();
    void applyRemoteResources(const QJsonObject& resources, qint64 sequence, int serverRevision);
    bool prepareTeamTilesetAssets(QString* error = nullptr);
    void reloadTeamTilesetSources(const QStringList& changedPaths);
    QJsonObject currentResources() const;
    core::Editor& ed;
    QWidget* window;
    QNetworkAccessManager network; // downloads/legacy synchronous request; JSON async lives in TeamConnection
    TeamConnection* connection = nullptr;
    TeamRasterTransport* rasterTransport = nullptr;
    bool rasterDownloading = false;
    quint64 teamEpoch = 0;
    bool atlasPreparationReady = false;
    bool atlasPreparationPending = false;
    quint64 atlasPreparationRevision = 0;
    bool assetIndexPending = false;
    bool assetIndexReady = false;
    quint64 assetIndexRevision = 0;
    QTimer timer;
    QTimer roomFlushTimer;
    QTimer roomWatchRetryTimer;
    QTimer assetSyncTimer;
    QTimer resourceSyncTimer;
    QTimer mapStructureTimer;
    QTimer structureRefreshTimer;
    QUrl server;
    QString token, role, project, localPath, identity;
    QString serverName, serverId;
    int serverProtocol = 1;
    int revision = 0;
    int remoteRevision = 0;
    bool busy = false;
    bool heartbeatPending = false;
    bool roomRequestPending = false;
    bool roomWatchPending = false;
    QNetworkReply* roomWatchReply = nullptr;
    QString roomWatchMap;

    bool applyingRemote = false;
    bool assetDirty = false;
    bool assetFullScanRequested = false;
    bool assetRequestPending = false;
    bool assetStateInitialized = false;
    bool preserveLocalAssetsOnBootstrap = false;
    bool resourceDirty = false;
    bool resourceRequestPending = false;

    // Um conflito de sala nunca pode entrar em retry infinito. Mantemos as
    // operações locais preservadas e paramos o envio até uma reconciliação
    // consciente, em vez de avançar cursor ou descartar edição.

    bool structureRefreshPending = false;
    QString clientId;
    QJsonArray pendingRoomOperations;
    // Mapas que falharam ao aplicar uma operação remota pedem snapshot
    // autoritativo sem avançar o cursor.
    QSet<QString> pendingRoomResets;
    QSet<QString> roomRebaseMaps;
    // IDs originados por ESTA instância do Editor. Não usamos apenas clientId
    // para decidir se uma operação é nossa: duas janelas no mesmo computador
    // podem compartilhar as mesmas configurações persistentes.
    QSet<QString> originatedRoomOperationIds;
    QHash<QString, qint64> roomCursors;
    qint64 assetCursor = 0;
    qint64 pendingAssetCursor = 0;
    qint64 remoteAssetSeq = 0;
    qint64 resourceCursor = 0;
    QStringList pendingAssetPaths;
    QHash<QString,QJsonObject> sharedAssets;
    QHash<QString,QJsonObject> bootstrapLocalAssets;
    QSet<QString> bootstrapLocalKnownAssetIds;
    QSet<QString> bootstrapLocalMissingAssetIds;
    QJsonObject bootstrapLocalAssetDatabase;
    QJsonArray pendingAssetOperations;
    QVector<QJsonObject> pendingAssetUploads;
    int pendingAssetUploadIndex = 0;
    QVector<QJsonObject> pendingAssetDownloads;
    int pendingAssetDownloadIndex = 0;
    QHash<QString,QJsonObject> pendingAssetNextState;
    QJsonObject pendingResourceSnapshot;
    QString pendingResourceOperationId;
    QJsonObject deferredRemoteResources;
    qint64 deferredRemoteResourceSeq = 0;
    int deferredRemoteResourceRevision = 0;
    QHash<QString, int> observedHistoryPtr;
    QHash<QString, quint64> observedHistoryRevision;
    QHash<QString, QString> teamTilesetSources;
    QSet<QString> teamTilesetBackingIds;
    QProgressDialog* progress = nullptr;
    QJsonObject serverBase, localBase;
    QJsonArray lastPresence;
};
}
