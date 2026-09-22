#pragma once

#include "core/ProjectReferenceIndex.h"

#include <QString>
#include <QVector>

namespace core { class Editor; }

namespace ui {

enum class NavigationKind {
    Map,
    MapEvent,
    CommonEvent,
    GameData,
    CustomDatabase,
    CustomRecord,
    DatabaseRecord,
    Tileset,
    Asset,
    Plugin,
    FootstepSurface
};

struct NavigationItem {
    NavigationKind kind = NavigationKind::Map;
    QString key;              ///< chave local estável para recentes/favoritos
    QString label;            ///< nome principal exibido ao usuário
    QString context;          ///< mapa/categoria/caminho
    QString keywords;         ///< texto adicional pesquisável
    core::ProjectReferenceLocation location;
    QString assetPath;        ///< relativo ao projeto para kind == Asset
    bool missing = false;
};

QString navigationKindId(NavigationKind kind);
QString navigationKindLabel(NavigationKind kind);
QString navigationGroupId(NavigationKind kind);
QString navigationReferenceKey(core::ReferenceSymbolKind kind, const QString& id,
                               const QString& parentId = QString());
QString navigationKeyForReference(const core::ProjectReferenceSymbol& symbol);
QVector<NavigationItem> navigationItems(const core::Editor& ed);

/// Retorna os índices dos itens que casam com a busca, já ordenados por
/// relevância textual. Query vazia preserva a ordem canônica do catálogo.
QVector<int> rankedNavigationMatches(const QVector<NavigationItem>& items,
                                     const QString& query,
                                     const QString& groupId = QString());

} // namespace ui
