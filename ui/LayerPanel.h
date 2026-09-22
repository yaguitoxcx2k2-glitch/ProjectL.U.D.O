// ============================================================================
//  LayerPanel.h — Lista/arvore de camadas (equivalente a #layersList).
//  Itens com visibilidade, cadeado, miniatura, cor, arrastar-e-soltar para
//  reordenar/agrupar, renomear no local e menu de contexto.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace ui {

class LayerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayerPanel(core::Editor& ed, QWidget* parent = nullptr);

    void refresh();

signals:
    void statusMessage(const QString& text);
    void requestAddLayer(int kind);           ///< 0 tiles; -1 objetos; -2 grupo; -3 imagem; -4 referência; -5 pintura; -6 reflexo MZ

private slots:
    void onItemSelectionChanged();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onContextMenu(const QPoint& pos);
    void onRowsMoved();

private:
    /// Atualizacoes vindas do Editor sao adiadas para o proximo ciclo do Qt.
    /// Isso evita destruir QTreeWidgetItems enquanto itemChanged/itemClicked
    /// ainda estao na pilha de sinais (causa de travamentos ao ocultar/excluir).
    void scheduleRefresh();
    void setCollapsedState(QTreeWidgetItem* item, bool collapsed);

    void buildItems(QTreeWidgetItem* parent, const QVector<core::LayerPtr>& nodes);
    QIcon thumbnailFor(const core::LayerPtr& l) const;
    core::LayerPtr layerOf(QTreeWidgetItem* item) const;
    /// Converte somente a hierarquia visual em um plano por IDs estáveis.
    /// A aplicação/validação da transação pertence ao Core (Editor/LayerTree).
    void rebuildModelFromTree();
    void collectPlacements(QTreeWidgetItem* item, const QString& parentId,
                           QVector<core::LayerTreePlacement>& out) const;

    core::Editor& ed;
    QTreeWidget*  m_tree = nullptr;
    bool          m_updating = false;
    bool          m_refreshQueued = false;
    bool          m_reorderQueued = false;
};

} // namespace ui
