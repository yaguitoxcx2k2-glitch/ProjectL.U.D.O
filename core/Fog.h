// ============================================================================
// Fog.h — Propriedades visuais/sonoras por mapa e definições de névoa.
// ============================================================================
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include "Picture.h"
#include "Weather.h"

namespace core {

enum class FogBlend { Normal, Add, Multiply, Screen, Overlay };
QString fogBlendId(FogBlend b);
FogBlend fogBlendFromId(const QString& id);
QString fogBlendLabel(FogBlend b);

struct FogDef {
    int slot = 1;                         ///< 1..5
    bool enabled = true;
    QImage image;
    QString sourcePath;                   ///< relativo ao projeto
    FogBlend blend = FogBlend::Normal;
    int opacity = 180;                    ///< 0..255
    double scrollX = 0.0;                 ///< pixels por frame a 60 fps
    double scrollY = 0.0;
    double zoom = 1.0;
    bool tileRepeat = true;
    int fadeInFrames = 0;
};


/// Uma camada de panorama/parallax. Várias podem coexistir e cada uma possui
/// animação, scroll e os mesmos efeitos visuais das Pictures.
struct PanoramaDef {
    bool enabled = true;
    QString name;
    QString sourcePath;
    QImage image;
    bool loopX = false;
    bool loopY = false;
    double speedX = 0.0;
    double speedY = 0.0;
    // Quanto a camada acompanha a translação da câmera em World Space.
    // 0 = cancela a translação (ainda recebe zoom), 0.5 = metade, 1 = mapa.
    // Para Screen Space real, sem câmera/zoom, use fixed=true.
    double parallaxX = 1.0;
    double parallaxY = 1.0;
    bool fixed = false;
    bool showInEditor = true;
    int opacity = 255;
    PictureBlend blend = PictureBlend::Normal;
    bool flipH = false;
    bool flipV = false;

    bool animated = false;
    int frameCount = 1;
    int frameColumns = 1;
    int frameRows = 1;
    int frameIndex = 0;
    double frameFps = 12.0;
    bool frameLoop = true;
    bool framePlaying = true;

    FrameSequence frameSequence() const
    {
        FrameSequence seq;
        seq.enabled = animated;
        seq.count = qMax(1, frameCount);
        seq.columns = qMax(1, frameColumns);
        seq.rows = qMax(1, frameRows);
        seq.firstFrame = qBound(0, frameIndex, seq.count - 1);
        seq.fps = qMax(0.0, frameFps);
        seq.loop = frameLoop;
        seq.playing = framePlaying;
        return seq;
    }

    VisualEffects fx;
};

struct MapEnvironment {
    bool autoBgm = false;
    QString bgmPath;
    int bgmVolume = 90;                    ///< 0..100
    bool autoBgs = false;
    QString bgsPath;
    int bgsVolume = 90;                    ///< 0..100
    QString battleBackgroundPath;

    QString panoramaPath;
    QImage panorama;
    bool panoramaLoopX = false;
    bool panoramaLoopY = false;
    double panoramaSpeedX = 0.0;
    double panoramaSpeedY = 0.0;
    /// Fixo na tela: não acompanha a câmera nem o scroll do mapa.
    bool panoramaFixed = false;
    bool panoramaInEditor = true;

    /// Novo sistema: múltiplas camadas. Quando vazio, os campos legados acima
    /// continuam valendo para projetos antigos.
    QVector<PanoramaDef> panoramas;

    /// Estado climático único compartilhado pelo editor e runtime. O JSON do
    /// projeto continua usando os mesmos campos `type/intensity/thunder*`;
    /// somente a representação C++ deixa de ter um schema paralelo.
    WeatherState weather;

    QString note;
    QVector<FogDef> fogs;                 ///< no máximo cinco slots
};

} // namespace core
