#pragma once

#include "Editor.h"

namespace core::mapworkflow {

struct MapTreePlacement {
    QString mapId;
    QString parentId; // vazio = raiz
};

enum class MapDeleteChildrenPolicy {
    PromoteChildren, ///< filhos diretos sobem para o pai do mapa removido
    DeleteSubtree    ///< mapa + todos os descendentes sao removidos
};

enum class MapDeleteReferencePolicy {
    RejectReferenced, ///< bloqueia se existir referencia externa por ID
    AllowDangling     ///< permite apos confirmacao explicita do autor
};

struct MapDeletePolicy {
    MapDeleteChildrenPolicy children = MapDeleteChildrenPolicy::PromoteChildren;
    MapDeleteReferencePolicy references = MapDeleteReferencePolicy::RejectReferenced;
};

struct MapDeleteResult {
    QStringList deletedMapIds;
    int externalReferenceCount = 0;
};

/// Retorna false quando o novo pai criaria auto-parenting ou ciclo.
bool canReparent(const Editor& editor, const QString& mapId, const QString& newParentId,
                 QString* error = nullptr);

/// Reancora um mapa (e, por identidade, toda sua subarvore) na arvore do projeto.
bool reparent(Editor& editor, const QString& mapId, const QString& newParentId,
              QString* error = nullptr);

/// Move o mapa entre irmaos do mesmo pai. A subarvore acompanha o mapa.
bool moveSibling(Editor& editor, const QString& mapId, int delta, QString* error = nullptr);

/// Aplica de forma transacional a ordem/hierarquia completa vinda da arvore
/// do Editor. Rejeita IDs ausentes/duplicados, pais invalidos e ciclos.
bool applyTreeOrder(Editor& editor, const QVector<MapTreePlacement>& placements,
                    QString* error = nullptr);

/// Duplica o mapa e TODA a sua subarvore. Todos os MapDoc/layers/objects
/// ganham IDs novos e referencias internas entre os clones sao remapeadas.
/// Retorna o ID da raiz da nova subarvore ou vazio em caso de erro.
QString duplicateMap(Editor& editor, const QString& mapId, QString* error = nullptr);

/// Cria um estado alternativo do mesmo cenário a partir do mapa-base ou de
/// outra variação. A cópia é independente (IDs de layers/objects novos) e
/// recebe seu próprio Map ID quando sincronizada com o RPG Maker.
QString createVariation(Editor& editor, const QString& sourceMapId,
                        const QString& variationName, QString* error = nullptr);

/// Exclusao com politica explicita para filhos e referencias. Nunca remove o
/// ultimo mapa do projeto. `result` informa impacto antes/apos a operacao.
bool deleteMap(Editor& editor, const QString& mapId, const MapDeletePolicy& policy,
               MapDeleteResult* result = nullptr, QString* error = nullptr);

/// Corrige pais ausentes/ciclicos sem apagar mapas e normaliza a ordem DFS.
/// Retorna a quantidade de ajustes estruturais realizados.
int normalizeHierarchy(Editor& editor);

} // namespace core::mapworkflow
