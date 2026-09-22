#include "TilesetOps.h"
#include "AutoTileTables.h"

#include <QPainter>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace core {

bool isBlankTile(const Editor& ed, const TileRef& tile)
{
    const Tileset* ts = ed.tilesetAt(tile.tilesetIdx);
    if (!ts || !ts->contains(tile.tx, tile.ty)) return false;
    // Um Tileset/célula visualmente vazio continua selecionável, como no RPG
    // Maker. Ao pintar com uma fonte sem pixels, o comportamento é o mesmo de
    // um tile transparente: apagar/limpar a célula em vez de inserir uma
    // referência invisível impossível de distinguir no mapa.
    if (ts->image.isNull()) return true;
    static QHash<QString, bool> cache;
    QString key = QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
        .arg(ts->image.cacheKey()).arg(tile.tx).arg(tile.ty).arg(ts->tilewidth).arg(ts->tileheight).arg(ts->margin).arg(ts->spacing);
    QVector<QRect> rectangles{ts->tileRect(tile.tx,tile.ty).intersected(ts->image.rect())};
    QPoint local;
    if (const auto* animation = animatedAutotileAt(*ts,tile.tx,tile.ty,true,nullptr,&local)) {
        for (const QPoint& origin : animation->frameOrigins) {
            const QPoint point = origin + local;
            key += QStringLiteral(":%1,%2").arg(point.x()).arg(point.y());
            rectangles.push_back(ts->tileRect(point.x(),point.y()).intersected(ts->image.rect()));
        }
    }
    const auto found = cache.constFind(key);
    if (found != cache.cend()) return found.value();
    bool blank = true;
    for (const QRect& rect : rectangles) {
        for (int y=rect.top();y<=rect.bottom() && blank;++y)
            for (int x=rect.left();x<=rect.right();++x) if (ts->image.pixelColor(x,y).alpha() != 0) { blank=false; break; }
        if (!blank) break;
    }
    if (cache.size() > 100000) cache.clear();
    cache.insert(key,blank);
    return blank;
}


Tileset makeTileset(const QImage& img, const QString& name, int tileWidth, int tileHeight,
                    int spacing, int margin, const QString& sourcePath)
{
    Tileset ts;
    ts.name = name;
    ts.image = img;
    ts.sourcePath = sourcePath;
    ts.tilewidth = qMax(1, tileWidth);
    ts.tileheight = qMax(1, tileHeight);
    ts.spacing = qMax(0, spacing);
    ts.margin = qMax(0, margin);
    ts.recomputeGrid();
    return ts;
}

/// Distancia por canal entre duas cores (metrica de Chebyshev): previsivel e
/// suficiente para fundos solidos com leve anti-aliasing/artefato de JPEG.
static inline int channelDistance(QRgb a, const QColor& b)
{
    return qMax(qMax(qAbs(qRed(a)   - b.red()),
                     qAbs(qGreen(a) - b.green())),
                qAbs(qBlue(a)  - b.blue()));
}

int countChromaKeyPixels(const QImage& img, const QColor& key, int tolerance)
{
    if (img.isNull() || !key.isValid()) return 0;
    const QImage src = img.format() == QImage::Format_ARGB32
                           ? img : img.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;          // ja transparente
            if (channelDistance(line[x], key) <= tolerance) ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------- charset ---
// Detecta a grade de uma folha de personagem pelas faixas transparentes.
//
// A ideia e simples e funciona para a maioria das folhas de verdade: se entre
// um quadro e outro existe pelo menos uma coluna (ou linha) 100% transparente,
// os blocos de conteudo SAO os quadros. So aceitamos o palpite quando ele
// divide a imagem em pedacos iguais — assim uma folha colada (sem folga) nao
// devolve resultado errado, devolve "nao sei".
static QVector<QPair<int,int>> contentRuns(const QVector<bool>& empty)
{
    QVector<QPair<int,int>> runs;
    int start = -1;
    for (int i = 0; i < empty.size(); ++i) {
        if (!empty[i] && start < 0) start = i;
        if ((empty[i] || i == empty.size() - 1) && start >= 0) {
            const int end = empty[i] ? i - 1 : i;
            runs.push_back(qMakePair(start, end));
            start = -1;
        }
    }
    return runs;
}


QString tilesetPageGroupKey(const Tileset& ts)
{
    const QString key = ts.pageGroupId.trimmed();
    return key.isEmpty() ? ts.id : key;
}

QVector<int> tilesetPageIndices(const Editor& ed, int anyPageIndex)
{
    QVector<int> out;
    const Tileset* anchor = ed.tilesetAt(anyPageIndex);
    if (!anchor) return out;
    const QString key = tilesetPageGroupKey(*anchor);
    for (int i = 0; i < ed.tilesets.size(); ++i)
        if (tilesetPageGroupKey(ed.tilesets.at(i)) == key) out.push_back(i);
    std::sort(out.begin(), out.end(), [&ed](int a, int b) {
        const Tileset& A = ed.tilesets.at(a), &B = ed.tilesets.at(b);
        if (A.pageIndex != B.pageIndex) return A.pageIndex < B.pageIndex;
        return a < b;
    });
    return out;
}

void renumberTilesetPages(Editor& ed, const QString& pageGroupId)
{
    const QString key = pageGroupId.trimmed();
    if (key.isEmpty()) return;
    QVector<int> pages;
    for (int i = 0; i < ed.tilesets.size(); ++i)
        if (tilesetPageGroupKey(ed.tilesets.at(i)) == key) pages.push_back(i);
    std::sort(pages.begin(), pages.end(), [&ed](int a, int b) {
        const Tileset& A = ed.tilesets.at(a), &B = ed.tilesets.at(b);
        if (A.pageIndex != B.pageIndex) return A.pageIndex < B.pageIndex;
        return a < b;
    });
    for (int i = 0; i < pages.size(); ++i) {
        Tileset& ts = ed.tilesets[pages.at(i)];
        ts.pageGroupId = key;
        ts.pageIndex = i;
    }
}

QVector<Tileset> splitTilesetForTextureLimit(const Tileset& source, int maxTextureSize)
{
    const QString logicalGroup = source.pageGroupId.trimmed().isEmpty() ? source.id : source.pageGroupId.trimmed();
    if (source.image.isNull()) {
        Tileset single = source; single.pageGroupId = logicalGroup; single.pageIndex = qMax(0, source.pageIndex);
        return {single};
    }
    maxTextureSize = qMax(qMax(1, source.tilewidth), qMax(1, maxTextureSize));
    if (source.image.width() <= maxTextureSize && source.image.height() <= maxTextureSize) {
        Tileset single = source; single.pageGroupId = logicalGroup; single.pageIndex = qMax(0, source.pageIndex);
        return {single};
    }

    const int stepX = qMax(1, source.tilewidth + source.spacing);
    const int stepY = qMax(1, source.tileheight + source.spacing);
    const int maxCols = qMax(1, (maxTextureSize - source.margin * 2 + source.spacing) / stepX);
    const int maxRows = qMax(1, (maxTextureSize - source.margin * 2 + source.spacing) / stepY);
    const int partsX = qMax(1, (source.columns + maxCols - 1) / maxCols);
    const int partsY = qMax(1, (source.rows + maxRows - 1) / maxRows);
    QVector<Tileset> out;
    out.reserve(partsX * partsY);

    auto copySparse = [](const QHash<QString, int>& src, QHash<QString, int>& dst,
                         int ox, int oy, int cols, int rows) {
        for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
            const QStringList p = it.key().split(QLatin1Char(':'));
            if (p.size() != 2) continue;
            const int x = p[0].toInt(), y = p[1].toInt();
            if (x < ox || y < oy || x >= ox + cols || y >= oy + rows) continue;
            dst.insert(Tileset::priorityKey(x - ox, y - oy), it.value());
        }
    };

    int partNo = 0;
    const int partCount = partsX * partsY;
    for (int py = 0; py < partsY; ++py) {
        for (int px = 0; px < partsX; ++px) {
            const int ox = px * maxCols, oy = py * maxRows;
            const int cols = qMin(maxCols, source.columns - ox);
            const int rows = qMin(maxRows, source.rows - oy);
            if (cols <= 0 || rows <= 0) continue;
            Tileset part = source;
            part.id = idGen();
            part.pageGroupId = logicalGroup;
            part.pageIndex = qMax(0, source.pageIndex) + partNo;
            part.name = source.name;
            ++partNo;
            const int w = source.margin * 2 + cols * source.tilewidth + qMax(0, cols - 1) * source.spacing;
            const int h = source.margin * 2 + rows * source.tileheight + qMax(0, rows - 1) * source.spacing;
            QImage image(qMax(1, w), qMax(1, h), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            for (int y = 0; y < rows; ++y) for (int x = 0; x < cols; ++x) {
                const QRect srcRect = source.tileRect(ox + x, oy + y);
                const QRect dstRect(source.margin + x * (source.tilewidth + source.spacing),
                                    source.margin + y * (source.tileheight + source.spacing),
                                    source.tilewidth, source.tileheight);
                painter.drawImage(dstRect, source.image, srcRect);
            }
            painter.end();
            part.image = image;
            part.sourceTileX = source.sourceTileX + ox;
            part.sourceTileY = source.sourceTileY + oy;
            part.combinedSources.clear();
            part.combinedTileCount = 0;
            part.packCursorX = 0;
            part.packCursorY = 0;
            if (source.combined) {
                for (const CombinedSource& original : source.combinedSources) {
                    const QRect block(original.x, original.y, original.cols, original.rows);
                    const QRect chunk(ox, oy, cols, rows);
                    const QRect inter = block.intersected(chunk);
                    if (inter.isEmpty()) continue;
                    CombinedSource clipped = original;
                    clipped.x = inter.x() - ox;
                    clipped.y = inter.y() - oy;
                    clipped.cols = inter.width();
                    clipped.rows = inter.height();
                    clipped.count = clipped.cols * clipped.rows;
                    clipped.sourceTileX = original.sourceTileX + (inter.x() - original.x);
                    clipped.sourceTileY = original.sourceTileY + (inter.y() - original.y);
                    part.combinedSources.push_back(clipped);
                    part.combinedTileCount += clipped.count;
                    part.packCursorX = qMax(part.packCursorX, clipped.x + clipped.cols);
                    part.packCursorY = qMax(part.packCursorY, clipped.y + clipped.rows);
                }
            }
            part.tilePriorities.clear(); part.tileCollisionMasks.clear(); part.tileProbabilities.clear();
            copySparse(source.tilePriorities, part.tilePriorities, ox, oy, cols, rows);
            copySparse(source.tileCollisionMasks, part.tileCollisionMasks, ox, oy, cols, rows);
            for (auto it = source.tileProbabilities.constBegin(); it != source.tileProbabilities.constEnd(); ++it) {
                const QStringList p = it.key().split(QLatin1Char(':'));
                if (p.size()!=2) continue; const int x=p[0].toInt(), y=p[1].toInt();
                if (x>=ox && y>=oy && x<ox+cols && y<oy+rows)
                    part.tileProbabilities.insert(Tileset::priorityKey(x-ox,y-oy), it.value());
            }
            part.animatedAutotiles.clear();
            for (const AnimatedAutotile& anim : source.animatedAutotiles) {
                bool fits = true;
                for (const QPoint& o : anim.frameOrigins) {
                    if (o.x() < ox || o.y() < oy || o.x()+anim.cols > ox+cols || o.y()+anim.rows > oy+rows) { fits=false; break; }
                }
                if (!fits) continue;
                AnimatedAutotile a = anim;
                a.id = idGen(); a.baseX -= ox; a.baseY -= oy;
                for (QPoint& o : a.frameOrigins) o -= QPoint(ox, oy);
                part.animatedAutotiles.push_back(a);
            }
            part.recomputeGrid();
            part.isVX512 = (part.image.width()==512 && part.image.height()==512);
            out.push_back(part);
        }
    }
    return out.isEmpty() ? QVector<Tileset>{source} : out;
}

QSize detectCharsetGrid(const QImage& sheet)
{
    if (sheet.isNull() || sheet.width() < 4 || sheet.height() < 4) return QSize();
    // Nao adianta perguntar hasAlphaChannel(): PNG com paleta + tRNS (o caso
    // de muitas folhas antigas) responde "nao" e mesmo assim tem transparencia.
    // Convertemos e olhamos os pixels — se nao houver nenhum transparente, a
    // deteccao devolve "nao sei" logo abaixo (nenhuma faixa vazia).
    const QImage img = sheet.convertToFormat(QImage::Format_ARGB32);

    QVector<bool> colEmpty(img.width(), true), rowEmpty(img.height(), true);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) >= 8) { colEmpty[x] = false; rowEmpty[y] = false; }
        }
    }

    const QVector<QPair<int,int>> cx = contentRuns(colEmpty);
    const QVector<QPair<int,int>> cy = contentRuns(rowEmpty);
    if (cx.isEmpty() || cy.isEmpty()) return QSize();

    const int cols = cx.size(), rows = cy.size();
    // Menos de dois blocos em algum eixo = folha colada (ou vazia): melhor
    // admitir que nao sabe do que chutar uma grade errada.
    if (cols < 2 || rows < 2 || cols > 64 || rows > 64) return QSize();
    if (img.width() % cols || img.height() % rows) return QSize();

    // Cada bloco de conteudo tem de caber INTEIRO na sua celula: e o que
    // separa uma folha realmente espacada de uma coincidencia.
    const int cw = img.width() / cols, ch = img.height() / rows;
    for (int i = 0; i < cols; ++i)
        if (cx[i].first < i * cw || cx[i].second >= (i + 1) * cw) return QSize();
    for (int i = 0; i < rows; ++i)
        if (cy[i].first < i * ch || cy[i].second >= (i + 1) * ch) return QSize();

    return QSize(cols, rows);
}

int applyChromaKey(QImage& img, const QColor& key, int tolerance)
{
    if (img.isNull() || !key.isValid()) return 0;
    if (img.format() != QImage::Format_ARGB32) img = img.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) == 0) continue;
            if (channelDistance(line[x], key) > tolerance) continue;
            // Mantem o RGB e zera so o alfa: evita franja escura se a imagem
            // for reescalada com interpolacao mais tarde.
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 0);
            ++count;
        }
    }
    return count;
}

/// Tamanho em tiles ocupado por uma imagem (blockSize do JS).
static QSize blockSize(const QImage& img, int tw, int th)
{
    return QSize(qMax(1, (img.width()  + tw - 1) / tw),
                 qMax(1, (img.height() + th - 1) / th));
}

struct Placement { QImage img; QString name; QString sourcePath; int cols, rows, x, y; };

/// Empacotamento LINEAR a partir de um cursor (packBlocks do JS).
static QVector<Placement> packBlocks(const QVector<NamedImage>& images, int tw, int th,
                                     QPoint cursor, const QString& direction, QPoint* endCursor)
{
    QVector<Placement> out;
    int x = cursor.x(), y = cursor.y();
    for (const NamedImage& ni : images) {
        const QSize s = blockSize(ni.img, tw, th);
        out.push_back(Placement{ ni.img, ni.name, ni.sourcePath, s.width(), s.height(), x, y });
        if (direction == QLatin1String("vertical")) y += s.height();
        else x += s.width();
    }
    if (endCursor) *endCursor = QPoint(x, y);
    return out;
}

Tileset makeCombinedTileset(const QVector<NamedImage>& images, const QString& name,
                            int tileWidth, int tileHeight, const QString& direction)
{
    const int tw = qMax(1, tileWidth), th = qMax(1, tileHeight);
    const QString dir = (direction == QLatin1String("vertical")) ? QStringLiteral("vertical")
                                                                 : QStringLiteral("horizontal");
    QPoint cursor(0, 0);
    const QVector<Placement> places = packBlocks(images, tw, th, QPoint(0, 0), dir, &cursor);

    int W = 1, totalRows = 1;
    if (dir == QLatin1String("vertical")) {
        for (const Placement& p : places) W = qMax(W, p.cols);
        totalRows = qMax(1, cursor.y());
    } else {
        W = qMax(1, cursor.x());
        for (const Placement& p : places) totalRows = qMax(totalRows, p.rows);
    }

    QImage canvas(W * tw, totalRows * th, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    {
        QPainter p(&canvas);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (const Placement& pl : places) p.drawImage(QPoint(pl.x * tw, pl.y * th), pl.img);
    }

    Tileset ts = makeTileset(canvas, name, tw, th, 0, 0);
    ts.combined = true;
    ts.combinedDirection = dir;
    ts.packCursorX = cursor.x();
    ts.packCursorY = cursor.y();
    int count = 0;
    for (const Placement& pl : places) {
        CombinedSource s;
        s.name = pl.name; s.sourcePath = pl.sourcePath; s.x = pl.x; s.y = pl.y; s.cols = pl.cols; s.rows = pl.rows;
        s.count = pl.cols * pl.rows;
        count += s.count;
        ts.combinedSources.push_back(s);
    }
    ts.combinedTileCount = count;
    return ts;
}

int appendToCombinedTileset(Tileset& ts, const QVector<NamedImage>& images)
{
    const int tw = ts.tilewidth, th = ts.tileheight;
    const QString dir = ts.combinedDirection.isEmpty() ? QStringLiteral("horizontal")
                                                       : ts.combinedDirection;
    QPoint newCursor;
    const QVector<Placement> places =
        packBlocks(images, tw, th, QPoint(ts.packCursorX, ts.packCursorY), dir, &newCursor);

    int W = ts.columns, totalRows = ts.rows;
    if (dir == QLatin1String("vertical")) {
        for (const Placement& p : places) W = qMax(W, p.cols);
        totalRows = qMax(ts.rows, newCursor.y());
    } else {
        W = qMax(ts.columns, newCursor.x());
        for (const Placement& p : places) totalRows = qMax(totalRows, p.rows);
    }

    if (W > ts.columns || totalRows > ts.rows) {
        QImage grown(W * tw, totalRows * th, QImage::Format_ARGB32);
        grown.fill(Qt::transparent);
        QPainter p(&grown);
        p.drawImage(0, 0, ts.image);      // conteudo antigo na MESMA posicao
        p.end();
        ts.image = grown;
        ts.columns = W;
        ts.rows = totalRows;
        ts.imagewidth = grown.width();
        ts.imageheight = grown.height();
    }
    if (ts.image.format() != QImage::Format_ARGB32)
        ts.image = ts.image.convertToFormat(QImage::Format_ARGB32);

    int added = 0;
    {
        QPainter p(&ts.image);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        for (const Placement& pl : places) {
            p.drawImage(QPoint(pl.x * tw, pl.y * th), pl.img);
            CombinedSource s;
            s.name = pl.name; s.sourcePath = pl.sourcePath; s.x = pl.x; s.y = pl.y; s.cols = pl.cols; s.rows = pl.rows;
            s.count = pl.cols * pl.rows;
            ts.combinedSources.push_back(s);
            added += s.count;
        }
    }
    ts.combinedTileCount += added;
    ts.packCursorX = newCursor.x();
    ts.packCursorY = newCursor.y();
    ts.tilecount = ts.columns * ts.rows;
    return added;
}

// ---------------------------- insercao em posicao exata -------------------
QImage extractTileBlock(const Tileset& ts, int tx, int ty, int cols, int rows)
{
    const int tw = ts.tilewidth, th = ts.tileheight;
    QImage out(qMax(1, cols * tw), qMax(1, rows * th), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    if (ts.image.isNull()) return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int dy = 0; dy < rows; ++dy)
        for (int dx = 0; dx < cols; ++dx) {
            if (!ts.contains(tx + dx, ty + dy)) continue;
            // tileRect() resolve margem e espacamento; a saida fica sem nenhum.
            p.drawImage(QRect(dx * tw, dy * th, tw, th), ts.image, ts.tileRect(tx + dx, ty + dy));
        }
    return out;
}

/// O tile (tx,ty) do tileset esta totalmente transparente?
static bool tileIsEmpty(const Tileset& ts, int tx, int ty)
{
    if (!ts.contains(tx, ty) || ts.image.isNull()) return true;
    const QImage tile = ts.image.copy(ts.tileRect(tx, ty)).convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < tile.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(tile.constScanLine(y));
        for (int x = 0; x < tile.width(); ++x)
            if (qAlpha(line[x]) != 0) return false;
    }
    return true;
}

int countOverwrittenTiles(const Tileset& ts, const QImage& img, int atCol, int atRow)
{
    if (img.isNull()) return 0;
    const int cols = (img.width()  + ts.tilewidth  - 1) / ts.tilewidth;
    const int rows = (img.height() + ts.tileheight - 1) / ts.tileheight;
    int n = 0;
    for (int dy = 0; dy < rows; ++dy)
        for (int dx = 0; dx < cols; ++dx)
            if (!tileIsEmpty(ts, atCol + dx, atRow + dy)) ++n;
    return n;
}

QPoint findFreeSlot(const Tileset& ts, const QImage& img)
{
    if (img.isNull()) return QPoint(0, 0);
    const int needCols = (img.width()  + ts.tilewidth  - 1) / ts.tilewidth;
    const int needRows = (img.height() + ts.tileheight - 1) / ts.tileheight;

    // Procura um retangulo livre dentro do atlas atual, de cima para baixo.
    for (int y = 0; y + needRows <= ts.rows; ++y)
        for (int x = 0; x + needCols <= ts.columns; ++x)
            if (countOverwrittenTiles(ts, img, x, y) == 0) return QPoint(x, y);

    // Nao coube: entra logo abaixo do conteudo atual.
    return QPoint(0, ts.rows);
}

bool insertImageAt(Tileset& ts, const QImage& img, int atCol, int atRow,
                   InsertAtResult* out, QString* error)
{
    if (img.isNull()) {
        if (error) *error = QObject::tr("Imagem vazia.");
        return false;
    }
    if (ts.spacing != 0 || ts.margin != 0) {
        if (error) *error = QObject::tr("Tilesets com espaçamento ou margem não aceitam inserção.");
        return false;
    }
    if (atCol < 0 || atRow < 0) {
        if (error) *error = QObject::tr("Posição inválida.");
        return false;
    }
    const int tw = ts.tilewidth, th = ts.tileheight;
    const int cols = (img.width()  + tw - 1) / tw;
    const int rows = (img.height() + th - 1) / th;

    InsertAtResult res;
    res.overwritten = countOverwrittenTiles(ts, img, atCol, atRow);

    const int needCols = qMax(ts.columns, atCol + cols);
    const int needRows = qMax(ts.rows,    atRow + rows);
    if (needCols > ts.columns || needRows > ts.rows) {
        // Cresce o atlas mantendo o conteudo antigo exatamente onde estava:
        // como as celulas do mapa guardam (tx,ty), nada se desloca.
        QImage grown(needCols * tw, needRows * th, QImage::Format_ARGB32);
        grown.fill(Qt::transparent);
        QPainter p(&grown);
        p.drawImage(0, 0, ts.image);
        p.end();
        res.addedCols = needCols - ts.columns;
        res.addedRows = needRows - ts.rows;
        ts.image = grown;
        ts.columns = needCols;
        ts.rows = needRows;
        ts.imagewidth = grown.width();
        ts.imageheight = grown.height();
    }
    if (ts.image.format() != QImage::Format_ARGB32)
        ts.image = ts.image.convertToFormat(QImage::Format_ARGB32);
    {
        QPainter p(&ts.image);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        // Limpa a area antes de desenhar, senao restos do que havia ali
        // apareceriam sob as partes transparentes do autotile.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(atCol * tw, atRow * th, cols * tw, rows * th), Qt::transparent);
        p.drawImage(QPoint(atCol * tw, atRow * th), img);
    }
    ts.tilecount = ts.columns * ts.rows;
    if (ts.combined) {
        CombinedSource cs;
        cs.name = QObject::tr("autotile");
        cs.x = atCol; cs.y = atRow; cs.cols = cols; cs.rows = rows;
        cs.count = cols * rows;
        ts.combinedSources.push_back(cs);
        ts.combinedTileCount += cs.count;
    }
    if (out) *out = res;
    return true;
}

bool insertCombinedSourceAt(Tileset& ts, const NamedImage& image, int atCol, int atRow,
                            InsertAtResult* out, QString* error)
{
    if (image.img.isNull()) {
        if (error) *error = QObject::tr("Imagem vazia.");
        return false;
    }
    const int incomingCols = (image.img.width() + qMax(1, ts.tilewidth) - 1) / qMax(1, ts.tilewidth);
    const int incomingRows = (image.img.height() + qMax(1, ts.tileheight) - 1) / qMax(1, ts.tileheight);
    removeAnimatedAutotilesOverlapping(ts, QRect(atCol, atRow, incomingCols, incomingRows));
    if (!ts.combined) {
        ts.combined = true;
        CombinedSource base;
        base.name = ts.name; base.sourcePath = ts.sourcePath; base.x = 0; base.y = 0;
        base.cols = ts.columns; base.rows = ts.rows; base.count = ts.tilecount;
        ts.combinedSources.push_back(base);
        ts.combinedTileCount = ts.tilecount;
    }
    const int before = ts.combinedSources.size();
    InsertAtResult res;
    if (!insertImageAt(ts, image.img, atCol, atRow, &res, error)) return false;
    if (ts.combinedSources.size() > before) {
        CombinedSource& added = ts.combinedSources.last();
        added.name = image.name.trimmed().isEmpty() ? QObject::tr("Imagem") : image.name.trimmed();
        added.sourcePath = image.sourcePath;
    }
    // O cursor automatico deixa de comandar o posicionamento, mas continua
    // coerente para projetos/rotinas antigas que ainda o consultem.
    const int cols = (image.img.width() + ts.tilewidth - 1) / ts.tilewidth;
    const int rows = (image.img.height() + ts.tileheight - 1) / ts.tileheight;
    ts.packCursorX = qMax(ts.packCursorX, atCol + cols);
    ts.packCursorY = qMax(ts.packCursorY, atRow + rows);
    if (out) *out = res;
    return true;
}


// ------------------------- vínculo/atualização de fontes ------------------
namespace {

QString absoluteSourcePath(const Editor& ed, const QString& stored)
{
    const QString clean = QDir::fromNativeSeparators(stored.trimmed());
    if (clean.isEmpty()) return QString();
    QFileInfo info(clean);
    if (info.isAbsolute()) return QDir::cleanPath(info.absoluteFilePath());
    if (ed.projectRoot().trimmed().isEmpty()) return QDir::cleanPath(clean);
    return QDir::cleanPath(QDir(ed.projectRoot()).filePath(clean));
}

QString comparablePath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    return path.toLower();
#else
    return path;
#endif
}

QString storedSourcePath(const Editor& ed, const QString& absolute)
{
    if (absolute.trimmed().isEmpty()) return QString();
    const QFileInfo info(absolute);
    if (!info.isAbsolute()) return QDir::fromNativeSeparators(absolute);
    if (ed.projectRoot().trimmed().isEmpty()) return QDir::fromNativeSeparators(info.absoluteFilePath());
    const QString rel = QDir(ed.projectRoot()).relativeFilePath(info.absoluteFilePath());
    // Só tornamos relativo quando o arquivo pertence ao projeto. Caminhos
    // externos continuam absolutos para que a associação não seja ambígua.
    if (!rel.startsWith(QStringLiteral("../")) && rel != QLatin1String(".."))
        return QDir::fromNativeSeparators(rel);
    return QDir::fromNativeSeparators(info.absoluteFilePath());
}

QImage preparedSourceImage(const Tileset& ts, const QString& absolute, QString* error)
{
    QImage img(absolute);
    if (img.isNull()) {
        if (error) *error = QObject::tr("Não foi possível ler a imagem-fonte: %1").arg(absolute);
        return QImage();
    }
    if (ts.chromaApplied) {
        const QColor key = ts.chromaColor.isValid() ? ts.chromaColor : img.pixelColor(0, 0);
        if (key.isValid()) applyChromaKey(img, key, ts.chromaTolerance);
    }
    return img;
}


QImage cropSimpleSourceWindow(const Tileset& ts, const QImage& full)
{
    if (full.isNull()) return QImage();
    const int tw = qMax(1, ts.tilewidth), th = qMax(1, ts.tileheight);
    const int fullCols = qMax(1, (full.width() - ts.margin * 2 + ts.spacing) / (tw + ts.spacing));
    const int fullRows = qMax(1, (full.height() - ts.margin * 2 + ts.spacing) / (th + ts.spacing));
    if (ts.sourceTileX == 0 && ts.sourceTileY == 0 && fullCols == ts.columns && fullRows == ts.rows) return full;
    const int outW = ts.margin * 2 + ts.columns * tw + qMax(0, ts.columns - 1) * ts.spacing;
    const int outH = ts.margin * 2 + ts.rows * th + qMax(0, ts.rows - 1) * ts.spacing;
    QImage out(qMax(1, outW), qMax(1, outH), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int y = 0; y < ts.rows; ++y) {
        for (int x = 0; x < ts.columns; ++x) {
            const int sx = ts.margin + (ts.sourceTileX + x) * (tw + ts.spacing);
            const int sy = ts.margin + (ts.sourceTileY + y) * (th + ts.spacing);
            const QRect src(sx, sy, tw, th);
            const QRect dst(ts.margin + x * (tw + ts.spacing),
                            ts.margin + y * (th + ts.spacing), tw, th);
            if (full.rect().contains(src)) painter.drawImage(dst, full, src);
        }
    }
    return out;
}

QImage cropCombinedSourceWindow(const Tileset& ts, const CombinedSource& source, const QImage& full)
{
    if (full.isNull()) return QImage();
    const int tw = qMax(1, ts.tilewidth), th = qMax(1, ts.tileheight);
    const QRect src(source.sourceTileX * tw, source.sourceTileY * th,
                    source.cols * tw, source.rows * th);
    if (source.sourceTileX == 0 && source.sourceTileY == 0 &&
        src.width() == full.width() && src.height() == full.height()) return full;
    return full.copy(src.intersected(full.rect()));
}

bool sameLogicalGrid(const Tileset& ts, const QImage& img)
{
    if (img.isNull()) return false;
    const int tw = qMax(1, ts.tilewidth), th = qMax(1, ts.tileheight);
    const int cols = qMax(1, (img.width() - ts.margin * 2 + ts.spacing) / (tw + ts.spacing));
    const int rows = qMax(1, (img.height() - ts.margin * 2 + ts.spacing) / (th + ts.spacing));
    return cols == ts.columns && rows == ts.rows;
}

bool sameBlockGrid(const Tileset& ts, const CombinedSource& source, const QImage& img)
{
    const int cols = qMax(1, (img.width() + qMax(1, ts.tilewidth) - 1) / qMax(1, ts.tilewidth));
    const int rows = qMax(1, (img.height() + qMax(1, ts.tileheight) - 1) / qMax(1, ts.tileheight));
    return cols == source.cols && rows == source.rows;
}

} // namespace

QStringList tilesetSourceFiles(const Editor& ed, int tilesetIdx)
{
    const Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) return {};
    QStringList out;
    QSet<QString> seen;
    auto append = [&](const QString& stored) {
        const QString absolute = absoluteSourcePath(ed, stored);
        if (absolute.isEmpty()) return;
        const QString key = comparablePath(absolute);
        if (seen.contains(key)) return;
        seen.insert(key);
        out.push_back(absolute);
    };
    if (ts->combined) {
        for (const CombinedSource& source : ts->combinedSources) append(source.sourcePath);
    } else {
        append(ts->sourcePath);
    }
    return out;
}

bool reloadTilesetSources(Editor& ed, int tilesetIdx, const QString& changedAbsolutePath,
                          QString* error, QStringList* updatedSources)
{
    Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) {
        if (error) *error = QObject::tr("Tileset inválido.");
        return false;
    }
    const QString changedKey = changedAbsolutePath.trimmed().isEmpty()
        ? QString() : comparablePath(QFileInfo(changedAbsolutePath).absoluteFilePath());
    bool changed = false;
    QStringList errors;

    if (!ts->combined) {
        const QString absolute = absoluteSourcePath(ed, ts->sourcePath);
        if (absolute.isEmpty() || (!changedKey.isEmpty() && comparablePath(absolute) != changedKey)) return false;
        QString readError;
        QImage img = preparedSourceImage(*ts, absolute, &readError);
        if (img.isNull()) { if (error) *error = readError; return false; }
        img = cropSimpleSourceWindow(*ts, img);
        if (!sameLogicalGrid(*ts, img)) {
            if (error) *error = QObject::tr(
                "A imagem '%1' mudou a grade do tileset (%2×%3 tiles). A atualização automática foi bloqueada para não deslocar tiles já usados no mapa.")
                .arg(QFileInfo(absolute).fileName()).arg(ts->columns).arg(ts->rows);
            return false;
        }
        ts->image = img;
        ts->imagewidth = img.width();
        ts->imageheight = img.height();
        ts->isVX512 = (img.width() == 512 && img.height() == 512);
        changed = true;
        if (updatedSources) updatedSources->push_back(absolute);
    } else {
        if (ts->image.isNull()) return false;
        if (ts->image.format() != QImage::Format_ARGB32)
            ts->image = ts->image.convertToFormat(QImage::Format_ARGB32);
        for (CombinedSource& source : ts->combinedSources) {
            const QString absolute = absoluteSourcePath(ed, source.sourcePath);
            if (absolute.isEmpty()) continue;
            if (!changedKey.isEmpty() && comparablePath(absolute) != changedKey) continue;
            QString readError;
            QImage img = preparedSourceImage(*ts, absolute, &readError);
            if (img.isNull()) { errors.push_back(readError); continue; }
            img = cropCombinedSourceWindow(*ts, source, img);
            if (!sameBlockGrid(*ts, source, img)) {
                errors.push_back(QObject::tr(
                    "'%1' não ocupa mais o mesmo bloco (%2×%3 tiles); atualização ignorada para preservar coordenadas do mapa.")
                    .arg(QFileInfo(absolute).fileName()).arg(source.cols).arg(source.rows));
                continue;
            }
            QPainter painter(&ts->image);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            const QRect target(source.x * ts->tilewidth, source.y * ts->tileheight,
                               source.cols * ts->tilewidth, source.rows * ts->tileheight);
            painter.fillRect(target, Qt::transparent);
            painter.drawImage(target.topLeft(), img);
            painter.end();
            changed = true;
            if (updatedSources) updatedSources->push_back(absolute);
        }
    }

    if (!errors.isEmpty() && error) *error = errors.join(QLatin1Char('\n'));
    return changed;
}

bool bindTilesetSource(Editor& ed, int tilesetIdx, int sourceIndex,
                       const QString& absolutePath, QString* error)
{
    Tileset* ts = ed.tilesetAt(tilesetIdx);
    if (!ts) { if (error) *error = QObject::tr("Tileset inválido."); return false; }
    const QFileInfo info(absolutePath);
    if (!info.isFile()) { if (error) *error = QObject::tr("Imagem-fonte inexistente."); return false; }

    QString readError;
    QImage img = preparedSourceImage(*ts, info.absoluteFilePath(), &readError);
    if (img.isNull()) { if (error) *error = readError; return false; }
    const QString stored = storedSourcePath(ed, info.absoluteFilePath());

    if (!ts->combined) {
        img = cropSimpleSourceWindow(*ts, img);
        if (!sameLogicalGrid(*ts, img)) {
            if (error) *error = QObject::tr(
                "A nova imagem possui outra grade. Para preservar todos os mapas, use uma imagem com a mesma quantidade de tiles (%1×%2).")
                .arg(ts->columns).arg(ts->rows);
            return false;
        }
        ts->sourcePath = stored;
        ts->image = img;
        ts->imagewidth = img.width();
        ts->imageheight = img.height();
        ts->isVX512 = (img.width() == 512 && img.height() == 512);
        return true;
    }

    if (sourceIndex < 0 || sourceIndex >= ts->combinedSources.size()) {
        if (error) *error = QObject::tr("Selecione uma parte do tileset combinado.");
        return false;
    }
    CombinedSource& source = ts->combinedSources[sourceIndex];
    img = cropCombinedSourceWindow(*ts, source, img);
    if (!sameBlockGrid(*ts, source, img)) {
        if (error) *error = QObject::tr(
            "A nova imagem precisa ocupar o mesmo bloco de %1×%2 tiles para não alterar coordenadas já pintadas.")
            .arg(source.cols).arg(source.rows);
        return false;
    }
    source.sourcePath = stored;
    if (ts->image.format() != QImage::Format_ARGB32)
        ts->image = ts->image.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&ts->image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const QRect target(source.x * ts->tilewidth, source.y * ts->tileheight,
                       source.cols * ts->tilewidth, source.rows * ts->tileheight);
    painter.fillRect(target, Qt::transparent);
    painter.drawImage(target.topLeft(), img);
    painter.end();
    return true;
}

// ------------------------------------------------ autotile animado --------
const AnimatedAutotile* animatedAutotileAt(const Tileset& ts, int tx, int ty,
                                           bool includeFrames,
                                           int* physicalFrame,
                                           QPoint* local)
{
    if (physicalFrame) *physicalFrame = -1;
    if (local) *local = QPoint();
    for (const AnimatedAutotile& anim : ts.animatedAutotiles) {
        if (!anim.valid()) continue;
        const QRect base(anim.baseX, anim.baseY, anim.cols, anim.rows);
        if (base.contains(tx, ty)) {
            if (physicalFrame) *physicalFrame = 0;
            if (local) *local = QPoint(tx - anim.baseX, ty - anim.baseY);
            return &anim;
        }
        if (!includeFrames) continue;
        for (int frame = 1; frame < anim.frameOrigins.size(); ++frame) {
            const QPoint origin = anim.frameOrigins.at(frame);
            const QRect frameRect(origin.x(), origin.y(), anim.cols, anim.rows);
            if (!frameRect.contains(tx, ty)) continue;
            if (physicalFrame) *physicalFrame = frame;
            if (local) *local = QPoint(tx - origin.x(), ty - origin.y());
            return &anim;
        }
    }
    return nullptr;
}

QPoint canonicalAnimatedTile(const Tileset& ts, int tx, int ty)
{
    QPoint local;
    const AnimatedAutotile* anim = animatedAutotileAt(ts, tx, ty, true, nullptr, &local);
    return anim ? QPoint(anim->baseX + local.x(), anim->baseY + local.y()) : QPoint(tx, ty);
}

int animatedAutotileFrame(const AnimatedAutotile& anim, qint64 elapsedMs, quint32 instanceSeed)
{
    const int frames = anim.frameOrigins.size();
    if (frames <= 1) return 0;
    const double fps = qBound(0.1, anim.fps, 120.0);
    const int sequence = anim.pingPong ? qMax(1, frames * 2 - 2) : frames;
    qint64 step = qMax<qint64>(0, qint64(std::floor((qMax<qint64>(0, elapsedMs) * fps) / 1000.0)));
    if (!anim.synchronized && sequence > 1) step += instanceSeed % quint32(sequence);
    if (anim.loop) step %= sequence;
    else step = qMin<qint64>(step, sequence - 1);
    int index = int(step);
    if (anim.pingPong && index >= frames) index = (frames * 2 - 2) - index;
    return qBound(0, index, frames - 1);
}

QRect animatedTileRect(const Tileset& ts, int tx, int ty, qint64 elapsedMs, quint32 instanceSeed)
{
    QPoint local;
    const AnimatedAutotile* anim = animatedAutotileAt(ts, tx, ty, true, nullptr, &local);
    if (!anim) return ts.tileRect(tx, ty);
    const int frame = animatedAutotileFrame(*anim, elapsedMs, instanceSeed);
    const QPoint origin = anim->frameOrigins.value(frame, QPoint(anim->baseX, anim->baseY));
    const int px = origin.x() + local.x();
    const int py = origin.y() + local.y();
    return ts.contains(px, py) ? ts.tileRect(px, py) : ts.tileRect(anim->baseX + local.x(), anim->baseY + local.y());
}

quint64 animatedTilesetFrameSignature(const Editor& ed, qint64 elapsedMs)
{
    quint64 signature = 1469598103934665603ULL;
    for (int tsi = 0; tsi < ed.tilesets.size(); ++tsi) {
        const Tileset& ts = ed.tilesets.at(tsi);
        for (const AnimatedAutotile& anim : ts.animatedAutotiles) {
            if (!anim.valid()) continue;
            const quint64 value = (quint64(tsi + 1) << 32)
                ^ quint64(animatedAutotileFrame(anim, elapsedMs, 0) + 1)
                ^ quint64(qHash(anim.id));
            signature ^= value;
            signature *= 1099511628211ULL;
        }
    }
    return signature;
}

int removeAnimatedAutotilesOverlapping(Tileset& ts, const QRect& tilesArea)
{
    if (!tilesArea.isValid() || tilesArea.isEmpty()) return 0;
    int removed = 0;
    for (int i = ts.animatedAutotiles.size() - 1; i >= 0; --i) {
        const AnimatedAutotile& anim = ts.animatedAutotiles.at(i);
        bool overlaps = QRect(anim.baseX, anim.baseY, anim.cols, anim.rows).intersects(tilesArea);
        if (!overlaps) {
            for (const QPoint& origin : anim.frameOrigins) {
                if (QRect(origin.x(), origin.y(), anim.cols, anim.rows).intersects(tilesArea)) {
                    overlaps = true;
                    break;
                }
            }
        }
        if (!overlaps) continue;
        ts.animatedAutotiles.remove(i);
        ++removed;
    }
    return removed;
}

// ------------------------------------------------- importacao de AutoTile
namespace autotile {

int suggestModeForSelection(int cols, int rows)
{
    if (cols == 3 && rows == 4) return atc::RPGM_XP_3x4;
    if (cols == 2 && rows == 3) return atc::RPGM_2x3;
    if (cols == 2 && rows == 2) return atc::RPGM_2x2;
    if (cols == 1 && rows == 1) return atc::COPY_1x1;
    // Selecao maior: usa o maior modo de autotile que divide certinho.
    // COPY_1x1 fica de fora de proposito — ele divide QUALQUER selecao e
    // faria um 5x7 "funcionar", gerando 35 copias sem sentido. Ele continua
    // disponivel para escolha manual no combo do dialogo.
    for (int m : { atc::RPGM_XP_3x4, atc::RPGM_2x3, atc::RPGM_2x2 }) {
        int bw = 1, bh = 1;
        atc::selTiles(m, &bw, &bh);
        if (cols % bw == 0 && rows % bh == 0) return m;
    }
    return 0;
}

QString modeName(int mode)
{
    switch (mode) {
    case atc::RPGM_2x3:    return QObject::tr("2×3");
    case atc::RPGM_XP_3x4: return QObject::tr("3×4");
    case atc::RPGM_2x2:    return QObject::tr("2×2");
    case atc::COPY_2x2:    return QObject::tr("2×2 — cópia 1:1");
    case atc::COPY_1x1:    return QObject::tr("1×1 — cópia 1:1");
    }
    return QObject::tr("(modo desconhecido)");
}

QSize blocksInSelection(int cols, int rows, int mode)
{
    int bw = 1, bh = 1;
    atc::selTiles(mode, &bw, &bh);
    if (bw <= 0 || bh <= 0) return QSize(0, 0);
    if (cols % bw || rows % bh) return QSize(0, 0);
    return QSize(cols / bw, rows / bh);
}

Converter fromTilesetBlock(const Tileset& ts, int tx, int ty, int cols, int rows, int mode)
{
    Converter c;
    c.tileSize = ts.tilewidth;
    c.mode = mode;
    c.layout = Layout::Vertical;
    c.img = extractTileBlock(ts, tx, ty, cols, rows);
    c.baseName = QObject::tr("autotile");

    int bw = 1, bh = 1;
    atc::selTiles(mode, &bw, &bh);
    const QSize blocks = blocksInSelection(cols, rows, mode);
    // Uma selecao maior vira varios autotiles: percorre em ordem de leitura.
    for (int by = 0; by < blocks.height(); ++by)
        for (int bx = 0; bx < blocks.width(); ++bx) {
            // selRect() trabalha pelo CENTRO do bloco.
            const int cx = (bx * bw) * c.tileSize + (bw * c.tileSize) / 2;
            const int cy = (by * bh) * c.tileSize + (bh * c.tileSize) / 2;
            c.selections.push_back(Selection{ cx, cy, mode });
        }
    return c;
}


int Converter::snap(int v) const
{
    const int t = qMax(1, tileSize);
    return int(std::floor((v + t / 2.0) / t)) * t;
}

QRect Converter::selRect(int x, int y, int m) const
{
    int sw = 1, sh = 1;
    atc::selTiles(m, &sw, &sh);
    const int w = tileSize * sw, h = tileSize * sh;
    return QRect(snap(x - w / 2), snap(y - h / 2), w, h);
}

QPoint Converter::packedSize(int limitX, int limitY, bool relative) const
{
    // Porte fiel de atcPackedSize(): acumula larguras/alturas unicas.
    QVector<int> xs, ws, ys, hs;
    int accW = 0, accH = 0;
    for (const Selection& sel : selections) {
        const QRect rect = selRect(sel.x, sel.y, sel.mode);
        int outW = 1, outH = 1;
        atc::outTiles(sel.mode, &outW, &outH);
        const bool isLeft = rect.x() < limitX;
        const bool isAbove = rect.y() < limitY;
        const bool belowBand = rect.y() >= limitY + outH;
        const bool rightBand = rect.x() >= limitX + outW;

        if (!relative || (isLeft && !belowBand)) {
            const int xi = xs.indexOf(rect.x());
            if (xi > -1 && outW > ws[xi]) { accW -= ws[xi]; ws[xi] = outW; accW += outW; }
            if (xi < 0) { xs.push_back(rect.x()); ws.push_back(outW); accW += outW; }
        }
        if (!relative || (isAbove && !rightBand && !isLeft)) {
            const int yi = ys.indexOf(rect.y());
            if (yi > -1 && outH > hs[yi]) { accH -= hs[yi]; hs[yi] = outH; accH += outH; }
            if (yi < 0) { ys.push_back(rect.y()); hs.push_back(outH); accH += outH; }
        }
    }
    return QPoint(accW * tileSize, accH * tileSize);
}

QPoint Converter::originFor(int index) const
{
    if (index < 0 || index >= selections.size()) return QPoint(0, 0);

    if (layout == Layout::AsInput) {
        // Comportamento original: a posicao no atlas espelha a posicao do bloco
        // na imagem de entrada (atcPackedSize com relative=true).
        const Selection& sel = selections[index];
        const QRect rect = selRect(sel.x, sel.y, sel.mode);
        return packedSize(rect.x(), rect.y(), true);
    }

    // Vertical/Horizontal: os blocos entram na ordem em que foram marcados.
    int accX = 0, accY = 0;
    for (int i = 0; i < index; ++i) {
        int w = 1, h = 1;
        atc::outTiles(selections[i].mode, &w, &h);
        if (layout == Layout::Vertical) accY += h;
        else                            accX += w;
    }
    return QPoint(accX * tileSize, accY * tileSize);
}

void Converter::toggleAt(int px, int py)
{
    const QRect r = selRect(px, py, mode);
    for (int i = 0; i < selections.size(); ++i) {
        const QRect other = selRect(selections[i].x, selections[i].y, selections[i].mode);
        if (other.intersects(r)) { selections.remove(i); return; }   // clique de novo remove
    }
    selections.push_back(Selection{ px, py, mode });
}

/// Constroi a lista de blits (atcBuildBlits) e o tamanho maximo alcancado.
struct Blit { QRectF src; QRectF dst; };

static QVector<Blit> buildBlits(const Converter& c, double* maxX, double* maxY)
{
    QVector<Blit> blits;
    double mx = 0, my = 0;
    const double t = c.tileSize;
    for (int si = 0; si < c.selections.size(); ++si) {
        const Selection& sel = c.selections[si];
        const QRect rect = c.selRect(sel.x, sel.y, sel.mode);
        const QPoint origin = c.originFor(si);
        int count = 0;
        const atc::AtcPiece* map = atc::mapping(sel.mode, &count);
        for (int i = 0; i < count; ++i) {
            const atc::AtcPiece& piece = map[i];
            const double sx = rect.x() + piece.src[0] * t;
            const double sy = rect.y() + piece.src[1] * t;
            const double sw = piece.src[2] * t;
            const double sh = piece.src[3] * t;
            for (const atc::AtcDest& d : piece.dest) {
                const double dx = origin.x() + d.x * sw;
                const double dy = origin.y() + d.y * sh;
                mx = qMax(mx, dx + sw);
                my = qMax(my, dy + sh);
                blits.push_back(Blit{ QRectF(sx, sy, sw, sh), QRectF(dx, dy, sw, sh) });
            }
        }
    }
    if (maxX) *maxX = mx;
    if (maxY) *maxY = my;
    return blits;
}

QSize Converter::outputSize() const
{
    double mx = 0, my = 0;
    buildBlits(*this, &mx, &my);
    return QSize(qMax(double(tileSize), mx), qMax(double(tileSize), my));
}

QImage Converter::buildOutput() const
{
    double mx = 0, my = 0;
    const QVector<Blit> blits = buildBlits(*this, &mx, &my);
    const int w = int(qMax(double(tileSize), mx));
    const int h = int(qMax(double(tileSize), my));
    QImage out(qMax(1, w), qMax(1, h), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    if (img.isNull() || blits.isEmpty()) return out;
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Blit& b : blits) p.drawImage(b.dst, img, b.src);
    return out;
}

QString Converter::info() const
{
    if (img.isNull()) return QObject::tr("Selecione uma imagem para começar.");
    const QSize s = outputSize();
    const QString dir = layout == Layout::Vertical   ? QObject::tr("vertical")
                      : layout == Layout::Horizontal ? QObject::tr("horizontal")
                                                     : QObject::tr("como na entrada");
    return QObject::tr("Entrada %1×%2 px · tile %3 px · %4 bloco(s) marcado(s) · "
                       "empilhamento %5 · saída %6×%7 px (%8×%9 tiles)")
        .arg(img.width()).arg(img.height()).arg(tileSize)
        .arg(selections.size()).arg(dir)
        .arg(s.width()).arg(s.height())
        .arg(s.width() / qMax(1, tileSize)).arg(s.height() / qMax(1, tileSize));
}

} // namespace autotile
} // namespace core
