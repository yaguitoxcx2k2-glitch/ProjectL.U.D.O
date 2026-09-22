// ============================================================================
// LayerRasterFilters.h — filtros raster não destrutivos do Map Editor.
//
// O sistema é deliberadamente separado dos filtros de tela/runtime: estes
// filtros pertencem à camada/máscara do mapa e são rasterizados no editor e
// na exportação. Resultados são cacheados pelo cacheKey do QImage + parâmetros.
// ============================================================================
#pragma once

#include "Model.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace core {

QString rasterLayerFilterTypeLabel(const QString& type);
QString rasterLayerFilterSummary(const RasterLayerFilter& filter);
bool hasEnabledRasterFilters(const QVector<RasterLayerFilter>& filters);

/// Aplica a pilha usando cache COW. maskTarget faz Ruído trabalhar em
/// luminância/alpha de máscara em vez de granular somente RGB.
QImage filteredRasterCached(const QImage& source,
                            const QVector<RasterLayerFilter>& filters,
                            bool maskTarget = false);

/// Mescla conteúdo e máscara já filtrados. Também possui cache próprio para
/// evitar percorrer todos os pixels a cada repaint.
QImage maskedRasterCached(const QImage& content, const QImage& mask);

/// Gera somente a sombra de contato (sem redesenhar o conteúdo de origem).
/// Usado por Tile Layer para manter tiles animados/editáveis e cachear apenas
/// o AO/sombra derivado da silhueta.
QImage contactShadowOverlayCached(const QImage& silhouette,
                                  const RasterLayerFilter& filter);

} // namespace core
