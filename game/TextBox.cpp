#include "TextBox.h"

#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <cmath>

namespace game {

int TextPage::drawableCount() const
{
    int n = 0;
    for (const TextLine& l : lines)
        for (const TypedChar& c : l.chars)
            if (c.isDrawable()) ++n;
    return n;
}

int totalDrawable(const QVector<TextPage>& pages)
{
    int n = 0;
    for (const TextPage& p : pages) n += p.drawableCount();
    return n;
}

int textPageWordCount(const TextPage& page)
{
    int words = 0;
    bool inside = false;
    for (const TextLine& line : page.lines) {
        for (const TypedChar& c : line.chars) {
            if (!c.isDrawable()) continue;
            const bool separator = !c.isIcon() && c.isWhitespace();
            if (separator) inside = false;
            else if (!inside) { ++words; inside = true; }
        }
        // Uma quebra de linha também encerra a palavra atual.
        inside = false;
    }
    return qMax(1, words);
}

QVector<QColor> messagePalette()
{
    // Paleta enxuta, no espírito do editores de RPG: 0 = normal e mais 7 cores.
    return { QColor("#ffffff"), QColor("#7fd4ff"), QColor("#ff9b7f"), QColor("#9bff9b"),
             QColor("#ffe27f"), QColor("#d9a0ff"), QColor("#9fb4ff"), QColor("#8a8a8a") };
}

QColor messageColor(int idx)
{
    const QVector<QColor> pal = messagePalette();
    if (idx < 0 || idx >= pal.size()) return pal.first();
    return pal[idx];
}

namespace {

/// Item intermediário da análise: uma letra ou um controle puro.
struct Token {
    TypedChar tc;
    bool      newline = false;
};

/// Lê um número entre colchetes logo depois da posição `i` (que aponta para
/// '['). Devolve -1 se não houver colchete bem formado.
int readBracketNumber(const QString& s, int& i)
{
    if (i >= s.size() || s[i] != QLatin1Char('[')) return -1;
    const int close = s.indexOf(QLatin1Char(']'), i);
    if (close < 0) return -1;
    bool ok = false;
    const int n = s.mid(i + 1, close - i - 1).toInt(&ok);
    if (!ok) return -1;
    i = close + 1;
    return n;
}

/// Lê uma lista de números entre colchetes: "[4,6]" -> {4,6}.
QVector<double> readBracketList(const QString& s, int& i)
{
    QVector<double> out;
    if (i >= s.size() || s[i] != QLatin1Char('[')) return out;
    const int close = s.indexOf(QLatin1Char(']'), i);
    if (close < 0) return out;
    const QStringList partes = s.mid(i + 1, close - i - 1).split(QLatin1Char(','));
    for (const QString& p : partes) {
        bool ok = false;
        const double v = p.trimmed().toDouble(&ok);
        out.push_back(ok ? v : 0.0);
    }
    i = close + 1;
    return out;
}

QVector<Token> tokenize(const QString& raw, const std::function<QString(int)>& varValue)
{
    QVector<Token> out;
    int colorIdx = 0;
    bool instant = false;
    TextFx fx;
    TextStyle estilo;                     // \FS \FC \OC \OW \B \IT
    double pendingPause = 0.0;
    bool pendingWait = false;

    // Lê "[algumacoisa]" logo depois do código; devolve vazio se não houver.
    auto lerColchetes = [&raw](int& i) {
        if (i >= raw.size() || raw[i] != QLatin1Char('[')) return QString();
        const int fim = raw.indexOf(QLatin1Char(']'), i);
        if (fim < 0) return QString();
        const QString dentro = raw.mid(i + 1, fim - i - 1);
        i = fim + 1;
        return dentro;
    };

    auto pushChar = [&](const QString& c) {
        Token t;
        t.tc.ch = c;
        t.tc.colorIdx = colorIdx;
        t.tc.instant = instant;
        t.tc.fx = fx;
        t.tc.style = estilo;
        t.tc.pauseSec = pendingPause;
        t.tc.waitKey = pendingWait;
        pendingPause = 0.0;
        pendingWait = false;
        out.push_back(t);
    };
    auto pushText = [&](const QString& value) {
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
        int start = 0;
        finder.toStart();
        while (true) {
            const int end = finder.toNextBoundary();
            if (end < 0) break;
            if (end > start) pushChar(value.mid(start, end - start));
            start = end;
        }
    };
    auto pushControl = [&] {
        // Controle sem letra depois (fim do texto/da linha): vira um item só
        // de controle, para a pausa/espera não se perder.
        Token t;
        t.tc.ch = QChar();
        t.tc.colorIdx = colorIdx;
        t.tc.instant = instant;
        t.tc.fx = fx;
        t.tc.style = estilo;
        t.tc.pauseSec = pendingPause;
        t.tc.waitKey = pendingWait;
        pendingPause = 0.0;
        pendingWait = false;
        out.push_back(t);
    };

    for (int i = 0; i < raw.size();) {
        const QChar c = raw[i];
        if (c == QLatin1Char('\n')) {
            if (pendingPause > 0 || pendingWait) pushControl();
            Token t; t.newline = true; out.push_back(t);
            ++i;
            continue;
        }
        if (c != QLatin1Char('\\')) {
            int end = i;
            while (end < raw.size() && raw[end] != QLatin1Char('\\') && raw[end] != QLatin1Char('\n')) ++end;
            pushText(raw.mid(i, end - i));
            i = end;
            continue;
        }

        // ----- código de controle -----
        ++i;
        if (i >= raw.size()) { pushChar(QString(QLatin1Char('\\'))); break; }
        const QChar code = raw[i++];
        switch (code.unicode()) {
        case 'c': case 'C': {
            const int n = readBracketNumber(raw, i);
            colorIdx = (n >= 0) ? n : 0;
            break;
        }
        case 'v': case 'V': {
            const int n = readBracketNumber(raw, i);
            const QString val = varValue ? varValue(n) : QStringLiteral("0");
            pushText(val);
            break;
        }
        case '.': pendingPause += 0.25; break;
        case '|': pendingPause += 1.0;  break;
        case '!': pendingWait = true;   break;
        case '>': instant = true;       break;
        case '<': instant = false;      break;
        case 'n': { Token t; t.newline = true; out.push_back(t); break; }
        // ---- efeitos animados (porte do SubtitleSystem) -------------------
        case 'w': case 'W': {          // \WV[amp,vel] — onda
            if (i < raw.size() && (raw[i] == QLatin1Char('v') || raw[i] == QLatin1Char('V'))) ++i;
            const QVector<double> n = readBracketList(raw, i);
            fx = TextFx{ TextFx::Wave, n.value(0, 4.0), n.value(1, 6.0), 0, 0 };
            break;
        }
        case 's': case 'S': {   // \SK tremida · \SC respiração · \SP girar · \SW brilho
            const QChar prox = (i < raw.size()) ? raw[i].toUpper() : QChar();
            if (prox == QLatin1Char('C')) {
                ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Scale, n.value(0, 10.0), n.value(1, 6.0), 0, 0 };
            } else if (prox == QLatin1Char('P')) {
                ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Spin, n.value(0, 4.0), 0, 0, 0 };
            } else if (prox == QLatin1Char('W')) {
                ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Sweep, n.value(0, 40.0), n.value(1, 2.0), 0, 0 };
            } else {
                if (prox == QLatin1Char('K')) ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Shake, n.value(0, 3.0), 0, 0, 0 };
            }
            break;
        }
        case 'r': case 'R': {          // \RB[vel] — arco-íris
            if (i < raw.size() && (raw[i] == QLatin1Char('b') || raw[i] == QLatin1Char('B'))) ++i;
            const QVector<double> n = readBracketList(raw, i);
            fx = TextFx{ TextFx::Rainbow, n.value(0, 90.0), 0, 0, 0 };
            break;
        }
        case 'f': case 'F': {   // \FL flash · \FS tamanho · \FC cor · \FN fonte
            const QChar prox = (i < raw.size()) ? raw[i].toUpper() : QChar();
            if (prox == QLatin1Char('S')) {
                ++i;
                const int n = readBracketNumber(raw, i);
                estilo.fontSize = (n > 0) ? n : 0;      // 0 volta ao tamanho base
            } else if (prox == QLatin1Char('C')) {
                ++i;
                const QColor c(lerColchetes(i));
                estilo.color = c.isValid() ? c : QColor();
            } else if (prox == QLatin1Char('N')) {
                ++i;
                estilo.fontFamily = lerColchetes(i).trimmed().left(256);
            } else {
                if (prox == QLatin1Char('L')) ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Flash, n.value(0, 255.0), n.value(1, 0.0),
                             n.value(2, 0.0), n.value(3, 50.0) };
            }
            break;
        }
        case 'b': case 'B': {
            // Três códigos começam com B: \BL (materializar), \BO (pulo) e
            // \B sozinho (negrito). A segunda letra decide — e sem ela é
            // negrito, não "materializar com valores padrão", que era o que
            // acontecia antes: escrever \B ligava um efeito sem querer.
            const QChar prox = (i < raw.size()) ? raw[i].toUpper() : QChar();
            if (prox == QLatin1Char('L')) {
                ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Blur, n.value(0, 12.0), 0, 0, 0 };
            } else if (prox == QLatin1Char('O')) {
                ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Bounce, n.value(0, 6.0), n.value(1, 8.0), 0, 0 };
            } else {
                estilo.bold = !estilo.bold;
            }
            break;
        }
        case 'g': case 'G': {          // \GL glitch · \GR gradiente
            const QChar prox = (i < raw.size()) ? raw[i].toUpper() : QChar();
            if (prox == QLatin1Char('R')) {
                ++i;
                const QString spec = lerColchetes(i);
                if (spec.trimmed().compare(QLatin1String("off"), Qt::CaseInsensitive) == 0 || spec.trimmed().isEmpty()) {
                    estilo.gradient = core::TextGradientSpec();
                } else {
                    const QStringList parts = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    core::TextGradientSpec grad;
                    int colorStart = 0;
                    if (!parts.isEmpty()) {
                        const QString maybeDir = parts.first().trimmed().toLower();
                        if (maybeDir == QLatin1String("vertical") || maybeDir == QLatin1String("horizontal") || maybeDir == QLatin1String("diagonal")) {
                            grad.direction = maybeDir;
                            colorStart = 1;
                        }
                    }
                    for (int k = colorStart; k < parts.size() && grad.colors.size() < 8; ++k) {
                        const QColor color(parts[k].trimmed());
                        if (color.isValid()) grad.colors.push_back(color);
                    }
                    estilo.gradient = grad.enabled() ? grad : core::TextGradientSpec();
                }
            } else {
                if (prox == QLatin1Char('L')) ++i;
                const QVector<double> n = readBracketList(raw, i);
                fx = TextFx{ TextFx::Glitch, n.value(0, 5.0), n.value(1, 4.0), 0, 0 };
            }
            break;
        }
        case 'o': case 'O': {   // \OC contorno · \OW espessura · \OFF desliga tudo
            const QChar p1 = (i < raw.size()) ? raw[i].toUpper() : QChar();
            const QChar p2 = (i + 1 < raw.size()) ? raw[i + 1].toUpper() : QChar();
            if (p1 == QLatin1Char('C')) {
                ++i;
                const QColor c(lerColchetes(i));
                estilo.outlineColor = c.isValid() ? c : QColor();
            } else if (p1 == QLatin1Char('W')) {
                ++i;
                const int n = readBracketNumber(raw, i);
                estilo.outlineWidth = (n >= 0) ? n : -1;
            } else if (p1 == QLatin1Char('F') && p2 == QLatin1Char('F')) {
                i += 2;
                fx = TextFx();
            }
            break;
        }
        case 'i': case 'I': {   // \I[n] ícone · \IT itálico
            const QChar prox = (i < raw.size()) ? raw[i].toUpper() : QChar();
            if (prox == QLatin1Char('T')) {
                ++i;
                estilo.italic = !estilo.italic;
            } else {
                const int n = readBracketNumber(raw, i);
                if (n >= 0) {
                    Token t;
                    t.tc.ch = QChar();
                    t.tc.icon = n;
                    t.tc.colorIdx = colorIdx;
                    t.tc.instant = instant;
                    t.tc.fx = fx;
                    t.tc.style = estilo;
                    t.tc.pauseSec = pendingPause;
                    t.tc.waitKey = pendingWait;
                    pendingPause = 0.0;
                    pendingWait = false;
                    out.push_back(t);
                }
            }
            break;
        }
        case 'j': case 'J': {   // \JD[intensidade,vel] — dança
            if (i < raw.size() && raw[i].toUpper() == QLatin1Char('D')) ++i;
            const QVector<double> n = readBracketList(raw, i);
            fx = TextFx{ TextFx::Jitter, n.value(0, 3.0), n.value(1, 5.0), 0, 0 };
            break;
        }
        case 'x': case 'X': fx = TextFx(); break;   // \X — desliga o efeito
        case '\\': pushChar(QString(QLatin1Char('\\'))); break;
        default:
            // Código desconhecido: mostra como estava escrito, em vez de sumir
            // com o texto (nada pior que perder uma fala e não saber por quê).
            pushChar(QString(QLatin1Char('\\')));
            pushChar(QString(code));
            break;
        }
    }
    if (pendingPause > 0 || pendingWait) pushControl();
    return out;
}

} // namespace

QVector<TextPage> layoutMessage(const QString& raw, const QFont& font,
                                double maxWidth, int maxLines,
                                const std::function<QString(int)>& varValue,
                                const core::IconSet* icones)
{
    QVector<TextPage> pages;
    if (maxLines < 1) maxLines = 1;
    if (maxWidth < 1) maxWidth = 1;

    const QFontMetricsF fm(font);
    const QVector<Token> tokens = tokenize(raw, varValue);

    TextPage page;
    TextLine line;
    double lineWidth = 0.0;
    // Palavra pendente: só sabemos se ela cabe quando ela termina.
    QVector<TypedChar> word;
    double wordWidth = 0.0;

    auto fecharLinha = [&] {
        page.lines.push_back(line);
        line = TextLine();
        lineWidth = 0.0;
        if (page.lines.size() >= maxLines) {
            pages.push_back(page);
            page = TextPage();
        }
    };
    auto despejarPalavra = [&] {
        if (word.isEmpty()) return;
        if (lineWidth + wordWidth > maxWidth && !line.chars.isEmpty())
            fecharLinha();                       // a palavra inteira desce
        for (const TypedChar& t : word) line.chars.push_back(t);
        lineWidth += wordWidth;
        word.clear();
        wordWidth = 0.0;
    };

    for (const Token& t : tokens) {
        if (t.newline) { despejarPalavra(); fecharLinha(); continue; }
        if (!t.tc.isDrawable()) {                 // controle puro
            word.push_back(t.tc);
            continue;
        }
        // A largura vem do estilo DA LETRA: com \FS[40] no meio da frase, a
        // quebra de linha precisa contar as letras grandes como grandes.
        const double w = charAdvance(font, t.tc, icones);
        if (t.tc.isWhitespace()) {
            despejarPalavra();
            // Espaço no começo da linha não conta (evita linha "empurrada").
            if (!line.chars.isEmpty()) {
                line.chars.push_back(t.tc);
                lineWidth += w;
            }
            continue;
        }
        // Palavra maior que a linha inteira: quebra na força bruta.
        if (wordWidth + w > maxWidth) {
            despejarPalavra();
            if (!line.chars.isEmpty()) fecharLinha();
        }
        word.push_back(t.tc);
        wordWidth += w;
    }
    despejarPalavra();
    if (!line.chars.isEmpty()) page.lines.push_back(line);
    if (!page.lines.isEmpty()) pages.push_back(page);
    return pages;
}

} // namespace game

// ============================================================================
//  Efeitos animados
//
//  Tudo é função do tempo `t` (em segundos) e do índice da letra — nenhum
//  estado guardado. É o que permite desenhar o mesmo texto em dois lugares
//  (jogo e preview do editor) sem sincronizar nada.
// ============================================================================
namespace game {

QPointF fxOffset(const TypedChar& c, double t, int index)
{
    switch (c.fx.kind) {
    case TextFx::Wave: {
        // amplitude em px, velocidade em "ciclos" — a fase anda com o índice,
        // por isso a onda percorre a frase em vez de subir tudo junto.
        const double amp = c.fx.a, vel = qMax(0.1, c.fx.b);
        return QPointF(0.0, std::sin(t * vel + index * 0.4) * amp);
    }
    case TextFx::Shake: {
        const double k = c.fx.a;
        // Ruído barato e determinístico: sem random, o preview do editor e o
        // jogo mostram exatamente a mesma coisa no mesmo instante.
        const double fx1 = std::sin((t * 37.0) + index * 12.9898) * 43758.5453;
        const double fx2 = std::sin((t * 41.0) + index * 78.2330) * 12345.6789;
        return QPointF((fx1 - std::floor(fx1) - 0.5) * 2.0 * k,
                       (fx2 - std::floor(fx2) - 0.5) * 2.0 * k);
    }
    case TextFx::Bounce: {
        // Pulo: cada letra sobe na sua vez (a fase anda com o índice), e o
        // valor absoluto do seno faz a "quicada" — nunca desce do chão.
        const double amp = c.fx.a, vel = qMax(0.1, c.fx.b);
        return QPointF(0.0, -std::fabs(std::sin(t * vel + index * 0.5)) * amp);
    }
    case TextFx::Jitter: {
        // Dança: como a tremida, mas com ritmo (a letra vai e volta no compasso
        // em vez de vibrar sem parar).
        const double k = c.fx.a, vel = qMax(0.1, c.fx.b);
        return QPointF(std::sin(t * vel * 1.7 + index * 1.1) * k,
                       std::cos(t * vel * 2.3 + index * 0.7) * k);
    }
    case TextFx::Glitch: {
        const double intens = c.fx.a, freq = qMax(0.1, c.fx.b);
        const double g = std::sin(t * freq * 6.0 + index * 2.7);
        if (g < 0.85) return QPointF();
        const double s = std::sin(t * 91.0 + index * 5.1) * 1000.0;
        return QPointF((s - std::floor(s) - 0.5) * 2.0 * intens, 0.0);
    }
    default:
        return QPointF();
    }
}

QColor fxColor(const TypedChar& c, double t, const QColor& base)
{
    switch (c.fx.kind) {
    case TextFx::Rainbow: {
        // `a` no plugin é "quanto maior, mais lento" — mantemos a mesma ideia.
        const double vel = qMax(1.0, c.fx.a);
        const double h = std::fmod((t * 360.0 * 60.0 / vel), 360.0);
        return QColor::fromHsv(int(h), 220, 255);
    }
    case TextFx::Flash: {
        const double vel = qMax(1.0, c.fx.d);
        const double k = 0.5 + 0.5 * std::sin(t * (60.0 / vel) * 6.2831853);
        const QColor alvo(qBound(0, int(c.fx.a), 255), qBound(0, int(c.fx.b), 255),
                          qBound(0, int(c.fx.c), 255));
        return QColor(int(base.red()   + (alvo.red()   - base.red())   * k),
                      int(base.green() + (alvo.green() - base.green()) * k),
                      int(base.blue()  + (alvo.blue()  - base.blue())  * k),
                      base.alpha());
    }
    default:
        return base;
    }
}

double fxOpacity(const TypedChar& c, double t)
{
    if (c.fx.kind != TextFx::Blur) return 1.0;
    // Sem shader de desfoque: o "materializar" vira um clarear rápido, que é o
    // efeito que a pessoa realmente percebe na tela.
    const double vel = qMax(1.0, c.fx.a);
    return qBound(0.0, t * (60.0 / vel) * 0.5, 1.0);
}

bool fxHidden(const TypedChar& c, double t, int index)
{
    if (c.fx.kind != TextFx::Glitch) return false;
    const double freq = qMax(0.1, c.fx.b);
    const double g = std::sin(t * freq * 7.3 + index * 4.1);
    return g > 0.97;               // sumiço rápido e esporádico
}

double fxScale(const TypedChar& c, double t, int index)
{
    if (c.fx.kind != TextFx::Scale) return 1.0;
    // `a` é a amplitude em PORCENTAGEM (como no plugin): 10 = ±10%.
    const double amp = c.fx.a / 100.0, vel = qMax(0.1, c.fx.b);
    return 1.0 + std::sin(t * vel + index * 0.3) * amp;
}

double fxAngle(const TypedChar& c, double t, int index)
{
    if (c.fx.kind != TextFx::Spin) return 0.0;
    return std::fmod(t * c.fx.a * 60.0 + index * 10.0, 360.0);
}

double fxSweep(const TypedChar& c, double t, int index)
{
    if (c.fx.kind != TextFx::Sweep) return 0.0;
    // A faixa de luz atravessa a frase: `a` é a largura em letras (convertida
    // de pixels por aproximação) e `b` a velocidade em passagens por segundo.
    const double larguraLetras = qMax(1.0, c.fx.a / 12.0);
    const double vel = qMax(0.1, c.fx.b);
    const double periodo = 24.0 + larguraLetras * 2.0;
    const double pos = std::fmod(t * vel * periodo, periodo) - larguraLetras;
    const double d = std::fabs(index - pos);
    if (d > larguraLetras) return 0.0;
    return 1.0 - d / larguraLetras;
}

QFont charFont(const QFont& base, const TypedChar& c)
{
    QFont f = base;
    if (!c.style.fontFamily.trimmed().isEmpty()) f.setFamily(c.style.fontFamily.trimmed());
    if (c.style.fontSize > 0) f.setPixelSize(c.style.fontSize);
    if (c.style.bold) f.setBold(true);
    if (c.style.italic) f.setItalic(true);
    return f;
}

double charAdvance(const QFont& base, const TypedChar& c, const core::IconSet* icones)
{
    if (c.isIcon()) {
        // O ícone ocupa um quadrado da altura da linha: assim ele acompanha o
        // tamanho da fonte em vez de ter tamanho fixo.
        const QFontMetricsF fm(charFont(base, c));
        return fm.height();
    }
    if (c.ch.isNull()) return 0.0;
    const QFontMetricsF fm(charFont(base, c));
    Q_UNUSED(icones);
    return fm.horizontalAdvance(c.ch);
}

double lineHeight(const TextLine& linha, const QFont& base)
{
    double h = QFontMetricsF(base).height();
    for (const TypedChar& c : linha.chars) {
        if (c.style.fontSize <= 0 && c.style.fontFamily.trimmed().isEmpty() && !c.style.bold && !c.style.italic) continue;
        h = qMax(h, QFontMetricsF(charFont(base, c)).height());
    }
    return h;
}

QSizeF measurePage(const TextPage& page, const QFont& base, const core::IconSet* icones)
{
    double largura = 0.0, altura = 0.0;
    for (const TextLine& l : page.lines) {
        double w = 0.0;
        for (const TypedChar& c : l.chars) w += charAdvance(base, c, icones);
        largura = qMax(largura, w);
        altura += lineHeight(l, base);
    }
    return QSizeF(largura, altura);
}

} // namespace game
