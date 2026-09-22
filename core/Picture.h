// ============================================================================
//  Picture.h — Imagens de tela ("pictures"), porte do InteractivePictureCore.js.
//
//  Uma picture é uma imagem que o EVENTO mostra por cima do jogo: um retrato
//  de quem fala, um título de capítulo, um mapa do tesouro, um balão. Ela não
//  é um tile e não é um personagem — vive num slot numerado (como no RPG
//  Maker: "Mostrar imagem nº 3") e o número decide quem fica na frente.
//
//  Duas coisas moram aqui, e só isso:
//    · `PictureAsset` — o arquivo de imagem guardado no projeto (biblioteca);
//    · `PictureDef`   — o estado visual de uma picture (posição, escala,
//                       opacidade, ângulo, âncora, mistura, física).
//
//  Nada de animação, nada de desenho: o que muda a cada quadro é o
//  `game::PictureManager`, e quem desenha é o `game::GameSession`. Assim o
//  editor pode montar e pré-visualizar uma picture sem tocar no runtime.
// ============================================================================
#pragma once

#include "Model.h"
#include "FrameSequence.h"
#include "VisualEffects.h"
#include "TextEffects.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>

namespace core {

// ----------------------------------------------------------- biblioteca
/// Uma imagem disponível para as pictures. Fica embutida no projeto (como o
/// charset e os tilesets já ficam), então o `.json` continua sendo um arquivo
/// só — nada de caminho quebrado quando o autor move a pasta.
struct PictureAsset {
    QString id = idGen();
    QString name;        ///< nome mostrado na interface ("titulo")
    QString sourcePath;  ///< de onde veio (informativo)
    QImage  image;

    bool  isValid() const { return !image.isNull(); }
    QSize size() const { return image.size(); }
};

// -------------------------------------------------------------- âncora
/// Qual ponto da imagem fica na posição X/Y informada.
enum class PictureAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    Custom
};

/// Fator 0..1 dentro da imagem (0,0 = canto superior esquerdo).
/// Para `Custom` devolve (customX, customY) como vieram.
QPointF pictureAnchorFactor(PictureAnchor a, double customX = 0.0, double customY = 0.0);
QString       pictureAnchorId(PictureAnchor a);
PictureAnchor pictureAnchorFromId(const QString& id);
QString       pictureAnchorLabel(PictureAnchor a);

// -------------------------------------------------------------- mistura
/// Como a imagem se combina com o que já está na tela.
enum class PictureBlend { Normal, Add, Multiply, Screen };

QString      pictureBlendId(PictureBlend b);
PictureBlend pictureBlendFromId(const QString& id);
QString      pictureBlendLabel(PictureBlend b);

// -------------------------------------------------------------- espaço
/// Onde a posição X/Y é medida.
enum class PictureSpace {
    Screen,   ///< pixels de tela: a picture não se move com a câmera (padrão RM)
    Map       ///< pixels do mapa: a picture rola junto com o cenário
};

QString      pictureSpaceId(PictureSpace s);
PictureSpace pictureSpaceFromId(const QString& id);
QString      pictureSpaceLabel(PictureSpace s);



// -------------------------------------------------------------- 9-slice
/// Redimensionamento preservando cantos. O layout pertence à Picture, não à
/// borda decorativa: a própria imagem vira um painel/janela escalável.
enum class PictureNineSliceMode { Stretch, Tile };

QString pictureNineSliceModeId(PictureNineSliceMode m);
PictureNineSliceMode pictureNineSliceModeFromId(const QString& id);
QString pictureNineSliceModeLabel(PictureNineSliceMode m);

struct PictureNineSlice {
    bool enabled = false;
    int width = 0;   ///< tamanho final em pixels lógicos; 0 = largura original
    int height = 0;  ///< tamanho final em pixels lógicos; 0 = altura original
    int left = 8, top = 8, right = 8, bottom = 8;
    PictureNineSliceMode edgeMode = PictureNineSliceMode::Stretch;
    PictureNineSliceMode centerMode = PictureNineSliceMode::Stretch;

    QVariantMap toParams() const;
    static PictureNineSlice fromParams(const QVariantMap& p);
    bool validFor(const QSize& source) const;
};

// -------------------------------------------------------------- camada
/// Em que altura da pilha de desenho a picture entra.
enum class PictureLayer {
    AboveParallax = 0,      ///< 0: acima do panorama/parallax
    BelowTiles = 1,         ///< 1: acima dos tiles inferiores
    BelowEvents = 2,        ///< 2: acima dos eventos abaixo do jogador
    SameAsPlayer = 3,       ///< 3: acima dos eventos no mesmo nível do jogador
    AboveTiles = 4,         ///< 4: acima dos tiles acima do jogador
    AboveEvents = 5,        ///< 5: acima dos eventos acima do jogador
    AboveWeather = 6,       ///< 6: acima dos efeitos de clima
    AboveAnimations = 7,    ///< 7: acima das animações
    AboveMessage = 8,       ///< 8: acima da janela de mensagem
    AboveTimers = 9,        ///< 9: acima dos timers / prioridade máxima

    // aliases antigos para compatibilidade com código e projetos existentes
    BelowMessage = AboveWeather,
    AboveAll = AboveTimers
};

QString      pictureLayerId(PictureLayer l);
PictureLayer pictureLayerFromId(const QString& id);
QString      pictureLayerLabel(PictureLayer l);

// ---------------------------------------------------------- interpolação
/// Curvas de animação do `TweenPicture`.
enum class PictureEase { Linear, EaseIn, EaseOut, EaseInOut, Bounce, Elastic };

QString     pictureEaseId(PictureEase e);
PictureEase pictureEaseFromId(const QString& id);
QString     pictureEaseLabel(PictureEase e);
/// Aplica a curva a um avanço 0..1. Fora desse intervalo o valor é grampeado —
/// um tween de duração zero termina no destino, nunca em NaN.
double      applyEase(PictureEase e, double t);

/// Qual propriedade um tween anima.
enum class PictureProp { X, Y, ScaleX, ScaleY, Opacity, Angle };

QString     picturePropId(PictureProp p);
PictureProp picturePropFromId(const QString& id);
QString     picturePropLabel(PictureProp p);

// ============================================================================
//  Texto como imagem (porte do ShowRichTextPicture)
// ============================================================================

/// Fundo da imagem de texto.
enum class PictureTextBg {
    None,      ///< transparente
    Solid,     ///< cor sólida
    Gradient,  ///< duas cores, de cima para baixo
    Frosted,   ///< vidro fosco (clareia e ilumina a borda)
    Blur,      ///< painel escuro difuso
    TextBlur,  ///< borrão só atrás das letras
    Window     ///< moldura 9-slice da biblioteca
};

QString       pictureTextBgId(PictureTextBg b);
PictureTextBg pictureTextBgFromId(const QString& id);
QString       pictureTextBgLabel(PictureTextBg b);

/// Alinhamento do texto dentro da imagem.
enum class PictureTextAlign { Left, Center, Right };
QString          pictureTextAlignId(PictureTextAlign a);
PictureTextAlign pictureTextAlignFromId(const QString& id);
QString          pictureTextAlignLabel(PictureTextAlign a);

/// Texto que vira imagem. Quando `enabled`, a picture não usa a biblioteca:
/// ela é DESENHADA a partir deste texto — e continua sendo uma picture, com
/// escala, rotação, efeitos e transições.
struct PictureRichText {
    bool    enabled = false;
    QString text;

    QString fontFamily;                 ///< vazio = fonte padrão do jogo
    int     fontSize = 24;
    bool    bold = false, italic = false;
    QColor  color = QColor(255, 255, 255);
    QColor  gradient2;                  ///< legado: segunda cor vertical
    TextGradientSpec gradient;          ///< RC2.55: multi-cor + direção
    TextEffectStack effects;            ///< RC2.55: entrada/loop/saída compartilhados
    QColor  outlineColor = QColor(0, 0, 0);
    int     outlineWidth = 4;
    bool    shadow = false;
    QColor  shadowColor = QColor(0, 0, 0, 128);
    int     shadowOffsetX = 2, shadowOffsetY = 2;

    bool    wordWrap = true;
    bool    autoSize = true;            ///< a imagem se ajusta ao texto
    int     width = 400, height = 120;  ///< usados quando autoSize = false
    int     padding = 12;
    PictureTextAlign align = PictureTextAlign::Left;

    PictureTextBg bg = PictureTextBg::None;
    QColor  bgColor = QColor(0, 0, 0, 128);
    QColor  bgGradient2 = QColor(146, 141, 171);
    int     bgRadius = 10;
    double  bgAlpha = 1.0;
    QString bgBorderImageId;            ///< moldura 9-slice (bg == Window)
    int     bgBorderSlice = 8;
    double  bgBorderScale = 1.0;

    QVariantMap toParams() const;
    static PictureRichText fromParams(const QVariantMap& p);
    /// Assinatura do que muda a IMAGEM (sem o tempo): é a chave do cache.
    QString cacheKey() const;
};

// Componentes lógicos da Picture. As definições ficam em PictureState.h para
// manter o formato legado de PictureDef sem obrigar uma migração de projeto.
struct PictureTransformState;
struct PictureDisplayState;
struct PictureAnimationState;
struct PicturePhysicsState;
struct PictureVisualState;

// ------------------------------------------------------------ definição
/// O estado visual completo de uma picture: é o que o comando "mostrar
/// imagem" grava e o que o editor deixa arrastar na tela de pré-visualização.
struct PictureDef {
    int     number = 1;             ///< slot (1..100); maior número = mais na frente
    QString assetId;                ///< imagem da biblioteca do projeto
    QString assetName;              ///< nome, usado quando o id não for achado

    double  x = 0.0, y = 0.0;       ///< posição, no espaço escolhido
    double  scaleX = 100.0;         ///< em porcentagem
    double  scaleY = 100.0;
    double  opacity = 255.0;        ///< 0..255
    double  angle = 0.0;            ///< graus, horário

    PictureAnchor anchor = PictureAnchor::TopLeft;
    double  anchorX = 0.0, anchorY = 0.0;   ///< usados quando anchor == Custom
    PictureBlend  blend = PictureBlend::Normal;
    PictureSpace  space = PictureSpace::Screen;
    PictureLayer  layer = PictureLayer::BelowMessage;
    bool    smooth = false;         ///< suavizar ao escalar (pixel art: não)
    bool    flipH = false;          ///< espelhar horizontalmente
    bool    flipV = false;          ///< espelhar verticalmente
    bool    duringBattle = true;    ///< desenhar também em batalhas
    bool    eraseOnMapChange = false; ///< apagar automaticamente ao trocar de mapa
    bool    affectedByTone = false; ///< reservado para a tonalidade global da tela

    // ---- spritesheet / imagem em sequência ------------------------------
    bool    animated = false;
    int     frameCount = 1;
    int     frameColumns = 1;
    int     frameRows = 1;
    int     frameIndex = 0;         ///< frame inicial/fixo (0-based)
    double  frameFps = 12.0;
    bool    frameLoop = true;
    bool    framePlaying = true;

    // ---- física (SetPhysicsAnimation) ------------------------------------
    // Movimentos contínuos que não precisam de comando por quadro: um ícone
    // que flutua, uma placa que balança, uma engrenagem que gira.
    double  floatSpeed = 0.0;       ///< ciclos por segundo (0 = desligado)
    double  floatRange = 12.0;      ///< amplitude vertical, em pixels
    double  swaySpeed = 0.0;
    double  swayRange = 10.0;       ///< amplitude horizontal, em pixels
    double  spinSpeed = 0.0;        ///< graus por segundo
    double  pulseSpeed = 0.0;
    double  pulseRange = 8.0;       ///< amplitude da escala, em pontos de %

    /// Layout 9-slice da própria Picture (não confundir com borda 9-slice).
    PictureNineSlice nineSlice;

    /// Efeitos desenhados por cima/por baixo da imagem.
    VisualEffects fx;
    /// Quando ligado, a imagem é o TEXTO desenhado (ShowRichTextPicture).
    PictureRichText rich;

    bool hasPhysics() const {
        return floatSpeed != 0.0 || swaySpeed != 0.0 || spinSpeed != 0.0 || pulseSpeed != 0.0;
    }

    FrameSequence frameSequence() const;
    void setFrameSequence(const FrameSequence& sequence);

    // Visões estruturadas usadas por editor/preview. Os campos públicos
    // continuam existindo para compatibilidade incremental com o runtime.
    PictureTransformState transformState() const;
    void setTransformState(const PictureTransformState& state);
    PictureDisplayState displayState() const;
    void setDisplayState(const PictureDisplayState& state);
    PictureAnimationState animationState() const;
    void setAnimationState(const PictureAnimationState& state);
    PicturePhysicsState physicsState() const;
    void setPhysicsState(const PicturePhysicsState& state);
    PictureVisualState visualState() const;
    void setVisualState(const PictureVisualState& state);

    /// Ida e volta para os parâmetros de um `EventCommand` (que já são
    /// gravados no projeto pelo ProjectIO — nada de formato novo).
    QVariantMap toParams() const;
    static PictureDef fromParams(const QVariantMap& p);
};

} // namespace core
