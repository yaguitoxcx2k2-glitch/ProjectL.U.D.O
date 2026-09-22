// ============================================================================
//  ProjectIO.h — contrato de persistência do LUDO Map Editor.
//  O destino de jogo é o RPG Maker MV/MZ; exportadores genéricos
//  da antiga LUDO Engine não fazem mais parte desta API.
//
//  O projeto .ludo guarda autoria de mapas/tilesets e continua aceitando
//  projetos antigos apenas para migração.
// ============================================================================
#pragma once

#include "Editor.h"
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace core { namespace io {

// ---- projeto -------------------------------------------------------------
QJsonObject buildProjectPayload(const Editor& ed);
/// Serializa somente um mapa no mesmo formato usado dentro de `maps` no
/// projeto .ludo. Usado por fluxos incrementais (ex.: colaboração) para não
/// precisar reconstruir o projeto inteiro quando apenas um mapa mudou.
QJsonObject buildProjectMapPayload(const MapDoc& map);
/// Desserializa somente um mapa sem tocar nos demais documentos/recursos do
/// Editor. O `Editor` é usado apenas para resolver caminhos relativos de assets.
bool        loadProjectMapPayload(const Editor& ed, const QJsonObject& payload,
                                  MapDoc* map, QString* error = nullptr);
bool        saveProject(Editor& ed, const QString& path, QString* error);
enum class ProjectLoadMode {
    Normal,
    ReadOnlyPreview
};
bool        loadProject(Editor& ed, const QString& path, QString* error,
                        ProjectLoadMode mode = ProjectLoadMode::Normal);
/// Atualiza referências em memória quando um asset é movido/renomeado.
/// Usado pelo Asset Browser para que o projeto continue funcional sem reload.
void        rewriteEditorAssetPath(Editor& ed, const QString& oldProjectRelativePath,
                                   const QString& newProjectRelativePath);

// ---- saídas auxiliares ---------------------------------------------------
// O destino oficial do mapa é a integração RPG Maker MV/MZ (ui/RpgMakerExporter.h).
// ProjectIO mantém apenas utilidades de autoria que ainda aparecem na UI.
bool exportPNG(const Editor& ed, const QString& path, QString* error);
bool exportTilesetImage(const Editor& ed, int tilesetIdx, const QString& path, QString* error);

// ---- presets Wang --------------------------------------------------------
/// Grava os presets NAO embutidos como um array JSON puro — mesmo formato da
/// ferramenta web (`exportWangPresets` do JS), para os arquivos serem
/// intercambiáveis entre as duas versões.
/// `count` recebe quantos foram gravados. Falha (com explicação) se não houver
/// nenhum preset customizado.
bool exportWangPresets(const Editor& ed, const QString& path, QString* error, int* count = nullptr);

/// Leitor tolerante: aceita array puro, `{presets:[…]}`, `{wangPresets:[…]}`,
/// um único objeto de preset, e `positions` tanto no formato `{"tl":true}`
/// quanto `["tl","t"]`. `count` recebe quantos foram importados.
bool importWangPresets(Editor& ed, const QString& path, QString* error, int* count = nullptr);

/// Persistência local dos presets customizados — equivale ao
/// localStorage['tileEditor_wangPresets'] da versão web.
void saveWangPresetsToSettings(const Editor& ed);
void loadWangPresetsFromSettings(Editor& ed);

// ---- utilidades ----------------------------------------------------------
QString imageToDataUri(const QImage& img);
QImage  dataUriToImage(const QString& uri);
/// Serializacao compacta de uma celula de Tile Layer usada tambem pela
/// colaboracao em tempo real. Mantem exatamente o formato do arquivo .ludo.
QJsonValue cellToJson(const Cell& cell);
Cell       cellFromJson(const QJsonValue& value);

}} // namespace core::io
