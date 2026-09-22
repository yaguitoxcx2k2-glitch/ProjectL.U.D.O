#pragma once

#include "core/Editor.h"

#include <QObject>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <functional>
#include <utility>

class QWidget;

namespace ui {
class CollaborationClient;

// Sincronização bidirecional segura da árvore de mapas LUDO <-> RPG Maker MV/MZ.
// Não instala/substitui plugins. Startup/link recebem primeiro o estado do RPG Maker
// sem regravar arquivos; MapInfos só é alterado após mudança local real.
// Mapas MV/MZ existentes preservam suas camadas nativas ao serem salvos pelo LUDO.
class RpgMakerProjectSync final : public QObject
{
public:
    explicit RpgMakerProjectSync(core::Editor& editor, QWidget* owner, QObject* parent = nullptr);

    bool isLinked() const;
    QString projectRoot() const;

    bool linkProjectInteractive();
    bool restoreLinkedProject();
    bool synchronizeNow(bool importNewMaps = true);

    void publishProject(CollaborationClient* team);
    bool saveAllDirtyMaps();
    bool hasPendingStructure() const;

    // Exclusões também são diferidas: apenas entram no RPG Maker quando o usuário
    // confirma uma sincronização explícita.
    bool deleteMaps(const QVector<int>& rpgMakerMapIds, QString* error = nullptr);

    // Mantém o nome histórico da API, mas agora apenas MARCA a estrutura como
    // pendente. Nunca escreve MapInfos/MapXXX automaticamente.
    void scheduleStructurePush();

    void setStatusHandler(std::function<void(const QString&)> fn) { m_status = std::move(fn); }
    void setModelChangedHandler(std::function<void()> fn) { m_modelChanged = std::move(fn); }

private:
    core::Editor& ed;
    QWidget* m_owner = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_externalDebounce;
    bool m_syncing = false;
    QByteArray m_lastWrittenMapInfosHash;
    QByteArray m_lastStructureSignature;
    QHash<int, QByteArray> m_seenMapHashes;
    QSet<int> m_pendingExternalMapIds;
    std::function<void(const QString&)> m_status;
    std::function<void()> m_modelChanged;

    void notifyStatus(const QString& text) const;
    void notifyModelChanged() const;
    void armWatcher();
    void onExternalChange();
    QByteArray fileHash(const QString& path) const;
    void refreshDiskBaseline();

    bool loadMapInfos(QJsonArray& infos, QString* error = nullptr) const;
    bool writeMapInfos(const QJsonArray& infos, QString* error = nullptr);
    QByteArray structureSignature() const;
    QByteArray rpgMakerStructureSignature(const QJsonArray& infos) const;
    void refreshPendingStructureState(const QJsonArray* knownInfos = nullptr);
    int firstFreeMapId(const QJsonArray& infos, const QSet<int>& reserved) const;

    bool pullStructure(bool importNewMaps, QString* error = nullptr);
    bool pushStructure(QString* error = nullptr);
    bool ensureLinked(QString* error = nullptr);
    bool persistProjectContainer(QString* error = nullptr);

    core::MapDoc importRpgMakerMap(int rpgMakerMapId, const QJsonObject& info, QString* error = nullptr) const;
    void importRegions(core::MapDoc& doc, const QJsonObject& rpgMakerMap) const;
};

} // namespace ui
