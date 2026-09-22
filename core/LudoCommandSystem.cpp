#include "LudoCommandSystem.h"
#include "CommandRegistry.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QtGlobal>
#include <initializer_list>
#include <utility>

namespace core {
namespace {

enum class ParamKind { Text, Integer, Real, Boolean, Enum };

struct ParamSpec {
    QString canonical;
    QStringList aliases;
    ParamKind kind = ParamKind::Text;
    bool required = false;
    bool hasDefault = false;
    QVariant defaultValue;
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
    QHash<QString, QString> enumAliases;
};

struct TextCommandSchema {
    QString canonicalName;
    QString nativeType;
    QStringList names;
    QVector<ParamSpec> params;
};

QString normalizedWord(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QChar(0x00E7), QLatin1Char('c')); // ç
    value.replace(QChar(0x00E3), QLatin1Char('a')); // ã
    value.replace(QChar(0x00E1), QLatin1Char('a')); // á
    value.replace(QChar(0x00E0), QLatin1Char('a')); // à
    value.replace(QChar(0x00E2), QLatin1Char('a')); // â
    value.replace(QChar(0x00E9), QLatin1Char('e')); // é
    value.replace(QChar(0x00EA), QLatin1Char('e')); // ê
    value.replace(QChar(0x00ED), QLatin1Char('i')); // í
    value.replace(QChar(0x00F3), QLatin1Char('o')); // ó
    value.replace(QChar(0x00F4), QLatin1Char('o')); // ô
    value.replace(QChar(0x00F5), QLatin1Char('o')); // õ
    value.replace(QChar(0x00FA), QLatin1Char('u')); // ú
    return value;
}

ParamSpec textParam(const char* canonical, std::initializer_list<const char*> aliases,
                    const QVariant& defaultValue = QVariant())
{
    ParamSpec p;
    p.canonical = QString::fromLatin1(canonical);
    for (const char* alias : aliases) p.aliases.push_back(QString::fromLatin1(alias));
    p.kind = ParamKind::Text;
    if (defaultValue.isValid()) { p.hasDefault = true; p.defaultValue = defaultValue; }
    return p;
}

ParamSpec numberParam(const char* canonical, ParamKind kind,
                      std::initializer_list<const char*> aliases,
                      const QVariant& defaultValue, double minimum, double maximum)
{
    ParamSpec p = textParam(canonical, aliases, defaultValue);
    p.kind = kind;
    p.hasRange = true;
    p.minimum = minimum;
    p.maximum = maximum;
    return p;
}

ParamSpec boolParam(const char* canonical, std::initializer_list<const char*> aliases, bool defaultValue)
{
    ParamSpec p = textParam(canonical, aliases, defaultValue);
    p.kind = ParamKind::Boolean;
    return p;
}

ParamSpec optionalBoolParam(const char* canonical, std::initializer_list<const char*> aliases)
{
    ParamSpec p = textParam(canonical, aliases);
    p.kind = ParamKind::Boolean;
    return p;
}

ParamSpec enumParam(const char* canonical, std::initializer_list<const char*> aliases,
                    const char* defaultValue,
                    std::initializer_list<std::pair<const char*, const char*>> values)
{
    ParamSpec p = textParam(canonical, aliases, QString::fromLatin1(defaultValue));
    p.kind = ParamKind::Enum;
    for (const auto& value : values)
        p.enumAliases.insert(normalizedWord(QString::fromLatin1(value.first)), QString::fromLatin1(value.second));
    return p;
}

const QVector<TextCommandSchema>& schemas()
{
    static const QVector<TextCommandSchema> table = [] {
        QVector<TextCommandSchema> out;

        TextCommandSchema camera;
        camera.canonicalName = QStringLiteral("LudoCamera");
        camera.nativeType = QStringLiteral("ludo.camera.move");
        camera.names = {QStringLiteral("ludocamera")};
        camera.params = {
            textParam("target", {"alvo", "target"}, QStringLiteral("player")),
            numberParam("x", ParamKind::Real, {"x"}, QVariant(), -99999.0, 99999.0),
            numberParam("y", ParamKind::Real, {"y"}, QVariant(), -99999.0, 99999.0),
            // Sem zoom explícito, o runtime histórico preserva o zoom atual.
            numberParam("zoom", ParamKind::Real, {"zoom"}, QVariant(), 0.25, 8.0),
            numberParam("duration", ParamKind::Integer, {"duracao", "duration"}, 30, 0.0, 3600.0),
            // Follow possui default calculado pelo target (player=true; demais=false).
            optionalBoolParam("follow", {"seguir", "follow"}),
            numberParam("followSpeed", ParamKind::Real, {"suavidade", "followspeed"}, 6.0, 0.1, 30.0),
            numberParam("deadzone", ParamKind::Real, {"zona", "deadzone"}, 0.0, 0.0, 9999.0),
            boolParam("wait", {"esperar", "wait"}, false)
        };
        out.push_back(camera);

        TextCommandSchema fade;
        fade.canonicalName = QStringLiteral("LudoFade");
        fade.nativeType = QStringLiteral("ludo.sprite.fade");
        fade.names = {QStringLiteral("ludofade")};
        fade.params = {
            textParam("target", {"alvo", "target"}, QStringLiteral("self")),
            numberParam("opacity", ParamKind::Integer, {"opacidade", "opacity"}, 255, 0.0, 255.0),
            numberParam("duration", ParamKind::Integer, {"duracao", "duration"}, 30, 0.0, 3600.0),
            boolParam("wait", {"esperar", "wait"}, false)
        };
        out.push_back(fade);

        TextCommandSchema phantom;
        phantom.canonicalName = QStringLiteral("LudoPhantom");
        phantom.nativeType = QStringLiteral("ludo.sprite.phantom");
        phantom.names = {QStringLiteral("ludophantom")};
        phantom.params = {
            textParam("target", {"alvo", "target"}, QStringLiteral("self")),
            enumParam("mode", {"modo", "mode"}, "visibleNear",
                      {{"visiblenear", "visibleNear"}, {"visiblefar", "visibleFar"},
                       {"perto", "visibleNear"}, {"longe", "visibleFar"}}),
            numberParam("near", ParamKind::Real, {"perto", "near"}, 1.0, 0.0, 9999.0),
            numberParam("distance", ParamKind::Real, {"distancia", "longe", "distance"}, 6.0, 0.0, 9999.0),
            numberParam("minimum", ParamKind::Integer, {"minimo", "minimum"}, 32, 0.0, 255.0),
            numberParam("maximum", ParamKind::Integer, {"maximo", "maximum"}, 255, 0.0, 255.0),
            numberParam("smoothness", ParamKind::Real, {"suavidade", "smoothness"}, 100.0, 0.0, 100.0)
        };
        out.push_back(phantom);

        TextCommandSchema cutscene;
        cutscene.canonicalName = QStringLiteral("LudoCutscene");
        cutscene.nativeType = QStringLiteral("ludo.cutscene.begin");
        cutscene.names = {QStringLiteral("ludocutscene")};
        out.push_back(cutscene);

        return out;
    }();
    return table;
}

const TextCommandSchema* schemaForName(const QString& rawName)
{
    const QString name = normalizedWord(rawName);
    for (const TextCommandSchema& schema : schemas())
        if (schema.names.contains(name)) return &schema;
    return nullptr;
}

const ParamSpec* paramForAlias(const TextCommandSchema& schema, const QString& rawAlias)
{
    const QString alias = normalizedWord(rawAlias);
    for (const ParamSpec& param : schema.params) {
        if (normalizedWord(param.canonical) == alias) return &param;
        for (const QString& candidate : param.aliases)
            if (normalizedWord(candidate) == alias) return &param;
    }
    return nullptr;
}

LudoCommandParseResult invalidResult(const QString& text, const QString& code, const QString& message,
                                     bool recognized = true)
{
    LudoCommandParseResult result;
    result.recognized = recognized;
    result.valid = false;
    result.code = code;
    result.message = message;
    result.sourceText = text;
    return result;
}

bool parseBoolean(const QString& raw, bool* value)
{
    const QString v = normalizedWord(raw);
    if (v == QLatin1String("true") || v == QLatin1String("1") || v == QLatin1String("sim") ||
        v == QLatin1String("on")) { *value = true; return true; }
    if (v == QLatin1String("false") || v == QLatin1String("0") || v == QLatin1String("nao") ||
        v == QLatin1String("off")) { *value = false; return true; }
    return false;
}

bool parseParameterValue(const ParamSpec& spec, const QString& raw, QVariant* out, QString* problem)
{
    if (spec.kind == ParamKind::Text) {
        QString value = raw.trimmed();
        if (spec.canonical == QLatin1String("target")) {
            const QString normalized = normalizedWord(value);
            if (normalized == QLatin1String("jogador")) value = QStringLiteral("player");
            else if (normalized == QLatin1String("esteevento") || normalized == QLatin1String("este_evento"))
                value = QStringLiteral("self");
            else if (normalized == QLatin1String("posicao")) value = QStringLiteral("position");
        }
        *out = value;
        return true;
    }

    if (spec.kind == ParamKind::Boolean) {
        bool value = false;
        if (!parseBoolean(raw, &value)) {
            *problem = QObject::tr("%1 precisa ser verdadeiro/falso (true/false, sim/não ou 1/0).").arg(spec.canonical);
            return false;
        }
        *out = value;
        return true;
    }

    if (spec.kind == ParamKind::Enum) {
        const QString key = normalizedWord(raw);
        const auto it = spec.enumAliases.constFind(key);
        if (it == spec.enumAliases.cend()) {
            *problem = QObject::tr("Valor desconhecido para %1: %2.").arg(spec.canonical, raw);
            return false;
        }
        *out = it.value();
        return true;
    }

    bool ok = false;
    const double numeric = raw.toDouble(&ok);
    if (!ok) {
        *problem = QObject::tr("%1 precisa ser numérico.").arg(spec.canonical);
        return false;
    }
    if (spec.kind == ParamKind::Integer) {
        bool integerOk = false;
        const int integerValue = raw.toInt(&integerOk);
        if (!integerOk) {
            *problem = QObject::tr("%1 precisa ser um número inteiro.").arg(spec.canonical);
            return false;
        }
        if (spec.hasRange && (integerValue < spec.minimum || integerValue > spec.maximum)) {
            *problem = QObject::tr("%1 precisa ficar entre %2 e %3.")
                           .arg(spec.canonical).arg(spec.minimum).arg(spec.maximum);
            return false;
        }
        *out = integerValue;
        return true;
    }
    if (spec.hasRange && (numeric < spec.minimum || numeric > spec.maximum)) {
        *problem = QObject::tr("%1 precisa ficar entre %2 e %3.")
                       .arg(spec.canonical).arg(spec.minimum).arg(spec.maximum);
        return false;
    }
    *out = numeric;
    return true;
}

QString stripSingleTag(const QString& input, bool* malformed, bool* multiple)
{
    *malformed = false;
    *multiple = false;
    const QString text = input.trimmed();
    const int ludoCount = text.count(QRegularExpression(QStringLiteral("<\\s*Ludo"),
                                                         QRegularExpression::CaseInsensitiveOption));
    if (ludoCount > 1) { *multiple = true; return text; }
    if (text.startsWith(QLatin1Char('<'))) {
        if (!text.endsWith(QLatin1Char('>'))) { *malformed = true; return text; }
        return text.mid(1, text.size() - 2).trimmed();
    }
    return text;
}

} // namespace

LudoCommandParseResult parseLudoCommandText(const QString& input, EventExecutionMode executionMode)
{
    const QString source = input.trimmed();
    if (source.isEmpty())
        return invalidResult(source, QStringLiteral("ludo.empty"), QObject::tr("O Comando Ludo está vazio."), false);

    bool malformed = false, multiple = false;
    QString body = stripSingleTag(source, &malformed, &multiple);
    if (multiple)
        return invalidResult(source, QStringLiteral("ludo.multiple"),
                             QObject::tr("Use um único comando <Ludo...> por Comando Ludo."));
    if (malformed)
        return invalidResult(source, QStringLiteral("ludo.malformed"),
                             QObject::tr("A tag Ludo não foi fechada com >."));

    const QRegularExpression nameRe(QStringLiteral("\\b(Ludo[A-Za-z0-9_.-]+)\\b"),
                                    QRegularExpression::CaseInsensitiveOption);
    const auto nameMatch = nameRe.match(body);
    if (!nameMatch.hasMatch()) {
        const bool ludoVisible = source.contains(QRegularExpression(
            QStringLiteral("^<?\\s*Ludo"), QRegularExpression::CaseInsensitiveOption));
        return invalidResult(source, QStringLiteral("ludo.not-command"),
                             QObject::tr("O texto não contém um comando Ludo reconhecível."), ludoVisible);
    }

    const QString rawName = nameMatch.captured(1);
    const TextCommandSchema* schema = schemaForName(rawName);
    if (!schema)
        return invalidResult(source, QStringLiteral("ludo.unknown-command"),
                             QObject::tr("Comando Ludo desconhecido: %1.").arg(rawName));

    EventCommand command;
    command.type = schema->nativeType;
    command.executionMode = executionMode;
    for (const ParamSpec& spec : schema->params)
        if (spec.hasDefault) command.params.insert(spec.canonical, spec.defaultValue);

    QString residue = body;
    residue.replace(nameMatch.capturedStart(1), nameMatch.capturedLength(1),
                    QString(nameMatch.capturedLength(1), QLatin1Char(' ')));

    const QRegularExpression paramRe(
        QStringLiteral("([\\p{L}\\p{N}_.-]+)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"));
    auto matches = paramRe.globalMatch(body);
    QSet<QString> assigned;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString alias = match.captured(1);
        const ParamSpec* spec = paramForAlias(*schema, alias);
        if (!spec)
            return invalidResult(source, QStringLiteral("ludo.unknown-parameter"),
                                 QObject::tr("Parâmetro desconhecido em %1: %2.")
                                     .arg(schema->canonicalName, alias));
        if (assigned.contains(spec->canonical))
            return invalidResult(source, QStringLiteral("ludo.duplicate-parameter"),
                                 QObject::tr("O parâmetro %1 foi informado mais de uma vez.").arg(spec->canonical));
        assigned.insert(spec->canonical);
        QString rawValue = match.captured(2);
        if (rawValue.isNull()) rawValue = match.captured(3);
        if (rawValue.isNull()) rawValue = match.captured(4);
        QVariant parsed;
        QString problem;
        if (!parseParameterValue(*spec, rawValue, &parsed, &problem))
            return invalidResult(source, QStringLiteral("ludo.invalid-parameter"), problem);
        command.params[spec->canonical] = parsed;
        residue.replace(match.capturedStart(), match.capturedLength(),
                        QString(match.capturedLength(), QLatin1Char(' ')));
    }

    for (const ParamSpec& spec : schema->params) {
        if (spec.required && !assigned.contains(spec.canonical) && !spec.hasDefault)
            return invalidResult(source, QStringLiteral("ludo.missing-parameter"),
                                 QObject::tr("O parâmetro obrigatório %1 não foi informado.").arg(spec.canonical));
    }

    QStringList words = residue.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (QString& word : words) word = normalizedWord(word);
    if (schema->canonicalName == QLatin1String("LudoCutscene")) {
        QString action = QStringLiteral("begin");
        for (const QString& word : words) {
            if (word == QLatin1String("inicio") || word == QLatin1String("begin") || word == QLatin1String("start"))
                action = QStringLiteral("begin");
            else if (word == QLatin1String("fim") || word == QLatin1String("end"))
                action = QStringLiteral("end");
            else
                return invalidResult(source, QStringLiteral("ludo.unexpected-token"),
                                     QObject::tr("Token desconhecido em LudoCutscene: %1.").arg(word));
        }
        command.type = action == QLatin1String("end") ? QStringLiteral("ludo.cutscene.end")
                                                       : QStringLiteral("ludo.cutscene.begin");
    } else if (!words.isEmpty()) {
        return invalidResult(source, QStringLiteral("ludo.unexpected-token"),
                             QObject::tr("Texto inesperado depois de %1: %2.")
                                 .arg(schema->canonicalName, words.join(QLatin1Char(' '))));
    }

    if (command.type == QLatin1String("ludo.camera.move") &&
        !assigned.contains(QStringLiteral("follow"))) {
        command.params[QStringLiteral("follow")] =
            command.params.value(QStringLiteral("target"), QStringLiteral("player")).toString() == QLatin1String("player");
    }

    if (command.params.contains(QStringLiteral("target"))) {
        const QString target = command.params.value(QStringLiteral("target")).toString().trimmed();
        const bool explicitEvent = target.startsWith(QLatin1String("event:")) &&
                                   target.size() > 6 && !target.mid(6).contains(QLatin1Char(':'));
        const bool allowPosition = command.type == QLatin1String("ludo.camera.move");
        if (target != QLatin1String("player") && target != QLatin1String("self") &&
            !(allowPosition && target == QLatin1String("position")) && !explicitEvent)
            return invalidResult(source, QStringLiteral("ludo.invalid-target"),
                                 QObject::tr("Alvo inválido em %1: %2.").arg(schema->canonicalName, target));
    }

    if (command.type == QLatin1String("ludo.camera.move") &&
        command.params.value(QStringLiteral("target")).toString() == QLatin1String("position") &&
        (!assigned.contains(QStringLiteral("x")) || !assigned.contains(QStringLiteral("y"))))
        return invalidResult(source, QStringLiteral("ludo.camera.position"),
                             QObject::tr("LudoCamera com alvo=position exige x e y."));

    if (command.type == QLatin1String("ludo.sprite.phantom")) {
        const double nearDistance = command.params.value(QStringLiteral("near")).toDouble();
        const double farDistance = command.params.value(QStringLiteral("distance")).toDouble();
        if (farDistance <= nearDistance)
            return invalidResult(source, QStringLiteral("ludo.phantom.distance"),
                                 QObject::tr("A distância de LudoPhantom precisa ser maior que perto/near."));
        if (command.params.value(QStringLiteral("maximum")).toInt() <
            command.params.value(QStringLiteral("minimum")).toInt())
            return invalidResult(source, QStringLiteral("ludo.phantom.opacity"),
                                 QObject::tr("O máximo de LudoPhantom não pode ser menor que o mínimo."));
    }

    LudoCommandParseResult result;
    result.recognized = true;
    result.valid = true;
    result.code = QStringLiteral("ludo.ok");
    result.sourceText = source;
    result.command = command;
    return result;
}

QVector<LudoCommandParseResult> parseLudoCommentCommands(const QString& text)
{
    QVector<LudoCommandParseResult> out;

    // Varre cada início de tag separadamente. Assim uma tag válida não esconde
    // outra desconhecida/malformada existente no mesmo Comentário.
    const QRegularExpression tagStartRe(QStringLiteral("<\\s*Ludo"),
                                        QRegularExpression::CaseInsensitiveOption);
    QVector<int> starts;
    auto tagStarts = tagStartRe.globalMatch(text);
    while (tagStarts.hasNext()) starts.push_back(tagStarts.next().capturedStart());
    if (!starts.isEmpty()) {
        for (int i = 0; i < starts.size(); ++i) {
            const int begin = starts.at(i);
            const int nextBegin = i + 1 < starts.size() ? starts.at(i + 1) : text.size();
            const int close = text.indexOf(QLatin1Char('>'), begin);
            if (close < 0 || close >= nextBegin) {
                const QString fragment = text.mid(begin, nextBegin - begin).trimmed();
                out.push_back(invalidResult(fragment, QStringLiteral("ludo.malformed"),
                                            QObject::tr("Comentário contém uma tag Ludo malformada ou sem >.")));
                continue;
            }
            out.push_back(parseLudoCommandText(text.mid(begin, close - begin + 1),
                                               EventExecutionMode::OnPageActivated));
        }
        return out;
    }

    // Compatibilidade com comentários antigos sem < >. Só ativamos este modo
    // quando um dos nomes historicamente suportados está presente.
    const QRegularExpression legacyRe(QStringLiteral("\\bLudo(?:Camera|Fade|Phantom|Cutscene)\\b"),
                                      QRegularExpression::CaseInsensitiveOption);
    const auto legacyMatch = legacyRe.match(text);
    if (legacyMatch.hasMatch()) {
        // Comentários antigos podiam ter texto explicativo em outras linhas.
        // O parser permanece estrito na linha do comando, sem quebrar esse formato.
        const int lineStart = text.lastIndexOf(QLatin1Char('\n'), legacyMatch.capturedStart()) + 1;
        int lineEnd = text.indexOf(QLatin1Char('\n'), legacyMatch.capturedStart());
        if (lineEnd < 0) lineEnd = text.size();
        out.push_back(parseLudoCommandText(text.mid(lineStart, lineEnd - lineStart).trimmed(),
                                           EventExecutionMode::OnPageActivated));
    }
    return out;
}

EventCommand normalizeLegacyLudoCommand(const EventCommand& command, LudoCommandParseResult* parseResult)
{
    if (command.type != QLatin1String("ludo.command")) {
        if (parseResult) *parseResult = LudoCommandParseResult();
        return command;
    }

    EventCommand preserved = command;
    EventExecutionMode mode = command.executionMode;
    if (command.params.contains(QStringLiteral("automatic")))
        mode = command.params.value(QStringLiteral("automatic"), true).toBool()
            ? EventExecutionMode::OnPageActivated : EventExecutionMode::Normal;
    else if (mode == EventExecutionMode::Normal)
        mode = EventExecutionMode::OnPageActivated; // default histórico do Comando Ludo
    preserved.executionMode = mode;

    LudoCommandParseResult parsed = parseLudoCommandText(
        command.params.value(QStringLiteral("text")).toString(), mode);
    if (parseResult) *parseResult = parsed;
    if (!parsed.valid) return preserved;
    return parsed.command;
}

bool ludoCommandSupportsPageActivation(const EventCommand& command)
{
    return CommandRegistry::supportsPageActivation(command.type);
}

QVector<EventCommand> ludoPageActivationCommands(const EventCommand& source,
                                                 QVector<LudoCommandParseResult>* diagnostics)
{
    QVector<EventCommand> out;
    const auto pageModeDiagnostic = [](LudoCommandParseResult result) {
        if (result.valid && result.command.executionMode == EventExecutionMode::OnPageActivated &&
            !ludoCommandSupportsPageActivation(result.command)) {
            result.valid = false;
            result.recognized = true;
            result.code = QStringLiteral("ludo.unsupported-execution-mode");
            result.message = QObject::tr("%1 é estrutural e não pode executar isoladamente em OnPageActivated.")
                .arg(result.command.type);
        }
        return result;
    };

    if (source.type == QLatin1String("comment")) {
        const QVector<LudoCommandParseResult> parsed =
            parseLudoCommentCommands(source.params.value(QStringLiteral("text")).toString());
        for (const LudoCommandParseResult& result : parsed) {
            const LudoCommandParseResult diagnostic = pageModeDiagnostic(result);
            if (diagnostics) diagnostics->push_back(diagnostic);
            if (diagnostic.valid) out.push_back(diagnostic.command);
        }
        return out;
    }

    if (source.type == QLatin1String("ludo.command")) {
        LudoCommandParseResult parsed;
        EventCommand normalized = normalizeLegacyLudoCommand(source, &parsed);
        parsed.command = normalized;
        const LudoCommandParseResult diagnostic = pageModeDiagnostic(parsed);
        if (diagnostics) diagnostics->push_back(diagnostic);
        if (diagnostic.valid && normalized.executionMode == EventExecutionMode::OnPageActivated)
            out.push_back(normalized);
        return out;
    }

    if (source.executionMode == EventExecutionMode::OnPageActivated) {
        if (ludoCommandSupportsPageActivation(source)) {
            out.push_back(source);
        } else if (diagnostics) {
            LudoCommandParseResult diagnostic;
            diagnostic.recognized = true;
            diagnostic.valid = false;
            diagnostic.code = QStringLiteral("ludo.unsupported-execution-mode");
            diagnostic.message = QObject::tr("%1 é estrutural e não pode executar isoladamente em OnPageActivated.")
                .arg(source.type);
            diagnostic.command = source;
            diagnostics->push_back(diagnostic);
        }
    }
    return out;
}

} // namespace core
