// ============================================================================
//  TextDraw.cpp — desenho unificado do Rich Text da LUDO.
// ============================================================================
#include "TextDraw.h"
#include "TextEffectRuntime.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace game {
namespace {

QColor brighten(QColor c, double amount)
{
    amount = qBound(0.0, amount, 1.0);
    c.setRedF(c.redF() + (1.0 - c.redF()) * amount);
    c.setGreenF(c.greenF() + (1.0 - c.greenF()) * amount);
    c.setBlueF(c.blueF() + (1.0 - c.blueF()) * amount);
    return c;
}

QLinearGradient makeGradient(const core::TextGradientSpec& spec, const QRectF& rect,
                             const std::function<QColor(const QColor&)>& transform)
{
    QPointF a(rect.left(), rect.top()), b(rect.left(), rect.bottom());
    const QString dir = spec.direction.trimmed().toLower();
    if (dir == QLatin1String("horizontal")) b = QPointF(rect.right(), rect.top());
    else if (dir == QLatin1String("diagonal")) b = rect.bottomRight();
    QLinearGradient g(a, b);
    const int n = spec.colors.size();
    for (int i = 0; i < n; ++i) {
        const double pos = n <= 1 ? 0.0 : double(i) / double(n - 1);
        g.setColorAt(pos, transform ? transform(spec.colors[i]) : spec.colors[i]);
    }
    return g;
}

} // namespace

int drawTextPage(QPainter& p, const TextPage& page, const QFont& base,
                 const QRectF& area, const TextDrawOpts& opt)
{
    int desenhadas = 0;
    int restam = (opt.revealed < 0) ? INT_MAX : opt.revealed;
    int indice = 0;
    int wordIndex = -1;
    bool insideWord = false;
    double y = area.top();
    const double opacidadeBase = qBound(0.0, opt.opacity, 1.0);
    const int charCount = qMax(1, page.drawableCount());
    const int wordCount = textPageWordCount(page);
    double loopStartOverride = -1.0;
    if (opt.effects.entrance.enabled && !opt.revealTimesSec.isEmpty()) {
        double lastReveal = -1.0;
        for (double t : opt.revealTimesSec) if (t >= 0.0) lastReveal = qMax(lastReveal, t);
        if (lastReveal >= 0.0) {
            int cycles = 1;
            const QString mode = opt.effects.entrance.loopMode.trimmed().toLower();
            if (mode == QLatin1String("count")) cycles = qMax(1, opt.effects.entrance.loopCount);
            else if (mode == QLatin1String("ping-pong")) cycles = 2;
            const double postBirthSpan = qMax(0, opt.effects.entrance.durationMs) * cycles / 1000.0;
            // O relógio do Loop começa quando o último glyph terminou a sua
            // Entrada real. `textEffectPhaseSpanSec` continua cobrindo delay/
            // stagger nominais; `lastReveal + duration` cobre revelações que
            // aconteceram mais tarde por typewriter/pausas do diálogo.
            loopStartOverride = qMax(textEffectPhaseSpanSec(opt.effects.entrance, charCount, wordCount),
                                     lastReveal + postBirthSpan);
        }
    }

    double alturaTexto = 0.0;
    double larguraTexto = 0.0;
    for (const TextLine& linha : page.lines) {
        alturaTexto += lineHeight(linha, base);
        double lw = 0.0;
        for (const TypedChar& c : linha.chars) lw += charAdvance(base, c, opt.icons);
        larguraTexto = qMax(larguraTexto, lw);
    }
    const QRectF gradientRect(area.left(), area.top(), qMax(1.0, area.width()), qMax(1.0, alturaTexto));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    for (const TextLine& linha : page.lines) {
        insideWord = false;
        const double alturaLinha = lineHeight(linha, base);
        double largura = 0.0;
        for (const TypedChar& c : linha.chars) largura += charAdvance(base, c, opt.icons);

        double x = area.left();
        if (opt.align == TextAlign::Center)     x += (area.width() - largura) / 2.0;
        else if (opt.align == TextAlign::Right) x += area.width() - largura;

        // Qt precisa receber o trecho RTL como uma unidade para aplicar BiDi e
        // shaping contextual (árabe/hebraico). Efeitos por grafema continuam no
        // caminho abaixo; linhas RTL simples usam o renderer textual nativo.
        QString rtlText;
        const TypedChar* rtlStyle = nullptr;
        bool simpleRtl = !opt.effects.enabled();
        for (const TypedChar& c : linha.chars) {
            if (!c.isDrawable()) continue;
            if (c.isIcon() || c.fx.kind != TextFx::None) { simpleRtl = false; break; }
            if (!rtlStyle) rtlStyle = &c;
            else if (!(c.style == rtlStyle->style) || c.colorIdx != rtlStyle->colorIdx) { simpleRtl = false; break; }
            rtlText += c.ch;
        }
        simpleRtl = simpleRtl && rtlStyle && rtlText.isRightToLeft();
        if (simpleRtl) {
            QString visible;
            int visibleGlyphs = 0;
            for (const TypedChar& c : linha.chars) {
                if (!c.isDrawable() || visibleGlyphs >= restam) continue;
                visible += c.ch; ++visibleGlyphs;
            }
            if (!visible.isEmpty()) {
                const QFont f = charFont(base, *rtlStyle); const QFontMetricsF fm(f);
                const double width = fm.horizontalAdvance(visible);
                const QPointF pos(area.right() - width, y + fm.ascent());
                QColor color = rtlStyle->style.color.isValid() ? rtlStyle->style.color
                    : (rtlStyle->colorIdx > 0 ? messageColor(rtlStyle->colorIdx) : opt.color);
                QPainterPath path; path.addText(pos, f, visible);
                p.save(); p.setOpacity(opacidadeBase);
                if (opt.shadow) { p.setPen(Qt::NoPen); p.setBrush(opt.shadowColor); p.drawPath(path.translated(opt.shadowOffset)); }
                const int outline = rtlStyle->style.outlineWidth >= 0 ? rtlStyle->style.outlineWidth : opt.outlineWidth;
                if (outline > 0) { p.setBrush(Qt::NoBrush); p.setPen(QPen(rtlStyle->style.outlineColor.isValid() ? rtlStyle->style.outlineColor : opt.outlineColor, outline * 2.0)); p.drawPath(path); }
                p.setPen(Qt::NoPen); p.setBrush(color); p.drawPath(path); p.restore();
                desenhadas += visibleGlyphs; restam -= visibleGlyphs; indice += visibleGlyphs;
            }
            y += alturaLinha;
            if (restam <= 0) break;
            continue;
        }

        for (const TypedChar& c : linha.chars) {
            if (!c.isDrawable()) continue;
            const bool separator = !c.isIcon() && c.isWhitespace();
            if (separator) insideWord = false;
            else if (!insideWord) { ++wordIndex; insideWord = true; }

            if (restam <= 0) break;
            --restam;
            const double av = charAdvance(base, c, opt.icons);
            const QFont f = charFont(base, c);
            const QFontMetricsF fm(f);

            const bool genericRevealed = textEffectTargetRevealed(
                opt.effects.entrance, opt.time, indice, qMax(0, wordIndex), charCount, wordCount);
            if (!fxHidden(c, opt.time, indice) && genericRevealed) {
                const double revealTime = indice < opt.revealTimesSec.size()
                    ? opt.revealTimesSec.at(indice) : -1.0;
                const TextEffectSample generic = evaluateTextEffects(
                    opt.effects, opt.time, opt.exitTime, indice, qMax(0, wordIndex), charCount, wordCount,
                    revealTime, loopStartOverride);
                const QPointF off = fxOffset(c, opt.time, indice) + generic.offset;
                const double inlineScale = fxScale(c, opt.time, indice);
                const double scaleX = inlineScale * generic.scaleX;
                const double scaleY = inlineScale * generic.scaleY;
                const double giro = fxAngle(c, opt.time, indice) + generic.rotation;
                const double brilho = qBound(0.0, fxSweep(c, opt.time, indice) + generic.brighten, 1.0);
                const double op = fxOpacity(c, opt.time) * generic.opacity * opacidadeBase;

                QColor corBase = opt.color;
                if (c.colorIdx > 0) corBase = messageColor(c.colorIdx);
                if (c.style.color.isValid()) corBase = c.style.color;

                core::TextGradientSpec gradSpec;
                if (c.style.gradient.enabled()) gradSpec = c.style.gradient;
                else if (!c.style.color.isValid() && c.colorIdx <= 0 && opt.gradient.enabled()) gradSpec = opt.gradient;
                else if (!c.style.color.isValid() && c.colorIdx <= 0 && opt.gradient2.isValid()) {
                    gradSpec.direction = QStringLiteral("vertical");
                    gradSpec.colors = { corBase, opt.gradient2 };
                }

                auto corComEfeito = [&](const QColor& baseColor) {
                    QColor r = generic.colorOverride.isValid() ? generic.colorOverride
                                                               : fxColor(c, opt.time, baseColor);
                    if (brilho > 0.0) r = brighten(r, brilho);
                    return r;
                };
                const QColor cor = corComEfeito(gradSpec.enabled() ? gradSpec.colors.first() : corBase);
                QBrush pincelLetra(cor);
                if (gradSpec.enabled() && !generic.colorOverride.isValid())
                    pincelLetra = QBrush(makeGradient(gradSpec, gradientRect, corComEfeito));

                const QColor corContorno = c.style.outlineColor.isValid() ? c.style.outlineColor
                                                                          : opt.outlineColor;
                const int larguraContorno = (c.style.outlineWidth >= 0) ? c.style.outlineWidth
                                                                        : opt.outlineWidth;

                p.save();
                p.setOpacity(qBound(0.0, op, 1.0));
                const QPointF pos(x + off.x(), y + fm.ascent() + off.y());
                if (!qFuzzyCompare(scaleX, 1.0) || !qFuzzyCompare(scaleY, 1.0) || !qFuzzyIsNull(giro)) {
                    const QPointF centro(pos.x() + av / 2.0, pos.y() - fm.ascent() / 2.0);
                    p.translate(centro);
                    p.rotate(giro);
                    p.scale(scaleX, scaleY);
                    p.translate(-centro);
                }

                if (c.isIcon()) {
                    if (opt.icons && opt.icons->isValid()) {
                        const QRect src = opt.icons->iconRect(c.icon);
                        if (!src.isNull()) {
                            const double lado = fm.height();
                            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
                            p.drawImage(QRectF(pos.x(), pos.y() - fm.ascent(), lado, lado),
                                        opt.icons->image, QRectF(src));
                        }
                    }
                } else {
                    p.setFont(f);
                    const QString txt(c.ch);
                    // RC2.64: contorno e sombra usam a geometria REAL do glyph.
                    // O caminho antigo de largura 1 desenhava oito cópias do texto,
                    // produzindo um halo quadrado. A sombra também era apenas o fill
                    // deslocado; com contorno grosso ela ficava completamente coberta
                    // e parecia "não funcionar". Agora ambos pertencem ao mesmo
                    // silhouette do glyph, inclusive quando há gradiente/efeitos.
                    QPainterPath caminho;
                    caminho.addText(pos, f, txt);

                    if (opt.shadow && !caminho.isEmpty()) {
                        QPainterPath sombra = caminho.translated(opt.shadowOffset);
                        p.setBrush(opt.shadowColor);
                        if (larguraContorno > 0) {
                            QPen shadowPen(opt.shadowColor, larguraContorno * 2.0);
                            shadowPen.setJoinStyle(Qt::RoundJoin);
                            shadowPen.setCapStyle(Qt::RoundCap);
                            p.setPen(shadowPen);
                        } else {
                            p.setPen(Qt::NoPen);
                        }
                        p.drawPath(sombra);
                    }

                    if (larguraContorno > 0 && !caminho.isEmpty()) {
                        // RC2.68: o contorno é visualmente EXTERNO. QPainter
                        // centraliza o stroke na borda do path; desenhamos o stroke
                        // primeiro e cobrimos sua metade interna com o fill do glyph.
                        QPen caneta(corContorno, larguraContorno * 2.0);
                        caneta.setJoinStyle(Qt::RoundJoin);
                        caneta.setCapStyle(Qt::RoundCap);
                        p.setPen(caneta);
                        p.setBrush(Qt::NoBrush);
                        p.drawPath(caminho);
                        p.setPen(Qt::NoPen);
                        p.setBrush(pincelLetra);
                        p.drawPath(caminho);
                        p.setBrush(Qt::NoBrush);
                    } else if (gradSpec.enabled() && !generic.colorOverride.isValid()) {
                        p.setPen(Qt::NoPen);
                        p.setBrush(pincelLetra);
                        p.drawPath(caminho);
                        p.setBrush(Qt::NoBrush);
                    } else {
                        p.setPen(cor);
                        p.drawText(pos, txt);
                    }
                }
                p.restore();
                ++desenhadas;
            }
            x += av;
            ++indice;
        }
        y += alturaLinha;
        if (restam <= 0) break;
    }
    p.setOpacity(1.0);
    Q_UNUSED(larguraTexto);
    return desenhadas;
}

} // namespace game
