// ============================================================================
//  AutotileTopology.h — Topologia canonica de Autotiles/Terrain.
//
//  Este modulo concentra a unica regra de vizinhanca usada pelo Terrain/Wang.
//  A politica de borda pertence ao recurso TilesetAutotile: quando habilitada,
//  coordenadas fora do mapa contam como continuidade do mesmo terreno, como em
//  editores de RPG/Wolf. Wang.cpp apenas consome esta regra para escolher a variante.
// ============================================================================
#pragma once

#include "Model.h"

namespace core { namespace autotile {

struct TopologyPolicy {
    bool extendAtMapBoundary = false;
};

/// Regra Blob 47: cantos so existem quando as duas bordas adjacentes tambem
/// pertencem ao mesmo terreno.
int filterBlobMask(bool top, bool topRight, bool right, bool bottomRight,
                   bool bottom, bool bottomLeft, bool left, bool topLeft);

/// Verdadeiro quando a coordenada pertence ao mesmo Terrain/Wang no TOPO da
/// pilha da camada. Qualquer tile diferente ocupando o topo interrompe a
/// continuidade, permitindo caminhos/objetos de tile abrirem o Autotile como
/// no editores de RPG. Fora da grade, a resposta e definida por `extendAtMapBoundary`.
bool terrainNeighbor(const LayerPtr& layer, int x, int y,
                     const QString& wangSetId, int colorId,
                     const TopologyPolicy& policy = {});

/// Mascara canonica TL/T/TR/L/R/BL/B/BR da celula. Esta funcao nao escolhe
/// tiles nem conhece UI: apenas resolve topologia.
int terrainNeighborMask(const LayerPtr& layer, int x, int y,
                        const QString& wangSetId, int colorId,
                        const TopologyPolicy& policy = {});

/// Uma celula esta na borda fisica da grade?
bool isMapBoundaryCell(const LayerPtr& layer, int x, int y);

}} // namespace core::autotile
