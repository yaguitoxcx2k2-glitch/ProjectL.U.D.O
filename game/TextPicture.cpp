// ============================================================================
//  TextPicture.cpp — Texto rico desenhado numa imagem.
// ============================================================================
#include "TextPicture.h"

#include "game/PictureFx.h"
#include "game/TextBox.h"
#include "game/TextDraw.h"

#include <QDir>
#include <QFontDatabase>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QSet>

using namespace core;

namespace game {

namespace {
QSet<QString>& fontesCarregadas()
{
    static QSet<QString> arquivos;
    return arquivos;
}
}

bool carregarFonteDoProjeto(const QString& arquivo)
{
    if (arquivo.trimmed().isEmpty()) return false;
    const QString key = QDir::cleanPath(QFileInfo(arquivo).absoluteFilePath()).toLower();
    auto& loaded = fontesCarregadas();
    if (loaded.contains(key)) return true;
    const int id = QFontDatabase::addApplicationFont(arquivo);
    if (id < 0) return false;
    loaded.insert(key);
    return true;
}

void carregarFontesDoProjeto(const QString& caminhoDoProjeto)
{
    if (caminhoDoProjeto.isEmpty()) return;
    const QString raiz = QFileInfo(caminhoDoProjeto).absolutePath();
    // Estrutura nova + pasta legada para projetos antigos.
    for (const QString& pasta : { QDir(raiz).filePath(QStringLiteral("Assets/Fonts")),
                                  QDir(raiz).filePath(QStringLiteral("fonts")) }) {
        QDir d(pasta);
        if (!d.exists()) continue;
        const QStringList arquivos = d.entryList(
            { QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files);
        for (const QString& f : arquivos) carregarFonteDoProjeto(d.filePath(f));
    }
}

bool richTextIsAnimated(const PictureRichText& rt)
{
    if (!rt.enabled) return false;
    if (rt.effects.enabled()) return true;
    // Basta procurar os códigos de animação: montar as páginas só para
    // descobrir isso seria caro para uma pergunta feita a cada quadro.
    static const char* codigos[] = { "\\WV", "\\SK", "\\RB", "\\FL", "\\BL", "\\GL",
                                     "\\BO", "\\JD", "\\SC", "\\SP", "\\SW" };
    for (const char* c : codigos)
        if (rt.text.contains(QLatin1String(c), Qt::CaseInsensitive)) return true;
    return false;
}

namespace {

/// Fundo do painel, conforme o estilo escolhido.
void desenharFundo(QPainter& p, const PictureRichText& rt, const QRectF& caixa,
                   const Editor& ed)
{
    if (rt.bg == PictureTextBg::None) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(qBound(0.0, rt.bgAlpha, 1.0));
    p.setPen(Qt::NoPen);
    const double r = rt.bgRadius;

    switch (rt.bg) {
    case PictureTextBg::None:
        break;
    case PictureTextBg::Solid:
        p.setBrush(rt.bgColor);
        p.drawRoundedRect(caixa, r, r);
        break;
    case PictureTextBg::Gradient: {
        QLinearGradient g(caixa.topLeft(), caixa.bottomLeft());
        g.setColorAt(0.0, rt.bgColor);
        g.setColorAt(1.0, rt.bgGradient2);
        p.setBrush(g);
        p.drawRoundedRect(caixa, r, r);
        break;
    }
    case PictureTextBg::Frosted: {
        // "Vidro fosco" HONESTO: a imagem é composta isolada, então não existe
        // o que está atrás para borrar (no caminho de GPU ela vira textura).
        // O que dá a leitura de vidro é o painel claro translúcido com uma
        // borda luminosa — a mesma escolha feita nas legendas.
        QColor c = rt.bgColor;
        c.setAlpha(qMin(255, c.alpha() / 2 + 40));
        p.setBrush(c);
        p.drawRoundedRect(caixa, r, r);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 70), 1));
        p.drawRoundedRect(caixa.adjusted(1, 1, -1, -1), r, r);
        break;
    }
    case PictureTextBg::Blur: {
        // Painel difuso: retângulo borrado, mais suave nas bordas.
        QImage painel(caixa.size().toSize() + QSize(32, 32),
                      QImage::Format_ARGB32_Premultiplied);
        painel.fill(Qt::transparent);
        {
            QPainter pp(&painel);
            pp.setRenderHint(QPainter::Antialiasing, true);
            pp.setPen(Qt::NoPen);
            pp.setBrush(rt.bgColor);
            pp.drawRoundedRect(QRectF(16, 16, caixa.width(), caixa.height()), r, r);
        }
        borrarCaixa(painel, 8);
        p.drawImage(caixa.topLeft() - QPointF(16, 16), painel);
        break;
    }
    case PictureTextBg::TextBlur:
        // Tratado depois de saber onde as letras ficam (ver render).
        break;
    case PictureTextBg::Window: {
        if (const PictureAsset* a = ed.pictureById(rt.bgBorderImageId)) {
            desenhar9Slice(p, a->image, caixa, rt.bgBorderSlice, rt.bgBorderScale, false, false);
        } else {
            // Sem moldura escolhida, uma janela padrão — melhor que nada na
            // tela e deixa claro que faltou escolher a imagem.
            p.setBrush(QColor(20, 24, 56, 220));
            p.drawRoundedRect(caixa, r, r);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(180, 180, 220, 200), 2));
            p.drawRoundedRect(caixa.adjusted(2, 2, -2, -2), r, r);
        }
        break;
    }
    }
    p.restore();
}

} // namespace

QImage renderTextPicture(const PictureRichText& rt, const Editor& ed, const QFont& fonteBase,
                         double tempo, int revealed,
                         const std::function<QString(int)>& varValue,
                         double exitTime)
{
    if (!rt.enabled) return QImage();

    QFont fonte = fonteBase;
    if (!rt.fontFamily.isEmpty()) fonte.setFamily(rt.fontFamily);
    fonte.setPixelSize(qMax(4, rt.fontSize));
    fonte.setBold(rt.bold);
    fonte.setItalic(rt.italic);

    const double pad = qMax(0, rt.padding);
    // Largura para quebrar: no automático deixamos folga grande (o texto só
    // quebra no \n); no manual, a largura pedida menos as margens.
    const double larguraUtil = (rt.autoSize || !rt.wordWrap)
                                   ? (rt.wordWrap ? 4000.0 : 100000.0)
                                   : qMax(8.0, rt.width - pad * 2);

    // Cache do LAYOUT: medir cada letra e quebrar as linhas é o passo caro, e
    // não muda com o tempo — só com o texto e o estilo. Texto com \v[n] fica
    // de fora, porque o valor da variável muda o que está escrito.
    static QHash<QString, QVector<TextPage>> cacheLayout;
    const bool temVariavel = rt.text.contains(QLatin1String("\\v"), Qt::CaseInsensitive);
    const QString chaveLayout = rt.cacheKey() + QLatin1Char('#') + QString::number(larguraUtil);
    QVector<TextPage> paginas;
    if (!temVariavel && cacheLayout.contains(chaveLayout)) {
        paginas = cacheLayout.value(chaveLayout);
    } else {
        paginas = layoutMessage(rt.text, fonte, larguraUtil, 9999, varValue, &ed.iconSet);
        if (!temVariavel) {
            if (cacheLayout.size() > 256) cacheLayout.clear();   // teto simples
            cacheLayout.insert(chaveLayout, paginas);
        }
    }
    const TextPage pagina = paginas.isEmpty() ? TextPage() : paginas.first();
    const QSizeF medida = measurePage(pagina, fonte, &ed.iconSet);

    // Contorno e sombra vazam para fora das letras: a imagem precisa de folga,
    // senão o texto sai cortado nas pontas.
    const int folga = qMax(rt.outlineWidth,
                           rt.shadow ? qMax(std::abs(rt.shadowOffsetX), std::abs(rt.shadowOffsetY))
                                     : 0) + 2;
    const int larg = rt.autoSize ? int(std::ceil(medida.width())) + int(pad) * 2 + folga * 2
                                 : qMax(8, rt.width);
    const int alt  = rt.autoSize ? int(std::ceil(medida.height())) + int(pad) * 2 + folga * 2
                                 : qMax(8, rt.height);

    QImage img(qBound(1, larg, 4096), qBound(1, alt, 4096), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF caixa(0, 0, img.width(), img.height());
    desenharFundo(p, rt, caixa.adjusted(0.5, 0.5, -0.5, -0.5), ed);

    const QRectF area(pad + folga, pad + folga,
                      img.width() - (pad + folga) * 2, img.height() - (pad + folga) * 2);

    // Borrão atrás do texto: desenha o texto uma vez, borra, escurece e usa
    // como sombra difusa — é o que dá legibilidade sobre cenário claro.
    if (rt.bg == PictureTextBg::TextBlur) {
        QImage atras(img.size(), QImage::Format_ARGB32_Premultiplied);
        atras.fill(Qt::transparent);
        {
            QPainter ap(&atras);
            ap.setRenderHint(QPainter::TextAntialiasing, true);
            TextDrawOpts o;
            o.color = QColor(0, 0, 0);
            o.outlineColor = QColor(0, 0, 0);
            o.outlineWidth = qMax(2, rt.outlineWidth);
            o.align = TextAlign(int(rt.align));
            o.icons = &ed.iconSet;
            o.revealed = revealed;
            o.time = tempo;
            o.exitTime = exitTime;
            o.effects = rt.effects;
            drawTextPage(ap, pagina, fonte, area, o);
        }
        borrarCaixa(atras, 6);
        p.setOpacity(qBound(0.0, rt.bgAlpha, 1.0));
        p.drawImage(0, 0, atras);
        p.drawImage(0, 0, atras);      // duas passadas: uma só fica fraca demais
        p.setOpacity(1.0);
    }

    TextDrawOpts opts;
    opts.color = rt.color;
    opts.gradient2 = rt.gradient2;
    opts.gradient = rt.gradient;
    opts.effects = rt.effects;
    opts.outlineColor = rt.outlineColor;
    opts.outlineWidth = qMax(0, rt.outlineWidth);
    opts.shadow = rt.shadow;
    opts.shadowColor = rt.shadowColor;
    opts.shadowOffset = QPointF(rt.shadowOffsetX, rt.shadowOffsetY);
    opts.align = TextAlign(int(rt.align));
    opts.icons = &ed.iconSet;
    opts.revealed = revealed;
    opts.time = tempo;
    opts.exitTime = exitTime;
    drawTextPage(p, pagina, fonte, area, opts);
    p.end();
    return img;
}

} // namespace game
