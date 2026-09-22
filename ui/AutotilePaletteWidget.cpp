#include "AutotilePaletteWidget.h"

#include "core/Renderer.h"
#include "core/TilesetCatalog.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>

using namespace core;

namespace ui {
namespace {
constexpr int kAutotileThumb = 36;
constexpr int kAutotileCell = 44;
constexpr int kPaletteHeight = 64;
}

QString AutotilePaletteWidget::uncategorizedCategoryToken()
{
    return QStringLiteral("__ludo_autotile_uncategorized__");
}

AutotilePaletteWidget::AutotilePaletteWidget(Editor& editor, QWidget* parent)
    : QWidget(parent), ed(editor)
{
    setObjectName(QStringLiteral("autotileEditorPalette"));
    setAccessibleName(tr("Autotiles do projeto"));
    setToolTip(tr("Biblioteca global de Autotiles em miniaturas. Passe o mouse para ver nome e categoria. "
                  "A categoria apenas filtra a biblioteca; trocar o Tileset normal não altera os Autotiles."));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kPaletteHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("autotileThumbnailStrip"));
    m_list->setAccessibleName(tr("Miniaturas dos Autotiles"));
    m_list->setMouseTracking(true);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setIconSize(QSize(kAutotileThumb, kAutotileThumb));
    m_list->setGridSize(QSize(kAutotileCell, kAutotileCell));
    m_list->setSpacing(0);
    m_list->setFixedHeight(kPaletteHeight);
    m_list->setToolTip(tr("Clique em uma miniatura para selecionar o Autotile. Use a barra horizontal para navegar. O nome completo aparece ao passar o mouse."));
    layout->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (m_refreshing || !item) return;
        const QString id = item->data(Qt::UserRole).toString();
        const int tilesetIdx = item->data(Qt::UserRole + 1).toInt();
        if (id.isEmpty() || tilesetIdx < 0) return;
        emit autotileActivated(tilesetIdx, id);
    });
    connect(m_list, &QListWidget::itemEntered, this, [this](QListWidgetItem* item) {
        if (!item) return;
        emit statusMessage(item->toolTip());
    });

    connect(&ed, &Editor::tilesetsChanged, this, &AutotilePaletteWidget::refresh);
    connect(&ed, &Editor::wangChanged, this, [this] {
        QTimer::singleShot(0, this, &AutotilePaletteWidget::refresh);
    });
    connect(&ed, &Editor::selectionChanged, this, &AutotilePaletteWidget::syncSelection);

    refresh();
}

QStringList AutotilePaletteWidget::categories() const
{
    return core::autotileCategories(ed);
}

bool AutotilePaletteWidget::hasUncategorized() const
{
    for (const TilesetAutotile& autotile : ed.autotiles)
        if (autotile.category.trimmed().isEmpty()) return true;
    return false;
}

void AutotilePaletteWidget::setCategoryFilter(const QString& categoryToken)
{
    if (m_categoryFilter == categoryToken) return;
    m_categoryFilter = categoryToken;
    refresh();
}

bool AutotilePaletteWidget::acceptsCategory(const TilesetAutotile& autotile) const
{
    if (m_categoryFilter.isEmpty()) return true;
    const QString category = autotile.category.trimmed();
    if (m_categoryFilter == uncategorizedCategoryToken()) return category.isEmpty();
    return category.compare(m_categoryFilter, Qt::CaseInsensitive) == 0;
}

QPixmap AutotilePaletteWidget::previewFor(int tilesetIdx, const TilesetAutotile& autotile) const
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) return QPixmap();

    QPoint tile(autotile.previewTx, autotile.previewTy);
    if (!ts->contains(tile.x(), tile.y()) && autotile.hasTerrain()) {
        if (const WangSet* set = wangSetById(ed, autotile.wangSetId)) {
            if (const WangColor* color = set->colorById(autotile.wangColorId)) {
                if (color->hasIcon && color->iconTilesetIdx == tilesetIdx)
                    tile = QPoint(color->iconTx, color->iconTy);
                else if (set->hasIcon && set->iconTilesetIdx == tilesetIdx)
                    tile = QPoint(set->iconTx, set->iconTy);
            }
        }
    }
    if (!ts->contains(tile.x(), tile.y())) {
        const QRect bounds = tilesetAutotileBounds(ed, tilesetIdx, autotile);
        if (!bounds.isEmpty()) tile = QPoint(bounds.left(), bounds.bottom());
    }
    if (!ts->contains(tile.x(), tile.y()) && autotile.hasRegion())
        tile = QPoint(autotile.baseX, autotile.baseY + qMax(0, autotile.rows - 1));
    if (!ts->contains(tile.x(), tile.y())) return QPixmap();

    return pixmapCache().pixmap(ed, tilesetIdx).copy(ts->tileRect(tile.x(), tile.y()));
}

void AutotilePaletteWidget::refresh()
{
    const QSignalBlocker blocker(m_list);
    const int oldScroll = m_list->horizontalScrollBar()->value();
    m_refreshing = true;
    m_list->clear();

    const QVector<TilesetAutotileInfo> catalog = tilesetAutotileCatalog(ed);
    for (const TilesetAutotileInfo& info : catalog) {
        if (!info.autotile || !info.tileset || info.tilesetIdx < 0) continue;
        const TilesetAutotile& autotile = *info.autotile;
        if (!acceptsCategory(autotile)) continue;

        const QString displayName = autotile.name.trimmed().isEmpty() ? tr("Autotile") : autotile.name.trimmed();
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, autotile.id);
        item->setData(Qt::UserRole + 1, info.tilesetIdx);
        item->setText(QString()); // A faixa exibe somente a miniatura.
        item->setTextAlignment(Qt::AlignCenter);
        item->setSizeHint(QSize(kAutotileCell, kAutotileCell));

        QString details = tr("Autotile: %1").arg(displayName);
        details += autotile.category.trimmed().isEmpty()
            ? tr("\nCategoria: Sem categoria")
            : tr("\nCategoria: %1").arg(autotile.category.trimmed());
        details += autotile.hasTerrain()
            ? tr("\nConexões automáticas configuradas")
            : tr("\nSem conexões automáticas configuradas");
        if (autotile.animated()) details += tr(" · animado");
        item->setToolTip(details);
        item->setStatusTip(details);

        const QPixmap preview = previewFor(info.tilesetIdx, autotile);
        if (!preview.isNull()) {
            const QPixmap thumb = (preview.width() > kAutotileThumb || preview.height() > kAutotileThumb)
                ? preview.scaled(QSize(kAutotileThumb, kAutotileThumb), Qt::KeepAspectRatio, Qt::FastTransformation)
                : preview;
            item->setIcon(QIcon(thumb));
        }
    }

    m_refreshing = false;
    m_list->horizontalScrollBar()->setValue(qMin(oldScroll, m_list->horizontalScrollBar()->maximum()));
    syncSelection();
}

void AutotilePaletteWidget::syncSelection()
{
    const QSignalBlocker blocker(m_list);
    if (ed.session.activeAutotileId.isEmpty()) {
        m_list->clearSelection();
        m_list->setCurrentItem(nullptr);
        return;
    }
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* listItem = m_list->item(row);
        if (listItem && listItem->data(Qt::UserRole).toString() == ed.session.activeAutotileId) {
            m_list->setCurrentRow(row);
            m_list->scrollToItem(listItem, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
    m_list->clearSelection();
    m_list->setCurrentItem(nullptr);
}

} // namespace ui
