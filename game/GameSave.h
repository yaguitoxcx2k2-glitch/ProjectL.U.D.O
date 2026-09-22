// ============================================================================
//  GameSave.h — Formato versionado e gravação atômica de uma partida.
// ============================================================================
#pragma once

#include "game/GameState.h"

#include <QPoint>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace core { class Editor; }

namespace game {

struct GameSaveData {
    QString projectId;
    QString projectName;
    QString mapId;
    QPoint playerHalfCell;
    int playerDirection = 0;
    qint64 playTimeSeconds = 0;
    GameState state;
    QJsonObject runtime; ///< tela, mundo e canais contínuos (formato v3+)
};

struct GameSaveSummary {
    bool valid = false;
    QString projectName;
    QString mapId;
    QString mapName;
    QDateTime savedAt;
    qint64 playTimeSeconds = 0;
};

/// Caminho estável entre o editor e o LudoPlayer (slot: 1..99).
QString gameSavePath(const core::Editor& ed, int slot);
bool writeGameSave(const QString& path, const GameSaveData& data, QString* error = nullptr);
bool readGameSave(const QString& path, const core::Editor& ed, GameSaveData* data,
                  QString* error = nullptr);
bool readGameSaveSummary(const QString& path,const core::Editor& ed,GameSaveSummary* summary);

} // namespace game
