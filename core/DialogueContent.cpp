#include "DialogueContent.h"
#include "Model.h"

#include <QJsonArray>
#include <QSet>
#include <utility>

namespace core {
namespace { QString cleanId(QString v) { return v.trimmed().left(128); } }

QVariantMap SpeakerProfile::toVariantMap() const
{
    QVariantMap expressions;for(auto it=expressionPortraits.cbegin();it!=expressionPortraits.cend();++it)expressions.insert(it.key(),it.value());
    QVariantMap out{{"id",id},{"name",name},{"portrait",portrait},{"font",font},
                    {"voicePrefix",voicePrefix},{"expressionPortraits",expressions},
                    {"defaultExpression",defaultExpression},{"portraitPosition",portraitPosition},
                    {"subtitleStyle",subtitleStyle},{"metadata",metadata}};
    if(nameColor.isValid()) out["nameColor"]=nameColor.name(QColor::HexArgb);
    if(textColor.isValid()) out["textColor"]=textColor.name(QColor::HexArgb);
    return out;
}
QString SpeakerProfile::portraitForExpression(const QString& requested) const
{
    QString expression=requested.trimmed();if(expression.isEmpty())expression=defaultExpression;
    const auto exact=expressionPortraits.constFind(expression);if(exact!=expressionPortraits.cend()&&!exact.value().isEmpty())return exact.value();
    for(auto it=expressionPortraits.cbegin();it!=expressionPortraits.cend();++it)if(it.key().compare(expression,Qt::CaseInsensitive)==0&&!it.value().isEmpty())return it.value();
    return portrait;
}
SpeakerProfile SpeakerProfile::fromVariantMap(const QVariantMap& v)
{
    SpeakerProfile out; out.id=cleanId(v.value("id").toString());out.name=v.value("name").toString().trimmed().left(256);
    out.nameColor=QColor(v.value("nameColor").toString());out.portrait=v.value("portrait").toString().trimmed().left(1024);const QVariantMap expressions=v.value("expressionPortraits").toMap();for(auto it=expressions.cbegin();it!=expressions.cend();++it){const QString key=it.key().trimmed().left(128),path=it.value().toString().trimmed().left(1024);if(!key.isEmpty()&&!path.isEmpty())out.expressionPortraits.insert(key,path);}out.defaultExpression=v.value("defaultExpression").toString().trimmed().left(128);out.portraitPosition=v.value("portraitPosition",QStringLiteral("left")).toString().trimmed().toLower();if(out.portraitPosition!="right")out.portraitPosition="left";
    out.font=v.value("font").toString().trimmed().left(256);out.textColor=QColor(v.value("textColor").toString());
    out.voicePrefix=v.value("voicePrefix").toString().trimmed().left(1024);out.subtitleStyle=v.value("subtitleStyle").toMap();out.metadata=v.value("metadata").toMap();return out;
}
QVariantMap DialogueContent::toVariantMap() const
{ return {{"speakerId",speakerId},{"text",text},{"expression",expression},{"voiceFile",voiceFile},{"textEffectPreset",textEffectPreset}}; }
DialogueContent DialogueContent::fromVariantMap(const QVariantMap& v)
{ DialogueContent out;out.speakerId=cleanId(v.value("speakerId").toString());out.text=v.value("text").toString().left(65535);out.expression=v.value("expression").toString().trimmed().left(128);out.voiceFile=v.value("voiceFile").toString().trimmed().left(1024);out.textEffectPreset=cleanId(v.value("textEffectPreset").toString());return out; }
const SpeakerProfile* SpeakerDatabase::findById(const QString& id) const
{ for(const auto& s:speakers)if(s.id.compare(id.trimmed(),Qt::CaseInsensitive)==0)return &s;return nullptr; }
const SpeakerProfile* SpeakerDatabase::findByName(const QString& name) const
{ for(const auto& s:speakers)if(s.name.compare(name.trimmed(),Qt::CaseInsensitive)==0)return &s;return nullptr; }
bool SpeakerDatabase::upsert(SpeakerProfile profile)
{ profile.id=cleanId(profile.id);if(profile.id.isEmpty())profile.id=idGen();if(profile.name.isEmpty())return false;for(auto& s:speakers)if(s.id.compare(profile.id,Qt::CaseInsensitive)==0){s=std::move(profile);return true;}speakers.push_back(std::move(profile));normalize();return true; }
void SpeakerDatabase::normalize()
{ QSet<QString> ids;QVector<SpeakerProfile> clean;for(auto s:speakers){s.id=cleanId(s.id);if(s.id.isEmpty())s.id=idGen();const QString key=s.id.toCaseFolded();if(s.name.trimmed().isEmpty()||ids.contains(key))continue;ids.insert(key);clean.push_back(std::move(s));}speakers=std::move(clean); }
QJsonObject SpeakerDatabase::toJson() const
{ QJsonArray a;for(const auto& s:speakers)a.append(QJsonObject::fromVariantMap(s.toVariantMap()));return {{"speakers",a}}; }
SpeakerDatabase SpeakerDatabase::fromJson(const QJsonObject& o)
{ SpeakerDatabase out;for(const auto& v:o.value("speakers").toArray())if(v.isObject())out.speakers.push_back(SpeakerProfile::fromVariantMap(v.toObject().toVariantMap()));out.normalize();return out; }
} // namespace core
