// ============================================================================
//  GameState.h — A memória da partida: interruptores, variáveis e switches
//  próprios de cada evento.
//
//  Nasce dos valores iniciais do projeto e vive SÓ enquanto o jogo roda. É o
//  que permite "o baú abre uma vez só", "a ponte caiu", "você tem 3 chaves" —
//  sem nunca escrever no projeto (a regra de ouro do runtime).
//
//  Sem interface e sem Qt Widgets: dá para testar a lógica inteira de uma
//  cutscene sem abrir tela.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "game/pure/GameStateStore.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace game {

/// Estado mutável de um personagem durante a partida. Os atributos-base
/// continuam no banco de dados; aqui ficam apenas progresso e equipamento.
struct PartyMemberState
{
    QString actorId;
    int level = 1;
    int experience = 0;
    int hp = 1;
    int mp = 0;
    QString weaponId;
    QString armorId;
    QString accessoryId;
    QStringList states;
    QHash<QString,int> stateTurns; ///< turnos restantes; 0 = sem expiração automática
};

struct QuestState
{
    QString status = QStringLiteral("active"); // active, completed, failed
    int progress = 0;
    int target = 1;
};

class GameState
{
public:
    /// Contrato RC2.59: inicia uma PARTIDA nova. Tudo que pertence ao save
    /// volta aos defaults do projeto; nenhum valor mutável da partida anterior
    /// atravessa Novo Jogo.
    void resetForNewGame(const core::Editor& ed);

    /// Nome legado mantido por compatibilidade interna. Equivale a
    /// resetForNewGame(); não representa estado de perfil/persistente.
    void resetFrom(const core::Editor& ed);

    /// Contrato RC2.59: reconcilia um save carregado com a versão atual do
    /// projeto. Preserva valores gravados e adiciona somente símbolos/runtime
    /// DB que surgiram depois do save.
    void reconcileLoadedSave(const core::Editor& ed);

    // Nome legado mantido para callers antigos.
    void ensureGlobalsFrom(const core::Editor& ed);

    // ---- interruptores ----------------------------------------------------
    bool switchOn(int id) const { return m_pureState.switchOn(id); }
    void setSwitch(int id, bool on) { m_pureState.setSwitch(id, on); }
    void toggleSwitch(int id) { m_pureState.setSwitch(id, !switchOn(id)); }

    // ---- variáveis --------------------------------------------------------
    int  variable(int id) const { return m_pureState.variable(id); }
    void setVariable(int id, int valor) { m_pureState.setVariable(id, valor); }

    // ---- strings globais ---------------------------------------------------
    QString stringValue(int id) const
    {
        const std::string raw = m_pureState.stringValue(id);
        return QString::fromUtf8(raw.data(), int(raw.size()));
    }
    void setStringValue(int id, const QString& value) { if (id > 0) m_pureState.setStringValue(id, value.toUtf8().toStdString()); }


    // ---- bancos de dados personalizados -----------------------------------
    /// Runtime DB nasce de uma cópia dos valores do projeto. ReadOnly nunca
    /// é duplicado: a leitura usa diretamente o projeto.
    void resetCustomDatabasesFrom(const core::Editor& ed);
    void ensureCustomDatabasesFrom(const core::Editor& ed);
    QVariant customDatabaseValue(const core::Editor& ed, const QString& databaseId,
                                 const QString& recordId, const QString& fieldId) const;
    bool setCustomDatabaseValue(const core::Editor& ed, const QString& databaseId,
                                const QString& recordId, const QString& fieldId,
                                const QVariant& value, QString* error = nullptr);
    bool resetCustomDatabaseRecord(const core::Editor& ed, const QString& databaseId,
                                   const QString& recordId, QString* error = nullptr);
    bool copyCustomDatabaseRecord(const core::Editor& ed, const QString& databaseId,
                                  const QString& sourceRecordId, const QString& targetRecordId,
                                  QString* error = nullptr);
    QString findCustomDatabaseRecord(const core::Editor& ed, const QString& databaseId,
                                     const QString& fieldId, const QString& operation,
                                     const QVariant& needle) const;
    int customDatabaseRecordCount(const core::Editor& ed, const QString& databaseId) const;
    bool customDatabaseRecordExists(const core::Editor& ed, const QString& databaseId,
                                    const QString& recordId) const;
    QStringList customDatabaseRecordIds(const core::Editor& ed, const QString& databaseId) const;

    // ---- mapa mutável em runtime (RC2.47 / Bloco I) ------------------------
    /// Retorna a célula efetiva de uma camada: projeto + override Runtime +
    /// remapeamento de tileset. O projeto nunca é modificado durante o jogo.
    core::Cell runtimeMapCell(const core::Editor& ed, const QString& mapId,
                              const QString& layerId, int x, int y) const;
    bool runtimeMapCellChanged(const QString& mapId, const QString& layerId, int x, int y) const;
    bool setRuntimeMapTile(const core::Editor& ed, const QString& mapId, const QString& layerId,
                           int x, int y, const QString& tilesetId, int tx, int ty,
                           bool clearCell = false, QString* error = nullptr);
    bool fillRuntimeMapArea(const core::Editor& ed, const QString& mapId, const QString& layerId,
                            int x, int y, int width, int height, const QString& tilesetId,
                            int tx, int ty, bool clearCell = false, QString* error = nullptr);
    bool copyRuntimeMapArea(const core::Editor& ed, const QString& mapId,
                            const QString& sourceLayerId, int sourceX, int sourceY,
                            int width, int height, const QString& targetLayerId,
                            int targetX, int targetY, QString* error = nullptr);

    /// Passage usa a mesma máscara SideTop/Right/Bottom/Left do Editor.
    /// -1 = herdar do mapa/tiles; 0 = totalmente livre; 15 = bloqueado.
    int runtimeMapPassageMask(const QString& mapId, int x, int y, int inheritedMask) const;
    bool runtimeMapHasPassageOverride(const QString& mapId, int x, int y) const;
    bool setRuntimeMapPassage(const core::Editor& ed, const QString& mapId, int x, int y,
                              int mask, QString* error = nullptr);
    bool resetRuntimeMapPassage(const QString& mapId, int x, int y);

    int runtimeMapTerrain(const QString& mapId, int x, int y, int fallback = 0) const;
    bool runtimeMapHasTerrainOverride(const QString& mapId, int x, int y) const;
    bool setRuntimeMapTerrain(const core::Editor& ed, const QString& mapId, int x, int y,
                              int terrainId, QString* error = nullptr);

    /// A LUDO permite vários tilesets no mesmo mapa. Em vez de uma propriedade
    /// singular, o runtime faz remap estável sourceTilesetId -> targetTilesetId.
    bool setRuntimeMapTilesetRemap(const core::Editor& ed, const QString& mapId,
                                   const QString& sourceTilesetId, const QString& targetTilesetId,
                                   QString* error = nullptr);
    QString runtimeMapTilesetRemap(const QString& mapId, const QString& sourceTilesetId) const;
    bool resetRuntimeMapTilesetRemap(const QString& mapId, const QString& sourceTilesetId);

    bool resetRuntimeMapCell(const QString& mapId, const QString& layerId, int x, int y,
                             bool resetPassage = true, bool resetTerrain = true);
    int resetRuntimeMapArea(const core::Editor& ed, const QString& mapId, int x, int y,
                            int width, int height);
    int resetRuntimeMap(const QString& mapId);
    bool runtimeMapChangedAt(const QString& mapId, int x, int y) const;
    int runtimeMapOverrideCount(const QString& mapId = QString()) const;
    quint64 runtimeMapRevision() const { return m_runtimeMapRevision; }
    void ensureRuntimeMapsFrom(const core::Editor& ed);

    // ---- interruptores próprios do evento ---------------------------------
    /// Cada evento tem os seus A, B, C e D — é como um baú lembra que já foi
    /// aberto sem gastar um interruptor global.
    bool selfSwitch(const QString& eventoId, const QString& letra) const
    {
        return m_pureState.selfSwitch(chave(eventoId, letra).toUtf8().toStdString());
    }
    void setSelfSwitch(const QString& eventoId, const QString& letra, bool on)
    {
        m_pureState.setSelfSwitch(chave(eventoId, letra).toUtf8().toStdString(), on);
    }

    /// Quantos valores estão guardados (usado em testes e no HUD de depuração).
    int switchCount() const { return int(m_pureState.switchCount()); }
    int variableCount() const { return int(m_pureState.variableCount()); }
    int stringCount() const { return int(m_pureState.stringCount()); }
    int selfSwitchCount() const { return int(m_pureState.selfSwitchCount()); }
    int gold() const { return m_pureState.gold(); }
    void setGold(int amount) { m_pureState.setGold(amount); }
    void addGold(int amount) { m_pureState.addGold(amount); }

    int itemCount(const QString& id) const { return m_pureState.itemCount(id.toUtf8().toStdString()); }
    void setItemCount(const QString& id, int amount) { if (!id.isEmpty()) m_pureState.setItemCount(id.toUtf8().toStdString(), amount); }
    void addItem(const QString& id, int amount) { if (!id.isEmpty()) m_pureState.addItem(id.toUtf8().toStdString(), amount); }
    QStringList inventoryIds() const
    {
        QStringList ids;
        for (const std::string& id : m_pureState.inventoryIds()) ids.push_back(QString::fromUtf8(id.data(), int(id.size())));
        return ids;
    }

    bool hasQuest(const QString& id) const { return m_quests.contains(id); }
    QuestState quest(const QString& id) const { return m_quests.value(id); }
    QStringList questIds() const;
    void startQuest(const QString& id, int target = 1);
    void setQuestProgress(const QString& id, int progress);
    void addQuestProgress(const QString& id, int amount);
    void setQuestStatus(const QString& id, const QString& status);

    const QVector<PartyMemberState>& party() const { return m_party; }
    QVector<PartyMemberState>& party() { return m_party; }
    PartyMemberState* partyMember(const QString& actorId);
    const PartyMemberState* partyMember(const QString& actorId) const;
    bool addActor(const core::Editor& ed, const QString& actorId, int level = -1);
    bool removeActor(const QString& actorId);
    /// Completa saves antigos que ainda não gravavam o grupo.
    void ensurePartyFrom(const core::Editor& ed);

    /// Snapshot completo e independente do projeto, usado pelos saves da partida.
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& object, QString* error = nullptr);

    /// Avalia as condições de uma página (formato de core::PageConditions).
    bool pageMatches(const QVariantMap& conditions, const QString& eventoId) const;
    /// Escolhe a página válida de um evento: a ÚLTIMA cujas condições batem,
    /// como no RPG Maker. Devolve -1 se nenhuma valer.
    int  choosePage(const core::MapEvent& ev) const;

private:
    bool runtimeMapCapacityAvailable(const QString& mapId, int addCells, int addPassage,
                                     int addTerrain, int addRemaps, QString* error) const;
    void pruneRuntimeMapIfEmpty(const QString& mapId);

    static QString chave(const QString& eventoId, const QString& letra)
    {
        return eventoId + QLatin1Char(':') + letra;
    }

    pure::GameStateStore m_pureState;

    /// dbId -> recordId -> (fieldId -> value). Somente bancos Runtime entram.
    QHash<QString, QHash<QString, QVariantMap>> m_customDatabaseRuntime;

    struct RuntimeMapTileRef {
        QString tilesetId;
        int tx = 0;
        int ty = 0;
        QString wangSetId;
        int wangColorId = -1;
    };
    using RuntimeMapCell = QVector<RuntimeMapTileRef>;
    struct RuntimeMapData {
        /// key = layerId + "\n" + x + "\n" + y
        QHash<QString, RuntimeMapCell> cells;
        /// key = "x:y"; valor = máscara de passagem 0..15
        QHash<QString, int> passage;
        /// key = "x:y"; valor = terrain/tag >= 0
        QHash<QString, int> terrain;
        /// source tileset stable id -> target tileset stable id
        QHash<QString, QString> tilesetRemap;
    };
    QHash<QString, RuntimeMapData> m_runtimeMaps;
    quint64 m_runtimeMapRevision = 1; // transitório; não precisa ser serializado

    QHash<QString, QuestState> m_quests;
    QVector<PartyMemberState> m_party;
};

} // namespace game
