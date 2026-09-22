#include "Quantize.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>
#include <algorithm>

namespace core { namespace quantize {

namespace {

struct ColorCount { QRgb rgb; int count; };

/// Caixa do median cut: um subconjunto de cores e os limites por canal.
struct Box {
    QVector<ColorCount> colors;
    int rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;

    void computeBounds()
    {
        rmin = gmin = bmin = 255;
        rmax = gmax = bmax = 0;
        for (const ColorCount& c : colors) {
            rmin = qMin(rmin, qRed(c.rgb));   rmax = qMax(rmax, qRed(c.rgb));
            gmin = qMin(gmin, qGreen(c.rgb)); gmax = qMax(gmax, qGreen(c.rgb));
            bmin = qMin(bmin, qBlue(c.rgb));  bmax = qMax(bmax, qBlue(c.rgb));
        }
    }
    int rangeR() const { return rmax - rmin; }
    int rangeG() const { return gmax - gmin; }
    int rangeB() const { return bmax - bmin; }
    /// 0 = vermelho, 1 = verde, 2 = azul.
    int longestAxis() const
    {
        const int r = rangeR(), g = rangeG(), b = rangeB();
        if (r >= g && r >= b) return 0;
        return g >= b ? 1 : 2;
    }
    int longestRange() const { return qMax(rangeR(), qMax(rangeG(), rangeB())); }

    /// Cor representativa: média ponderada pela frequência de cada cor.
    QRgb average() const
    {
        qint64 r = 0, g = 0, b = 0, n = 0;
        for (const ColorCount& c : colors) {
            r += qint64(qRed(c.rgb))   * c.count;
            g += qint64(qGreen(c.rgb)) * c.count;
            b += qint64(qBlue(c.rgb))  * c.count;
            n += c.count;
        }
        if (n <= 0) return qRgb(0, 0, 0);
        return qRgb(int(r / n), int(g / n), int(b / n));
    }
};

inline int distance2(QRgb a, QRgb b)
{
    const int dr = qRed(a)   - qRed(b);
    const int dg = qGreen(a) - qGreen(b);
    const int db = qBlue(a)  - qBlue(b);
    return dr * dr + dg * dg + db * db;
}

} // namespace

int countDistinctColors(const QImage& img, int alphaThreshold)
{
    if (img.isNull()) return 0;
    const QImage src = img.format() == QImage::Format_ARGB32
                           ? img : img.convertToFormat(QImage::Format_ARGB32);
    QSet<QRgb> seen;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(line[x]) < alphaThreshold) continue;
            seen.insert(line[x] | 0xFF000000);
        }
    }
    return seen.size();
}

QImage toIndexed(const QImage& srcIn, const Options& opt, Result* result)
{
    Result res;
    if (srcIn.isNull()) {
        if (result) *result = res;
        return srcIn;
    }
    const QImage src = srcIn.format() == QImage::Format_ARGB32
                           ? srcIn : srcIn.convertToFormat(QImage::Format_ARGB32);
    const int w = src.width(), h = src.height();
    const int paletteBudget = qMax(1, opt.maxColors - 1);   // índice 0 é reservado

    // ---- histograma das cores opacas ------------------------------------
    QHash<QRgb, int> hist;
    hist.reserve(4096);
    bool anyTransparent = false;
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) < opt.alphaThreshold) { anyTransparent = true; continue; }
            ++hist[line[x] | 0xFF000000];
        }
    }
    res.originalColors = hist.size();
    res.hasTransparency = anyTransparent;

    // ---- paleta ----------------------------------------------------------
    QVector<QRgb> palette;
    if (hist.size() <= paletteBudget) {
        // Cabe tudo: paleta exata, conversão sem perda nenhuma.
        palette.reserve(hist.size());
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) palette.push_back(it.key());
        std::sort(palette.begin(), palette.end());
        res.lossless = true;
    } else {
        // Median cut.
        Box first;
        first.colors.reserve(hist.size());
        for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
            first.colors.push_back(ColorCount{ it.key(), it.value() });
        first.computeBounds();

        QVector<Box> boxes;
        boxes.push_back(first);
        while (boxes.size() < paletteBudget) {
            // Divide a caixa com maior amplitude que ainda tenha mais de uma cor.
            int target = -1, best = -1;
            for (int i = 0; i < boxes.size(); ++i) {
                if (boxes[i].colors.size() < 2) continue;
                const int r = boxes[i].longestRange();
                if (r > best) { best = r; target = i; }
            }
            if (target < 0) break;                    // não dá para dividir mais

            Box box = boxes[target];
            const int axis = box.longestAxis();
            std::sort(box.colors.begin(), box.colors.end(),
                      [axis](const ColorCount& a, const ColorCount& b) {
                          switch (axis) {
                          case 0:  return qRed(a.rgb)   < qRed(b.rgb);
                          case 1:  return qGreen(a.rgb) < qGreen(b.rgb);
                          default: return qBlue(a.rgb)  < qBlue(b.rgb);
                          }
                      });
            // Corta na mediana ponderada pela frequência: evita que uma cor
            // muito usada fique espremida junto com muitas cores raras.
            qint64 total = 0;
            for (const ColorCount& c : box.colors) total += c.count;
            qint64 acc = 0;
            int cut = 0;
            for (int i = 0; i < box.colors.size() - 1; ++i) {
                acc += box.colors[i].count;
                cut = i + 1;
                if (acc * 2 >= total) break;
            }
            Box a, b;
            a.colors = box.colors.mid(0, cut);
            b.colors = box.colors.mid(cut);
            a.computeBounds();
            b.computeBounds();
            boxes[target] = a;
            boxes.push_back(b);
        }
        palette.reserve(boxes.size());
        for (const Box& b : boxes) palette.push_back(b.average());
    }

    // ---- tabela de cores: índice 0 = transparente ------------------------
    QList<QRgb> table;
    table.reserve(palette.size() + 1);
    // Alfa 0 no índice 0: o PNG sai com transparência de verdade para outras
    // ferramentas, e o editores de RPG ignora isso e usa o índice 0 como transparente
    // de qualquer forma — atende aos dois casos.
    table.append(qRgba(opt.index0.red(), opt.index0.green(), opt.index0.blue(), 0));
    for (QRgb c : palette) table.append(c | 0xFF000000);
    res.paletteColors = table.size();

    // ---- mapeamento ------------------------------------------------------
    QImage out(w, h, QImage::Format_Indexed8);
    out.setColorTable(table);
    out.fill(0);

    QHash<QRgb, int> cache;                       // cor -> índice (acelera muito)
    cache.reserve(qMin(res.originalColors * 2, 1 << 16));

    // Erro acumulado por linha, só quando o dithering está ligado.
    QVector<int> errR, errG, errB, nextR, nextG, nextB;
    if (opt.dither) {
        errR.fill(0, w + 2); errG.fill(0, w + 2); errB.fill(0, w + 2);
        nextR.fill(0, w + 2); nextG.fill(0, w + 2); nextB.fill(0, w + 2);
    }

    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uchar* dst = out.scanLine(y);
        if (opt.dither) {
            errR = nextR; errG = nextG; errB = nextB;
            nextR.fill(0); nextG.fill(0); nextB.fill(0);
        }
        for (int x = 0; x < w; ++x) {
            if (qAlpha(line[x]) < opt.alphaThreshold) { dst[x] = 0; continue; }

            QRgb want = line[x] | 0xFF000000;
            if (opt.dither) {
                want = qRgb(qBound(0, qRed(want)   + errR[x + 1] / 16, 255),
                            qBound(0, qGreen(want) + errG[x + 1] / 16, 255),
                            qBound(0, qBlue(want)  + errB[x + 1] / 16, 255));
            }

            int idx;
            if (!opt.dither) {
                auto it = cache.constFind(want);
                if (it != cache.constEnd()) { idx = it.value(); }
                else {
                    int bestI = 0, bestD = INT_MAX;
                    for (int i = 0; i < palette.size(); ++i) {
                        const int d = distance2(want, palette[i]);
                        if (d < bestD) { bestD = d; bestI = i; }
                    }
                    idx = bestI + 1;               // +1: índice 0 é o transparente
                    cache.insert(want, idx);
                }
            } else {
                int bestI = 0, bestD = INT_MAX;
                for (int i = 0; i < palette.size(); ++i) {
                    const int d = distance2(want, palette[i]);
                    if (d < bestD) { bestD = d; bestI = i; }
                }
                idx = bestI + 1;
                // Floyd–Steinberg: distribui o erro para os vizinhos.
                const QRgb got = palette[bestI];
                const int dr = qRed(want) - qRed(got);
                const int dg = qGreen(want) - qGreen(got);
                const int db = qBlue(want) - qBlue(got);
                errR[x + 2] += dr * 7; errG[x + 2] += dg * 7; errB[x + 2] += db * 7;
                nextR[x]     += dr * 3; nextG[x]     += dg * 3; nextB[x]     += db * 3;
                nextR[x + 1] += dr * 5; nextG[x + 1] += dg * 5; nextB[x + 1] += db * 5;
                nextR[x + 2] += dr;     nextG[x + 2] += dg;     nextB[x + 2] += db;
            }
            dst[x] = uchar(idx);
        }
    }

    if (result) *result = res;
    return out;
}

}} // namespace core::quantize
