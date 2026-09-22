#include "ScreenTone.h"

#include <QtGlobal>
#include <cmath>

namespace game {

void ScreenToneState::start(int red, int green, int blue, int gray,
                            double durationSeconds)
{
    m_fromR = m_r;
    m_fromG = m_g;
    m_fromB = m_b;
    m_fromGray = m_gray;

    m_toR = qBound(-255, red, 255);
    m_toG = qBound(-255, green, 255);
    m_toB = qBound(-255, blue, 255);
    m_toGray = qBound(0, gray, 255);

    m_elapsed = 0.0;
    m_duration = qMax(0.0, durationSeconds);
    m_active = m_duration > 0.0;

    if (!m_active) {
        m_r = m_toR;
        m_g = m_toG;
        m_b = m_toB;
        m_gray = m_toGray;
    }
}

bool ScreenToneState::update(double dt)
{
    if (!m_active) return false;

    const int beforeR = m_r;
    const int beforeG = m_g;
    const int beforeB = m_b;
    const int beforeGray = m_gray;

    m_elapsed += qMax(0.0, dt);
    double t = m_duration <= 0.0 ? 1.0 : qBound(0.0, m_elapsed / m_duration, 1.0);
    // Smoothstep: preserva exatamente a curva usada originalmente em GameSession.
    t = t * t * (3.0 - 2.0 * t);
    const auto lerp = [t](int a, int b) {
        return int(std::lround(a + (b - a) * t));
    };

    m_r = lerp(m_fromR, m_toR);
    m_g = lerp(m_fromG, m_toG);
    m_b = lerp(m_fromB, m_toB);
    m_gray = lerp(m_fromGray, m_toGray);

    if (m_elapsed >= m_duration) {
        m_active = false;
        m_r = m_toR;
        m_g = m_toG;
        m_b = m_toB;
        m_gray = m_toGray;
    }

    return beforeR != m_r || beforeG != m_g || beforeB != m_b ||
           beforeGray != m_gray || !m_active;
}

QJsonObject ScreenToneState::toJson() const
{
    return {
        {QStringLiteral("r"), m_r},
        {QStringLiteral("g"), m_g},
        {QStringLiteral("b"), m_b},
        {QStringLiteral("gray"), m_gray},
        {QStringLiteral("targetR"), m_toR},
        {QStringLiteral("targetG"), m_toG},
        {QStringLiteral("targetB"), m_toB},
        {QStringLiteral("targetGray"), m_toGray},
        {QStringLiteral("elapsed"), m_elapsed},
        {QStringLiteral("duration"), m_duration},
        {QStringLiteral("active"), m_active}
    };
}

void ScreenToneState::fromJson(const QJsonObject& json)
{
    if (json.isEmpty()) return;

    m_r = qBound(-255, json.value(QStringLiteral("r")).toInt(0), 255);
    m_g = qBound(-255, json.value(QStringLiteral("g")).toInt(0), 255);
    m_b = qBound(-255, json.value(QStringLiteral("b")).toInt(0), 255);
    m_gray = qBound(0, json.value(QStringLiteral("gray")).toInt(0), 255);

    m_toR = qBound(-255, json.value(QStringLiteral("targetR")).toInt(m_r), 255);
    m_toG = qBound(-255, json.value(QStringLiteral("targetG")).toInt(m_g), 255);
    m_toB = qBound(-255, json.value(QStringLiteral("targetB")).toInt(m_b), 255);
    m_toGray = qBound(0, json.value(QStringLiteral("targetGray")).toInt(m_gray), 255);

    // O formato legado não gravava fromR/fromG/fromB/fromGray. Para retomar
    // sem salto, o valor atualmente visível vira a nova origem e usamos só
    // o TEMPO RESTANTE da transição. Assim o primeiro tick após carregar não
    // reaplica metade da curva sobre uma nova origem.
    m_fromR = m_r;
    m_fromG = m_g;
    m_fromB = m_b;
    m_fromGray = m_gray;
    const double savedElapsed = qMax(0.0, json.value(QStringLiteral("elapsed")).toDouble(0.0));
    const double savedDuration = qMax(0.0, json.value(QStringLiteral("duration")).toDouble(0.0));
    const bool savedActive = json.value(QStringLiteral("active")).toBool(false);
    m_elapsed = 0.0;
    m_duration = qMax(0.0, savedDuration - savedElapsed);
    m_active = savedActive && m_duration > 0.0;
    if (!m_active && savedActive) {
        m_r = m_toR;
        m_g = m_toG;
        m_b = m_toB;
        m_gray = m_toGray;
    }
}

} // namespace game
