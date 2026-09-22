// ============================================================================
// VisualEffects.h — efeitos visuais compartilhados por Pictures e Panoramas.
// ============================================================================
#pragma once

#include <QColor>
#include <QString>
#include <QVariantMap>

namespace core {

/// Como a borda é desenhada.
enum class PictureBorderStyle {
    Simple,     ///< um retângulo de linha em volta
    NineSlice   ///< uma imagem de moldura esticada em 9 pedaços
};

QString            pictureBorderStyleId(PictureBorderStyle s);
PictureBorderStyle pictureBorderStyleFromId(const QString& id);
QString            pictureBorderStyleLabel(PictureBorderStyle s);

struct PictureBorder {
    bool    enabled = false;
    double  width = 3.0;                     ///< espessura (estilo simples)
    QColor  color = QColor(255, 255, 255);
    double  alpha = 1.0;
    PictureBorderStyle style = PictureBorderStyle::Simple;
    QString imageAssetId;                    ///< moldura 9-slice (biblioteca)
    int     slice = 5;                       ///< tamanho do canto, na imagem
    double  scale = 4.0;                     ///< ampliação dos pedaços
    int     paddingX = 0, paddingY = 0;      ///< folga entre imagem e moldura
    QColor  tint = QColor(255, 255, 255);
    bool    tileH = false, tileV = false;    ///< repetir em vez de esticar
};

struct PictureGlow {
    bool   enabled = false;
    QColor color = QColor(255, 255, 0);
    double strength = 8.0;                   ///< raio do brilho, em pixels
    bool   blink = false;                    ///< pulsar o brilho
    double blinkSpeed = 2.0;                 ///< ciclos por segundo
    bool   borderOnly = false;                ///< usar a silhueta da borda/moldura como fonte do glow
};

struct PictureBlink {
    bool   enabled = false;
    bool   hard = false;                     ///< true = liga/desliga; false = suave
    double speed = 3.0;                      ///< 1..10, como no plugin
    double minAlpha = 0.0;                   ///< opacidade mínima (0..1)
    double delay = 0.0;                      ///< pausa entre as piscadas, em segundos
};

struct PictureDistort {
    bool   enabled = false;
    double amplitude = 8.0;                  ///< deslocamento máximo, em pixels
    double wavelength = 20.0;                ///< altura de uma onda, em pixels
    double speed = 1.0;                      ///< ciclos por segundo
};

struct PictureShine {
    bool   enabled = false;
    QColor color = QColor(255, 255, 255);
    double speed = 2.0;                      ///< passagens por segundo
    double width = 40.0;                     ///< largura da faixa, em pixels
    double delay = 0.0;                      ///< pausa entre passagens, em segundos
};

struct PictureMask {
    bool    enabled = false;
    QString assetId;                         ///< imagem que recorta (usa o alfa)
    bool    invert = false;                  ///< recortar POR FORA
    double  offsetX = 0.0;                   ///< deslocamento independente, em pixels
    double  offsetY = 0.0;
    double  scaleX = 100.0;                  ///< escala da máscara, em porcentagem
    double  scaleY = 100.0;
    double  angle = 0.0;                     ///< rotação em torno do centro
};

/// Tonalidade no mesmo contrato da Tela da LUDO: canais aditivos RGB e cinza.
struct PictureTone {
    bool enabled = false;
    int red = 0;
    int green = 0;
    int blue = 0;
    int gray = 0;
};

/// Colorização/tingimento aplicado na própria imagem antes dos demais efeitos.
/// Serve tanto para o comando Mostrar Imagem quanto para o comando externo
/// Efeitos da Imagem, como o "Tint Image" do editores de RPG/plugins.
enum class PictureTintMode { Normal, Multiply, Screen, Overlay, SoftLight, HardLight };
QString          pictureTintModeId(PictureTintMode m);
PictureTintMode  pictureTintModeFromId(const QString& id);
QString          pictureTintModeLabel(PictureTintMode m);

struct PictureTint {
    bool enabled = false;
    QColor color = QColor(255, 255, 255);
    double strength = 1.0;                   ///< 0..1
    PictureTintMode mode = PictureTintMode::Normal;
};

/// Inversão de cores ("Negative"). A força permite misturar o original com
/// o negativo sem criar um segundo sistema de color grading.
struct PictureNegative {
    bool enabled = false;
    double strength = 1.0;                   ///< 0..1
    int transitionFrames = 0;                ///< 0 = imediato; >0 interpola a intensidade no runtime
};

/// Transições de entrada e de saída (as 11 do SetTransitionOut, mais o
/// "quicar" que só existe na entrada).
enum class PictureTransition {
    None, Fade, SlideDown, SlideUp, SlideLeft, SlideRight,
    Zoom, ZoomIn, ZoomOut, Rotate, Flip, Elastic, Shake, Dissolve, Bounce
};

QString           pictureTransitionId(PictureTransition t);
PictureTransition pictureTransitionFromId(const QString& id);
QString           pictureTransitionLabel(PictureTransition t);

/// Efeitos visuais independentes do tipo de objeto.
/// Pictures e Panoramas usam exatamente a mesma estrutura e serialização.
struct VisualEffects {
    PictureBorder  border;
    PictureGlow    glow;
    PictureBlink   blink;
    PictureTone    tone;
    PictureTint    tint; // legado: apenas leitura/compatibilidade de projetos antigos
    PictureNegative negative;
    PictureDistort distort;
    PictureShine   shine;
    PictureMask    mask;

    PictureTransition transitionIn = PictureTransition::None;
    int    transitionInFrames = 30;
    PictureTransition transitionOut = PictureTransition::None;
    int    transitionOutFrames = 30;

    /// Algum efeito ligado? (sem isto, nem vale compor imagem nenhuma)
    bool any() const {
        return border.enabled || glow.enabled || blink.enabled || tone.enabled || tint.enabled || negative.enabled ||
               distort.enabled || shine.enabled || mask.enabled;
    }
    /// Algum efeito que muda a cada quadro? Decide se a imagem composta pode
    /// ficar em cache ou precisa ser refeita (e, na GPU, reenviada).
    bool animated() const {
        return (distort.enabled && distort.amplitude > 0.0) ||
               (shine.enabled && shine.width > 0.0) ||
               (glow.enabled && glow.blink);
    }
    /// Efeitos que só mexem na OPACIDADE não precisam recompor a imagem.
    bool animatesOpacity() const { return blink.enabled; }

    QVariantMap toParams() const;
    static VisualEffects fromParams(const QVariantMap& p);
    /// Assinatura dos efeitos ESTÁTICOS: duas pictures com a mesma assinatura
    /// podem compartilhar a imagem composta em cache.
    QString staticKey() const;
};


// Nome legado mantido para código/plugin que ainda fala em PictureEffects.
using PictureEffects = VisualEffects;

} // namespace core
