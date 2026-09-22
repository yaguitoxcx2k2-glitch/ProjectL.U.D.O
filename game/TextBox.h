// ============================================================================
//  TextBox.h — Preparo do texto da caixa de mensagem.
//
//  Transforma o texto cru escrito no editor em PÁGINAS já quebradas em linhas,
//  prontas para desenhar letra a letra. Fica aqui, no runtime sem interface,
//  para poder ser testado sem abrir tela — e para o editor poder mostrar o
//  mesmo resultado no preview (é a mesma função nos dois lugares).
//
//  Códigos aceitos dentro do texto (estilo editores de RPG):
//      \n        quebra de linha
//      \c[n]     muda a cor do texto (0 = cor normal)
//      \FN[nome] muda a família da fonte; \FS[n] muda o tamanho
//      \GR[vertical,#fff,#69f] gradiente (horizontal/diagonal também)
//      \.        pausa curta   (0,25 s)
//      \|        pausa longa   (1 s)
//      \!        espera o jogador apertar a tecla de confirmar
//      \>  \<    liga/desliga trecho instantâneo (sem digitação)
//      \v[n]     valor da variável n (resolvido por quem chama)
//      \\        uma barra invertida de verdade
//
//  Efeitos animados (usados pelas legendas, porte do SubtitleSystem.js):
//      \WV[amp,vel]        onda
//      \SK[intensidade]    tremida
//      \RB[vel]            arco-íris
//      \FL[r,g,b,vel]      flash entre a cor atual e a cor dada
//      \BL[vel]            desfoque que vai clareando (materialização)
//      \GL[intens,freq]    glitch
// ============================================================================
#pragma once

#include "core/IconSet.h"
#include "core/TextEffects.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

namespace game {

/// Efeito animado que vale a partir de uma letra.
struct TextFx {
    enum Kind { None, Wave, Shake, Rainbow, Flash, Blur, Glitch,
                Bounce,   ///< \BO[amp,vel] — pulo
                Jitter,   ///< \JD[int,vel] — dança
                Scale,    ///< \SC[amp,vel] — respiração
                Spin,     ///< \SP[vel]     — girar
                Sweep };  ///< \SW[larg,vel]— brilho deslizante
    Kind   kind = None;
    double a = 0, b = 0, c = 0, d = 0;   ///< parâmetros do código

    bool operator==(const TextFx& o) const {
        return kind == o.kind && qFuzzyCompare(a + 1, o.a + 1) && qFuzzyCompare(b + 1, o.b + 1)
            && qFuzzyCompare(c + 1, o.c + 1) && qFuzzyCompare(d + 1, o.d + 1);
    }
};

/// Formatação que vale a partir de uma letra (os códigos do texto rico).
/// Valor "não definido" significa herdar de quem está desenhando.
struct TextStyle {
    int    fontSize = 0;         ///< \FS[n]; 0 = tamanho de quem desenha
    QString fontFamily;          ///< \FN[nome]; vazio = fonte de quem desenha
    QColor color;                ///< \FC[#hex]; inválida = cor de quem desenha
    QColor outlineColor;         ///< \OC[#hex]
    int    outlineWidth = -1;    ///< \OW[n]; -1 = o de quem desenha
    bool   bold = false;         ///< \B
    bool   italic = false;       ///< \IT
    core::TextGradientSpec gradient; ///< \GR[dir,#cor1,#cor2,...]

    bool operator==(const TextStyle& o) const {
        return fontSize == o.fontSize && fontFamily == o.fontFamily && color == o.color && outlineColor == o.outlineColor
            && outlineWidth == o.outlineWidth && bold == o.bold && italic == o.italic && gradient == o.gradient;
    }
};

/// Uma letra já posicionada, com formatação e efeitos que valem a partir dela.
struct TypedChar {
    QString ch;               ///< grafema a desenhar (vazio = só controle)
    int    colorIdx = 0;      ///< índice de cor (\c[n]); 0 = cor normal
    double pauseSec = 0.0;    ///< pausa ANTES de revelar esta letra
    bool   waitKey  = false;  ///< espera a tecla ANTES de revelar esta letra
    bool   instant  = false;  ///< revela sem digitar
    TextFx fx;                ///< efeito animado (onda, tremida, glitch…)
    TextStyle style;          ///< tamanho, cor, contorno, negrito, itálico
    int    icon = -1;         ///< \I[n]: ícone da folha do projeto (-1 = nenhum)

    /// Ícone conta como "letra" para digitação e para a medida da linha.
    bool isDrawable() const { return !ch.isNull() || icon >= 0; }
    bool isIcon() const { return icon >= 0; }
    bool isWhitespace() const {
        if (ch.isEmpty()) return false;
        for (const QChar c : ch) if (!c.isSpace()) return false;
        return true;
    }
};

struct TextLine {
    QVector<TypedChar> chars;
};

struct TextPage {
    QVector<TextLine> lines;
    int drawableCount() const;
};

/// Cores usadas por \c[n]. O índice 0 é a cor normal do texto.
QVector<QColor> messagePalette();
QColor          messageColor(int idx);

/// Quebra o texto em páginas.
///  `maxWidth`   largura útil em pixels lógicos (dentro da moldura)
///  `maxLines`   linhas por página
///  `varValue`   resolve \v[n]; se vazio, devolve "0"
QVector<TextPage> layoutMessage(const QString& raw, const QFont& font,
                                double maxWidth, int maxLines,
                                const std::function<QString(int)>& varValue = {},
                                const core::IconSet* icones = nullptr);

/// Fonte de uma letra, aplicando \FS \B \IT sobre a fonte base.
QFont charFont(const QFont& base, const TypedChar& c);
/// Quanto esta letra (ou ícone) ocupa na horizontal.
double charAdvance(const QFont& base, const TypedChar& c, const core::IconSet* icones);
/// Tamanho de uma página inteira (maior linha × soma das alturas).
QSizeF measurePage(const TextPage& page, const QFont& base, const core::IconSet* icones);
/// Altura de uma linha, considerando a maior letra dela.
double lineHeight(const TextLine& linha, const QFont& base);

/// Quantas letras "de verdade" a página tem (para saber quando terminou).
int totalDrawable(const QVector<TextPage>& pages);
/// Quantas palavras visuais existem na página. Espaços/quebras separam grupos;
/// ícones contam como conteúdo. Usado pelo stagger por palavra no motor comum.
int textPageWordCount(const TextPage& page);

// ------------------------------------------------------- efeitos animados --
/// Deslocamento em pixels desta letra no instante `t` (segundos).
/// `index` é a posição da letra no texto — é o que faz a onda "andar".
QPointF fxOffset(const TypedChar& c, double t, int index);
/// Cor final da letra no instante `t` (arco-íris e flash mexem na cor).
QColor  fxColor(const TypedChar& c, double t, const QColor& base);
/// Opacidade 0..1 (o blur do plugin vira "aparecer clareando" aqui).
double  fxOpacity(const TypedChar& c, double t);
/// A letra some neste quadro? (glitch)
bool    fxHidden(const TypedChar& c, double t, int index);
/// Escala da letra (respiração \SC) — 1,0 quando não há efeito de escala.
double  fxScale(const TypedChar& c, double t, int index);
/// Rotação da letra em graus (girar \SP).
double  fxAngle(const TypedChar& c, double t, int index);
/// Clarão do brilho deslizante (\SW): 0..1, quanto esta letra está iluminada.
double  fxSweep(const TypedChar& c, double t, int index);

} // namespace game
