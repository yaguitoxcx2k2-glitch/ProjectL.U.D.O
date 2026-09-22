#pragma once

#include "core/Database.h"

#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace game {

/// Um clip de animação de batalha é puramente declarativo. O editor grava os
/// cues no Banco de Dados e CPU/QRhi consomem os mesmos dados em runtime.
struct BattleAnimationCue
{
    QString type = QStringLiteral("picture"); // picture, particle, se, flash, shake
    int timeMs = 0;
    int durationMs = 250;
    QString assetPath;
    int volume = 90;

    // Picture / spritesheet.
    int frames = 1;
    int columns = 1;
    int rows = 1;
    bool loop = false;
    QSizeF size = QSizeF(128.0, 128.0);
    QPointF offset;
    qreal opacity = 1.0;
    qreal scale = 1.0;
    qreal rotation = 0.0;

    // Particle / flash / shake.
    QColor color = Qt::white;
    int particleCount = 16;
    qreal speed = 80.0;
    qreal spreadDegrees = 360.0;
    qreal strength = 6.0;
    QString particleShape = QStringLiteral("circle");

    QVariantMap toVariant() const;
    static BattleAnimationCue fromVariant(const QVariantMap& map);
};

struct BattleAnimationDefinition
{
    int durationMs = 500;
    int hitFrameMs = 250;
    QVector<BattleAnimationCue> cues;

    QVariantMap toData() const;
    static BattleAnimationDefinition fromData(const QVariantMap& data);
    static BattleAnimationDefinition fromRecord(const core::DatabaseRecord* record);
};

struct BattleSoundCue
{
    QString path;
    int volume = 90;
};

/// Retorna o quadro 0-based de uma Picture/Spritesheet para o tempo do cue.
int battleAnimationFrame(const BattleAnimationCue& cue, int cueElapsedMs);

} // namespace game
