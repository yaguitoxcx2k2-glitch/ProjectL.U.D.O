// ============================================================================
// LayerTree.h — operacoes estruturais seguras para a arvore de camadas.
//
// RC2.52: a arvore visual nunca deve reescrever Layer::children diretamente.
// Toda reorganizacao passa por uma transacao por IDs estaveis, validada antes
// de tocar no modelo. As rotinas de leitura tambem sao cycle-safe para que um
// estado corrompido nao derrube Editor/Validator em recursao infinita.
// ============================================================================
#pragma once

#include "Model.h"

#include <QString>
#include <QVector>

namespace core {

struct LayerTreePlacement {
    QString id;
    QString parentId;  ///< vazio = raiz do mapa
    int order = 0;     ///< ordem de renderizacao dentro do pai (baixo -> cima)
};

struct LayerTreeValidationResult {
    bool ok = true;
    QString error;
    int nodeCount = 0;
};

/// Estado efetivo de uma camada considerando todos os grupos/masks ancestrais.
/// `visible` e AND ao longo da arvore; `locked` e OR. Isso evita que o canvas
/// edite um filho que esta invisivel/bloqueado por um container pai.
struct LayerEffectiveState {
    bool found = false;
    bool visible = true;
    bool locked = false;
};

/// Valida IDs, referencias duplicadas, filhos em nos nao-container e ciclos.
LayerTreeValidationResult validateLayerTree(const QVector<LayerPtr>& roots);

/// Lista achatada usada pelo editor/runtime: grupos puros nao desenham; masks
/// continuam na lista porque possuem conteudo proprio e filhos recortados.
QVector<LayerPtr> flattenRenderableLayers(const QVector<LayerPtr>& roots);

/// Aplica a hierarquia inteira em uma unica transacao. `roots` so e alterado
/// depois que todos os IDs/pais/ordens/ciclos foram validados.
bool applyLayerTreePlacements(QVector<LayerPtr>& roots,
                              const QVector<LayerTreePlacement>& placements,
                              QString* error = nullptr);

/// Teste cycle-safe de pertencimento de uma subarvore.
bool layerSubtreeContains(const LayerPtr& root, const QString& id);

/// Resolve visibilidade/bloqueio herdados para uma camada por ID.
LayerEffectiveState layerEffectiveState(const QVector<LayerPtr>& roots, const QString& id);

} // namespace core
