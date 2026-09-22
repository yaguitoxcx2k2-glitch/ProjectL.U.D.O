#include "TilesetView.h"

#include "MapAuthoringDialogs.h"

#include "core/Renderer.h"
#include "core/TilesetOps.h"
#include "core/TilesetCatalog.h"
#include "core/Wang.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

using namespace core;

namespace ui {

TilesetView::TilesetView(Editor& editorRef, QWidget* parent)
    : QWidget(parent), ed(editorRef)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(&ed, &Editor::tilesetsChanged, this, [this] {
        // O QScrollArea da paleta usa widgetResizable(false). Apenas chamar
        // updateGeometry() não altera o tamanho físico deste widget. Quando
        // um tileset é recarregado/trocado, isso deixava a viewport com a
        // geometria anterior e a parte restante do atlas parecia desaparecer
        // até o usuário mudar o zoom (setPaletteZoom chama resize()).
        //
        // Recalcular o cache ANTES do sizeHint() garante que compactação de
        // frames/autotiles e novas dimensões sejam consideradas na mesma
        // atualização visual.
        pixmapCache().invalidate();
        invalidatePaletteCache();
        if (m_autoFitWidth) fitToViewportWidth();
        else {
            updateGeometry();
            const QSize wanted = sizeHint();
            if (size() != wanted) resize(wanted);
            update();
        }
    });
    connect(&ed, &Editor::selectionChanged, this, [this] {
        if (m_autoFitWidth) fitToViewportWidth();
        else {
            updateGeometry();
            const QSize wanted = sizeHint();
            if (size() != wanted) resize(wanted);
            update();
        }
    });
    connect(&ed, &Editor::wangChanged, this, [this] {
        if (m_hideAutotileTiles) {
            invalidatePaletteCache();
            updateGeometry();
            resize(sizeHint());
        }
        update();
    });
}

int TilesetView::activeTilesetIndex() const
{
    return m_tilesetOverride >= 0 ? m_tilesetOverride : ed.session.activeTilesetIdx;
}

const Tileset* TilesetView::activeTileset() const
{
    return ed.tilesetAt(activeTilesetIndex());
}

QScrollArea* TilesetView::hostingScrollArea() const
{
    QWidget* node = parentWidget();
    while (node) {
        if (auto* area = qobject_cast<QScrollArea*>(node)) return area;
        node = node->parentWidget();
    }
    return nullptr;
}

void TilesetView::setAutoFitWidth(bool on)
{
    if (m_tilesetOverride >= 0) on = false;
    if (m_autoFitWidth == on) {
        if (on) fitToViewportWidth();
        return;
    }
    m_autoFitWidth = on;
    if (QScrollArea* area = hostingScrollArea()) {
        if (area->viewport()) area->viewport()->installEventFilter(this);
    }
    if (m_autoFitWidth) fitToViewportWidth();
}

void TilesetView::refreshAutoFit()
{
    if (m_autoFitWidth) fitToViewportWidth();
    else {
        updateGeometry();
        const QSize wanted = sizeHint();
        if (size() != wanted) resize(wanted);
        update();
    }
}

void TilesetView::fitToViewportWidth()
{
    if (!m_autoFitWidth || m_autoFitBusy || m_tilesetOverride >= 0) return;
    const Tileset* ts = activeTileset();
    QScrollArea* area = hostingScrollArea();
    if (!ts || !area || !area->viewport()) return;

    const int columns = 8;
    const int baseWidth = ts->margin * 2 + columns * ts->tilewidth +
        qMax(0, columns - 1) * ts->spacing;
    if (baseWidth <= 0) return;

    // Deixa uma folga mínima para a borda da viewport. A paleta continua
    // exatamente com 8 colunas; somente a escala visual muda.
    const int available = qMax(1, area->viewport()->contentsRect().width() - 2);
    const double fitted = double(available) / double(baseWidth);
    const double nextZoom = clampd(fitted, 0.10, 4.0);

    m_autoFitBusy = true;
    if (std::abs(nextZoom - m_zoom) > 0.0005) m_zoom = nextZoom;
    updateGeometry();
    const QSize wanted = sizeHint();
    if (size() != wanted) resize(wanted);
    update();
    m_autoFitBusy = false;
}

bool TilesetView::eventFilter(QObject* watched, QEvent* event)
{
    if (m_autoFitWidth) {
        if (QScrollArea* area = hostingScrollArea()) {
            if (watched == area->viewport() &&
                (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
                fitToViewportWidth();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

namespace {
quint64 paletteTileKey(int tx, int ty)
{
    return (quint64(quint32(ty)) << 32) | quint64(quint32(tx));
}
}

void TilesetView::invalidatePaletteCache() const
{
    m_paletteCacheTilesetId.clear();
    m_paletteCacheTiles.clear();
    m_paletteCacheIndex.clear();
    m_paletteCacheColumns = -1;
    m_paletteCacheRows = -1;
    m_paletteCacheAnimationCount = -1;
    m_paletteCacheHideAutotiles = false;
}

void TilesetView::ensurePaletteCache(const Tileset& ts) const
{
    // tilesetsChanged invalida alterações internas. Estes campos também
    // protegem trocas de tileset que não emitam o sinal antes do sizeHint().
    if (m_paletteCacheTilesetId == ts.id &&
        m_paletteCacheColumns == ts.columns &&
        m_paletteCacheRows == ts.rows &&
        m_paletteCacheAnimationCount == ts.animatedAutotiles.size() &&
        m_paletteCacheHideAutotiles == m_hideAutotileTiles) return;

    m_paletteCacheTilesetId = ts.id;
    m_paletteCacheColumns = ts.columns;
    m_paletteCacheRows = ts.rows;
    m_paletteCacheAnimationCount = ts.animatedAutotiles.size();
    m_paletteCacheHideAutotiles = m_hideAutotileTiles;
    m_paletteCacheTiles.clear();
    m_paletteCacheIndex.clear();
    m_paletteCacheTiles.reserve(ts.tilecount);
    m_paletteCacheIndex.reserve(ts.tilecount);

    QSet<QPoint> autotileTiles;
    if (m_hideAutotileTiles) {
        const int ownerIdx = activeTilesetIndex();
        for (const TilesetAutotile& autotile : ed.autotiles) {
            if (autotile.tilesetId != ts.id) continue;
            const QVector<QPoint> logical = tilesetAutotileTiles(ed, ownerIdx, autotile);
            for (const QPoint& pt : logical) autotileTiles.insert(pt);
        }
    }

    for (int ty = 0; ty < ts.rows; ++ty) {
        for (int tx = 0; tx < ts.columns; ++tx) {
            int physicalFrame = -1;
            animatedAutotileAt(ts, tx, ty, true, &physicalFrame, nullptr);
            // Frame 0 é o tile lógico; frames > 0 existem só para o runtime.
            if (physicalFrame > 0) continue;
            if (m_hideAutotileTiles && autotileTiles.contains(QPoint(tx, ty))) continue;
            const int visualIndex = m_paletteCacheTiles.size();
            m_paletteCacheTiles.push_back(QPoint(tx, ty));
            m_paletteCacheIndex.insert(paletteTileKey(tx, ty), visualIndex);
        }
    }
}

const QVector<QPoint>& TilesetView::paletteTiles(const Tileset& ts) const
{
    ensurePaletteCache(ts);
    return m_paletteCacheTiles;
}


int TilesetView::visualColumnCount(const Tileset& ts) const
{
    // Somente a paleta de autoria é "dobrada" em blocos de 8 colunas. O
    // Gerenciador continua exibindo o atlas físico exatamente como foi importado.
    return m_tilesetOverride >= 0 ? qMax(1, ts.columns) : 8;
}

int TilesetView::visualRowCount(const Tileset& ts) const
{
    if (m_tilesetOverride >= 0) return qMax(1, ts.rows);
    const int blocks = qMax(1, (qMax(1, ts.columns) + 7) / 8);
    return qMax(1, ts.rows * blocks);
}

QPoint TilesetView::physicalTileAtVisual(const Tileset& ts, int vx, int vy) const
{
    if (vx < 0 || vy < 0 || vx >= visualColumnCount(ts) || vy >= visualRowCount(ts))
        return QPoint(-1, -1);
    int tx = vx, ty = vy;
    if (m_tilesetOverride < 0) {
        const int rowsPerBlock = qMax(1, ts.rows);
        const int block = vy / rowsPerBlock;
        ty = vy % rowsPerBlock;
        tx = block * 8 + vx;
    }
    if (!ts.contains(tx, ty)) return QPoint(-1, -1);
    ensurePaletteCache(ts);
    // Frames auxiliares de animação e Autotiles ocultos permanecem como
    // espaços na grade, em vez de deslocar todos os tiles seguintes.
    if (!m_paletteCacheIndex.contains(paletteTileKey(tx, ty))) return QPoint(-1, -1);
    return QPoint(tx, ty);
}

int TilesetView::paletteVisualIndex(const Tileset& ts, int tx, int ty) const
{
    ensurePaletteCache(ts);
    if (!m_paletteCacheIndex.contains(paletteTileKey(tx, ty))) return -1;
    if (m_tilesetOverride >= 0)
        return ty * qMax(1, ts.columns) + tx;
    const int block = tx / 8;
    const int vx = tx % 8;
    const int vy = block * qMax(1, ts.rows) + ty;
    return vy * 8 + vx;
}

QSize TilesetView::sizeHint() const
{
    const Tileset* ts = activeTileset();
    if (!ts) return QSize(240, 240);
    const int columns = visualColumnCount(*ts);
    const int rows = visualRowCount(*ts);
    const int w = ts->margin * 2 + columns * ts->tilewidth + qMax(0, columns - 1) * ts->spacing;
    const int h = ts->margin * 2 + rows * ts->tileheight + qMax(0, rows - 1) * ts->spacing;
    return QSize(int(w * m_zoom), int(h * m_zoom));
}

void TilesetView::setPaletteZoom(double z)
{
    // No modo responsivo a largura da viewport é a fonte de verdade. Os
    // controles manuais ficam desativados no MainWindow e chamadas de
    // atualização apenas recalculam o encaixe.
    if (m_autoFitWidth && m_tilesetOverride < 0) {
        fitToViewportWidth();
        return;
    }
    m_zoom = clampd(z, 0.10, 6.0);
    updateGeometry();
    resize(sizeHint());
    update();
}

QPoint TilesetView::visualCellAt(const QPoint& p) const
{
    const Tileset* ts = activeTileset();
    if (!ts) return QPoint(-1, -1);
    const double x = p.x() / m_zoom, y = p.y() / m_zoom;
    const int vx = int(std::floor((x - ts->margin) / double(ts->tilewidth + ts->spacing)));
    const int vy = int(std::floor((y - ts->margin) / double(ts->tileheight + ts->spacing)));
    if (vx < 0 || vy < 0 || vx >= visualColumnCount(*ts) || vy >= visualRowCount(*ts))
        return QPoint(-1, -1);
    return QPoint(vx, vy);
}

QPoint TilesetView::tileAt(const QPoint& p) const
{
    const Tileset* ts = activeTileset();
    if (!ts) return QPoint(-1, -1);
    const QPoint visual = visualCellAt(p);
    if (visual.x() < 0) return QPoint(-1, -1);
    return physicalTileAtVisual(*ts, visual.x(), visual.y());
}

QRect TilesetView::paletteRect(const Tileset& ts, int tx, int ty) const
{
    const int index = paletteVisualIndex(ts, tx, ty);
    if (index < 0) return QRect();
    const int columns = visualColumnCount(ts);
    const int vx = index % columns, vy = index / columns;
    return QRect(ts.margin + vx * (ts.tilewidth + ts.spacing),
                 ts.margin + vy * (ts.tileheight + ts.spacing),
                 ts.tilewidth, ts.tileheight);
}

bool TilesetView::paletteSelectionMatchesEditor() const
{
    if (m_paletteSelectionTileset != activeTilesetIndex() || m_paletteSelection.isEmpty()) return false;
    if (ed.session.customStamp.valid()) {
        QSet<QPoint> actual;
        for (const TileRef& ref : ed.session.customStamp.tiles)
            if (ref.tilesetIdx == activeTilesetIndex()) actual.insert(QPoint(ref.tx, ref.ty));
        if (actual.size() != m_paletteSelection.size()) return false;
        for (const QPoint& pt : m_paletteSelection) if (!actual.contains(pt)) return false;
        return true;
    }
    return m_paletteSelection.size() == 1 && ed.session.tsSel.valid() &&
           ed.session.tsSel.tilesetIdx == activeTilesetIndex() && ed.session.tsSel.w == 1 && ed.session.tsSel.h == 1 &&
           m_paletteSelection.first() == QPoint(ed.session.tsSel.x, ed.session.tsSel.y);
}

int TilesetView::collisionSideAt(const QPoint& p) const
{
    const Tileset* ts = activeTileset();
    if (!ts) return 0;
    const double x = p.x() / m_zoom, y = p.y() / m_zoom;
    const double lx = std::fmod(x - ts->margin, ts->tilewidth + ts->spacing) / ts->tilewidth;
    const double ly = std::fmod(y - ts->margin, ts->tileheight + ts->spacing) / ts->tileheight;
    const double edge = 0.28;                      // faixa considerada "borda"
    // A borda mais próxima vence; o miolo devolve 0 (= alternar tudo).
    const double dTop = ly, dBottom = 1.0 - ly, dLeft = lx, dRight = 1.0 - lx;
    const double best = qMin(qMin(dTop, dBottom), qMin(dLeft, dRight));
    if (best > edge) return 0;
    if (best == dTop)    return Editor::SideTop;
    if (best == dBottom) return Editor::SideBottom;
    if (best == dLeft)   return Editor::SideLeft;
    return Editor::SideRight;
}

QString TilesetView::describeCollision(int mask)
{
    if (mask == 0) return tr("livre");
    if (mask == Editor::SideAll) return tr("bloqueado dos 4 lados");
    QStringList sides;
    if (mask & Editor::SideTop)    sides << tr("topo");
    if (mask & Editor::SideRight)  sides << tr("direita");
    if (mask & Editor::SideBottom) sides << tr("baixo");
    if (mask & Editor::SideLeft)   sides << tr("esquerda");
    return tr("bloqueia %1").arg(sides.join(QStringLiteral(", ")));
}

QString TilesetView::wangPositionAt(const QPoint& p) const
{
    const Tileset* ts = activeTileset();
    if (!ts) return QString();
    const double x = p.x() / m_zoom, y = p.y() / m_zoom;
    const double lx = std::fmod(x - ts->margin, ts->tilewidth + ts->spacing) / ts->tilewidth;
    const double ly = std::fmod(y - ts->margin, ts->tileheight + ts->spacing) / ts->tileheight;
    const int cx = lx < 0.34 ? 0 : (lx < 0.67 ? 1 : 2);
    const int cy = ly < 0.34 ? 0 : (ly < 0.67 ? 1 : 2);
    static const char* grid[3][3] = {
        { "tl", "t", "tr" },
        { "l",  "",  "r"  },
        { "bl", "b", "br" }
    };
    return QString::fromLatin1(grid[cy][cx]);
}

void TilesetView::locateTile(int tilesetIdx, int tx, int ty)
{
    if (tilesetIdx != activeTilesetIndex()) {
        if (m_tilesetOverride >= 0) {
            setTilesetOverride(tilesetIdx);
        } else {
            ed.session.activeTilesetIdx = tilesetIdx;
            emit ed.tilesetsChanged();
        }
    }
    const Tileset* ts = activeTileset();
    if (!ts) return;
    if (QScrollArea* area = qobject_cast<QScrollArea*>(parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
        const QRect r = paletteRect(*ts, tx, ty);
        if (r.isValid()) area->ensureVisible(int(r.center().x() * m_zoom), int(r.center().y() * m_zoom), 60, 60);
    }
    update();
}

// -------------------------------------------------------------------- pintura
void TilesetView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#1e1e1e"));

    const Tileset* ts = activeTileset();
    if (!ts) {
        p.setPen(QColor("#777"));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                   tr("Nenhum tileset carregado.\n\nUse Tileset ▸ Novo tileset… (Ctrl+T)\npara carregar uma imagem."));
        return;
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.scale(m_zoom, m_zoom);

    const int tw = ts->tilewidth, th = ts->tileheight;
    const int visualColumns = visualColumnCount(*ts);
    const QVector<QPoint>& visibleTiles = paletteTiles(*ts);
    const int visualRows = visualRowCount(*ts);
    const QPixmap& atlas = pixmapCache().pixmap(ed, activeTilesetIndex());

    // Exibição no estilo RPG Maker: o atlas NÃO é alterado. Na paleta
    // principal, cada faixa física de 8 colunas é colocada abaixo da anterior.
    // Ex.: colunas 0..7 (todas as linhas), depois 8..15 (todas as linhas).
    // Tiles transparentes continuam ocupando sua célula e podem ser selecionados.
    for (const QPoint& physical : visibleTiles) {
        const QRect dst = paletteRect(*ts, physical.x(), physical.y());
        if (dst.isValid()) p.drawPixmap(dst, atlas, ts->tileRect(physical.x(), physical.y()));
    }

    // grade visual; espaços sem tile físico no último bloco permanecem vazios.
    if (m_zoom * tw >= 6) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 26), 0));
        for (int x = 0; x <= visualColumns; ++x) {
            const int px = ts->margin + x * (tw + ts->spacing);
            p.drawLine(px, ts->margin, px, ts->margin + visualRows * (th + ts->spacing));
        }
        for (int y = 0; y <= visualRows; ++y) {
            const int py = ts->margin + y * (th + ts->spacing);
            p.drawLine(ts->margin, py, ts->margin + visualColumns * (tw + ts->spacing), py);
        }
    }

    // pool aleatorio
    if (!ed.randomPool.isEmpty()) {
        for (const RandomEntry& e : ed.randomPool) {
            if (e.tilesetIdx != activeTilesetIndex()) continue;
            p.setPen(QPen(QColor("#c58af9"), 2.0 / m_zoom));
            p.setBrush(QColor(197, 138, 249, 46));
            for (int py=e.y; py<e.y+e.h; ++py) for (int px=e.x; px<e.x+e.w; ++px) {
                const QRect r=paletteRect(*ts,px,py);
                if(r.isValid())p.drawRect(r);
            }
        }
        p.setBrush(Qt::NoBrush);
    }

    // Marcadores de passagem aparecem somente enquanto a ferramenta
    // correspondente da toolbar está ativa. Isso mantém a paleta limpa no
    // desenho normal sem esconder os dados persistidos do tileset.
    QFont f = p.font();
    f.setPixelSize(qMax(6, int(th * 0.45)));
    p.setFont(f);
    for (int ty = 0; ty < ts->rows; ++ty)
        for (int tx = 0; tx < ts->columns; ++tx) {
            const QRect r = paletteRect(*ts, tx, ty);
            if (!r.isValid()) continue;
            if (m_priorityMode || ed.session.starMarkMode) {
                const int priority = ed.tilePriority(activeTilesetIndex(), tx, ty);
                // Quanto maior a prioridade, mais forte o realce. O número
                // permanece legível mesmo em tiles claros/escuros.
                if (priority > 0) {
                    p.fillRect(r, QColor(255, 196, 82, 22 + priority * 16));
                    p.setPen(QPen(QColor(255, 212, 94, 135 + priority * 20),
                                  qMax(1.0, (0.7 + priority * 0.18) / m_zoom)));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(r.adjusted(1, 1, -1, -1));
                }
                p.setPen(priority > 0 ? QColor("#fff0b0") : QColor(235,235,235,190));
                p.drawText(r.adjusted(2, 1, -2, -1), Qt::AlignTop | Qt::AlignRight,
                           QString::number(priority));
            }
            const int cm = (m_collisionMode || ed.session.collisionMarkMode)
                ? ed.collisionMask(activeTilesetIndex(), tx, ty) : 0;
            if (cm) {
                if (m_collisionMode) {
                    // A mascara detalhada so e visualizada/editada na
                    // superficie tecnica do Gerenciador de Tilesets.
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(255, 107, 107, 210));
                    const double th2 = qMax(2.0, r.height() * 0.16);
                    if (cm & Editor::SideTop)    p.fillRect(QRectF(r.x(), r.y(), r.width(), th2), QColor(255,107,107,210));
                    if (cm & Editor::SideBottom) p.fillRect(QRectF(r.x(), r.bottom() - th2, r.width(), th2), QColor(255,107,107,210));
                    if (cm & Editor::SideLeft)   p.fillRect(QRectF(r.x(), r.y(), th2, r.height()), QColor(255,107,107,210));
                    if (cm & Editor::SideRight)  p.fillRect(QRectF(r.right() - th2, r.y(), th2, r.height()), QColor(255,107,107,210));
                    p.setBrush(Qt::NoBrush);
                    if (cm == Editor::SideAll) {
                        p.setPen(QColor("#ff6b6b"));
                        p.drawText(r, Qt::AlignCenter, QStringLiteral("✖"));
                    }
                } else {
                    // Editor principal: somente estado simples. Mascara parcial
                    // vinda do Gerenciador e indicada, mas seus lados nao sao
                    // expostos como affordance de edicao.
                    p.setPen(QColor("#ff6b6b"));
                    p.drawText(r, Qt::AlignCenter,
                               cm == Editor::SideAll ? QStringLiteral("✖") : QStringLiteral("◐"));
                }
            }
            if (m_reflectionOverlay) {
                const QString preset = ed.tileReflectionPreset(activeTilesetIndex(), tx, ty);
                if (!preset.isEmpty()) {
                    p.setPen(QPen(QColor(110, 210, 255, 230), qMax(1.0, 1.2 / m_zoom)));
                    p.setBrush(QColor(50, 150, 210, 38));
                    p.drawRect(r.adjusted(1,1,-1,-1));
                    p.setBrush(Qt::NoBrush);
                    p.drawText(r.adjusted(2,1,-2,-1), Qt::AlignBottom | Qt::AlignLeft, QStringLiteral("≈"));
                }
            }
            const double prob = ed.tileProb(activeTilesetIndex(), tx, ty);
            if (!qFuzzyCompare(prob, 1.0)) {
                p.setPen(QColor("#8fd18f"));
                p.drawText(r, Qt::AlignBottom | Qt::AlignRight, QString::number(prob, 'g', 2));
            }
        }

    // rotulos Wang do set ativo
    const WangSet* ws = ed.activeWangSet();
    if (ws && m_wangOverlay) {
        for (auto it = ws->tiles.constBegin(); it != ws->tiles.constEnd(); ++it) {
            int tsIdx, tx, ty;
            if (!WangSet::parseTileKey(it.key(), &tsIdx, &tx, &ty)) continue;
            if (tsIdx != activeTilesetIndex() || !ts->contains(tx, ty)) continue;
            const QRect r = paletteRect(*ts, tx, ty);
            if (!r.isValid()) continue;
            const WangTileData& d = it.value();
            struct { const char* pos; double fx, fy; } spots[] = {
                { "tl", 0.16, 0.16 }, { "t", 0.5, 0.14 }, { "tr", 0.84, 0.16 },
                { "l",  0.14, 0.5  }, { "r", 0.86, 0.5  },
                { "bl", 0.16, 0.84 }, { "b", 0.5, 0.86 }, { "br", 0.84, 0.84 }
            };
            for (const auto& s : spots) {
                const int cid = d.get(QString::fromLatin1(s.pos));
                if (cid < 0) continue;
                const WangColor* wc = ws->colorById(cid);
                p.setPen(Qt::NoPen);
                p.setBrush(wc ? wc->color : QColor("#4a90d7"));
                const double rad = qMax(1.5, th * 0.11);
                p.drawEllipse(QPointF(r.x() + r.width() * s.fx, r.y() + r.height() * s.fy), rad, rad);
            }
        }
        p.setBrush(Qt::NoBrush);
    }

    // Seleção atual. Como a paleta é apenas reorganizada visualmente, mantemos
    // as coordenadas físicas exatas do atlas para pintura/exportação.
    if (paletteSelectionMatchesEditor()) {
        p.setPen(QPen(QColor("#4a90d7"), 2.0 / m_zoom));
        p.setBrush(QColor(74, 144, 215, 40));
        for (const QPoint& pt : m_paletteSelection) {
            const QRect r = paletteRect(*ts, pt.x(), pt.y());
            if (r.isValid()) p.drawRect(r);
        }
        p.setBrush(Qt::NoBrush);
    } else if (ed.session.tsSel.valid() && ed.session.tsSel.tilesetIdx == activeTilesetIndex() && !ed.session.customStamp.valid()) {
        p.setPen(QPen(QColor("#4a90d7"), 2.0 / m_zoom));
        p.setBrush(QColor(74, 144, 215, 40));
        for(int sy=ed.session.tsSel.y;sy<ed.session.tsSel.y+ed.session.tsSel.h;++sy)
            for(int sx=ed.session.tsSel.x;sx<ed.session.tsSel.x+ed.session.tsSel.w;++sx){
                const QRect r=paletteRect(*ts,sx,sy);
                if(r.isValid())p.drawRect(r);
            }
        p.setBrush(Qt::NoBrush);
    }
}

// ---------------------------------------------------------------- prioridade
void TilesetView::applyPriorityAt(const QPoint& tile, bool allowCycle)
{
    const Tileset* ts = activeTileset();
    if (!ts || !ts->contains(tile.x(), tile.y())) return;
    const int value = (allowCycle || m_priorityPaintValue < 0)
        ? (ed.tilePriority(activeTilesetIndex(), tile.x(), tile.y()) + 1) % 6
        : m_priorityPaintValue;
    ed.setTilePriority(activeTilesetIndex(), tile.x(), tile.y(), value);
    emit statusMessage(tr("Tile (%1,%2) · prioridade %3")
                           .arg(tile.x()).arg(tile.y()).arg(value));
    update();
}

void TilesetView::applyCollisionAt(const QPoint& tile)
{
    const Tileset* ts = activeTileset();
    if (!ts || !ts->contains(tile.x(), tile.y()) || m_collisionPaintMask < 0) return;
    ed.setCollisionMask(activeTilesetIndex(), tile.x(), tile.y(), m_collisionPaintMask);
    emit statusMessage(tr("Tile (%1,%2) · colisão: %3")
                           .arg(tile.x()).arg(tile.y()).arg(describeCollision(m_collisionPaintMask)));
    update();
}

QVector<QPoint> TilesetView::selectionTargetsFor(const QPoint& anchorTile) const
{
    QVector<QPoint> points;
    const Tileset* active = activeTileset();
    if (!active) return points;

    if (paletteSelectionMatchesEditor() && m_paletteSelection.size() > 1 &&
        m_paletteSelection.contains(anchorTile))
        return m_paletteSelection;

    const TilesetSelection& sel = ed.session.tsSel;
    const bool inside = sel.valid() && sel.tilesetIdx == activeTilesetIndex() &&
                        anchorTile.x() >= sel.x && anchorTile.x() < sel.x + sel.w &&
                        anchorTile.y() >= sel.y && anchorTile.y() < sel.y + sel.h;
    if (inside && (sel.w > 1 || sel.h > 1)) {
        for (const QPoint& pt : paletteTiles(*active))
            if (pt.x() >= sel.x && pt.x() < sel.x + sel.w &&
                pt.y() >= sel.y && pt.y() < sel.y + sel.h)
                points.push_back(pt);
    } else if (active->contains(anchorTile.x(), anchorTile.y())) {
        points.push_back(anchorTile);
    }
    return points;
}

QVector<QPoint> TilesetView::currentSelectionTiles() const
{
    QVector<QPoint> points;
    const Tileset* active = activeTileset();
    if (!active) return points;

    if (paletteSelectionMatchesEditor() && !m_paletteSelection.isEmpty())
        return m_paletteSelection;

    const TilesetSelection& sel = ed.session.tsSel;
    if (!sel.valid() || sel.tilesetIdx != activeTilesetIndex()) return points;
    for (const QPoint& pt : paletteTiles(*active))
        if (pt.x() >= sel.x && pt.x() < sel.x + sel.w &&
            pt.y() >= sel.y && pt.y() < sel.y + sel.h)
            points.push_back(pt);
    return points;
}

void TilesetView::applyPriorityBottomUpToSelection()
{
    const QVector<QPoint> targets = currentSelectionTiles();
    if (targets.isEmpty()) {
        emit statusMessage(tr("Selecione um ou mais tiles antes de distribuir a prioridade."));
        return;
    }
    const int rows = ed.applyTilePrioritiesBottomUp(activeTilesetIndex(), targets);
    if (rows <= 0) return;
    if (rows > 5)
        emit statusMessage(tr("Prioridade de baixo para cima aplicada a %1 tile(s), em %2 linhas; "
                              "as linhas acima do 5º nível ficaram em prioridade 5.")
                           .arg(targets.size()).arg(rows));
    else
        emit statusMessage(tr("Prioridade de baixo para cima aplicada a %1 tile(s), em %2 linha(s).")
                           .arg(targets.size()).arg(rows));
    update();
}

// ------------------------------------------------------------------- eventos
void TilesetView::mousePressEvent(QMouseEvent* e)
{
    const QPoint visual = visualCellAt(e->pos());
    const QPoint t = tileAt(e->pos());
    if (t.x() < 0 || visual.x() < 0) return;

    // O botao direito NAO altera a selecao aqui: quem decide e o
    // contextMenuEvent(). Sem isto, clicar com o direito sobre um bloco
    // multi-tile ja selecionado o reduzia a 1x1 e o menu "Criar terreno
    // usando preset" (que depende do tamanho do bloco) nunca aparecia.
    if (e->button() == Qt::RightButton) return;

    // Ctrl + clique = adicionar/remover do pool aleatorio (igual ao original).
    if (e->modifiers() & Qt::ControlModifier) {
        m_poolDrag = true;
        m_selStart = m_selEnd = t;
        m_selStartVisual = m_selEndVisual = visual;
        return;
    }
    auto beginSelection = [this, t, visual]() {
        m_selecting = true;
        m_selStart = m_selEnd = t;
        m_selStartVisual = m_selEndVisual = visual;
        m_paletteSelection = {t};
        m_paletteSelectionTileset = activeTilesetIndex();
        ed.session.customStamp.clear();
        ed.session.tsSel = TilesetSelection{ activeTilesetIndex(), t.x(), t.y(), 1, 1 };
        emit ed.selectionChanged();
        update();
    };

    // A aba de prioridade preserva o comportamento de pincel/ciclo. Os modos
    // da toolbar ★ e ✖ usam clique para aplicar, mas arraste para selecionar:
    // assim nao e mais necessario desativar o marcador para formar um bloco.
    if (m_priorityMode) {
        const QVector<QPoint> targets = selectionTargetsFor(t);
        const int targetPriority = m_priorityPaintValue >= 0
            ? m_priorityPaintValue
            : (ed.tilePriority(activeTilesetIndex(), t.x(), t.y()) + 1) % 6;
        for (const QPoint& pt : targets)
            ed.setTilePriority(activeTilesetIndex(), pt.x(), pt.y(), targetPriority);
        m_priorityPainting = m_priorityPaintValue >= 0 && e->button() == Qt::LeftButton;
        m_lastPriorityTile = t;
        emit statusMessage(tr("Prioridade %1 aplicada a %2 tile(s).")
                               .arg(targetPriority).arg(targets.size()));
        update();
        return;
    }

    if (m_collisionMode) {
        const QVector<QPoint> targets = selectionTargetsFor(t);
        if (m_collisionPaintMask >= 0) {
            for (const QPoint& pt : targets)
                ed.setCollisionMask(activeTilesetIndex(), pt.x(), pt.y(), m_collisionPaintMask);
            m_collisionPainting = e->button() == Qt::LeftButton;
            m_lastCollisionTile = t;
            emit statusMessage(tr("Colisão %1 aplicada a %2 tile(s).")
                                   .arg(describeCollision(m_collisionPaintMask)).arg(targets.size()));
        } else {
            const int side = collisionSideAt(e->pos());
            const int clickedMask = ed.collisionMask(activeTilesetIndex(), t.x(), t.y());
            if (side) {
                const bool block = !(clickedMask & side);
                for (const QPoint& pt : targets) {
                    int mask = ed.collisionMask(activeTilesetIndex(), pt.x(), pt.y());
                    mask = block ? (mask | side) : (mask & ~side);
                    ed.setCollisionMask(activeTilesetIndex(), pt.x(), pt.y(), mask);
                }
                emit statusMessage(tr("Lado de colisão alterado em %1 tile(s).").arg(targets.size()));
            } else {
                const int targetMask = clickedMask == Editor::SideAll ? 0 : Editor::SideAll;
                for (const QPoint& pt : targets)
                    ed.setCollisionMask(activeTilesetIndex(), pt.x(), pt.y(), targetMask);
                emit statusMessage(tr("Colisão %1 aplicada a %2 tile(s).")
                                       .arg(describeCollision(targetMask)).arg(targets.size()));
            }
        }
        update();
        return;
    }

    if (ed.session.starMarkMode) {
        const QVector<QPoint> targets = selectionTargetsFor(t);
        if (targets.size() > 1) {
            const int targetPriority = (ed.tilePriority(activeTilesetIndex(), t.x(), t.y()) + 1) % 6;
            for (const QPoint& pt : targets)
                ed.setTilePriority(activeTilesetIndex(), pt.x(), pt.y(), targetPriority);
            emit statusMessage(tr("Prioridade %1 aplicada a %2 tile(s).")
                                   .arg(targetPriority).arg(targets.size()));
            update();
            return;
        }
        m_pendingMarkerTool = PendingMarkerTool::Star;
        m_pendingMarkerTile = t;
        beginSelection();
        return;
    }

    if (ed.session.collisionMarkMode) {
        const QVector<QPoint> targets = selectionTargetsFor(t);
        if (targets.size() > 1) {
            const bool block = !ed.isTileFullyBlocked(activeTilesetIndex(), t.x(), t.y());
            for (const QPoint& pt : targets)
                ed.setTileBlocked(activeTilesetIndex(), pt.x(), pt.y(), block);
            emit statusMessage(tr("Colisão simples %1 aplicada a %2 tile(s) selecionado(s).")
                                   .arg(block ? tr("Bloqueado") : tr("Livre"))
                                   .arg(targets.size()));
            update();
            return;
        }
        m_pendingMarkerTool = PendingMarkerTool::Collision;
        m_pendingMarkerTile = t;
        beginSelection();
        return;
    }

    if (m_wangOverlay && e->button() == Qt::LeftButton) {
        const QString pos = wangPositionAt(e->pos());
        if (!pos.isEmpty()) {
            emit wangTileClicked(activeTilesetIndex(), t.x(), t.y(), pos);
            update();
            return;
        }
    }

    m_selecting = true;
    m_selStart = m_selEnd = t;
    m_selStartVisual = m_selEndVisual = visual;
    m_paletteSelection = {t};
    m_paletteSelectionTileset = activeTilesetIndex();
    ed.session.customStamp.clear();
    ed.session.tsSel = TilesetSelection{ activeTilesetIndex(), t.x(), t.y(), 1, 1 };
    emit regularTilePicked(activeTilesetIndex(), t.x(), t.y());
    emit ed.selectionChanged();
    update();
}

void TilesetView::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint visual = visualCellAt(e->pos());
    const QPoint t = tileAt(e->pos());
    if (t.x() >= 0) {
        const Tileset* ts = activeTileset();
        if (ts) {
            const int id = t.y() * ts->columns + t.x();
            const int priority = ed.tilePriority(activeTilesetIndex(), t.x(), t.y());
            emit statusMessage(tr("Tile (%1,%2) · id %3 · gid %4 · prioridade %5")
                                   .arg(t.x()).arg(t.y()).arg(id)
                                   .arg(ed.gidFor(activeTilesetIndex(), t.x(), t.y()))
                                   .arg(priority));
        }
    }
    if (m_priorityPainting && m_priorityMode && m_priorityPaintValue >= 0 &&
        (e->buttons() & Qt::LeftButton) && t.x() >= 0 && t != m_lastPriorityTile) {
        applyPriorityAt(t, false);
        m_lastPriorityTile = t;
        return;
    }
    if (m_collisionPainting && m_collisionMode && m_collisionPaintMask >= 0 &&
        (e->buttons() & Qt::LeftButton) && t.x() >= 0 && t != m_lastCollisionTile) {
        applyCollisionAt(t);
        m_lastCollisionTile = t;
        return;
    }
    if ((m_selecting || m_poolDrag) && visual.x() >= 0) {
        m_selEndVisual = visual;
        const Tileset* active = activeTileset();
        QVector<QPoint> selected;
        int vx0=0,vx1=0,vy0=0,vy1=0;
        if (active) {
            vx0=qMin(m_selStartVisual.x(),m_selEndVisual.x());vx1=qMax(m_selStartVisual.x(),m_selEndVisual.x());
            vy0=qMin(m_selStartVisual.y(),m_selEndVisual.y());vy1=qMax(m_selStartVisual.y(),m_selEndVisual.y());
            for(int vy=vy0;vy<=vy1;++vy)for(int vx=vx0;vx<=vx1;++vx){
                const QPoint physical = physicalTileAtVisual(*active, vx, vy);
                if (physical.x() >= 0) selected.push_back(physical);
            }
        }
        if(!selected.isEmpty()){
            int minX=selected.first().x(),maxX=minX,minY=selected.first().y(),maxY=minY;
            for(const QPoint&pt:selected){minX=qMin(minX,pt.x());maxX=qMax(maxX,pt.x());minY=qMin(minY,pt.y());maxY=qMax(maxY,pt.y());}
            m_selStart=QPoint(minX,minY);m_selEnd=QPoint(maxX,maxY);
            m_paletteSelection=selected;m_paletteSelectionTileset=activeTilesetIndex();
            if(m_selecting){
                ed.session.tsSel=TilesetSelection{activeTilesetIndex(),minX,minY,maxX-minX+1,maxY-minY+1};
                if(selected.size()>1){
                    CustomStamp stamp;stamp.w=vx1-vx0+1;stamp.h=vy1-vy0+1;
                    for(int vy=vy0;vy<=vy1;++vy)for(int vx=vx0;vx<=vx1;++vx){
                        const QPoint physical = physicalTileAtVisual(*active, vx, vy);
                        if (physical.x() < 0) continue;
                        TileRef ref;ref.tilesetIdx=activeTilesetIndex();ref.tx=physical.x();ref.ty=physical.y();
                        stamp.tiles.push_back(ref);stamp.offsets.push_back(QPoint(vx-vx0,vy-vy0));
                    }
                    ed.session.customStamp=stamp;
                }else ed.session.customStamp.clear();
                emit ed.selectionChanged();
            }
        }
        update();
    }
}

void TilesetView::mouseReleaseEvent(QMouseEvent*)
{
    m_priorityPainting = false;
    m_lastPriorityTile = QPoint(-1, -1);
    m_collisionPainting = false;
    m_lastCollisionTile = QPoint(-1, -1);
    if (m_poolDrag) {
        const int x = qMin(m_selStart.x(), m_selEnd.x()), y = qMin(m_selStart.y(), m_selEnd.y());
        const int w = qAbs(m_selEnd.x() - m_selStart.x()) + 1;
        const int h = qAbs(m_selEnd.y() - m_selStart.y()) + 1;
        ed.addRectToRandomPool(activeTilesetIndex(), x, y, w, h);
        emit statusMessage(tr("Pool aleatório: %1 entrada(s).").arg(ed.randomPool.size()));
        m_poolDrag = false;
    }

    if (m_pendingMarkerTool != PendingMarkerTool::None) {
        const QVector<QPoint> targets = currentSelectionTiles();
        if (targets.size() > 1) {
            emit statusMessage(tr("%1 tiles selecionados. Clique dentro da seleção para aplicar %2 a todos.")
                               .arg(targets.size())
                               .arg(m_pendingMarkerTool == PendingMarkerTool::Star ? tr("★/prioridade") : tr("✖/colisão")));
        } else if (!targets.isEmpty()) {
            if (m_pendingMarkerTool == PendingMarkerTool::Star) {
                const int targetPriority = (ed.tilePriority(activeTilesetIndex(), m_pendingMarkerTile.x(),
                                                            m_pendingMarkerTile.y()) + 1) % 6;
                for (const QPoint& pt : targets)
                    ed.setTilePriority(activeTilesetIndex(), pt.x(), pt.y(), targetPriority);
                emit statusMessage(tr("Prioridade %1 aplicada ao tile.").arg(targetPriority));
            } else {
                const bool block = !ed.isTileFullyBlocked(activeTilesetIndex(),
                                                           m_pendingMarkerTile.x(),
                                                           m_pendingMarkerTile.y());
                for (const QPoint& pt : targets)
                    ed.setTileBlocked(activeTilesetIndex(), pt.x(), pt.y(), block);
                emit statusMessage(block ? tr("Colisão simples: tile inteiro bloqueado.")
                                         : tr("Colisão simples: tile livre."));
            }
        }
        m_pendingMarkerTool = PendingMarkerTool::None;
        m_pendingMarkerTile = QPoint(-1, -1);
    }

    m_selecting = false;
    update();
}

void TilesetView::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPoint t = tileAt(e->pos());
    if (t.x() >= 0) emit tileDoubleClicked(activeTilesetIndex(), t.x(), t.y());
}

void TilesetView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        if (!m_autoFitWidth)
            setPaletteZoom(m_zoom * (e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15));
        e->accept();
        return;
    }
    e->ignore();     // deixa o QScrollArea rolar verticalmente
}

void TilesetView::contextMenuEvent(QContextMenuEvent* e)
{
    const QPoint t = tileAt(e->pos());
    if (t.x() < 0) return;
    const int tsIdx = activeTilesetIndex();

    // Clique direito FORA de um bloco multi-tile seleciona aquele tile (1x1),
    // como na versao web; dentro do bloco, a selecao e preservada para que o
    // menu de presets saiba o tamanho.
    {
        const TilesetSelection& s0 = ed.session.tsSel;
        const bool inside = s0.valid() && s0.tilesetIdx == tsIdx &&
                            t.x() >= s0.x && t.x() < s0.x + s0.w &&
                            t.y() >= s0.y && t.y() < s0.y + s0.h;
        const bool multi = s0.valid() && (s0.w > 1 || s0.h > 1);
        const bool exactInside = paletteSelectionMatchesEditor() && m_paletteSelection.contains(t);
        if (!((inside && multi) || (exactInside && m_paletteSelection.size() > 1))) {
            m_paletteSelection = {t};m_paletteSelectionTileset = tsIdx;
            ed.session.customStamp.clear();
            ed.session.tsSel = TilesetSelection{ tsIdx, t.x(), t.y(), 1, 1 };
            emit regularTilePicked(tsIdx, t.x(), t.y());
            emit ed.selectionChanged();
            update();
        }
    }

    const auto selectionTargets = [this, tsIdx, t]() {
        QVector<QPoint> points;
        const Tileset* active = ed.tilesetAt(tsIdx);
        if (!active) return points;
        if (paletteSelectionMatchesEditor() && m_paletteSelection.size() > 1 && m_paletteSelection.contains(t))
            return m_paletteSelection;
        const TilesetSelection& sel = ed.session.tsSel;
        const bool inside = sel.valid() && sel.tilesetIdx == tsIdx &&
                            t.x() >= sel.x && t.x() < sel.x+sel.w &&
                            t.y() >= sel.y && t.y() < sel.y+sel.h;
        if (inside && (sel.w>1 || sel.h>1)) {
            QSet<QPoint> unique;
            for(int y=sel.y;y<sel.y+sel.h;++y)for(int x=sel.x;x<sel.x+sel.w;++x)
                unique.insert(canonicalAnimatedTile(*active,x,y));
            for(const QPoint& pt:unique)points.push_back(pt);
        } else points.push_back(t);
        return points;
    };

    QMenu menu(this);
    {
        QMenu* priority = menu.addMenu(tr("Prioridade de tile — %1").arg(ed.tilePriority(tsIdx, t.x(), t.y())));
        priority->addAction(tr("Ciclar 0 → 1 → 2 → 3 → 4 → 5"),
                            [this, tsIdx, t, selectionTargets] {
            const int value = (ed.tilePriority(tsIdx, t.x(), t.y()) + 1) % 6;
            for (const QPoint& pt : selectionTargets()) ed.setTilePriority(tsIdx, pt.x(), pt.y(), value);
        });
        priority->addSeparator();
        for (int value = 0; value <= 5; ++value) {
            QAction* action = priority->addAction(tr("Prioridade %1").arg(value));
            action->setCheckable(true);
            action->setChecked(ed.tilePriority(tsIdx, t.x(), t.y()) == value);
            connect(action, &QAction::triggered, this, [this, tsIdx, selectionTargets, value] {
                for (const QPoint& pt : selectionTargets()) ed.setTilePriority(tsIdx, pt.x(), pt.y(), value);
            });
        }
        priority->addSeparator();
        priority->addAction(tr("Distribuir de baixo para cima"), [this, tsIdx, selectionTargets] {
            const QVector<QPoint> targets = selectionTargets();
            const int rows = ed.applyTilePrioritiesBottomUp(tsIdx, targets);
            if (rows > 0)
                emit statusMessage(tr("Prioridade distribuída de baixo para cima em %1 linha(s).").arg(rows));
            update();
        });
    }
    {
        const int cm = ed.collisionMask(tsIdx, t.x(), t.y());
        QMenu* col = menu.addMenu(tr("✖ Colisão — %1").arg(describeCollision(cm)));
        col->addAction(tr("Bloquear tile inteiro"), [this, tsIdx, selectionTargets] {
            for (const QPoint& pt : selectionTargets()) ed.setTileBlocked(tsIdx, pt.x(), pt.y(), true);
        });
        col->addAction(tr("Deixar tile livre"), [this, tsIdx, selectionTargets] {
            for (const QPoint& pt : selectionTargets()) ed.setTileBlocked(tsIdx, pt.x(), pt.y(), false);
        });
        if (m_collisionMode) {
            // Edicao por lados e uma capacidade exclusiva da superficie
            // tecnica do Gerenciador de Tilesets.
            col->addSeparator();
            struct SideDef { int side; const char* label; };
            static const SideDef sides[] = {
                { Editor::SideTop,    "Bloquear topo" },
                { Editor::SideRight,  "Bloquear direita" },
                { Editor::SideBottom, "Bloquear baixo" },
                { Editor::SideLeft,   "Bloquear esquerda" }
            };
            for (const SideDef& sd : sides) {
                QAction* a = col->addAction(tr(sd.label));
                a->setCheckable(true);
                a->setChecked(cm & sd.side);
                connect(a, &QAction::triggered, this, [this, tsIdx, t, selectionTargets, side = sd.side] {
                    const bool block = !(ed.collisionMask(tsIdx, t.x(), t.y()) & side);
                    for (const QPoint& pt : selectionTargets()) {
                        int mask = ed.collisionMask(tsIdx, pt.x(), pt.y());
                        ed.setCollisionMask(tsIdx, pt.x(), pt.y(), block ? (mask | side) : (mask & ~side));
                    }
                });
            }
        } else {
            col->addSeparator();
            QAction* hint = col->addAction(tr("Colisão por lados: Gerenciador de Tilesets → Prioridade e Colisão"));
            hint->setEnabled(false);
        }
    }
    menu.addSeparator();
    menu.addAction(tr("Adicionar seleção ao pool aleatório"), [this, tsIdx] {
        if (ed.session.tsSel.valid())
            ed.addRectToRandomPool(tsIdx, ed.session.tsSel.x, ed.session.tsSel.y, ed.session.tsSel.w, ed.session.tsSel.h);
    });
    menu.addAction(tr("Limpar pool aleatório"), [this] { ed.clearRandomPool(); });
    // ---- Gerar autotile a partir da seleção -------------------------------
    // Junta a paleta com o conversor: o recorte do próprio chipset vira o
    // atlas bitmask, sem reimportar a imagem.
    {
        const TilesetSelection sel = ed.session.tsSel;
        const bool selHere = sel.valid() && sel.tilesetIdx == tsIdx;
        const int sc = selHere ? sel.w : 1;
        const int sr = selHere ? sel.h : 1;
        const int mode = autotile::suggestModeForSelection(sc, sr);
        QString label;
        if (mode) {
            const QSize blocks = autotile::blocksInSelection(sc, sr, mode);
            const int n = blocks.width() * blocks.height();
            label = n > 1 ? tr("Gerar autotiles (%1×%2 — %3 blocos de %4)")
                                .arg(sc).arg(sr).arg(n).arg(autotile::modeName(mode))
                          : tr("Gerar autotile (%1)").arg(autotile::modeName(mode));
        } else {
            label = tr("Gerar autotile (seleção %1×%2 não suportada)").arg(sc).arg(sr);
        }
        QAction* gen = menu.addAction(label);
        gen->setEnabled(mode != 0);
        if (!mode)
            gen->setStatusTip(tr("Selecione um bloco de 1×1, 2×2, 2×3 ou 3×4 tiles (ou um múltiplo)."));
        const int gx = selHere ? sel.x : t.x();
        const int gy = selHere ? sel.y : t.y();
        connect(gen, &QAction::triggered, this, [this, tsIdx, gx, gy, sc, sr] {
            GenerateAutotileDialog dlg(ed, tsIdx, gx, gy, sc, sr, this);
            dlg.exec();
            update();
        });
    }
    menu.addSeparator();

    QMenu* prob = menu.addMenu(tr("Probabilidade do tile"));
    for (double v : { 0.1, 0.25, 0.5, 1.0, 2.0, 4.0 })
        prob->addAction(QString::number(v), [this, tsIdx, t, v] {
            ed.setTileProb(tsIdx, t.x(), t.y(), v);
        });

    WangSet* ws = ed.activeWangSet();
    // Wang/Terrain e autoria tecnica do Gerenciador. A paleta normal do Editor
    // nao expoe mais comandos paralelos que possam editar a mesma fonte.
    if (ws && m_tilesetOverride >= 0) {
        menu.addSeparator();

        // ---- Tile isolado (mascara 0) ----------------------------------------
        // Um tile que JÁ é isolado deixa de aparecer na opção de selecionar
        // novos tiles isolados. A remoção continua disponível, mas como ação
        // separada — assim a lista contém apenas candidatos ainda livres.
        const QString isolatedKey = WangSet::tileKeyOf(tsIdx, t.x(), t.y());
        const WangTileData isolatedData = ws->tiles.value(isolatedKey);
        if (isolatedData.isolatedColorId >= 0) {
            const WangColor* currentColor=ws->colorById(isolatedData.isolatedColorId);
            menu.addAction(tr("Remover definição de tile isolado%1")
                               .arg(currentColor?tr(" de “%1”").arg(currentColor->name):QString()),
                           [this,ws,isolatedKey,t,currentColor]{
                const QString name=currentColor?currentColor->name:QString();
                ws->tiles.remove(isolatedKey);ed.markDirty();emit ed.wangChanged();update();
                emit statusMessage(tr("Tile (%1, %2) removido dos tiles isolados%3.")
                    .arg(t.x()).arg(t.y()).arg(name.isEmpty()?QString():tr(" de “%1”").arg(name)));
            });
        } else {
            QMenu* isoMenu = menu.addMenu(tr("Tile isolado (sem conexões)"));
            if (ws->colors.isEmpty()) {
                QAction* none = isoMenu->addAction(tr("Crie um terreno primeiro"));
                none->setEnabled(false);
            } else {
                for (const WangColor& c : ws->colors) {
                    isoMenu->addAction(tr("Definir como tile isolado de “%1”").arg(c.name),
                                       [this, ws, isolatedKey, cid = c.id, name = c.name, t] {
                        WangTileData d; d.isolatedColorId = cid; ws->tiles.insert(isolatedKey, d);
                        ed.markDirty(); emit ed.wangChanged(); update();
                        emit statusMessage(tr("Tile (%1, %2) definido como isolado de “%3”.")
                                               .arg(t.x()).arg(t.y()).arg(name));
                    });
                }
            }
        }

        // ---- Icones -----------------------------------------------------------
        menu.addSeparator();
        menu.addAction(tr("Usar como ícone do terreno selecionado"), [this, ws, tsIdx, t] {
            WangColor* c = ws->colorById(ed.session.activeWangColorId);
            if (!c) {
                emit statusMessage(tr("Selecione primeiro um terreno na seção de conexões do Autotile."));
                return;
            }
            c->hasIcon = true; c->iconTilesetIdx = tsIdx; c->iconTx = t.x(); c->iconTy = t.y();
            ed.markDirty();
            emit ed.wangChanged();
            emit statusMessage(tr("Ícone do terreno “%1” definido como o tile (%2, %3).")
                                   .arg(c->name).arg(t.x()).arg(t.y()));
        });
        menu.addAction(tr("Usar como ícone do Conjunto de conexões"), [this, ws, tsIdx, t] {
            ws->hasIcon = true; ws->iconTilesetIdx = tsIdx; ws->iconTx = t.x(); ws->iconTy = t.y();
            ed.markDirty();
            emit ed.wangChanged();
            emit statusMessage(tr("Ícone do Conjunto de conexões “%1” definido como o tile (%2, %3).")
                                   .arg(ws->name).arg(t.x()).arg(t.y()));
        });
        if (WangColor* c = ws->colorById(ed.session.activeWangColorId)) {
            if (c->hasIcon)
                menu.addAction(tr("Remover ícone de “%1”").arg(c->name), [this, ws, c] {
                    c->hasIcon = false;
                    ed.markDirty();
                    emit ed.wangChanged();
                    emit statusMessage(tr("Ícone removido."));
                });
        }
        if (ws->hasIcon)
            menu.addAction(tr("Remover ícone do Conjunto de conexões"), [this, ws] {
                ws->hasIcon = false;
                ed.markDirty();
                emit ed.wangChanged();
                emit statusMessage(tr("Ícone do Conjunto de conexões removido."));
            });
    }
    menu.exec(e->globalPos());
}

} // namespace ui
