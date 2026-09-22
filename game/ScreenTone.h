// ============================================================================
//  ScreenTone.h — Estado e interpolação da tonalidade global da tela.
//
//  Este módulo não desenha nada. Ele mantém a regra temporal da tonalidade em
//  um único lugar para que GameSession, save/load, CPU e GPU consumam o mesmo
//  estado. A transformação dos pixels continua em core::applyScreenTone().
// ============================================================================
#pragma once

#include <QJsonObject>

namespace game {

class ScreenToneState
{
public:
    int red() const { return m_r; }
    int green() const { return m_g; }
    int blue() const { return m_b; }
    int gray() const { return m_gray; }

    bool hasTone() const { return m_r != 0 || m_g != 0 || m_b != 0 || m_gray != 0; }
    bool isTransitioning() const { return m_active; }

    /// Inicia uma transição. Valores são grampeados aos limites do comando.
    void start(int red, int green, int blue, int gray, double durationSeconds);
    void clear(double durationSeconds) { start(0, 0, 0, 0, durationSeconds); }

    /// Avança a interpolação. Retorna true quando o valor visível mudou.
    bool update(double dt);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

private:
    int m_r = 0, m_g = 0, m_b = 0, m_gray = 0;
    int m_fromR = 0, m_fromG = 0, m_fromB = 0, m_fromGray = 0;
    int m_toR = 0, m_toG = 0, m_toB = 0, m_toGray = 0;
    double m_elapsed = 0.0;
    double m_duration = 0.0;
    bool m_active = false;
};

} // namespace game
