#include "ExpressionEvaluator.h"

#include <QRegularExpression>
#include <QtMath>
#include <cmath>

namespace core {
namespace {

double numberValue(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::Bool) return value.toBool() ? 1.0 : 0.0;
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok && std::isfinite(number) ? number : 0.0;
}

bool boolValue(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QString) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QLatin1String("false") || text == QLatin1String("off") || text == QLatin1String("0") || text.isEmpty()) return false;
        return true;
    }
    return value.toBool();
}

bool numericVariant(const QVariant& value)
{
    const int id = value.metaType().id();
    return id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::UInt ||
           id == QMetaType::ULongLong || id == QMetaType::Double || id == QMetaType::Float || id == QMetaType::Bool;
}

class Parser
{
public:
    Parser(QString source, ExpressionContext context)
        : m_source(std::move(source)), m_context(std::move(context)) {}

    ExpressionResult run()
    {
        if (m_source.startsWith(QLatin1Char('='))) ++m_pos;
        m_result.value = parseTernary();
        skip();
        if (m_result.error.isEmpty() && m_pos != m_source.size()) fail(QStringLiteral("Token inesperado."), m_pos);
        return m_result;
    }

private:
    QString m_source;
    ExpressionContext m_context;
    ExpressionResult m_result;
    int m_pos = 0;
    int m_gameValueCalls = 0;

    void skip() { while (m_pos < m_source.size() && m_source.at(m_pos).isSpace()) ++m_pos; }
    bool match(const QString& token)
    {
        skip();
        if (m_source.mid(m_pos, token.size()) != token) return false;
        m_pos += token.size(); return true;
    }
    void fail(const QString& message, int position = -1)
    {
        if (!m_result.error.isEmpty()) return;
        m_result.error = message;
        m_result.errorPosition = position >= 0 ? position : m_pos;
    }

    QVariant parseTernary()
    {
        QVariant condition = parseOr();
        if (!m_result.error.isEmpty() || !match(QStringLiteral("?"))) return condition;
        QVariant yes = parseTernary();
        if (!match(QStringLiteral(":"))) { fail(QStringLiteral("O operador ternário precisa de ‘:’.")); return {}; }
        QVariant no = parseTernary();
        return boolValue(condition) ? yes : no;
    }
    QVariant parseOr()
    {
        QVariant value = parseAnd();
        while (match(QStringLiteral("||"))) {
            const QVariant rhs = parseAnd();
            value = boolValue(value) || boolValue(rhs);
        }
        return value;
    }
    QVariant parseAnd()
    {
        QVariant value = parseEquality();
        while (match(QStringLiteral("&&"))) {
            const QVariant rhs = parseEquality();
            value = boolValue(value) && boolValue(rhs);
        }
        return value;
    }
    QVariant parseEquality()
    {
        QVariant value = parseComparison();
        for (;;) {
            if (match(QStringLiteral("=="))) { QVariant rhs=parseComparison(); value = numericVariant(value)&&numericVariant(rhs) ? qFuzzyCompare(numberValue(value)+1.0,numberValue(rhs)+1.0) : value.toString()==rhs.toString(); }
            else if (match(QStringLiteral("!="))) { QVariant rhs=parseComparison(); value = numericVariant(value)&&numericVariant(rhs) ? !qFuzzyCompare(numberValue(value)+1.0,numberValue(rhs)+1.0) : value.toString()!=rhs.toString(); }
            else break;
        }
        return value;
    }
    QVariant parseComparison()
    {
        QVariant value = parseAdditive();
        for (;;) {
            if (match(QStringLiteral(">="))) value = numberValue(value) >= numberValue(parseAdditive());
            else if (match(QStringLiteral("<="))) value = numberValue(value) <= numberValue(parseAdditive());
            else if (match(QStringLiteral(">"))) value = numberValue(value) > numberValue(parseAdditive());
            else if (match(QStringLiteral("<"))) value = numberValue(value) < numberValue(parseAdditive());
            else break;
        }
        return value;
    }
    QVariant parseAdditive()
    {
        QVariant value = parseMultiplicative();
        for (;;) {
            if (match(QStringLiteral("+"))) {
                QVariant rhs = parseMultiplicative();
                if (value.metaType().id() == QMetaType::QString || rhs.metaType().id() == QMetaType::QString) value = value.toString() + rhs.toString();
                else value = numberValue(value) + numberValue(rhs);
            } else if (match(QStringLiteral("-"))) value = numberValue(value) - numberValue(parseMultiplicative());
            else break;
        }
        return value;
    }
    QVariant parseMultiplicative()
    {
        QVariant value = parseUnary();
        for (;;) {
            if (match(QStringLiteral("*"))) value = numberValue(value) * numberValue(parseUnary());
            else if (match(QStringLiteral("/"))) { const int at=m_pos; const double rhs=numberValue(parseUnary()); if(qFuzzyIsNull(rhs)){fail(QStringLiteral("Divisão por zero."),at);return 0.0;} value=numberValue(value)/rhs; }
            else if (match(QStringLiteral("%"))) { const int at=m_pos; const double rhs=numberValue(parseUnary()); if(qFuzzyIsNull(rhs)){fail(QStringLiteral("Módulo por zero."),at);return 0.0;} value=std::fmod(numberValue(value),rhs); }
            else break;
        }
        return value;
    }
    QVariant parseUnary()
    {
        if (match(QStringLiteral("!"))) return !boolValue(parseUnary());
        if (match(QStringLiteral("-"))) return -numberValue(parseUnary());
        if (match(QStringLiteral("+"))) return numberValue(parseUnary());
        return parsePrimary();
    }
    QVariant parsePrimary()
    {
        skip();
        if (m_pos >= m_source.size()) { fail(QStringLiteral("Expressão incompleta.")); return {}; }
        if (match(QStringLiteral("("))) { QVariant value=parseTernary(); if(!match(QStringLiteral(")")))fail(QStringLiteral("Parêntese ‘)’ ausente.")); return value; }
        const QChar ch = m_source.at(m_pos);
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) return parseString();
        if (ch.isDigit() || ch == QLatin1Char('.')) return parseNumber();
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            const QString name = parseIdentifier();
            if (name.compare(QLatin1String("true"),Qt::CaseInsensitive)==0) return true;
            if (name.compare(QLatin1String("false"),Qt::CaseInsensitive)==0) return false;
            if (name.compare(QLatin1String("PI"),Qt::CaseInsensitive)==0) return 3.14159265358979323846;
            if (name.compare(QLatin1String("E"),Qt::CaseInsensitive)==0) return 2.71828182845904523536;
            if (match(QStringLiteral("("))) return parseFunction(name, QLatin1Char(')'));
            if (match(QStringLiteral("["))) return parseFunction(name, QLatin1Char(']'));
            fail(QStringLiteral("Identificador desconhecido: %1.").arg(name), m_pos-name.size()); return {};
        }
        fail(QStringLiteral("Caractere inesperado: %1.").arg(ch), m_pos++); return {};
    }
    QVariant parseString()
    {
        const QChar quote=m_source.at(m_pos++);QString out;
        while(m_pos<m_source.size()){
            QChar ch=m_source.at(m_pos++);
            if(ch==quote)return out;
            if(ch==QLatin1Char('\\')&&m_pos<m_source.size()){
                const QChar escaped=m_source.at(m_pos++);
                if(escaped==QLatin1Char('n'))out+=QLatin1Char('\n');else if(escaped==QLatin1Char('t'))out+=QLatin1Char('\t');else out+=escaped;
            }else out+=ch;
        }
        fail(QStringLiteral("Texto sem aspas de fechamento."));return out;
    }
    QVariant parseNumber()
    {
        const int start=m_pos;
        while(m_pos<m_source.size()&&(m_source.at(m_pos).isDigit()||m_source.at(m_pos)==QLatin1Char('.')))++m_pos;
        bool ok=false;const double value=m_source.mid(start,m_pos-start).toDouble(&ok);
        if(!ok){fail(QStringLiteral("Número inválido."),start);return 0.0;}return value;
    }
    QString parseIdentifier()
    {
        const int start=m_pos;
        while(m_pos<m_source.size()&&(m_source.at(m_pos).isLetterOrNumber()||m_source.at(m_pos)==QLatin1Char('_')||m_source.at(m_pos)==QLatin1Char('.')))++m_pos;
        return m_source.mid(start,m_pos-start);
    }
    QVariant parseFunction(const QString& name, QChar close)
    {
        QVariantList args;skip();
        if(m_pos<m_source.size()&&m_source.at(m_pos)==close){++m_pos;return call(name,args);}
        for(;;){args.push_back(parseTernary());if(!m_result.error.isEmpty())return {};skip();if(m_pos<m_source.size()&&m_source.at(m_pos)==close){++m_pos;break;}if(!match(QStringLiteral(","))){fail(QStringLiteral("Esperado ‘,’ ou ‘%1’ na função %2.").arg(close).arg(name));return {};}}
        return call(name,args);
    }
    QVariant call(const QString& rawName,const QVariantList& args)
    {
        const QString name=rawName.toLower();
        auto need=[&](int count){if(args.size()==count)return true;fail(QStringLiteral("A função %1 espera %2 argumento(s).").arg(rawName).arg(count));return false;};
        if(name==QLatin1String("abs")){if(!need(1))return {};return std::abs(numberValue(args[0]));}
        if(name==QLatin1String("min")){if(!need(2))return {};return qMin(numberValue(args[0]),numberValue(args[1]));}
        if(name==QLatin1String("max")){if(!need(2))return {};return qMax(numberValue(args[0]),numberValue(args[1]));}
        if(name==QLatin1String("floor")){if(!need(1))return {};return std::floor(numberValue(args[0]));}
        if(name==QLatin1String("ceil")){if(!need(1))return {};return std::ceil(numberValue(args[0]));}
        if(name==QLatin1String("round")){if(!need(1))return {};return std::round(numberValue(args[0]));}
        if(name==QLatin1String("clamp")){if(!need(3))return {};return qBound(numberValue(args[1]),numberValue(args[0]),numberValue(args[2]));}
        if(name==QLatin1String("lerp")){if(!need(3))return {};const double a=numberValue(args[0]);return a+(numberValue(args[1])-a)*numberValue(args[2]);}
        if(name==QLatin1String("sqrt")){if(!need(1))return {};const double v=numberValue(args[0]);if(v<0){fail(QStringLiteral("Raiz quadrada de número negativo."));return 0.0;}return std::sqrt(v);}
        if(name==QLatin1String("pow")){if(!need(2))return {};return std::pow(numberValue(args[0]),numberValue(args[1]));}
        if(name==QLatin1String("sin")){if(!need(1))return {};return std::sin(numberValue(args[0]));}
        if(name==QLatin1String("cos")){if(!need(1))return {};return std::cos(numberValue(args[0]));}
        if(name==QLatin1String("gv")&&++m_gameValueCalls>m_context.maxGameValueDepth){fail(QStringLiteral("Recursão excessiva em gv()."));return {};}
        bool handled=false;QString error;
        QVariant value=m_context.resolveFunction?m_context.resolveFunction(rawName,args,&handled,&error):QVariant();
        if(!error.isEmpty()){fail(error);return {};}
        if(handled)return value;
        m_result.warnings.push_back(QStringLiteral("Função desconhecida: %1.").arg(rawName));
        return 0.0;
    }
};

} // namespace

ExpressionResult evaluateExpression(const QString& expression, const ExpressionContext& context)
{
    if (expression.trimmed().isEmpty()) return {{}, QStringLiteral("A expressão está vazia."), 0, {}};
    return Parser(expression, context).run();
}

bool looksLikeExpression(const QString& text)
{
    const QString value=text.trimmed();
    if(value.startsWith(QLatin1Char('=')))return true;
    // Mantém o detector leve e, sobretudo, com uma expressão PCRE2 válida.
    // A versão RC2.83.3 tinha um ')' excedente no agrupamento externo, o que
    // fazia QRegularExpression rejeitar o padrão e retornar falso para tudo.
    static const QRegularExpression syntax(QStringLiteral(
        R"((?:\b(?:abs|min|max|floor|ceil|round|clamp|lerp|sqrt|pow|sin|cos|gv|switch|v|s)\s*[\[(]|\b(?:actor|party|map|input|timer|picture)\.[A-Za-z_]\w*\s*[\[(]|\?|&&|\|\||==|!=|>=|<=|[+*/%]|\s-\s))"));
    return syntax.match(value).hasMatch();
}

} // namespace core
