#include "RuntimeScreenEffects.h"

#include <QtGlobal>
#include <cmath>

namespace game {
namespace {

int finiteAlpha(int value)
{
    return qBound(0, value, 255);
}

double finiteNonNegative(double value)
{
    return std::isfinite(value) ? qMax(0.0, value) : 0.0;
}

double finiteFrequency(double value)
{
    if (!std::isfinite(value) || value <= 0.0) return 12.0;
    return qBound(0.5, value, 60.0);
}

int lerpAlpha(int a, int b, double t)
{
    return finiteAlpha(int(std::lround(a + (b - a) * qBound(0.0, t, 1.0))));
}

QColor rgbOnly(const QColor& color, const QColor& fallback)
{
    const QColor c = color.isValid() ? color : fallback;
    return QColor(c.red(), c.green(), c.blue(), 255);
}

} // namespace

void RuntimeScreenEffectsState::clear()
{
    m_flashAlpha = m_flashStartAlpha = 0;
    m_flashElapsed = m_flashDuration = 0.0;
    m_flashActive = false;

    m_fadeAlpha = m_fadeFromAlpha = m_fadeToAlpha = 0;
    m_fadeElapsed = m_fadeDuration = 0.0;
    m_fadeActive = false;

    m_shakeX = m_shakeY = 0.0;
    m_shakeElapsed = m_shakeDuration = 0.0;
    m_shakeFrequency = 12.0;
    m_shakeActive = false;
}

void RuntimeScreenEffectsState::startFlash(const QColor& color, int alpha,
                                           double durationSeconds)
{
    m_flashColor = rgbOnly(color, QColor(Qt::white));
    m_flashStartAlpha = finiteAlpha(alpha);
    m_flashAlpha = m_flashStartAlpha;
    m_flashElapsed = 0.0;
    m_flashDuration = finiteNonNegative(durationSeconds);
    m_flashActive = m_flashStartAlpha > 0 && m_flashDuration > 0.0;
    if (!m_flashActive && m_flashDuration <= 0.0) m_flashAlpha = 0;
}

void RuntimeScreenEffectsState::startFade(const QColor& color, int fromAlpha,
                                          int toAlpha, double durationSeconds)
{
    m_fadeColor = rgbOnly(color, QColor(Qt::black));
    m_fadeFromAlpha = finiteAlpha(fromAlpha);
    m_fadeToAlpha = finiteAlpha(toAlpha);
    m_fadeAlpha = m_fadeFromAlpha;
    m_fadeElapsed = 0.0;
    m_fadeDuration = finiteNonNegative(durationSeconds);
    m_fadeActive = m_fadeDuration > 0.0 && m_fadeFromAlpha != m_fadeToAlpha;
    if (!m_fadeActive) m_fadeAlpha = m_fadeToAlpha;
}

void RuntimeScreenEffectsState::startShake(double strengthX, double strengthY,
                                           double durationSeconds, double frequencyHz)
{
    m_shakeX = finiteNonNegative(strengthX);
    m_shakeY = finiteNonNegative(strengthY);
    m_shakeElapsed = 0.0;
    m_shakeDuration = finiteNonNegative(durationSeconds);
    m_shakeFrequency = finiteFrequency(frequencyHz);
    m_shakeActive = m_shakeDuration > 0.0 && (m_shakeX > 0.0 || m_shakeY > 0.0);
}

bool RuntimeScreenEffectsState::update(double dt, bool, bool)
{
    dt = finiteNonNegative(dt);
    bool changed = false;

    if (m_flashActive) {
        const int before = m_flashAlpha;
        m_flashElapsed = qMin(m_flashDuration, m_flashElapsed + dt);
        const double t = m_flashDuration <= 0.0 ? 1.0 : m_flashElapsed / m_flashDuration;
        m_flashAlpha = lerpAlpha(m_flashStartAlpha, 0, t);
        if (m_flashElapsed >= m_flashDuration) {
            m_flashActive = false;
            m_flashAlpha = 0;
        }
        changed = changed || before != m_flashAlpha || !m_flashActive;
    }

    if (m_fadeActive) {
        const int before = m_fadeAlpha;
        m_fadeElapsed = qMin(m_fadeDuration, m_fadeElapsed + dt);
        const double t = m_fadeDuration <= 0.0 ? 1.0 : m_fadeElapsed / m_fadeDuration;
        // Smoothstep mantém o comportamento visual alinhado à câmera/tone.
        const double eased = t * t * (3.0 - 2.0 * t);
        m_fadeAlpha = lerpAlpha(m_fadeFromAlpha, m_fadeToAlpha, eased);
        if (m_fadeElapsed >= m_fadeDuration) {
            m_fadeActive = false;
            m_fadeAlpha = m_fadeToAlpha;
        }
        changed = changed || before != m_fadeAlpha || !m_fadeActive;
    }

    if (m_shakeActive) {
        m_shakeElapsed = qMin(m_shakeDuration, m_shakeElapsed + dt);
        if (m_shakeElapsed >= m_shakeDuration) m_shakeActive = false;
        changed = true; // fase muda continuamente mesmo com mesma amplitude inteira
    }

    return changed;
}

RuntimeScreenOverlay RuntimeScreenEffectsState::overlay(bool reduceFlash) const
{
    RuntimeScreenOverlay out;
    int flashAlpha = finiteAlpha(m_flashAlpha);
    if (reduceFlash) flashAlpha = qMin(flashAlpha, 64);
    if (flashAlpha > 0) {
        out.flashColor = m_flashColor;
        out.flashColor.setAlpha(flashAlpha);
    }
    if (m_fadeAlpha > 0) {
        out.fadeColor = m_fadeColor;
        out.fadeColor.setAlpha(finiteAlpha(m_fadeAlpha));
    }
    return out;
}

QPointF RuntimeScreenEffectsState::shakeOffset(bool reduceShake) const
{
    if (!m_shakeActive || reduceShake || m_shakeDuration <= 0.0) return {};
    const double remaining = qBound(0.0, 1.0 - m_shakeElapsed / m_shakeDuration, 1.0);
    const double phase = m_shakeElapsed * m_shakeFrequency * 2.0 * 3.14159265358979323846;
    // Frequências levemente distintas evitam o movimento perfeitamente diagonal.
    return QPointF(std::sin(phase) * m_shakeX * remaining,
                   std::sin(phase * 1.173 + 0.47) * m_shakeY * remaining);
}

QJsonObject RuntimeScreenEffectsState::toJson() const
{
    return QJsonObject{
        {QStringLiteral("flash"), QJsonObject{
            {QStringLiteral("r"), m_flashColor.red()}, {QStringLiteral("g"), m_flashColor.green()},
            {QStringLiteral("b"), m_flashColor.blue()}, {QStringLiteral("alpha"), m_flashAlpha},
            {QStringLiteral("startAlpha"), m_flashStartAlpha}, {QStringLiteral("elapsed"), m_flashElapsed},
            {QStringLiteral("duration"), m_flashDuration}, {QStringLiteral("active"), m_flashActive}}},
        {QStringLiteral("fade"), QJsonObject{
            {QStringLiteral("r"), m_fadeColor.red()}, {QStringLiteral("g"), m_fadeColor.green()},
            {QStringLiteral("b"), m_fadeColor.blue()}, {QStringLiteral("alpha"), m_fadeAlpha},
            {QStringLiteral("fromAlpha"), m_fadeFromAlpha}, {QStringLiteral("toAlpha"), m_fadeToAlpha},
            {QStringLiteral("elapsed"), m_fadeElapsed}, {QStringLiteral("duration"), m_fadeDuration},
            {QStringLiteral("active"), m_fadeActive}}},
        {QStringLiteral("shake"), QJsonObject{
            {QStringLiteral("x"), m_shakeX}, {QStringLiteral("y"), m_shakeY},
            {QStringLiteral("elapsed"), m_shakeElapsed}, {QStringLiteral("duration"), m_shakeDuration},
            {QStringLiteral("frequency"), m_shakeFrequency}, {QStringLiteral("active"), m_shakeActive}}}
    };
}

void RuntimeScreenEffectsState::fromJson(const QJsonObject& json)
{
    if (json.isEmpty()) return;
    clear();

    const QJsonObject flash = json.value(QStringLiteral("flash")).toObject();
    if (!flash.isEmpty()) {
        m_flashColor = QColor(qBound(0, flash.value(QStringLiteral("r")).toInt(255), 255),
                              qBound(0, flash.value(QStringLiteral("g")).toInt(255), 255),
                              qBound(0, flash.value(QStringLiteral("b")).toInt(255), 255));
        m_flashAlpha = finiteAlpha(flash.value(QStringLiteral("alpha")).toInt());
        m_flashStartAlpha = finiteAlpha(flash.value(QStringLiteral("startAlpha")).toInt(m_flashAlpha));
        const double savedElapsed = finiteNonNegative(flash.value(QStringLiteral("elapsed")).toDouble());
        const double savedDuration = finiteNonNegative(flash.value(QStringLiteral("duration")).toDouble());
        const bool savedActive = flash.value(QStringLiteral("active")).toBool(false);
        // Preserva o relógio original: o próximo tick continua exatamente na
        // mesma curva e a imagem não muda no frame imediatamente após o load.
        m_flashElapsed = qMin(savedElapsed, savedDuration);
        m_flashDuration = savedDuration;
        m_flashActive = savedActive && m_flashAlpha > 0 && m_flashElapsed < m_flashDuration;
    }

    const QJsonObject fade = json.value(QStringLiteral("fade")).toObject();
    if (!fade.isEmpty()) {
        m_fadeColor = QColor(qBound(0, fade.value(QStringLiteral("r")).toInt(0), 255),
                             qBound(0, fade.value(QStringLiteral("g")).toInt(0), 255),
                             qBound(0, fade.value(QStringLiteral("b")).toInt(0), 255));
        m_fadeAlpha = finiteAlpha(fade.value(QStringLiteral("alpha")).toInt());
        m_fadeFromAlpha = finiteAlpha(fade.value(QStringLiteral("fromAlpha")).toInt(m_fadeAlpha));
        m_fadeToAlpha = finiteAlpha(fade.value(QStringLiteral("toAlpha")).toInt(m_fadeAlpha));
        const double savedElapsed = finiteNonNegative(fade.value(QStringLiteral("elapsed")).toDouble());
        const double savedDuration = finiteNonNegative(fade.value(QStringLiteral("duration")).toDouble());
        const bool savedActive = fade.value(QStringLiteral("active")).toBool(false);
        m_fadeElapsed = qMin(savedElapsed, savedDuration);
        m_fadeDuration = savedDuration;
        m_fadeActive = savedActive && m_fadeFromAlpha != m_fadeToAlpha && m_fadeElapsed < m_fadeDuration;
        if (!m_fadeActive && savedActive && m_fadeElapsed >= m_fadeDuration) m_fadeAlpha = m_fadeToAlpha;
    }

    const QJsonObject shake = json.value(QStringLiteral("shake")).toObject();
    if (!shake.isEmpty()) {
        m_shakeX = finiteNonNegative(shake.value(QStringLiteral("x")).toDouble());
        m_shakeY = finiteNonNegative(shake.value(QStringLiteral("y")).toDouble());
        const double savedElapsed = finiteNonNegative(shake.value(QStringLiteral("elapsed")).toDouble());
        const double savedDuration = finiteNonNegative(shake.value(QStringLiteral("duration")).toDouble());
        m_shakeFrequency = finiteFrequency(shake.value(QStringLiteral("frequency")).toDouble(12.0));
        const bool savedActive = shake.value(QStringLiteral("active")).toBool(false);
        m_shakeElapsed = qMin(savedElapsed, savedDuration);
        m_shakeDuration = savedDuration;
        m_shakeActive = savedActive && m_shakeElapsed < m_shakeDuration &&
                        (m_shakeX > 0.0 || m_shakeY > 0.0);
    }
}

} // namespace game
