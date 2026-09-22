#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace core {
class Editor;

namespace migration {

/// Normaliza payloads históricos antes da desserialização moderna.
/// - extrai projetos LudoEngineProject antigos para o payload de mapas;
/// - recupera caminhos de imagens anteriores ao Asset Database para Assets/Recovered.
QJsonObject normalizeHistoricalProjectPayload(const QJsonObject& root,
                                              const QString& projectRoot,
                                              bool recoverLegacyAssets,
                                              bool* changed = nullptr);

/// Converte os antigos sufixos visuais "— parte N/M" / "— continuação N"
/// para o modelo lógico pageGroupId/pageIndex, sem alterar IDs nem TileRefs.
bool migrateLegacyTilesetPages(Editor& ed);

/// Importa os antigos mapas globais starTiles/collisionTiles/tileProbability
/// para a metadata moderna de cada Tileset.
void migrateLegacyTileMetadata(Editor& ed, const QJsonObject& root);

/// Pós-migrações que dependem do modelo já carregado: camada ativa estável,
/// identidade persistente de projetos antigos e topologia de borda de Autotiles.
void finalizeLoadedHistoricalState(Editor& ed, const QJsonDocument& originalDocument,
                                   bool* changed = nullptr);

} // namespace migration
} // namespace core
