#include "GameData.h"

#include <QCoreApplication>
#include <QMetaType>

namespace core {

QString commonTriggerId(CommonTrigger t)
{
    switch (t) {
    case CommonTrigger::None:     return QStringLiteral("none");
    case CommonTrigger::Autorun:  return QStringLiteral("autorun");
    case CommonTrigger::Parallel: return QStringLiteral("parallel");
    }
    return QStringLiteral("none");
}

CommonTrigger commonTriggerFromId(const QString& id)
{
    if (id == QLatin1String("autorun"))  return CommonTrigger::Autorun;
    if (id == QLatin1String("parallel")) return CommonTrigger::Parallel;
    return CommonTrigger::None;
}

QString commonValueTypeId(CommonValueType t)
{
    switch (t) {
    case CommonValueType::Number:  return QStringLiteral("number");
    case CommonValueType::Boolean: return QStringLiteral("boolean");
    case CommonValueType::Text:    return QStringLiteral("text");
    }
    return QStringLiteral("number");
}

CommonValueType commonValueTypeFromId(const QString& id)
{
    if (id.compare(QLatin1String("boolean"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("bool"), Qt::CaseInsensitive) == 0)
        return CommonValueType::Boolean;
    // Compatibilidade defensiva com protótipos internos que gravavam
    // "string" antes da nomenclatura pública "text".
    if (id.compare(QLatin1String("text"), Qt::CaseInsensitive) == 0 ||
        id.compare(QLatin1String("string"), Qt::CaseInsensitive) == 0)
        return CommonValueType::Text;
    return CommonValueType::Number;
}

QString commonValueTypeLabel(CommonValueType t)
{
    switch (t) {
    case CommonValueType::Number:  return QCoreApplication::translate("GameData", "Número");
    case CommonValueType::Boolean: return QCoreApplication::translate("GameData", "Booleano (Ligado/Desligado)");
    case CommonValueType::Text:    return QCoreApplication::translate("GameData", "Texto");
    }
    return QString();
}

QVariant commonValueDefault(CommonValueType t)
{
    switch (t) {
    case CommonValueType::Number:  return 0;
    case CommonValueType::Boolean: return false;
    case CommonValueType::Text:    return QString();
    }
    return QVariant();
}

QVariant normalizeCommonValue(const QVariant& value, CommonValueType t)
{
    switch (t) {
    case CommonValueType::Number:
        return value.toInt();
    case CommonValueType::Boolean: {
        if (value.metaType().id() == QMetaType::QString) {
            const QString text = value.toString().trimmed().toCaseFolded();
            if (text.isEmpty() || text == QLatin1String("0") ||
                text == QLatin1String("false") || text == QLatin1String("off") ||
                text == QStringLiteral("não") || text == QLatin1String("nao") ||
                text == QLatin1String("no") || text == QLatin1String("desligado"))
                return false;
            if (text == QLatin1String("1") || text == QLatin1String("true") ||
                text == QLatin1String("on") || text == QLatin1String("sim") ||
                text == QLatin1String("yes") || text == QLatin1String("ligado"))
                return true;
            // Valor textual desconhecido não deve virar true só por ser não-vazio.
            return false;
        }
        return value.toBool();
    }
    case CommonValueType::Text:
        return value.toString();
    }
    return QVariant();
}

QString commonTriggerLabel(CommonTrigger t)
{
    switch (t) {
    case CommonTrigger::None:
        return QCoreApplication::translate("GameData", "Só quando chamado");
    case CommonTrigger::Autorun:
        return QCoreApplication::translate("GameData", "Automático (com interruptor)");
    case CommonTrigger::Parallel:
        return QCoreApplication::translate("GameData", "Paralelo (com interruptor)");
    }
    return QString();
}

QString commonSchedulePolicyId(CommonSchedulePolicy p)
{
    switch (p) {
    case CommonSchedulePolicy::WhileTrue: return QStringLiteral("whileTrue");
    case CommonSchedulePolicy::OnTrue: return QStringLiteral("onTrue");
    case CommonSchedulePolicy::Interval: return QStringLiteral("interval");
    }
    return QStringLiteral("whileTrue");
}

CommonSchedulePolicy commonSchedulePolicyFromId(const QString& id)
{
    if (id == QLatin1String("onTrue")) return CommonSchedulePolicy::OnTrue;
    if (id == QLatin1String("interval")) return CommonSchedulePolicy::Interval;
    return CommonSchedulePolicy::WhileTrue;
}

QString commonSchedulePolicyLabel(CommonSchedulePolicy p)
{
    switch (p) {
    case CommonSchedulePolicy::WhileTrue: return QCoreApplication::translate("GameData", "Enquanto verdadeiro");
    case CommonSchedulePolicy::OnTrue: return QCoreApplication::translate("GameData", "Ao ficar verdadeiro (uma vez)");
    case CommonSchedulePolicy::Interval: return QCoreApplication::translate("GameData", "Em intervalo enquanto verdadeiro");
    }
    return QString();
}

bool compareWithOp(int a, const QString& op, int b)
{
    if (op == QLatin1String("=="))  return a == b;
    if (op == QLatin1String("!="))  return a != b;
    if (op == QLatin1String(">"))   return a >  b;
    if (op == QLatin1String("<"))   return a <  b;
    if (op == QLatin1String("<="))  return a <= b;
    return a >= b;                                   // padrao: >=
}

QJsonObject PageConditions::toJson() const
{
    QJsonObject o;
    if (useSwitchA)   { o["switchA"] = switchAId; }
    if (useSwitchB)   { o["switchB"] = switchBId; }
    if (useVariable)  {
        o["variable"] = variableId;
        o["varOp"] = variableOp;
        o["varValue"] = variableValue;
    }
    if (useSelfSwitch) o["selfSwitch"] = selfSwitchLetter;
    return o;
}

PageConditions PageConditions::fromJson(const QJsonObject& o)
{
    PageConditions c;
    if (o.contains("switchA")) { c.useSwitchA = true; c.switchAId = o.value("switchA").toInt(1); }
    if (o.contains("switchB")) { c.useSwitchB = true; c.switchBId = o.value("switchB").toInt(1); }
    if (o.contains("variable")) {
        c.useVariable = true;
        c.variableId = o.value("variable").toInt(1);
        c.variableOp = o.value("varOp").toString(QStringLiteral(">="));
        c.variableValue = o.value("varValue").toInt(0);
    }
    if (o.contains("selfSwitch")) {
        c.useSelfSwitch = true;
        c.selfSwitchLetter = o.value("selfSwitch").toString(QStringLiteral("A"));
    }
    return c;
}


bool compareCommonValues(const QVariant& a, CommonValueType type, const QString& op, const QVariant& b)
{
    if (type == CommonValueType::Number) {
        const double av=a.toDouble(), bv=b.toDouble();
        if(op==QLatin1String("=="))return qFuzzyCompare(av+1.0,bv+1.0);
        if(op==QLatin1String("!="))return !qFuzzyCompare(av+1.0,bv+1.0);
        if(op==QLatin1String(">="))return av>=bv;
        if(op==QLatin1String("<="))return av<=bv;
        if(op==QLatin1String(">"))return av>bv;
        if(op==QLatin1String("<"))return av<bv;
        return false;
    }
    if (type == CommonValueType::Boolean) {
        const bool av=a.toBool(), bv=b.toBool();
        return op==QLatin1String("!=") ? av!=bv : av==bv;
    }
    const QString av=a.toString(), bv=b.toString();
    if(op==QLatin1String("!="))return av!=bv;
    if(op==QLatin1String("contains"))return av.contains(bv,Qt::CaseInsensitive);
    if(op==QLatin1String("startsWith"))return av.startsWith(bv,Qt::CaseInsensitive);
    if(op==QLatin1String("endsWith"))return av.endsWith(bv,Qt::CaseInsensitive);
    return av==bv;
}

} // namespace core
