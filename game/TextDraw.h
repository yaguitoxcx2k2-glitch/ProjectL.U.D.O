// ============================================================================
//  TextDraw.h — Desenho de texto formatado, num lugar só.
//
//  Antes, a caixa de mensagem e as legendas desenhavam letra a letra cada uma
//  do seu jeito, com o mesmo código copiado. Agora existe UMA função que sabe
//  desenhar uma página de texto: com cor, contorno, sombra, ícones, tamanho e
//  estilo por letra, e com os onze efeitos animados.
//
//  Isso é o que permite prometer que mensagem, legenda e imagem de texto falam
//  a mesma língua: quando um código novo entra no parser, os três ganham.
// ============================================================================
#pragma once

#include "core/IconSet.h"
#include "game/TextBox.h"
#include "core/TextEffects.h"

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QVector>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace game {

/// Alinhamento horizontal do bloco de texto.
enum class TextAlign { Left, Center, Right };

/// Tudo que é aparência "de fora" — o que a letra não define sozinha.
struct TextDrawOpts {
    QColor  color = QColor(255, 255, 255);   ///< cor padrão das letras
    QColor  outlineColor = QColor(0, 0, 0);
    int     outlineWidth = 0;                ///< 0 = sem contorno
    bool    shadow = false;
    QColor  shadowColor = QColor(0, 0, 0, 140);
    QPointF shadowOffset = QPointF(2, 2);
    double  opacity = 1.0;
    /// Quantas letras já apareceram (digitação). -1 = todas.
    int     revealed = -1;
    double  time = 0.0;                      ///< relógio dos efeitos, em segundos
    TextAlign align = TextAlign::Left;
    const core::IconSet* icons = nullptr;
    /// Compatibilidade antiga: segunda cor = gradiente vertical.
    QColor  gradient2;
    /// RC2.55: gradiente multi-cor/direção compartilhado por todos consumidores.
    core::TextGradientSpec gradient;
    /// RC2.55: Entrada / Loop / Saída genéricos; presets são só configuração.
    core::TextEffectStack effects;
    /// >=0 quando o consumidor entrou na fase de saída. -1 = ainda visível.
    double exitTime = -1.0;
    /// RC2.55.1: instante em que cada glyph realmente se tornou visível.
    /// Mensagens preenchem isso a partir da máquina de reveal (incluindo
    /// pausas/\!); consumidores sem reveal próprio deixam vazio. Assim a
    /// Entrada nunca termina escondida antes da letra nascer.
    QVector<double> revealTimesSec;
};

/// Desenha a página dentro de `area` (o topo-esquerda é o começo do texto).
/// Devolve quantas letras foram efetivamente desenhadas.
int drawTextPage(QPainter& p, const TextPage& page, const QFont& base,
                 const QRectF& area, const TextDrawOpts& opt);

} // namespace game
