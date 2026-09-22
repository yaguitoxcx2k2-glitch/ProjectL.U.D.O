// ============================================================================
// MoveRoutePresetStore.h — presets editoriais de Rota de Movimento.
//
// RC2.22 / Rota C: presets vivem em um sidecar do projeto e NÃO entram no
// payload runtime/game.ludo. Isso mantém ProjectFormat estável e impede que uma
// ferramenta de produtividade altere compatibilidade de jogo/save.
// ============================================================================
#pragma once

#include "EventModel.h"

#include <QString>
#include <QVector>

namespace core {

struct MoveRoutePreset {
    QString id;
    QString name;
    MoveRoute route;
    bool builtIn = false;
};

inline constexpr int MoveRoutePresetFileVersion = 1;
inline constexpr char MoveRoutePresetRelativePath[] = "LudoEditor/MoveRoutePresets.json";
inline constexpr int MoveRoutePresetMaxCount = 128;
inline constexpr int MoveRoutePresetMaxCommands = 512;

QString moveRoutePresetFilePath(const QString& projectRoot);
QVector<MoveRoutePreset> builtInMoveRoutePresets();

/// Carrega apenas presets do usuário. Arquivo inexistente = lista vazia.
/// Arquivo futuro/corrompido falha explicitamente; nunca é sobrescrito durante
/// a leitura, para preservar dados que uma versão mais nova possa ter criado.
QVector<MoveRoutePreset> loadUserMoveRoutePresets(const QString& projectRoot,
                                                  QString* error = nullptr);

/// Escrita atômica. Presets built-in são ignorados porque são definidos em
/// código e não devem ser duplicados no sidecar do projeto.
bool saveUserMoveRoutePresets(const QString& projectRoot,
                              const QVector<MoveRoutePreset>& presets,
                              QString* error = nullptr);

/// Normalização defensiva usada por UI/store/testes. Retorna false quando o
/// preset não pode ser salvo sem perder significado.
bool normalizeMoveRoutePreset(MoveRoutePreset* preset, QString* error = nullptr);

} // namespace core
