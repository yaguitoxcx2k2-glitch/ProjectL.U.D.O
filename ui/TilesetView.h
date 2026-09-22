// ============================================================================
//  TilesetView.h — Paleta de tiles (equivalente ao canvas #tilesetCanvas).
//  Selecao retangular, zoom da paleta, marcadores ★/✖, pool aleatorio,
//  rotulos de Wang e menu de contexto.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include <QWidget>
#include <QHash>

class QEvent;
class QScrollArea;

namespace ui {

class TilesetView : public QWidget
{
    Q_OBJECT
public:
    explicit TilesetView(core::Editor& ed, QWidget* parent = nullptr);

    void setPaletteZoom(double z);
    double paletteZoom() const { return m_zoom; }
    /// Quando ativo, a paleta principal mantém 8 colunas e ajusta o zoom
    /// automaticamente à largura disponível do painel. Views técnicas do
    /// Gerenciador continuam usando zoom manual/atlas físico.
    void setAutoFitWidth(bool on);
    bool autoFitWidth() const { return m_autoFitWidth; }
    void refreshAutoFit();
    /// Rola a paleta ate deixar o tile visivel (locateTileInPalette).
    void locateTile(int tilesetIdx, int tx, int ty);
    /// Liga/desliga a sobreposicao dos rotulos Wang (modo de edicao Wang).
    void setWangOverlay(bool on) { m_wangOverlay = on; update(); }
    bool wangOverlay() const { return m_wangOverlay; }

    /// Views técnicas do Gerenciador podem inspecionar o backing físico de um
    /// Autotile sem trocar o Tileset normal ativo no Editor. -1 usa a seleção normal.
    void setTilesetOverride(int index) { if (m_tilesetOverride == index) return; m_tilesetOverride = index; invalidatePaletteCache(); updateGeometry(); resize(sizeHint()); update(); }
    int tilesetOverride() const { return m_tilesetOverride; }

    /// Na paleta principal, Autotiles aparecem em uma biblioteca separada.
    /// O atlas de tiles comuns pode ocultar os tiles pertencentes a esses
    /// recursos sem alterar coordenadas/IDs do projeto.
    void setHideAutotileTiles(bool on) { if (m_hideAutotileTiles == on) return; m_hideAutotileTiles = on; invalidatePaletteCache(); updateGeometry(); resize(sizeHint()); update(); }
    bool hideAutotileTiles() const { return m_hideAutotileTiles; }

    /// Editor de prioridade 0..5. Valor -1 = clicar cicla 0→1→…→5→0;
    /// 0..5 = pincel fixo, inclusive com arraste.
    void setPriorityMode(bool on) { m_priorityMode = on; m_priorityPainting = false; update(); }
    bool priorityMode() const { return m_priorityMode; }

    /// Editor detalhado de colisão usado pelo Gerenciador de Tilesets 2.0.
    /// Não depende do modo global da toolbar do editor.
    void setCollisionMode(bool on) { m_collisionMode = on; m_collisionPainting = false; update(); }
    bool collisionMode() const { return m_collisionMode; }
    /// -1 = clique contextual (miolo alterna tudo; borda alterna o lado).
    /// 0..15 = pincel de máscara exata, com arraste.
    void setCollisionPaintMask(int mask) { m_collisionPaintMask = mask < 0 ? -1 : (mask & 0x0f); }
    int collisionPaintMask() const { return m_collisionPaintMask; }
    void setPriorityPaintValue(int value) { m_priorityPaintValue = value < 0 ? -1 : qBound(0, value, 5); }
    int priorityPaintValue() const { return m_priorityPaintValue; }
    /// Aplica 1 na linha mais baixa, 2 na seguinte etc. à seleção atual.
    void applyPriorityBottomUpToSelection();
    void setReflectionOverlay(bool on) { if (m_reflectionOverlay == on) return; m_reflectionOverlay = on; update(); }
    QVector<QPoint> selectedTiles() const { return currentSelectionTiles(); }
    int displayedTilesetIndex() const { return activeTilesetIndex(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(120, 120); }

signals:
    void statusMessage(const QString& text);
    /// Emitido ao clicar num tile com o overlay Wang ligado (edicao de rotulos).
    void wangTileClicked(int tilesetIdx, int tx, int ty, const QString& position);
    void tileDoubleClicked(int tilesetIdx, int tx, int ty);
    /// Somente a paleta principal conecta este sinal. Views do Gerenciador
    /// ignoram-no, preservando seus modos técnicos locais.
    void regularTilePicked(int tilesetIdx, int tx, int ty);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    const QVector<QPoint>& paletteTiles(const core::Tileset& ts) const;
    void invalidatePaletteCache() const;
    void ensurePaletteCache(const core::Tileset& ts) const;
    int paletteVisualIndex(const core::Tileset& ts, int tx, int ty) const;
    /// Paleta principal no formato familiar do RPG Maker: 8 colunas fixas.
    /// Views técnicas do Gerenciador preservam as colunas físicas do atlas.
    int visualColumnCount(const core::Tileset& ts) const;
    int visualRowCount(const core::Tileset& ts) const;
    QPoint physicalTileAtVisual(const core::Tileset& ts, int vx, int vy) const;
    QPoint visualCellAt(const QPoint& widgetPos) const;
    QPoint tileAt(const QPoint& widgetPos) const;
    QRect paletteRect(const core::Tileset& ts, int tx, int ty) const;
    bool paletteSelectionMatchesEditor() const;
    /// Sub-regiao (tl/t/tr/l/r/bl/b/br) dentro de um tile, para editar Wang.
    QString wangPositionAt(const QPoint& widgetPos) const;
    /// Lado do tile sob o cursor (Editor::Side), ou 0 se o clique foi no miolo.
    int collisionSideAt(const QPoint& widgetPos) const;
    static QString describeCollision(int mask);
    int activeTilesetIndex() const;
    const core::Tileset* activeTileset() const;
    QScrollArea* hostingScrollArea() const;
    void fitToViewportWidth();

    core::Editor& ed;
    double m_zoom = 1.0;
    bool   m_autoFitWidth = false;
    bool   m_autoFitBusy = false;
    bool   m_selecting = false;
    bool   m_poolDrag = false;
    QPoint m_selStart{ 0, 0 };
    QPoint m_selEnd{ 0, 0 };
    QPoint m_selStartVisual{ 0, 0 };
    QPoint m_selEndVisual{ 0, 0 };
    QVector<QPoint> m_paletteSelection;  ///< seleção exata na paleta visual reorganizada
    int m_paletteSelectionTileset = -1;
    int    m_tilesetOverride = -1;
    bool   m_wangOverlay = false;
    bool   m_hideAutotileTiles = false;
    bool   m_priorityMode = false;
    int    m_priorityPaintValue = -1; ///< -1 = ciclo; 0..5 = pincel fixo
    bool   m_priorityPainting = false;
    QPoint m_lastPriorityTile{-1, -1};
    bool   m_reflectionOverlay = false;
    bool   m_collisionMode = false;
    int    m_collisionPaintMask = -1;
    bool   m_collisionPainting = false;
    QPoint m_lastCollisionTile{-1, -1};

    enum class PendingMarkerTool { None, Star, Collision };
    PendingMarkerTool m_pendingMarkerTool = PendingMarkerTool::None;
    QPoint m_pendingMarkerTile{-1, -1};

    void applyPriorityAt(const QPoint& tile, bool allowCycle);
    void applyCollisionAt(const QPoint& tile);
    QVector<QPoint> selectionTargetsFor(const QPoint& anchor) const;
    QVector<QPoint> currentSelectionTiles() const;

    // Cache da paleta lógica compactada. Folhas A1 podem ter milhares de
    // tiles físicos e muitos frames auxiliares; reconstruir a lista durante
    // cada paletteRect()/paintEvent() causa custo quadrático. O cache só é
    // invalidado quando o tileset muda.
    mutable QString m_paletteCacheTilesetId;
    mutable QVector<QPoint> m_paletteCacheTiles;
    mutable QHash<quint64, int> m_paletteCacheIndex;
    mutable int m_paletteCacheColumns = -1;
    mutable int m_paletteCacheRows = -1;
    mutable int m_paletteCacheAnimationCount = -1;
    mutable bool m_paletteCacheHideAutotiles = false;
};

} // namespace ui
