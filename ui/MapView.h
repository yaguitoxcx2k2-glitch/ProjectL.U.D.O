// ============================================================================
//  MapView.h — Area de desenho do mapa (equivalente aos tres <canvas>
//  sobrepostos mapCanvas/gridCanvas/objectCanvas + o wrapper com scroll).
//
//  Responsabilidades: zoom/pan, conversao tela<->mapa, todas as ferramentas
//  (pincel, borracha, balde, retangulo, circulo, linha, terreno, objetos),
//  preview fantasma, marcacao de retangulo e leitura de coordenadas.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/PaintOps.h"
#include "core/Wang.h"

#include <QWidget>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>

class QScrollBar;

namespace ui {

class MapView : public QWidget
{
    Q_OBJECT
public:
    explicit MapView(core::Editor& ed, QWidget* parent = nullptr);

    double zoom() const { return m_zoom; }
    void   setZoom(double z, const QPointF& anchorScreen = QPointF(-1, -1));
    void   zoomIn()  { setZoom(m_zoom * 1.25); }
    void   zoomOut() { setZoom(m_zoom / 1.25); }
    void   zoomReset() { setZoom(1.0); }
    void   fitToView();
    /// Centraliza a vista num ponto do mapa (em pixels).
    void   centerOn(const QPointF& mapPos);
    QPointF viewCenterInMap() const;

    QSize sizeHint() const override { return QSize(900, 600); }

    // Comandos globais de edição. MainWindow direciona Ctrl+A/C/X/V/Delete
    // para o contexto atual do canvas em vez de manter atalhos separados por
    // ferramenta. Retornam true quando o contexto tratou o comando.
    bool editSelectAll();
    bool editCopy();
    bool editCut();
    bool editPaste();
    bool editDelete();
    bool editClearSelection();

signals:
    void zoomChanged(double zoom);
    void cursorMoved(const QPoint& mapPixel, const QPoint& cellPos);
    void statusMessage(const QString& text);
    void viewportChanged();
    /// Solicita a configuracao/aplicacao do Slope sobre a selecao local da camada.
    void slopeSelectionRequested(const QString& layerId, const QRect& localPixelRect);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QElapsedTimer m_animationClock;
    // conversao de coordenadas
    QPointF screenToMap(const QPointF& p) const;
    QPointF mapToScreen(const QPointF& p) const;
    QPoint  cellAt(const QPointF& mapPos, const core::LayerPtr& layer) const;
    QPoint  regionCellAt(const QPointF& mapPos) const;
    QRectF  visibleMapRect() const;
    QRectF  screenRectToMapRect(const QRect& screenRect) const;
    QRect   mapRectToUpdateRect(const QRectF& mapRect, int marginPx = 3) const;
    void    updateMapRect(const QRectF& mapRect, int marginPx = 3);
    QRectF  cellMapRect(const QPoint& cell) const;
    QRectF  tileCellMapRect(const core::LayerPtr& layer, const QPoint& cell) const;
    QPointF mapToLayerPoint(const core::LayerPtr& layer, const QPointF& mapPos) const;
    QPointF mapToImageLocalPoint(const core::LayerPtr& layer, const QPointF& mapPos) const;
    QRectF  objectMapRect(const core::LayerPtr& layer, const core::MapObject& object) const;
    QRectF  hoverVisualMapRect(const QPoint& cell, const QPointF& mapPos) const;
    QRectF  shapePreviewMapRect() const;
    QRectF  selectedObjectBounds(const core::LayerPtr& layer) const;
    void    cancelLayerInteraction();
    void    beginEditSelection(const QPointF& mapPos);
    void    updateEditSelection(const QPointF& mapPos);
    void    finishEditSelection(const QPointF& mapPos);
    QRect   activeCellSelection() const;
    QRect   activeRasterSelection() const;
    bool    rasterEditContext(core::LayerPtr* layerOut = nullptr, QImage** imageOut = nullptr, bool* maskOut = nullptr) const;

    void drawGhost(QPainter& p);
    void drawShapePreview(QPainter& p);
    void drawTilePickPreview(QPainter& p);
    void drawObjectDecorations(QPainter& p);
    void drawMarkers(QPainter& p);
    void drawImageTransformControls(QPainter& p);
    int imageTransformHandleAt(const core::LayerPtr& layer, const QPointF& mapPos) const;

    void paintRegionCell(const QPoint& cell, bool erase);
    void paintRegionBrush(const QPoint& cell, bool erase);
    void fillRegionAt(const QPoint& cell, bool erase);
    core::LayerPtr ensureGridFreeScatterLayer();
    void placeGridFreeAt(const core::LayerPtr& layer, const QPointF& mapPos);
    bool pickPlacedTileAt(const QPointF& mapPos, bool exactVariant = false);
    void beginStroke(const QPointF& mapPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void continueStroke(const QPointF& mapPos, Qt::KeyboardModifiers mods);
    void endStroke(const QPointF& mapPos, Qt::KeyboardModifiers mods);
    void applyTerrainAt(const core::LayerPtr& layer, int gx, int gy, bool erase);
    bool semanticAutotileActive() const;
    core::wang::TerrainPaintContext activeAutotileContext() const;
    QVector<QPoint> semanticShapeCells(core::Tool tool, const QPoint& a, const QPoint& b, bool hollow) const;
    void applySemanticAutotileCells(const core::LayerPtr& layer, const QVector<QPoint>& cells, bool erase);
    void applySemanticAutotileFill(const core::LayerPtr& layer, const QPoint& start, bool erase);
    void clampPan();

    void updateHeldModifiers(Qt::KeyboardModifiers mods);
    core::Editor& ed;
    double  m_zoom = 1.0;
    QPointF m_pan{ 0, 0 };              ///< canto superior-esquerdo visivel, em px do mapa

    // sessao de arraste
    bool    m_panning = false;
    QPoint  m_panStart;
    QPointF m_panOrigin;
    bool    m_painting = false;
    bool    m_erasing = false;
    /// Shift + apagar preserva as variantes atuais dos Autotiles e não recalcula vizinhos.
    bool    m_preserveAutotileShape = false;
    QPoint  m_shapeStart{ -1, -1 };
    QPoint  m_shapeEnd{ -1, -1 };
    bool    m_shapeActive = false;
    bool    m_tilePickDrag = false;          ///< Shift+botão direito: captura stamp
    QPoint  m_tilePickStart{ -1, -1 };
    QPoint  m_tilePickEnd{ -1, -1 };
    bool    m_rightEraseCandidate = false;   ///< Borracha + botão direito iniciou seleção de área
    bool    m_rightEraseActive = false;      ///< seleção retangular de apagamento em andamento
    QPoint  m_rightEraseStart{ -1, -1 };
    QPoint  m_rightEraseLast{ -1, -1 };
    core::Editor::EditSession m_rightEraseSession;
    bool    m_gridFreePainting = false;
    QString m_gridFreeLayerId;
    QPointF m_gridFreeLastPoint;
    core::Editor::EditSession m_gridFreeSession;

    // Slope: arraste uma area e aplique inclinacao progressiva pixel-perfect.
    bool    m_slopeSelecting = false;
    QPointF m_slopeStartMap;
    QPointF m_slopeEndMap;

    // Image Layer / Slope: seleção direta, arraste e cópia sem converter em objeto.
    bool    m_imageDragging = false;
    QString m_imageDragLayerId;
    QPointF m_imageDragStartMap;
    QPoint  m_imageDragStartOffset;
    QHash<QString, QPoint> m_imageDragStartOffsets;
    core::DocSnapshot m_imageGroupDragBefore;
    core::Editor::EditSession m_imageDragSession;
    int     m_imageTransformHandle = 0; // 0 nenhum, 1 escala, 2 rotação
    double  m_imageTransformStartScaleX = 1.0;
    double  m_imageTransformStartScaleY = 1.0;
    double  m_imageTransformStartRotation = 0.0;
    QPointF m_imageTransformCenter;
    double  m_imageRotationPointerOffset = 0.0;

    // Paint Brush raster livre em Image Layer dedicada. Um stroke inteiro vira
    // uma única entrada de Undo/Redo; QImage usa copy-on-write no snapshot.
    bool    m_rasterPainting = false;
    bool    m_rasterErasing = false;
    QPointF m_rasterLastLocal;
    // Pixel-Perfect trabalha com um ponto de atraso: o ponto intermediário só
    // é carimbado quando o próximo chega e confirma que ele não é redundante.
    QPointF m_rasterPixelPerfectAnchor;
    QPointF m_rasterPixelPerfectPending;
    bool    m_rasterPixelPerfectPendingValid = false;
    double  m_rasterSpacingCarry = 0.0;
    core::Editor::EditSession m_rasterSession;
    QRegion m_rasterClipRegion;             ///< Alpha Lock da máscara em Tile Layer
    bool    m_rasterUseClipRegion = false;

    QPoint  m_lastCell{ -1, -1 };
    int     m_brushSpacingCounter = 0;      ///< continuidade do espaçamento durante o drag
    // Random Tile + Scattering em grid normal: cada célula recebe somente
    // uma decisão de probabilidade por gesto. Sem isso, o mouse revisita a
    // mesma célula várias vezes durante o drag e acaba preenchendo os buracos.
    QSet<quint64> m_randomScatterVisited;
    core::Editor::EditSession m_session;

    // Pintura da camada nativa de Regiões do RPG Maker MV/MZ. É independente
    // da camada visual ativa e usa sempre a grade base do mapa.
    QVector<QPoint> mergedRegionCells(const QVector<QPoint>& cells) const;
    QVector<int> regionAreaValues(const QVector<QPoint>& cells) const;
    void updateRegionAreaGradient();
    int regionPaintValue(const QPoint& cell) const;
    QPoint m_regionOrigin;
    bool    m_regionPainting = false;
    bool    m_regionErase = false;
    bool    m_regionShapeActive = false;
    QPoint  m_regionShapeStart{ -1, -1 };
    QPoint  m_regionShapeEnd{ -1, -1 };
    bool    m_regionBeforeAuthored = false;
    QPoint  m_regionLastCell{ -1, -1 };
    QHash<quint64, int> m_regionBeforeValues;


    // Seleção/clipboard unificados do canvas. A seleção usa células para
    // Tile/Região e pixels locais para Paint/Mask. O clipboard fica volátil
    // e nunca entra no .ludo.
    bool    m_editSelecting = false;
    QString m_editSelectionLayerId;
    QRect   m_editCellSelection;
    QRect   m_editRasterSelection;
    QPoint  m_editSelectStartCell{-1, -1};
    QPointF m_editSelectStartMap;
    QVector<QVector<core::Cell>> m_tileClipboard;
    QVector<int> m_regionClipboard;
    QSize   m_regionClipboardSize;
    QImage  m_rasterClipboard;
    bool    m_rasterClipboardIsMask = false;

    // objetos
    QString m_dragObjectId;
    int     m_dragHandle = -1;
    bool    m_rotatingObject = false;
    QPointF m_dragOffset;
    QRectF  m_dragStartRect;
    double  m_dragStartRotation = 0.0;
    double  m_rotationGrabOffset = 0.0;
    bool    m_marquee = false;
    QPointF m_marqueeStart, m_marqueeEnd;

    // preview
    QPoint  m_hoverCell{ -1, -1 };
    QPointF m_hoverMap{ -1, -1 };
    bool    m_hasHover = false;
    bool m_suppressFullMapUpdate = false; ///< commit local ja repintado via dirty rect
};

} // namespace ui
