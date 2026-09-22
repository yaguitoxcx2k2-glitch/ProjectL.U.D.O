// ============================================================================
//  PictureFx.h — Efeitos das imagens de tela, desenhados com QPainter.
//
//  Porte de SetBorder / SetGlow / SetBlink / SetDistort / SetEdgeShine /
//  SetMask / SetTransitionOut do InteractivePictureCore.js.
//
//  A ideia central: TUDO vira uma imagem composta. A borda, o brilho, a
//  máscara e a onda são pintados numa QImage; quem desenha na tela só recebe
//  essa imagem pronta, uma opacidade e alguns ajustes de transformação.
//
//  Por que assim, e não com shaders: o renderizador de CPU e o de GPU
//  precisam mostrar a MESMA coisa. Uma imagem composta serve aos dois — na
//  GPU ela vira textura. Um shader daria mais desempenho, mas duplicaria a
//  regra e abriria caminho para as duas telas divergirem (já aconteceu com a
//  mistura aditiva, e custou caro achar).
//
//  Custo: efeitos ESTÁTICOS (borda, brilho, máscara) são compostos uma vez e
//  ficam em cache. Só a onda e o brilho deslizante recompõem a cada quadro.
// ============================================================================
#pragma once

#include "core/Editor.h"
#include "core/Picture.h"
#include "game/Pictures.h"

#include <QFont>
#include <QHash>
#include <QImage>
#include <QPointF>

#include <functional>

namespace game {

/// O que a janela precisa para desenhar uma picture neste quadro.
struct PictureFrame {
    QImage  image;              ///< imagem já com os efeitos
    QSizeF  baseSize;           ///< tamanho da imagem ORIGINAL (a âncora usa este)
    QPointF drawOffset;         ///< canto da imagem composta em relação à original
    double  opacity = 255.0;    ///< já inclui piscar e transição
    double  scaleMulX = 1.0;    ///< multiplicadores vindos da transição
    double  scaleMulY = 1.0;
    double  angleAdd = 0.0;
    QPointF posOffset;          ///< deslocamento da transição, em px de tela
    bool    valid = false;
};

/// Guarda as imagens compostas para não refazer o trabalho a cada quadro.
/// Uma instância vive na sessão de jogo; o editor tem a sua.
class VisualFxCache
{
public:
    /// Monta o quadro de uma picture. `tempo` é o relógio dela (segundos).
    PictureFrame build(const LivePicture& lp, const core::Editor& ed);
    /// Igual ao build normal, mas usa uma imagem fornecida pelo chamador.
    /// Serve aos panoramas, que não precisam ocupar um slot da biblioteca.
    PictureFrame buildImage(const LivePicture& lp, const core::Editor& ed,
                            const QImage& source, const QString& sourceKey);
    /// Fonte usada quando a picture é TEXTO (ShowRichTextPicture).
    void setFont(const QFont& f) { m_fonte = f; }
    /// Resolvedor de \v[n] para o texto (a memória da partida).
    void setVariableResolver(std::function<QString(int)> r) { m_var = std::move(r); }
    /// Versão que muda toda vez que a imagem composta de um slot é refeita —
    /// é como o renderizador de GPU sabe que precisa reenviar a textura.
    quint64 version(int numero) const { return m_versoes.value(numero, 0); }
    void clear() { m_estaticos.clear(); m_versoes.clear(); m_ultimaChave.clear(); m_textos.clear(); }
    int  cachedCount() const { return int(m_estaticos.size()); }

private:
    struct Estatico { QImage img; QPointF offset; };
    /// chave = imagem + assinatura dos efeitos estáticos
    QHash<QString, Estatico> m_estaticos;
    QHash<int, quint64>      m_versoes;
    QHash<int, QString>      m_ultimaChave;   ///< para detectar recomposição
    QHash<QString, QImage>   m_textos;        ///< texto rico já desenhado
    QFont m_fonte;
    std::function<QString(int)> m_var;
};

// ---- peças usadas pelo cache (públicas para os testes) --------------------
/// Desfoque em caixa, separável, feito na mão. Três passadas aproximam bem um
/// gaussiano e é barato o suficiente para rodar uma vez por composição.
void borrarCaixa(QImage& img, int raio);

/// Moldura 9-slice: porte literal do `draw9Slice` do plugin (cantos inteiros,
/// laterais esticadas ou repetidas, miolo esticado).
void desenhar9Slice(QPainter& p, const QImage& src, const QRectF& dst, int slice,
                    double escala, bool tileH, bool tileV);

/// Redimensiona a própria imagem usando os quatro cortes do PictureNineSlice.
/// Esta função é compartilhada por runtime, preview e testes.
QImage renderPictureNineSlice(const QImage& src, const core::PictureNineSlice& spec);

/// Máscara determinística do "dissolver": pixels somem em ordem de ruído fixo.
/// `avanco` 0..1. A imagem devolvida é 64×64 e se repete.
QImage mascaraDissolver(double avanco);

/// Estado de uma transição num instante: multiplicadores e deslocamentos.
/// `entrando` = true na transição de entrada.
struct TransitionState {
    double opacityMul = 1.0;
    double scaleMulX = 1.0, scaleMulY = 1.0;
    double angleAdd = 0.0;
    QPointF offset;
    double dissolve = 0.0;      ///< 0 = inteira; 1 = sumiu
};
TransitionState transitionAt(core::PictureTransition t, double avanco, bool entrando);

// Nome legado para plugins/testes existentes.
using PictureFxCache = VisualFxCache;

} // namespace game
