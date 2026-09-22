#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace core {

struct SpeakerProfile {
    QString id, name;
    QColor nameColor;
    QString portrait, font;
    QColor textColor;
    QString voicePrefix;
    QHash<QString, QString> expressionPortraits;
    QString defaultExpression;
    QString portraitPosition=QStringLiteral("left");
    QVariantMap subtitleStyle, metadata;
    QString portraitForExpression(const QString& expression) const;
    QVariantMap toVariantMap() const;
    static SpeakerProfile fromVariantMap(const QVariantMap& value);
};

struct DialogueContent {
    QString speakerId, text, expression, voiceFile, textEffectPreset;
    QVariantMap toVariantMap() const;
    static DialogueContent fromVariantMap(const QVariantMap& value);
};

struct SpeakerDatabase {
    QVector<SpeakerProfile> speakers;
    const SpeakerProfile* findById(const QString& id) const;
    const SpeakerProfile* findByName(const QString& name) const;
    bool upsert(SpeakerProfile profile);
    void normalize();
    QJsonObject toJson() const;
    static SpeakerDatabase fromJson(const QJsonObject& object);
};

} // namespace core
