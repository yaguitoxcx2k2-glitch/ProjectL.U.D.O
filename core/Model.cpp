#include "Model.h"

#include <QRandomGenerator>
#include <QStringList>
#include <QDateTime>
#include <cmath>

namespace core {

QString idGen()
{
    // Mesma ideia do idGen() JS: aleatorio base36 + timestamp base36.
    const quint64 r = QRandomGenerator::global()->generate64();
    return QString::number(r, 36).left(11) %
           QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
}

// ------------------------------------------------------------------ Tileset
void Tileset::recomputeGrid()
{
    imagewidth  = image.width();
    imageheight = image.height();
    const int tw = qMax(1, tilewidth), th = qMax(1, tileheight);
    columns = qMax(1, (imagewidth  - margin * 2 + spacing) / (tw + spacing));
    rows    = qMax(1, (imageheight - margin * 2 + spacing) / (th + spacing));
    tilecount = columns * rows;
    isVX512 = (imagewidth == 512 && imageheight == 512);

    // Se o atlas for reduzido, metadados de prioridade fora da nova grade
    // não podem continuar escondidos no projeto. Crescer o atlas preserva
    // todas as chaves existentes.
    for (auto it = tilePriorities.begin(); it != tilePriorities.end(); ) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        bool okX = false, okY = false;
        const int tx = parts.size() == 2 ? parts[0].toInt(&okX) : -1;
        const int ty = parts.size() == 2 ? parts[1].toInt(&okY) : -1;
        if (!okX || !okY || tx < 0 || ty < 0 || tx >= columns || ty >= rows || it.value() < 1 || it.value() > 5)
            it = tilePriorities.erase(it);
        else ++it;
    }
    for (auto it = tileCollisionMasks.begin(); it != tileCollisionMasks.end(); ) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        bool okX = false, okY = false;
        const int tx = parts.size() == 2 ? parts[0].toInt(&okX) : -1;
        const int ty = parts.size() == 2 ? parts[1].toInt(&okY) : -1;
        const int mask = it.value() & 0x0f;
        if (!okX || !okY || tx < 0 || ty < 0 || tx >= columns || ty >= rows || mask == 0 || mask != it.value())
            it = tileCollisionMasks.erase(it);
        else ++it;
    }
    for (auto it = tileProbabilities.begin(); it != tileProbabilities.end(); ) {
        const QStringList parts = it.key().split(QLatin1Char(':'));
        bool okX = false, okY = false;
        const int tx = parts.size() == 2 ? parts[0].toInt(&okX) : -1;
        const int ty = parts.size() == 2 ? parts[1].toInt(&okY) : -1;
        if (!okX || !okY || tx < 0 || ty < 0 || tx >= columns || ty >= rows || !std::isfinite(it.value()) || qFuzzyCompare(it.value(), 1.0))
            it = tileProbabilities.erase(it);
        else ++it;
    }
}

// -------------------------------------------------------------------- Layer
void Layer::allocGrid()
{
    data2D.clear();
    data2D.resize(qMax(0, rows));
    for (int y = 0; y < rows; ++y) data2D[y].resize(qMax(0, cols));
}

void Layer::resizeGrid(int newCols, int newRows)
{
    newCols = qMax(1, newCols);
    newRows = qMax(1, newRows);
    QVector<QVector<Cell>> grid(newRows);
    for (int y = 0; y < newRows; ++y) {
        grid[y].resize(newCols);
        if (y < data2D.size()) {
            const QVector<Cell>& src = data2D[y];
            const int n = qMin(newCols, src.size());
            for (int x = 0; x < n; ++x) grid[y][x] = src[x];
        }
    }
    data2D = grid;
    cols = newCols;
    rows = newRows;
}

bool Layer::isEmptyLayer() const
{
    switch (type) {
    case LayerType::Tile:
        for (const QVector<Cell>& row : data2D)
            for (const Cell& c : row)
                if (!c.isEmpty()) return false;
        return true;
    case LayerType::Object: return objects.isEmpty();
    case LayerType::Image:  return image.isNull();
    case LayerType::Group:  return children.isEmpty();
    }
    return true;
}

LayerPtr makeTileLayer(const QString& name, int tw, int th, int cols, int rows)
{
    LayerPtr l(new Layer);
    l->type = LayerType::Tile;
    l->name = name.isEmpty() ? QStringLiteral("Camada %1x%2").arg(tw).arg(th) : name;
    l->tileWidth = qMax(1, tw);
    l->tileHeight = qMax(1, th);
    l->cols = qMax(1, cols);
    l->rows = qMax(1, rows);
    l->allocGrid();
    return l;
}

LayerPtr makeObjectLayer(const QString& name)
{
    LayerPtr l(new Layer);
    l->type = LayerType::Object;
    l->name = name.isEmpty() ? QStringLiteral("Camada de objetos") : name;
    return l;
}

LayerPtr makeImageLayer(const QImage& img, const QString& name, const QString& path, bool referenceOnly)
{
    LayerPtr l(new Layer);
    l->type = LayerType::Image;
    l->name = name.isEmpty()
        ? (referenceOnly ? QStringLiteral("Imagem de referência") : QStringLiteral("Imagem rasterizada"))
        : name;
    l->image = img;
    l->imagePath = path;
    l->imagewidth = img.width();
    l->imageheight = img.height();
    l->imageScaleX = 1.0;
    l->imageScaleY = 1.0;
    l->imageRotation = 0.0;
    l->imageFlipX = false;
    l->imageFlipY = false;
    l->imageFilter = QStringLiteral("nearest");
    l->imageReferenceOnly = referenceOnly;
    l->opacity = referenceOnly ? 0.6 : 1.0;
    return l;
}

LayerPtr makePaintLayer(const QSize& pixelSize, const QString& name)
{
    const QSize size(qMax(1, pixelSize.width()), qMax(1, pixelSize.height()));
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    LayerPtr l = makeImageLayer(canvas, name.isEmpty() ? QStringLiteral("Pintura") : name, QString(), false);
    l->imagePaintLayer = true;
    l->imageFilter = QStringLiteral("bilinear");
    l->blendMode = QStringLiteral("source-over");
    return l;
}

LayerPtr makeGroupLayer(const QString& name)
{
    LayerPtr l(new Layer);
    l->type = LayerType::Group;
    l->name = name.isEmpty() ? QStringLiteral("Grupo") : name;
    return l;
}

LayerPtr cloneLayer(const LayerPtr& src, bool newIds)
{
    if (!src) return LayerPtr();
    LayerPtr l(new Layer(*src));            // copia rasa dos containers Qt (COW)
    if (newIds) l->id = idGen();
    l->children.clear();
    for (const LayerPtr& c : src->children) l->children.push_back(cloneLayer(c, newIds));
    if (newIds) {
        for (MapObject& o : l->objects) o.id = idGen();
    }
    return l;
}

// ------------------------------------------------------------------ WangSet
QStringList WangTileData::positions()
{
    return { QStringLiteral("tl"), QStringLiteral("t"),  QStringLiteral("tr"), QStringLiteral("r"),
             QStringLiteral("br"), QStringLiteral("b"),  QStringLiteral("bl"), QStringLiteral("l") };
}

int WangTileData::get(const QString& pos) const
{
    if (pos == QLatin1String("tl")) return tl;
    if (pos == QLatin1String("t"))  return t;
    if (pos == QLatin1String("tr")) return tr;
    if (pos == QLatin1String("l"))  return l;
    if (pos == QLatin1String("r"))  return r;
    if (pos == QLatin1String("bl")) return bl;
    if (pos == QLatin1String("b"))  return b;
    if (pos == QLatin1String("br")) return br;
    return -1;
}

void WangTileData::set(const QString& pos, int colorId)
{
    if (pos == QLatin1String("tl")) tl = colorId;
    else if (pos == QLatin1String("t"))  t = colorId;
    else if (pos == QLatin1String("tr")) tr = colorId;
    else if (pos == QLatin1String("l"))  l = colorId;
    else if (pos == QLatin1String("r"))  r = colorId;
    else if (pos == QLatin1String("bl")) bl = colorId;
    else if (pos == QLatin1String("b"))  b = colorId;
    else if (pos == QLatin1String("br")) br = colorId;
}

bool WangSet::parseTileKey(const QString& key, int* tilesetIdx, int* tx, int* ty)
{
    const int colon = key.indexOf(QLatin1Char(':'));
    const int comma = key.indexOf(QLatin1Char(','), colon + 1);
    if (colon < 0 || comma < 0) return false;
    bool a = false, b = false, c = false;
    const int ts = key.left(colon).toInt(&a);
    const int x  = key.mid(colon + 1, comma - colon - 1).toInt(&b);
    const int y  = key.mid(comma + 1).toInt(&c);
    if (!a || !b || !c) return false;
    *tilesetIdx = ts; *tx = x; *ty = y;
    return true;
}

const WangColor* WangSet::colorById(int id) const
{
    for (const WangColor& c : colors) if (c.id == id) return &c;
    return nullptr;
}

WangColor* WangSet::colorById(int id)
{
    for (WangColor& c : colors) if (c.id == id) return &c;
    return nullptr;
}

// ------------------------------------------------------------------- Tools
QString toolId(Tool t)
{
    switch (t) {
    case Tool::Stamp:   return QStringLiteral("stamp");
    case Tool::Eraser:  return QStringLiteral("eraser");
    case Tool::Fill:    return QStringLiteral("fill");
    case Tool::Rect:    return QStringLiteral("rect");
    case Tool::Circle:  return QStringLiteral("circle");
    case Tool::Line:    return QStringLiteral("line");
    case Tool::Terrain: return QStringLiteral("terrain");
    case Tool::Select:  return QStringLiteral("select");
    case Tool::Object:  return QStringLiteral("object");
    case Tool::Paint:   return QStringLiteral("paint");
    case Tool::Slope:   return QStringLiteral("slope");
    }
    return QStringLiteral("stamp");
}

Tool toolFromId(const QString& id)
{
    if (id == QLatin1String("eraser"))  return Tool::Eraser;
    if (id == QLatin1String("fill"))    return Tool::Fill;
    if (id == QLatin1String("rect"))    return Tool::Rect;
    if (id == QLatin1String("circle"))  return Tool::Circle;
    if (id == QLatin1String("line"))    return Tool::Line;
    if (id == QLatin1String("terrain")) return Tool::Terrain;
    if (id == QLatin1String("select"))  return Tool::Select;
    if (id == QLatin1String("object"))  return Tool::Object;
    if (id == QLatin1String("paint"))   return Tool::Paint;
    if (id == QLatin1String("slope"))   return Tool::Slope;
    return Tool::Stamp;
}

QString toolLabel(Tool t)
{
    switch (t) {
    case Tool::Stamp:   return QStringLiteral("Pincel de tiles");
    case Tool::Eraser:  return QStringLiteral("Borracha");
    case Tool::Fill:    return QStringLiteral("Balde");
    case Tool::Rect:    return QStringLiteral("Retângulo");
    case Tool::Circle:  return QStringLiteral("Círculo / Elipse");
    case Tool::Line:    return QStringLiteral("Linha / Caminho");
    case Tool::Terrain: return QStringLiteral("Autotile / Terrain");
    case Tool::Select:  return QStringLiteral("Selecionar objeto");
    case Tool::Object:  return QStringLiteral("Colocar objeto");
    case Tool::Paint:   return QStringLiteral("Pincel de pintura");
    case Tool::Slope:   return QStringLiteral("Inclinação");
    }
    return QString();
}

} // namespace core
