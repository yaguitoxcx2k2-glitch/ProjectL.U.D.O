// ============================================================================
// RuntimeScreenEffects.h — efeitos globais de Screen Space do runtime.
//
// Bloco 6 / RC2.14: Flash, Fade e Shake deixam de morar implicitamente em
// renderizadores. O estado temporal é compartilhado por CPU/QRhi; o renderer
// apenas consome cor/opacidade e o offset de projeção resultantes.
// ============================================================================
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>

namespace game {

struct RuntimeScreenOverlay {
    QColor flashColor = Qt::transparent;
    QColor fadeColor = Qt::transparent;

    bool active() const
    { return flashColor.alpha() > 0 || fadeColor.alpha() > 0; }
};

class RuntimeScreenEffectsState
{
public:
    void clear();

    /// Flash começa no alpha informado e desaparece até zero.
    void startFlash(const QColor& color, int alpha, double durationSeconds);
    /// Fade interpola entre duas opacidades da mesma cor.
    void startFade(const QColor& color, int fromAlpha, int toAlpha, double durationSeconds);
    /// Shake desloca SOMENTE World Space em pixels lógicos de tela.
    void startShake(double strengthX, double strengthY, double durationSeconds,
                    double frequencyHz = 12.0);

    /// Avança todos os relógios. reduceFlash/reduceShake vêm da preferência de
    /// acessibilidade do jogador e não alteram a duração lógica do comando.
    bool update(double dt, bool reduceFlash, bool reduceShake);

    bool flashActive() const { return m_flashActive; }
    bool fadeActive() const { return m_fadeActive; }
    bool shakeActive() const { return m_shakeActive; }
    bool anyActive() const { return m_flashActive || m_fadeActive || m_shakeActive; }

    RuntimeScreenOverlay overlay(bool reduceFlash) const;
    QPointF shakeOffset(bool reduceShake) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

private:
    QColor m_flashColor = QColor(Qt::white);
    int m_flashAlpha = 0;
    int m_flashStartAlpha = 0;
    double m_flashElapsed = 0.0;
    double m_flashDuration = 0.0;
    bool m_flashActive = false;

    QColor m_fadeColor = QColor(Qt::black);
    int m_fadeAlpha = 0;
    int m_fadeFromAlpha = 0;
    int m_fadeToAlpha = 0;
    double m_fadeElapsed = 0.0;
    double m_fadeDuration = 0.0;
    bool m_fadeActive = false;

    double m_shakeX = 0.0;
    double m_shakeY = 0.0;
    double m_shakeElapsed = 0.0;
    double m_shakeDuration = 0.0;
    double m_shakeFrequency = 12.0;
    bool m_shakeActive = false;
};

} // namespace game
