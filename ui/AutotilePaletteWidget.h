// ============================================================================
// AutotilePaletteWidget.h — Faixa horizontal da biblioteca global de Autotiles.
//
// Autotiles e tiles normais sao duas ferramentas distintas. Esta paleta exibe
// apenas miniaturas dos recursos semanticos em uma unica faixa horizontal
// com scrollbar horizontal nativo. Nome/categoria ficam em tooltip; a
// categoria continua sendo apenas um filtro externo e nunca altera o Tileset
// normal ativo.
// ============================================================================
#pragma once

#include "core/Editor.h"

#include <QPixmap>
#include <QStringList>
#include <QWidget>

class QListWidget;
class QListWidgetItem;

namespace ui {

class AutotilePaletteWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AutotilePaletteWidget(core::Editor& ed, QWidget* parent = nullptr);

    void refresh();
    void setCategoryFilter(const QString& categoryToken);
    QString categoryFilter() const { return m_categoryFilter; }
    QStringList categories() const;
    bool hasUncategorized() const;

    static QString uncategorizedCategoryToken();

signals:
    void autotileActivated(int tilesetIdx, const QString& autotileId);
    void statusMessage(const QString& text);

private:
    QPixmap previewFor(int tilesetIdx, const core::TilesetAutotile& autotile) const;
    bool acceptsCategory(const core::TilesetAutotile& autotile) const;
    void syncSelection();

    core::Editor& ed;
    QListWidget* m_list = nullptr;
    bool m_refreshing = false;
    QString m_categoryFilter;
};

} // namespace ui
