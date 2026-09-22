#include "EventCommandValidator.h"
#include "EventCommandCodec.h"
#include "EventExecutionContext.h"
#include "LudoCommandSystem.h"
#include "InputMap.h"
#include "FilterSystem.h"

#include "CommandRegistry.h"
#include "ConditionTree.h"
#include "ExpressionEvaluator.h"
#include "GameValueRegistry.h"
#include "Editor.h"
#include "NoCodePlugin.h"
#include "ProjectValidator.h"
#include "Weather.h"
#include "TextEffects.h"
#include "DialogueContent.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QMetaType>
#include <QSet>
#include <QRegularExpression>
#include <cmath>

namespace core {
namespace {

void addIssue(ProjectValidationResult& result, ValidationSeverity severity,
              const QString& code, const QString& location, const QString& message,
              bool safelyFixable = false)
{
    result.issues.push_back({severity, code, location, message, safelyFixable});
    if (severity == ValidationSeverity::Error) ++result.errorCount;
    else if (severity == ValidationSeverity::Warning) ++result.warningCount;
    else ++result.infoCount;
}


bool projectFontFamilyKnown(const Editor& ed, const QString& family)
{
    const QString wanted = family.trimmed();
    if (wanted.isEmpty() || wanted == ed.legacyImport.mainFontFamily) return true;
    for (const ProjectFont& font : ed.legacyImport.projectFonts)
        if (font.family.compare(wanted, Qt::CaseInsensitive) == 0) return true;
    return false;
}

void validateTextEffectPhaseRaw(const QVariantMap& phase, const QString& phaseName,
                                const QString& location, ProjectValidationResult& result)
{
    if (phase.isEmpty()) return;
    const auto warn = [&](const QString& code, const QString& message) {
        addIssue(result, ValidationSeverity::Warning, code, location,
                 QObject::tr("%1: %2").arg(phaseName, message));
    };
    const QSet<QString> targets{QStringLiteral("character"),QStringLiteral("word"),QStringLiteral("block")};
    const QSet<QString> motions{QStringLiteral("tween"),QStringLiteral("wave"),QStringLiteral("shake"),QStringLiteral("float"),
                                QStringLiteral("jitter"),QStringLiteral("pulse"),QStringLiteral("rainbow"),QStringLiteral("glow"),
                                QStringLiteral("sweep"),QStringLiteral("typewriter")};
    const QSet<QString> easings{QStringLiteral("linear"),QStringLiteral("quad-in"),QStringLiteral("quad-out"),
                                QStringLiteral("quad-in-out"),QStringLiteral("expo-out"),QStringLiteral("back-out"),
                                QStringLiteral("elastic-out")};
    const QSet<QString> loops{QStringLiteral("once"),QStringLiteral("while-visible"),QStringLiteral("count"),QStringLiteral("ping-pong")};

    const QString target=phase.value(QStringLiteral("target"),QStringLiteral("character")).toString().trimmed().toLower();
    const QString motion=phase.value(QStringLiteral("motion"),QStringLiteral("tween")).toString().trimmed().toLower();
    const QString easing=phase.value(QStringLiteral("easing"),QStringLiteral("linear")).toString().trimmed().toLower();
    const QString loop=phase.value(QStringLiteral("loopMode"),QStringLiteral("once")).toString().trimmed().toLower();
    if(!targets.contains(target)) warn(QStringLiteral("text.effects.target"),QObject::tr("Alvo desconhecido. O jogo usará Caractere."));
    if(!motions.contains(motion)) warn(QStringLiteral("text.effects.motion"),QObject::tr("Movimento desconhecido. O jogo usará Interpolação."));
    if(!easings.contains(easing)) warn(QStringLiteral("text.effects.easing"),QObject::tr("Suavização desconhecida. O jogo usará Linear."));
    if(!loops.contains(loop)) warn(QStringLiteral("text.effects.loop"),QObject::tr("Modo de repetição desconhecido. O jogo usará Uma vez."));
    if(phase.value(QStringLiteral("durationMs"),600).toInt()<=0)
        warn(QStringLiteral("text.effects.duration"),QObject::tr("a duração deve ser maior que zero e será normalizada."));
    if(phase.value(QStringLiteral("delayMs"),0).toInt()<0 || phase.value(QStringLiteral("staggerMs"),0).toInt()<0)
        warn(QStringLiteral("text.effects.timing"),QObject::tr("atraso/stagger não podem ser negativos e serão normalizados."));
    if(loop==QLatin1String("count") && phase.value(QStringLiteral("loopCount"),1).toInt()<1)
        warn(QStringLiteral("text.effects.count"),QObject::tr("N vezes precisa ser pelo menos 1."));
    for(const QString& key:{QStringLiteral("opacityFrom"),QStringLiteral("opacityTo")}) {
        if(phase.contains(key)) {const double v=phase.value(key).toDouble(); if(v<0.0||v>1.0)
            warn(QStringLiteral("text.effects.opacity"),QObject::tr("opacidade deve ficar entre 0 e 1 e será limitada."));}
    }
    for(const QString& key:{QStringLiteral("scaleXFrom"),QStringLiteral("scaleXTo"),QStringLiteral("scaleYFrom"),QStringLiteral("scaleYTo")}) {
        if(phase.contains(key) && phase.value(key).toDouble()<=0.0)
            warn(QStringLiteral("text.effects.scale"),QObject::tr("escala precisa ser maior que zero e será normalizada."));
    }
    if(phase.contains(QStringLiteral("frequency")) && phase.value(QStringLiteral("frequency")).toDouble()<=0.0)
        warn(QStringLiteral("text.effects.frequency"),QObject::tr("frequência precisa ser maior que zero e será normalizada."));
}

void validateRichTextParams(const Editor& ed, const EventCommand& command,
                            const QString& location, ProjectValidationResult& result)
{
    QStringList texts;
    QVariantMap styleParams = command.params;
    if (command.type == QLatin1String("message") || command.type == QLatin1String("subtitle.show") || command.type == QLatin1String("subtitle.enqueue")) {
        texts.push_back(command.params.value(QStringLiteral("text")).toString());
    } else if (command.type == QLatin1String("choice.show")) {
        const QVariant choicesValue = command.params.value(QStringLiteral("choices"));
        for (const QString& v : choicesValue.toStringList()) texts.push_back(v);
        if (texts.isEmpty()) for (const QVariant& v : choicesValue.toList()) texts.push_back(v.toString());
    } else if (command.type == QLatin1String("picture.show") || command.type == QLatin1String("picture.showByName")) {
        const QVariantMap rich = command.params.value(QStringLiteral("rich")).toMap();
        if (!rich.isEmpty()) {
            styleParams = rich;
            texts.push_back(rich.value(QStringLiteral("text")).toString());
            const QString family = rich.value(QStringLiteral("fontFamily")).toString().trimmed();
            if (!family.isEmpty() && !projectFontFamilyKnown(ed, family))
                addIssue(result, ValidationSeverity::Warning, QStringLiteral("text.font.reference"), location,
                         QObject::tr("A fonte ‘%1’ usada pela Picture de texto não está cadastrada nas Fontes do projeto; ela pode faltar no Player exportado.").arg(family));
        }
    }

    static const QRegularExpression fnRe(QStringLiteral(R"(\\FN\[([^\]]+)\])"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression grRe(QStringLiteral(R"(\\GR\[([^\]]*)\])"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& text : texts) {
        auto it = fnRe.globalMatch(text);
        while (it.hasNext()) {
            const QString family = it.next().captured(1).trimmed();
            if (!projectFontFamilyKnown(ed, family))
                addIssue(result, ValidationSeverity::Warning, QStringLiteral("text.font.reference"), location,
                         QObject::tr("A fonte ‘%1’ usada por \\FN[...] não está cadastrada nas Fontes do projeto; ela pode faltar no Player exportado.").arg(family));
        }
        auto git = grRe.globalMatch(text);
        while (git.hasNext()) {
            const QString spec = git.next().captured(1).trimmed();
            if (spec.isEmpty() || spec.compare(QLatin1String("off"),Qt::CaseInsensitive)==0) continue;
            const QStringList parts=spec.split(QLatin1Char(','),Qt::SkipEmptyParts);
            int firstColor=0;
            if(!parts.isEmpty()) {
                const QString dir=parts.first().trimmed().toLower();
                if(dir==QLatin1String("vertical")||dir==QLatin1String("horizontal")||dir==QLatin1String("diagonal")) firstColor=1;
            }
            int validColors=0;
            for(int i=firstColor;i<parts.size();++i) if(QColor(parts[i].trimmed()).isValid()) ++validColors;
            if(validColors<2)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("text.gradient.inline.invalid"),location,
                         QObject::tr("O código \\GR[...] precisa de pelo menos duas cores válidas ou \\GR[off]."));
        }
    }

    QVariantMap gradientMap;
    if (command.type == QLatin1String("picture.show")) gradientMap = styleParams.value(QStringLiteral("gradient")).toMap();
    else gradientMap = styleParams.value(QStringLiteral("textGradient")).toMap();
    if (!gradientMap.isEmpty() && !TextGradientSpec::fromVariantMap(gradientMap).enabled())
        addIssue(result, ValidationSeverity::Warning, QStringLiteral("text.gradient.invalid"), location,
                 QObject::tr("O gradiente de texto precisa de pelo menos duas cores válidas."));

    const QVariantMap effectsMap = styleParams.value(QStringLiteral("textEffects")).toMap();
    if (!effectsMap.isEmpty()) {
        const TextEffectStack stack = TextEffectStack::fromVariantMap(effectsMap);
        if (!stack.enabled())
            addIssue(result, ValidationSeverity::Warning, QStringLiteral("text.effects.empty"), location,
                     QObject::tr("O bloco de efeitos de texto não possui nenhuma fase ativa."), true);
        validateTextEffectPhaseRaw(effectsMap.value(QStringLiteral("entrance")).toMap(), QObject::tr("Entrada"), location, result);
        validateTextEffectPhaseRaw(effectsMap.value(QStringLiteral("loop")).toMap(), QObject::tr("Permanência / Loop"), location, result);
        validateTextEffectPhaseRaw(effectsMap.value(QStringLiteral("exit")).toMap(), QObject::tr("Saída"), location, result);
    }
}


void validatePicture2Params(const Editor& ed, const EventCommand& command,
                            const QString& location, ProjectValidationResult& result)
{
    const auto validateNegativeRaw = [&](const QVariantMap& fxMap) {
        const QVariantMap negative = fxMap.value(QStringLiteral("negative")).toMap();
        if (negative.isEmpty()) return;
        const double strength = negative.value(QStringLiteral("strength"), 1.0).toDouble();
        if (!std::isfinite(strength) || strength < 0.0 || strength > 1.0)
            addIssue(result, ValidationSeverity::Warning,
                     QStringLiteral("picture.negative.strength"), location,
                     QObject::tr("A intensidade do negativo precisa ficar entre 0 e 1. O valor será ajustado automaticamente."), true);
    };

    if (command.type == QLatin1String("picture.show") || command.type == QLatin1String("picture.showByName") || command.type == QLatin1String("picture.text")) {
        // O validator olha primeiro os valores RAW. PictureDef::fromParams()
        // normaliza entradas antigas/ruins para manter o Player seguro, mas o
        // autor ainda precisa ser avisado de que o projeto continha valores
        // inválidos em vez de o warning desaparecer depois do clamp.
        const QVariantMap nineRaw = command.params.value(QStringLiteral("nineSlice")).toMap();
        if (!nineRaw.isEmpty()) {
            const int width = nineRaw.value(QStringLiteral("width"), 0).toInt();
            const int height = nineRaw.value(QStringLiteral("height"), 0).toInt();
            const int left = nineRaw.value(QStringLiteral("left"), 8).toInt();
            const int top = nineRaw.value(QStringLiteral("top"), 8).toInt();
            const int right = nineRaw.value(QStringLiteral("right"), 8).toInt();
            const int bottom = nineRaw.value(QStringLiteral("bottom"), 8).toInt();
            if (width < 0 || height < 0)
                addIssue(result, ValidationSeverity::Warning,
                         QStringLiteral("picture.nineslice.size"), location,
                         QObject::tr("O tamanho final do 9-slice não pode ser negativo. O tamanho original será usado."), true);
            if (left < 0 || top < 0 || right < 0 || bottom < 0)
                addIssue(result, ValidationSeverity::Warning,
                         QStringLiteral("picture.nineslice.insets"), location,
                         QObject::tr("Os cortes do 9-slice não podem ser negativos. Valores abaixo de zero serão ajustados."), true);
        }

        const PictureDef def = PictureDef::fromParams(command.params);
        if (def.nineSlice.enabled && !def.rich.enabled) {
            if (const PictureAsset* asset = ed.legacyImport.pictureFor(def.assetId, def.assetName)) {
                if (!def.nineSlice.validFor(asset->image.size()))
                    addIssue(result, ValidationSeverity::Warning,
                             QStringLiteral("picture.nineslice.invalid"), location,
                             QObject::tr("O 9-slice não cabe na imagem ou no tamanho final. A imagem original será usada."), true);
            }
        }
        validateNegativeRaw(command.params.value(QStringLiteral("fx")).toMap());
    } else if (command.type == QLatin1String("picture.effects")) {
        validateNegativeRaw(command.params.value(QStringLiteral("fx")).toMap());
    } else if (command.type == QLatin1String("picture.negative")) {
        const double strength = command.params.value(QStringLiteral("strength"), 1.0).toDouble();
        if (!std::isfinite(strength) || strength < 0.0 || strength > 1.0)
            addIssue(result, ValidationSeverity::Warning, QStringLiteral("picture.negative.strength"), location,
                     QObject::tr("A intensidade do Negative precisa ficar no intervalo 0–1."), true);
        if (command.params.value(QStringLiteral("duration"), 0).toInt() < 0)
            addIssue(result, ValidationSeverity::Warning, QStringLiteral("picture.negative.duration"), location,
                     QObject::tr("A duração da transição Negative não pode ser negativa."), true);
    }
}

bool mapContains(const MapDoc& map, const QPoint& cell)
{
    return cell.x() >= 0 && cell.y() >= 0 &&
           cell.x() < map.map.width && cell.y() < map.map.height;
}

bool recordExists(const Editor& ed, const QString& category, const QString& id)
{
    if (id.isEmpty()) return false;
    for (const DatabaseRecord& record : ed.legacyImport.database.value(category))
        if (record.id == id) return true;
    return false;
}

bool inventoryRecordExists(const Editor& ed, const QString& id)
{
    return recordExists(ed, QStringLiteral("items"), id) ||
           recordExists(ed, QStringLiteral("weapons"), id) ||
           recordExists(ed, QStringLiteral("armors"), id);
}

LayerPtr runtimeMapLayerById(const QVector<LayerPtr>& layers, const QString& id)
{
    if (id.isEmpty()) return {};
    for (const LayerPtr& layer : layers) {
        if (!layer) continue;
        if (layer->id == id) return layer;
        if (const LayerPtr found = runtimeMapLayerById(layer->children, id)) return found;
    }
    return {};
}

const Tileset* runtimeTilesetById(const Editor& ed, const QString& id)
{
    if (id.isEmpty()) return nullptr;
    for (const Tileset& tileset : ed.tilesets)
        if (tileset.id == id) return &tileset;
    return nullptr;
}

bool variableExists(const Editor& ed, int id)
{
    if (id <= 0) return false;
    for (const VariableDef& variable : ed.legacyImport.variables)
        if (variable.id == id) return true;
    return false;
}

bool switchExists(const Editor& ed, int id)
{
    if (id <= 0) return false;
    for (const SwitchDef& sw : ed.legacyImport.switches)
        if (sw.id == id) return true;
    return false;
}

bool stringExists(const Editor& ed, int id)
{
    if (id <= 0) return false;
    for (const StringDef& value : ed.legacyImport.strings)
        if (value.id == id) return true;
    return false;
}

bool commonValueExists(const CommonEvent* common, const QString& id)
{
    if (!common || id.isEmpty()) return false;
    for (const CommonEventParameter& def : common->parameters) if (def.id == id) return true;
    for (const CommonEventLocal& def : common->locals) if (def.id == id) return true;
    return false;
}

bool commonValueTypeForId(const CommonEvent* common, const QString& id, CommonValueType* type)
{
    if (!common || id.isEmpty()) return false;
    for (const CommonEventParameter& def : common->parameters)
        if (def.id == id) { if (type) *type = def.type; return true; }
    for (const CommonEventLocal& def : common->locals)
        if (def.id == id) { if (type) *type = def.type; return true; }
    return false;
}

bool commonLocalTypeForId(const CommonEvent* common, const QString& id, CommonValueType* type)
{
    if (!common || id.isEmpty()) return false;
    for (const CommonEventLocal& def : common->locals)
        if (def.id == id) { if (type) *type = def.type; return true; }
    return false;
}

CommonValueType customDatabaseCommonType(CustomDatabaseFieldType type)
{
    switch (type) {
    case CustomDatabaseFieldType::Number: return CommonValueType::Number;
    case CustomDatabaseFieldType::Boolean: return CommonValueType::Boolean;
    case CustomDatabaseFieldType::Text:
    case CustomDatabaseFieldType::RecordReference: return CommonValueType::Text;
    }
    return CommonValueType::Text;
}

ExpressionContext validationExpressionContext()
{
    ExpressionContext context;
    context.resolveFunction=[](const QString& name,const QVariantList& arguments,bool* handled,QString* error)->QVariant{
        const QString function=name.toLower();
        if(function==QLatin1String("v")||function==QLatin1String("s")||function==QLatin1String("switch")){
            if(handled)*handled=true;if(arguments.size()!=1){if(error)*error=QObject::tr("A função %1 espera um ID.").arg(name);return {};}
            if(function==QLatin1String("s"))return QStringLiteral("texto");
            if(function==QLatin1String("switch"))return false;
            return 0.0;
        }
        if(!isKnownUniversalExpressionFunction(name))return {};
        if(handled)*handled=true;QString queryError;const QVariantMap query=gameValueQueryForExpressionFunction(name,arguments,&queryError);
        if(!queryError.isEmpty()){if(error)*error=queryError;return {};}
        const GameValueDescriptor* descriptor=gameValueDescriptor(query.value(QStringLiteral("key")).toString());
        const CommonValueType type=descriptor?descriptor->type:universalExpressionFunctionType(name);
        if(type==CommonValueType::Text)return QStringLiteral("texto");
        if(type==CommonValueType::Boolean)return false;
        return 0.0;
    };
    return context;
}

void collectExpressionWarnings(const QVariant& value,const QString& location,ProjectValidationResult& result)
{
    const QVariantMap map=value.toMap();
    if(!map.isEmpty()){
        if(map.value(QStringLiteral("source")).toString()==QLatin1String("expression")){
            const ExpressionResult expression=evaluateExpression(map.value(QStringLiteral("expression")).toString(),validationExpressionContext());
            for(const QString& warning:expression.warnings)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.expression.function"),location,warning);
        }
        for(auto it=map.constBegin();it!=map.constEnd();++it)collectExpressionWarnings(it.value(),location,result);
        return;
    }
    for(const QVariant& item:value.toList())collectExpressionWarnings(item,location,result);
}

bool commonSourceMatchesType(const Editor& ed, const QVariantMap& spec, CommonValueType expected,
                             const CommonEvent* commonContext, QString* problem)
{
    const QString source = spec.value(QStringLiteral("source"), QStringLiteral("constant")).toString();
    if (source == QLatin1String("constant")) return true;
    if (source == QLatin1String("expression")) {
        const ExpressionResult expression=evaluateExpression(spec.value(QStringLiteral("expression")).toString(),validationExpressionContext());
        if(!expression.ok()){if(problem)*problem=QObject::tr("Expressão inválida na posição %1: %2").arg(expression.errorPosition+1).arg(expression.error);return false;}
        if(!expression.warnings.isEmpty())return true;
        const int type=expression.value.metaType().id();
        const bool text=type==QMetaType::QString,boolean=type==QMetaType::Bool;
        if((expected==CommonValueType::Text&&!text)||(expected==CommonValueType::Boolean&&!boolean)||(expected==CommonValueType::Number&&(text||boolean))){if(problem)*problem=QObject::tr("O resultado da expressão não corresponde ao tipo esperado.");return false;}
        return true;
    }
    if (source == QLatin1String("variable")) {
        if (!variableExists(ed, spec.value(QStringLiteral("variableId")).toInt())) {
            if (problem) *problem = QObject::tr("A variável global usada como origem não existe.");
            return false;
        }
        if (expected != CommonValueType::Number) {
            if (problem) *problem = QObject::tr("Variáveis Globais são numéricas e só podem alimentar valores do tipo Número.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("switch")) {
        if (!switchExists(ed, spec.value(QStringLiteral("switchId")).toInt())) {
            if (problem) *problem = QObject::tr("O Switch Global usado como origem não existe.");
            return false;
        }
        if (expected != CommonValueType::Boolean) {
            if (problem) *problem = QObject::tr("Switches Globais só podem alimentar valores Booleanos.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("string")) {
        if (!stringExists(ed, spec.value(QStringLiteral("stringId")).toInt())) {
            if (problem) *problem = QObject::tr("A String Global usada como origem não existe.");
            return false;
        }
        if (expected != CommonValueType::Text) {
            if (problem) *problem = QObject::tr("Strings Globais só podem alimentar valores do tipo Texto.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("gameValue")) {
        const QVariantMap query = spec.value(QStringLiteral("query")).toMap();
        const QString key = query.value(QStringLiteral("key")).toString();
        const GameValueDescriptor* descriptor = gameValueDescriptor(key);
        if (!descriptor) {
            if (problem) *problem = QObject::tr("O Valor do Jogo usado como origem é desconhecido.");
            return false;
        }
        if (descriptor->type != expected) {
            if (problem) *problem = QObject::tr("O tipo do Valor do Jogo não corresponde ao tipo esperado.");
            return false;
        }
        const QString queryProblem = gameValueQueryProblem(query);
        if (!queryProblem.isEmpty()) {
            if (problem) *problem = queryProblem;
            return false;
        }
        return true;
    }
    if (source == QLatin1String("databaseField")) {
        const QString databaseId = spec.value(QStringLiteral("databaseId")).toString();
        const CustomDatabaseDefinition* database = ed.legacyImport.customDatabase(databaseId);
        if (!database) { if (problem) *problem = QObject::tr("O Banco de Dados Personalizado usado como origem não existe."); return false; }
        const CustomDatabaseField* field = customDatabaseFieldById(*database, spec.value(QStringLiteral("fieldId")).toString());
        if (!field) { if (problem) *problem = QObject::tr("O campo do Banco de Dados Personalizado usado como origem não existe."); return false; }
        if (customDatabaseCommonType(field->type) != expected) { if (problem) *problem = QObject::tr("O tipo do campo do banco não corresponde ao tipo esperado."); return false; }
        const QVariantMap recordSpec = spec.value(QStringLiteral("recordSpec")).toMap();
        if (!recordSpec.isEmpty()) {
            QString recordProblem;
            if (!commonSourceMatchesType(ed, recordSpec, CommonValueType::Text, commonContext, &recordProblem)) {
                if (problem) *problem = QObject::tr("Registro dinâmico: %1").arg(recordProblem);
                return false;
            }
        } else if (!customDatabaseRecordById(*database, spec.value(QStringLiteral("recordId")).toString())) {
            if (problem) *problem = QObject::tr("O registro do Banco de Dados Personalizado usado como origem não existe.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("random")) {
        if (expected != CommonValueType::Number) {
            if (problem) *problem = QObject::tr("Sorteio só pode alimentar valores do tipo Número.");
            return false;
        }
        if (spec.value(QStringLiteral("maximum"), 0).toInt() < spec.value(QStringLiteral("minimum"), 0).toInt()) {
            if (problem) *problem = QObject::tr("No Sorteio, o valor máximo não pode ser menor que o mínimo.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("stringMetric")) {
        if (expected != CommonValueType::Number) {
            if (problem) *problem = QObject::tr("Conversões/métricas de String só podem alimentar Número.");
            return false;
        }
        if (!stringExists(ed, spec.value(QStringLiteral("stringId")).toInt())) {
            if (problem) *problem = QObject::tr("A String Global usada na conversão não existe.");
            return false;
        }
        const QString operation = spec.value(QStringLiteral("operation"), QStringLiteral("toNumber")).toString();
        const QSet<QString> valid{QStringLiteral("toNumber"),QStringLiteral("length"),QStringLiteral("lineCount"),QStringLiteral("indexOf"),QStringLiteral("count")};
        if (!valid.contains(operation)) {
            if (problem) *problem = QObject::tr("A operação String → Número é desconhecida.");
            return false;
        }
        if ((operation == QLatin1String("indexOf") || operation == QLatin1String("count")) && spec.value(QStringLiteral("needle")).toString().isEmpty()) {
            if (problem) *problem = QObject::tr("A operação de busca na String precisa de um texto para procurar.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("numberText")) {
        if (expected != CommonValueType::Text) {
            if (problem) *problem = QObject::tr("Variável → Texto só pode alimentar valores de Texto.");
            return false;
        }
        if (!variableExists(ed, spec.value(QStringLiteral("variableId")).toInt())) {
            if (problem) *problem = QObject::tr("A Variável Global usada na conversão para Texto não existe.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("switchText")) {
        if (expected != CommonValueType::Text) {
            if (problem) *problem = QObject::tr("Switch → Texto só pode alimentar valores de Texto.");
            return false;
        }
        if (!switchExists(ed, spec.value(QStringLiteral("switchId")).toInt())) {
            if (problem) *problem = QObject::tr("O Switch Global usado na conversão para Texto não existe.");
            return false;
        }
        return true;
    }
    if (source == QLatin1String("commonValue")) {
        CommonValueType actual = CommonValueType::Number;
        if (!commonValueTypeForId(commonContext, spec.value(QStringLiteral("commonValueId")).toString(), &actual)) {
            if (problem) *problem = QObject::tr("O parâmetro/local usado como origem não existe neste Evento Comum.");
            return false;
        }
        if (actual != expected) {
            if (problem) *problem = QObject::tr("O tipo do parâmetro/local de origem não corresponde ao tipo esperado.");
            return false;
        }
        return true;
    }
    if (problem) *problem = QObject::tr("A origem do valor é desconhecida.");
    return false;
}


QVariantMap normalizedCommonSourceSpec(const QVariantMap& params)
{
    QVariantMap spec = params.value(QStringLiteral("sourceSpec")).toMap();
    if (!spec.isEmpty()) return spec;

    // Compatibilidade com protótipos/arquivos intermediários da RC2.42 que
    // gravavam a origem diretamente no comando, antes de `sourceSpec` virar
    // o contrato único. O runtime aceita esse formato; o Validator precisa
    // aceitar exatamente a mesma forma para não produzir falso positivo.
    QString source = params.value(QStringLiteral("source")).toString();
    if (source.isEmpty() && params.contains(QStringLiteral("commonValueId")))
        source = QStringLiteral("commonValue");
    if (source.isEmpty()) source = QStringLiteral("constant");
    spec[QStringLiteral("source")] = source;
    if (params.contains(QStringLiteral("value")))
        spec[QStringLiteral("value")] = params.value(QStringLiteral("value"));
    if (params.contains(QStringLiteral("variableId")))
        spec[QStringLiteral("variableId")] = params.value(QStringLiteral("variableId"));
    if (params.contains(QStringLiteral("switchId")))
        spec[QStringLiteral("switchId")] = params.value(QStringLiteral("switchId"));
    if (params.contains(QStringLiteral("stringId")))
        spec[QStringLiteral("stringId")] = params.value(QStringLiteral("stringId"));
    if (params.contains(QStringLiteral("commonValueId")))
        spec[QStringLiteral("commonValueId")] = params.value(QStringLiteral("commonValueId"));
    if (params.contains(QStringLiteral("query")))
        spec[QStringLiteral("query")] = params.value(QStringLiteral("query"));
    if (params.contains(QStringLiteral("minimum")))
        spec[QStringLiteral("minimum")] = params.value(QStringLiteral("minimum"));
    if (params.contains(QStringLiteral("maximum")))
        spec[QStringLiteral("maximum")] = params.value(QStringLiteral("maximum"));
    return spec;
}

bool valueTargetMatchesType(const Editor& ed, const QVariantMap& target, CommonValueType expected,
                            const CommonEvent* commonContext, QString* problem)
{
    const QString kind = target.value(QStringLiteral("target"), QStringLiteral("none")).toString();
    if (kind == QLatin1String("none")) return true;
    if (kind == QLatin1String("variable")) {
        if (expected != CommonValueType::Number) { if (problem) *problem = QObject::tr("Variável Global só recebe Número."); return false; }
        if (!variableExists(ed, target.value(QStringLiteral("id")).toInt())) { if (problem) *problem = QObject::tr("A Variável Global de destino não existe."); return false; }
        return true;
    }
    if (kind == QLatin1String("switch")) {
        if (expected != CommonValueType::Boolean) { if (problem) *problem = QObject::tr("Switch Global só recebe Booleano."); return false; }
        if (!switchExists(ed, target.value(QStringLiteral("id")).toInt())) { if (problem) *problem = QObject::tr("O Switch Global de destino não existe."); return false; }
        return true;
    }
    if (kind == QLatin1String("string")) {
        if (expected != CommonValueType::Text) { if (problem) *problem = QObject::tr("String Global só recebe Texto."); return false; }
        if (!stringExists(ed, target.value(QStringLiteral("id")).toInt())) { if (problem) *problem = QObject::tr("A String Global de destino não existe."); return false; }
        return true;
    }
    if (kind == QLatin1String("commonValue")) {
        CommonValueType actual = CommonValueType::Number;
        if (!commonLocalTypeForId(commonContext, target.value(QStringLiteral("commonValueId")).toString(), &actual)) {
            if (problem) *problem = QObject::tr("A variável local de destino não existe neste Evento Comum."); return false;
        }
        if (actual != expected) { if (problem) *problem = QObject::tr("O tipo da variável local de destino não corresponde ao valor."); return false; }
        return true;
    }
    if (problem) *problem = QObject::tr("O destino do valor é desconhecido.");
    return false;
}

bool assetExists(const Editor& ed, const QString& source)
{
    if (source.trimmed().isEmpty()) return true;
    const QFileInfo info(source);
    return info.isAbsolute() ? info.exists()
                             : QFileInfo::exists(QDir(ed.projectRoot()).filePath(source));
}

} // namespace

void validateMoveRouteDefinition(const MoveRoute& route, const QString& location,
                                 ProjectValidationResult& result, bool autonomousPageRoute)
{
    const QString target=route.target.trimmed();
    const bool validTarget = target == QLatin1String("self") || target == QLatin1String("player") ||
        (target.startsWith(QLatin1String("event:")) && target.size() > 6);
    if(!validTarget)
        addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.target"),location,
                 QObject::tr("A rota possui um alvo inválido. Use Este Evento, Jogador ou um Evento válido."));
    if(route.commands.isEmpty())
        addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.empty"),location,
                 QObject::tr("A rota de movimento não possui nenhum passo."));
    if(route.repeat && route.waitForCompletion && !autonomousPageRoute)
        addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.infiniteWait"),location,
                 QObject::tr("A rota está em repetição e também em ‘Esperar terminar’. O evento ficará esperando até a rota ser cancelada explicitamente."));
    if(autonomousPageRoute && target != QLatin1String("self"))
        addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.autonomousTarget"),location,
                 QObject::tr("Rota personalizada de página sempre move o próprio evento; o alvo salvo será ignorado."),true);
    if(autonomousPageRoute && route.startMode != MoveRouteStartMode::Replace)
        addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.autonomousQueue"),location,
                 QObject::tr("Rota personalizada de página não usa fila. O modo de início será tratado como Substituir."),true);
    if(autonomousPageRoute && route.waitForCompletion)
        addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.autonomousWait"),location,
                 QObject::tr("Rotas personalizadas de página não usam “Esperar terminar”. Essa opção será removida automaticamente."),true);

    for(int i=0;i<route.commands.size();++i){
        const MoveCommand& command=route.commands.at(i);
        const QString step=QObject::tr("%1 / passo %2").arg(location).arg(i+1);
        if(command.type.compare(QLatin1String("script"),Qt::CaseInsensitive)==0){
            addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.script"),step,
                     QObject::tr("Rotas da LUDO são No-Code e não aceitam comandos de script."));
            continue;
        }
        if(!isKnownMoveRouteCommandType(command.type)){
            addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.unknown"),step,
                     QObject::tr("Passo de rota desconhecido: %1. Atualize/remova este passo antes de exportar.").arg(command.type));
            continue;
        }
        if(command.type==QLatin1String("rememberPosition")&&!autonomousPageRoute&&target==QLatin1String("player"))
            addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.remember.player"),step,
                     QObject::tr("Lembrar Posição pertence a Eventos. Escolha Este Evento ou um Evento específico como alvo da rota."));
        const QVariantMap& params=command.params;
        if(command.type==QLatin1String("wait") && params.contains(QStringLiteral("frames")) &&
           params.value(QStringLiteral("frames")).toInt()<=0)
            addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.wait"),step,
                     QObject::tr("A espera precisa ter pelo menos 1 quadro. O valor será ajustado automaticamente."),true);
        else if(command.type==QLatin1String("speed") && params.contains(QStringLiteral("value"))){
            const double value=params.value(QStringLiteral("value")).toDouble();
            if(value<0.25||value>20.0)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.speed"),step,
                         QObject::tr("A velocidade precisa ficar entre 0,25 e 20. O valor será ajustado automaticamente."),true);
        } else if(command.type==QLatin1String("frequency") && params.contains(QStringLiteral("value"))){
            const int value=params.value(QStringLiteral("value")).toInt();
            if(value<1||value>5)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.frequency"),step,
                         QObject::tr("A frequência precisa ficar entre 1 e 5. O valor será ajustado automaticamente."),true);
        } else if(command.type==QLatin1String("pathfind")){
            const QString rawBehavior=params.value(QStringLiteral("behavior"),QStringLiteral("reach")).toString().trimmed();
            const QString rawTarget=params.value(QStringLiteral("targetKind"),QStringLiteral("cell")).toString().trimmed();
            const bool validBehavior=rawBehavior.compare(QLatin1String("reach"),Qt::CaseInsensitive)==0||
                rawBehavior.compare(QLatin1String("follow"),Qt::CaseInsensitive)==0||
                rawBehavior.compare(QLatin1String("flee"),Qt::CaseInsensitive)==0||
                rawBehavior.compare(QLatin1String("keepDistance"),Qt::CaseInsensitive)==0;
            const bool validTarget=rawTarget.compare(QLatin1String("cell"),Qt::CaseInsensitive)==0||
                rawTarget.compare(QLatin1String("player"),Qt::CaseInsensitive)==0||
                rawTarget.compare(QLatin1String("sourceEvent"),Qt::CaseInsensitive)==0||
                rawTarget.compare(QLatin1String("event"),Qt::CaseInsensitive)==0;
            if(!validBehavior)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.behavior"),step,
                         QObject::tr("O comportamento de pathfinding é desconhecido: %1.").arg(rawBehavior));
            if(!validTarget)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.targetKind"),step,
                         QObject::tr("O tipo de alvo do pathfinding é desconhecido: %1.").arg(rawTarget));
            const MoveRoutePathOptions path=moveRoutePathOptionsFromCommand(command);
            if(path.targetKind==MoveRoutePathTargetKind::Event&&path.eventId.isEmpty())
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.event"),step,
                         QObject::tr("O pathfinding aponta para um Evento sem ID."));
            if(path.targetKind==MoveRoutePathTargetKind::Cell&&(path.cell.x()<0||path.cell.y()<0))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.cell"),step,
                         QObject::tr("O destino do pathfinding possui coordenada negativa."));
            if(path.targetKind==MoveRoutePathTargetKind::SourceEvent&&autonomousPageRoute)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.source.autonomous"),step,
                         QObject::tr("Rota autônoma não possui Evento Chamador. Escolha Jogador, Evento ou Tile do mapa."));
            if(path.targetKind!=MoveRoutePathTargetKind::Cell&&
               (path.behavior==MoveRoutePathBehavior::Reach||path.behavior==MoveRoutePathBehavior::Follow)&&
               path.maxDistance==0)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.actorExact"),step,
                         QObject::tr("O pathfinding tenta ocupar exatamente o mesmo tile de outro ator. Se o alvo for sólido, prefira tolerância de 1 tile."));
            const int rawMin=params.value(QStringLiteral("minDistance"),0).toInt();
            const int rawMax=params.value(QStringLiteral("maxDistance"),path.maxDistance).toInt();
            if(path.behavior==MoveRoutePathBehavior::KeepDistance&&rawMax<rawMin)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.distance"),step,
                         QObject::tr("A distância máxima é menor que a mínima. A faixa será corrigida automaticamente."),true);
            const int rawSearch=params.value(QStringLiteral("maxSearchNodes"),4096).toInt();
            if(rawSearch<64||rawSearch>65536)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.searchLimit"),step,
                         QObject::tr("O limite de busca do caminho está fora do intervalo permitido. O valor será ajustado automaticamente."),true);
            const bool expectedContinuous=path.behavior==MoveRoutePathBehavior::Follow||
                                          path.behavior==MoveRoutePathBehavior::KeepDistance;
            if(params.contains(QStringLiteral("continuous"))&&params.value(QStringLiteral("continuous")).toBool()!=expectedContinuous)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.continuous.normalized"),step,
                         QObject::tr("O modo contínuo não corresponde ao comportamento escolhido e será normalizado automaticamente."),true);
            if(path.continuous&&i+1<route.commands.size())
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.unreachableAfterContinuous"),step,
                         QObject::tr("Há passos depois de um pathfinding contínuo. Esses passos não serão alcançados enquanto a rota atual permanecer ativa."));
            const QString routeTarget=route.target.trimmed();
            const bool selfPlayer=routeTarget==QLatin1String("player")&&path.targetKind==MoveRoutePathTargetKind::Player;
            const bool selfSource=routeTarget==QLatin1String("self")&&path.targetKind==MoveRoutePathTargetKind::SourceEvent;
            const bool selfEvent=routeTarget.startsWith(QLatin1String("event:"))&&path.targetKind==MoveRoutePathTargetKind::Event&&
                                 routeTarget.mid(6)==path.eventId;
            if(selfPlayer||selfSource||selfEvent)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.selfTarget"),step,
                         QObject::tr("O alvo do pathfinding é o próprio ator controlado; a rota não produzirá deslocamento útil."));
            if(path.continuous&&route.waitForCompletion&&!autonomousPageRoute)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("move.route.path.continuousWait"),step,
                         QObject::tr("Um pathfinding contínuo com ‘Esperar terminar’ só termina quando a rota for cancelada por outro comando/evento."));
        } else if((command.type==QLatin1String("switchOn")||command.type==QLatin1String("switchOff")) &&
                  params.value(QStringLiteral("id")).toInt()<=0)
            addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.switch"),step,
                     QObject::tr("O passo usa um interruptor inválido."));
    }
}

bool staticNumberValue(const QVariant& raw, double* value)
{
    if (isValueSpec(raw)) {
        const QVariantMap spec=raw.toMap();
        if (spec.value(QStringLiteral("source"),QStringLiteral("constant")).toString()!=QLatin1String("constant")) return false;
        bool ok=false;const double number=spec.value(QStringLiteral("value")).toDouble(&ok);if(!ok)return false;if(value)*value=number;return true;
    }
    bool ok=false;const double number=raw.toDouble(&ok);if(!ok)return false;if(value)*value=number;return true;
}

void validateEventCommands(const Editor& ed, const QVector<EventCommand>& commands,
                      const QString& location, ProjectValidationResult& result,
                      const MapDoc* contextMap, const CommonEvent* contextCommon,
                      bool pageActivationReachable)
{
    QSet<QString> labels;
    QSet<QString> duplicateLabels;
    QSet<QString> pictureNames;
    QSet<QString> duplicatePictureNames;
    QSet<QString> pictureGroups;
    QSet<QString> pictureTimelines;
    for (const EventCommand& command : commands)
        if (command.type == QLatin1String("label")) {
            const QString name = command.params.value(QStringLiteral("name")).toString();
            if (!name.isEmpty() && labels.contains(name)) duplicateLabels.insert(name);
            else if (!name.isEmpty()) labels.insert(name);
        } else if(command.type==QLatin1String("picture.show")||command.type==QLatin1String("picture.showByName")) {
            const QString name=command.params.value(QStringLiteral("logicalName")).toString().trimmed().toCaseFolded();
            if(!name.isEmpty()){if(pictureNames.contains(name))duplicatePictureNames.insert(name);else pictureNames.insert(name);}
            const QString group=command.params.value(QStringLiteral("group")).toString().trimmed();if(!group.isEmpty())pictureGroups.insert(group);
        } else if(command.type==QLatin1String("picture.setGroup")) {
            const QString group=command.params.value(QStringLiteral("group")).toString().trimmed();if(!group.isEmpty())pictureGroups.insert(group);
        } else if(command.type==QLatin1String("picture.timeline.define")) {
            const QString name=command.params.value(QStringLiteral("name")).toString().trimmed().toCaseFolded();if(!name.isEmpty())pictureTimelines.insert(name);
        }
    int ifDepth = 0;
    int loopDepth = 0;
    int repeatDepth = 0;
    int databaseEachDepth = 0;
    int parallelDepth = 0;
    int cutsceneDepth = 0;
    QVariantMap pendingCutsceneSettings;
    for (int i = 0; i < commands.size(); ++i) {
        const EventCommand& command = commands[i];
        const QString commandLocation = QObject::tr("%1 / comando %2").arg(location).arg(i + 1);
        const bool conditionCommand = command.type == QLatin1String("if") || command.type == QLatin1String("wait.until");
        collectExpressionWarnings(command.params,commandLocation,result);
        if(command.type.startsWith(QLatin1String("picture."))){
            const QString logicalName=command.params.value(QStringLiteral("logicalName")).toString().trimmed();
            if(command.type==QLatin1String("picture.showByName")&&logicalName.isEmpty())
                addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.logicalName.empty"),commandLocation,QObject::tr("Mostrar por nome lógico exige um nome."));
            if(!logicalName.isEmpty()&&duplicatePictureNames.contains(logicalName.toCaseFolded()))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.logicalName.duplicate"),commandLocation,QObject::tr("O nome lógico ‘%1’ está duplicado nesta lista de eventos.").arg(logicalName));
            for(const QString& key:{QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("opacity")}){
                if(!command.params.contains(key))continue;
                QVariantMap spec=command.params.value(key).toMap();
                if(spec.isEmpty()&&command.params.value(key).metaType().id()==QMetaType::QString&&looksLikeExpression(command.params.value(key).toString()))
                    spec={{QStringLiteral("source"),QStringLiteral("expression")},{QStringLiteral("expression"),command.params.value(key).toString()}};
                if(!spec.isEmpty()&&spec.contains(QStringLiteral("source"))){QString problem;if(!commonSourceMatchesType(ed,spec,CommonValueType::Number,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.dynamic.invalid"),commandLocation,QObject::tr("Valor dinâmico %1: %2").arg(key,problem));}
            }
            if(command.type==QLatin1String("picture.setGroup")||command.type==QLatin1String("picture.moveGroup")||command.type==QLatin1String("picture.eraseGroup")){
                const QString group=command.params.value(QStringLiteral("group")).toString().trimmed();
                if(group.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.group.empty"),commandLocation,QObject::tr("O comando exige um nome de grupo."));
                else if(command.type!=QLatin1String("picture.setGroup")&&!pictureGroups.contains(group))addIssue(result,ValidationSeverity::Warning,QStringLiteral("picture.group.unknown"),commandLocation,QObject::tr("O grupo “%1” não é criado nesta lista. Confirme se ele já existe no jogo.").arg(group));
            }
            if(command.type==QLatin1String("picture.attach")){
                const int number=command.params.value(QStringLiteral("number"),0).toInt();
                const int parentId=command.params.value(QStringLiteral("parentId"),0).toInt();
                const QString target=command.params.value(QStringLiteral("target")).toString().trimmed();
                const QString axis=command.params.value(QStringLiteral("followAxis"),QStringLiteral("both")).toString();
                if(axis!=QLatin1String("both")&&axis!=QLatin1String("x")&&axis!=QLatin1String("y"))addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.attach.axis"),commandLocation,QObject::tr("O eixo de acompanhamento é inválido."));
                if(parentId==number&&number>0)addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.attach.self"),commandLocation,QObject::tr("Uma Picture não pode ser filha dela mesma."));
                if(parentId<=0&&target.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.attach.target"),commandLocation,QObject::tr("Informe player, event:ID ou picture:nome como alvo."));
                else if(target.startsWith(QLatin1String("event:"))){const QString id=target.mid(6);bool found=false;if(contextMap)for(const MapEvent& event:contextMap->events)if(event.id==id){found=true;break;}if(contextMap&&!found)addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.attach.event"),commandLocation,QObject::tr("O evento alvo ‘%1’ não existe neste mapa.").arg(id));}
                else if(target.startsWith(QLatin1String("picture:"))){const QString parentName=target.mid(8).trimmed().toCaseFolded();bool numeric=false;target.mid(8).toInt(&numeric);if(!numeric&&!pictureNames.contains(parentName))addIssue(result,ValidationSeverity::Warning,QStringLiteral("picture.attach.picture"),commandLocation,QObject::tr("A imagem pai “%1” não é criada nesta lista. Confirme se ela já existe no jogo.").arg(target.mid(8)));}
                else if(!target.isEmpty()&&target!=QLatin1String("player"))addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.attach.target"),commandLocation,QObject::tr("Alvo inválido. Use player, event:ID ou picture:nome."));
            }
            if(command.type==QLatin1String("picture.timeline.define")){const QString name=command.params.value(QStringLiteral("name")).toString().trimmed();const QVariantList frames=command.params.value(QStringLiteral("keyframes")).toList();if(name.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.timeline.name"),commandLocation,QObject::tr("A timeline precisa de um nome."));if(command.params.value(QStringLiteral("duration")).toInt()<=0)addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.timeline.duration"),commandLocation,QObject::tr("A timeline precisa ter duração maior que zero."));if(frames.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.timeline.keyframes"),commandLocation,QObject::tr("A timeline precisa de pelo menos um keyframe."));}
            if(command.type==QLatin1String("picture.timeline.play")){const QString name=command.params.value(QStringLiteral("name")).toString().trimmed();if(name.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.timeline.name"),commandLocation,QObject::tr("Informe a timeline a reproduzir."));else if(!pictureTimelines.contains(name.toCaseFolded()))addIssue(result,ValidationSeverity::Warning,QStringLiteral("picture.timeline.unknown"),commandLocation,QObject::tr("A timeline ‘%1’ não é definida nesta lista; confirme se um evento anterior já a definiu.").arg(name));}
            if(command.type==QLatin1String("picture.onClick")||command.type==QLatin1String("picture.onTouch")){const QString id=command.params.value(QStringLiteral("commonEventId")).toString();if(!id.isEmpty()){bool found=false;for(const CommonEvent& event:ed.legacyImport.commonEvents)if(event.id==id){found=true;break;}if(!found)addIssue(result,ValidationSeverity::Error,QStringLiteral("picture.interaction.commonEvent"),commandLocation,QObject::tr("O Evento Comum vinculado à Picture não existe."));}}
        }
        if(command.type==QLatin1String("message")){const DialogueContent dialogue=DialogueContent::fromVariantMap(command.params.value(QStringLiteral("dialogueContent")).toMap());if(!dialogue.speakerId.isEmpty()&&!ed.legacyImport.speakerDatabase.findById(dialogue.speakerId))addIssue(result,ValidationSeverity::Error,QStringLiteral("dialogue.speaker.missing"),commandLocation,QObject::tr("O perfil de personagem “%1” não existe no banco de personagens.").arg(dialogue.speakerId));const QString overflow=command.params.value(QStringLiteral("overflow"),QStringLiteral("scroll")).toString();const QSet<QString> modes{QStringLiteral("scroll"),QStringLiteral("shrink"),QStringLiteral("truncate")};if(!modes.contains(overflow))addIssue(result,ValidationSeverity::Error,QStringLiteral("dialogue.overflow.invalid"),commandLocation,QObject::tr("Escolha como tratar texto excedente: rolar, reduzir ou cortar."));QRegularExpression loc(QStringLiteral("\\\\LOC\\[([^\\]]*)\\]"),QRegularExpression::CaseInsensitiveOption);auto it=loc.globalMatch(command.params.value(QStringLiteral("text")).toString());while(it.hasNext()){const auto match=it.next();const QString key=match.captured(1).section(QLatin1Char('|'),0,0).trimmed();if(key.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("dialogue.loc.empty"),commandLocation,QObject::tr("Uma referência de tradução possui uma chave vazia."));else if(!ed.legacyImport.localization.hasKey(key))addIssue(result,ValidationSeverity::Warning,QStringLiteral("dialogue.loc.missing"),commandLocation,QObject::tr("A chave de tradução “%1” ainda não existe. O texto original será usado.").arg(key));}}
        if(command.type==QLatin1String("dialogue.fastForward")){const double speed=command.params.value(QStringLiteral("speed"),1.0).toDouble();if(!std::isfinite(speed)||speed<1.0||speed>20.0)addIssue(result,ValidationSeverity::Error,QStringLiteral("dialogue.fastForward.invalid"),commandLocation,QObject::tr("A velocidade do avanço rápido deve ficar entre 1× e 20×."));}
        if(command.type==QLatin1String("subtitle.show")||command.type==QLatin1String("subtitle.enqueue")||command.type==QLatin1String("subtitle.clearQueue")){const QString track=command.params.value(QStringLiteral("track"),QStringLiteral("dialogue")).toString();const QSet<QString> tracks{QStringLiteral("dialogue"),QStringLiteral("notification"),QStringLiteral("system"),QStringLiteral("custom1"),QStringLiteral("custom2")};if(!tracks.contains(track))addIssue(result,ValidationSeverity::Error,QStringLiteral("subtitle.track.invalid"),commandLocation,QObject::tr("O canal de legendas “%1” não existe.").arg(track));if(command.type!=QLatin1String("subtitle.clearQueue")&&command.params.value(QStringLiteral("duration"),-1).toInt()<-1)addIssue(result,ValidationSeverity::Error,QStringLiteral("subtitle.duration.invalid"),commandLocation,QObject::tr("A duração deve ser -1 (padrão), 0 (automática) ou positiva."));}
        if(command.type==QLatin1String("bubble.show")||command.type==QLatin1String("notification.show")){
            if(command.params.value(QStringLiteral("text")).toString().trimmed().isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("bubble.text.empty"),commandLocation,QObject::tr("Informe o texto do balão ou da notificação."));
            if(command.params.value(QStringLiteral("duration"),180).toInt()<=0)addIssue(result,ValidationSeverity::Error,QStringLiteral("bubble.duration.invalid"),commandLocation,QObject::tr("A duração precisa ser maior que zero."));
            for(const QString& key:{QStringLiteral("bgColor"),QStringLiteral("textColor")})if(command.params.contains(key)&&!QColor(command.params.value(key).toString()).isValid())addIssue(result,ValidationSeverity::Error,QStringLiteral("bubble.color.invalid"),commandLocation,QObject::tr("A cor ‘%1’ não é válida.").arg(command.params.value(key).toString()));
            if(command.type==QLatin1String("notification.show")){const QSet<QString> positions{QStringLiteral("top-left"),QStringLiteral("top-center"),QStringLiteral("top-right"),QStringLiteral("bottom-left"),QStringLiteral("bottom-center"),QStringLiteral("bottom-right")};const QString position=command.params.value(QStringLiteral("position"),QStringLiteral("top-right")).toString();if(!positions.contains(position))addIssue(result,ValidationSeverity::Error,QStringLiteral("notification.position.invalid"),commandLocation,QObject::tr("A posição da notificação é inválida."));}
        }
        if(command.type==QLatin1String("bubble.show")||command.type==QLatin1String("bubble.hide")){const QString target=command.params.value(QStringLiteral("target"),QStringLiteral("self")).toString();if(target!=QLatin1String("self")&&target!=QLatin1String("player")&&!target.startsWith(QLatin1String("event:")))addIssue(result,ValidationSeverity::Error,QStringLiteral("bubble.target.invalid"),commandLocation,QObject::tr("Escolha o jogador, este evento ou outro evento do mapa como alvo do balão."));if(target.startsWith(QLatin1String("event:"))&&contextMap){bool found=false;const QString id=target.mid(6);for(const MapEvent& event:contextMap->events)if(event.id==id){found=true;break;}if(!found)addIssue(result,ValidationSeverity::Error,QStringLiteral("bubble.target.missing"),commandLocation,QObject::tr("O evento alvo ‘%1’ não existe neste mapa.").arg(id));}}
        if(command.type.startsWith(QLatin1String("portrait."))||command.type==QLatin1String("voice.play")){const QString speakerId=command.params.value(QStringLiteral("speakerId")).toString();if(speakerId.isEmpty()||!ed.legacyImport.speakerDatabase.findById(speakerId))addIssue(result,ValidationSeverity::Error,QStringLiteral("speaker.command.missing"),commandLocation,QObject::tr("Escolha um personagem existente no banco de personagens."));}
        if(command.type==QLatin1String("voice.play")&&command.params.value(QStringLiteral("lineId")).toString().trimmed().isEmpty()&&command.params.value(QStringLiteral("source")).toString().trimmed().isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("voice.line.empty"),commandLocation,QObject::tr("Informe o ID da fala ou um arquivo de voz."));
        QVector<QVariantMap> conditionPredicates;
        if(conditionCommand){
            const QVariantMap explicitTree=command.params.value(QStringLiteral("conditionTree")).toMap();
            if(!explicitTree.isEmpty()){
                const QString treeProblem=conditionTreeStructureProblem(explicitTree,64);
                if(!treeProblem.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("event.conditionTree.structure"),commandLocation,treeProblem);
                conditionPredicates=conditionTreePredicates(explicitTree);
                if(conditionTreeMaxDepth(explicitTree)>10)
                    addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.conditionTree.depth"),commandLocation,QObject::tr("A árvore ultrapassa 10 níveis. Simplifique os grupos para manter edição e execução previsíveis."));
                static const QSet<QString> knownKinds={QStringLiteral("switch"),QStringLiteral("selfSwitch"),QStringLiteral("variable"),QStringLiteral("string"),QStringLiteral("commonValue"),QStringLiteral("random"),QStringLiteral("playerPosition"),QStringLiteral("direction"),QStringLiteral("distance"),QStringLiteral("routeRunning"),QStringLiteral("map"),QStringLiteral("button"),QStringLiteral("gold"),QStringLiteral("item")};
                static const QSet<QString> numericOps={QStringLiteral(">="),QStringLiteral("<="),QStringLiteral("=="),QStringLiteral("!="),QStringLiteral(">"),QStringLiteral("<")};
                static const QSet<QString> textOps={QStringLiteral("=="),QStringLiteral("!="),QStringLiteral("contains"),QStringLiteral("startsWith"),QStringLiteral("endsWith")};
                for(const QVariantMap& predicate:conditionPredicates){
                    const QString kind=predicate.value(QStringLiteral("kind")).toString();
                    if(!knownKinds.contains(kind))
                        addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.conditionTree.atomKind"),commandLocation,QObject::tr("A árvore contém um tipo de condição desconhecido: %1.").arg(kind));
                    if(predicate.contains(QStringLiteral("op"))){const QString op=predicate.value(QStringLiteral("op")).toString();const bool valid=kind==QLatin1String("string")?textOps.contains(op):numericOps.contains(op);if(!valid)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.conditionTree.operator"),commandLocation,QObject::tr("O operador ‘%1’ não é válido para a condição %2.").arg(op,kind));}
                }
            }else conditionPredicates.push_back(command.params);
        }else if(command.type==QLatin1String("choice.show")){
            const QVariantList specs=command.params.value(QStringLiteral("conditions")).toList();
            for(const QVariant& value:specs){const QVariantMap spec=value.toMap();QVariantMap tree=spec.value(QStringLiteral("conditionTree"),spec.value(QStringLiteral("condition"))).toMap();if(tree.isEmpty()&&isConditionTreeNode(spec))tree=spec;if(!tree.isEmpty())conditionPredicates+=conditionTreePredicates(tree);}
        }

        // Bloco 12: os campos dinâmicos são declarados uma vez no GameValueRegistry
        // e validados aqui com o mesmo Value Resolver usado pelo runtime/editor.
        for(const CommandValueFieldDescriptor& field:commandValueFields(command.type)){
            if(!command.params.contains(field.parameter)||!isValueSpec(command.params.value(field.parameter)))continue;
            QString problem;if(!commonSourceMatchesType(ed,command.params.value(field.parameter).toMap(),field.type,contextCommon,&problem))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.valueField.source"),commandLocation,
                         QObject::tr("%1.%2: %3").arg(command.type,field.parameter,problem));
        }

        // Blocos 7+8: parser/schema e intenção de execução são contratos formais.
        // Um Ludo visível inválido nunca pode depender do no-op tolerante do runtime.
        if (command.type == QLatin1String("ludo.command")) {
            LudoCommandParseResult parsed;
            const EventCommand normalized = normalizeLegacyLudoCommand(command, &parsed);
            Q_UNUSED(normalized);
            if (!parsed.valid)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.ludo.invalid"),
                         commandLocation, parsed.message);
            else {
                addIssue(result, ValidationSeverity::Info, QStringLiteral("event.ludo.legacy"),
                         commandLocation, QObject::tr("Este Comando Ludo legado será normalizado para %1 ao salvar/carregar.").arg(parsed.command.type), true);
                if (parsed.command.executionMode == EventExecutionMode::OnPageActivated &&
                    !ludoCommandSupportsPageActivation(parsed.command))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.page.command"),
                             commandLocation, QObject::tr("Este comando Ludo é estrutural e precisa executar dentro do fluxo normal da página; não pode usar OnPageActivated."));
            }
        } else if (command.type == QLatin1String("comment")) {
            const QVector<LudoCommandParseResult> parsed =
                parseLudoCommentCommands(command.params.value(QStringLiteral("text")).toString());
            for (const LudoCommandParseResult& item : parsed) {
                if (item.recognized && !item.valid)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.ludo.comment.invalid"),
                             commandLocation, item.message);
                else if (item.valid && !ludoCommandSupportsPageActivation(item.command))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.page.command"),
                             commandLocation, QObject::tr("Esta tag Ludo é estrutural e não pode executar isoladamente no ciclo OnPageActivated; use os comandos nativos Begin/End no fluxo do evento."));
            }
        }

        if (command.executionMode == EventExecutionMode::OnPageActivated) {
            if (!pageActivationReachable)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.page.nested"),
                         commandLocation, QObject::tr("OnPageActivated só pode existir diretamente na lista de comandos da página; comandos aninhados não participam do ciclo de ativação da página."));
            else if (contextCommon)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.page.common"),
                         commandLocation, QObject::tr("OnPageActivated pertence ao ciclo de vida de uma página de Map Event e não pode ser usado dentro de Evento Comum."));
            else if (!ludoCommandSupportsPageActivation(command) && command.type != QLatin1String("ludo.command"))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.page.command"),
                         commandLocation, QObject::tr("OnPageActivated só é suportado por comandos nativos do Ludo System; use o gatilho da página para outros comandos."));
        } else if (command.executionMode == EventExecutionMode::Autorun ||
                   command.executionMode == EventExecutionMode::Parallel) {
            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.executionMode.scheduler"),
                     commandLocation, QObject::tr("Execução automática e paralela são modos da página ou do Evento Comum, não de um comando isolado. Configure o gatilho do evento em vez de criar outro fluxo."));
        }

        if (!CommandRegistry::isKnown(command.type)) {
            addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.command.unknown"),
                     commandLocation, QObject::tr("Comando desconhecido: %1. Ele poderá ser ignorado durante o jogo; revise antes de exportar.").arg(command.type));
        } else if (CommandRegistry::executionRoute(command.type) == CommandExecutionRoute::LegacyNoOp) {
            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.command.legacy-no-op"),
                     commandLocation, QObject::tr("Este comando legado não produz efeito no Player. Substitua ou remova o comando antes de exportar."));
        }
        validateRichTextParams(ed, command, commandLocation, result);
        validatePicture2Params(ed, command, commandLocation, result);

        // EventExecutionContext: Common Events podem herdar "Este Evento" de
        // um Map Event chamador, mas Autorun/Parallel/reserva independente não
        // possuem esse alvo. O editor não proíbe o uso legítimo; sinaliza a
        // dependência de contexto para impedir no-op silencioso em outro modo.
        if (contextCommon) {
            bool requiresMapEventCaller = command.type == QLatin1String("selfSwitch.set");
            if (command.type == QLatin1String("move.route"))
                requiresMapEventCaller = moveRouteFromMap(command.params.value(QStringLiteral("route")).toMap()).target == QLatin1String("self");
            else if (command.type == QLatin1String("move.route.control"))
                requiresMapEventCaller = command.params.value(QStringLiteral("target"), QStringLiteral("self")).toString() == QLatin1String("self");
            else if (command.type == QLatin1String("ludo.camera.move") || command.type == QLatin1String("ludo.camera.moveOnly"))
                requiresMapEventCaller = command.params.value(QStringLiteral("target"), QStringLiteral("player")).toString() == QLatin1String("self");
            else if (command.type.startsWith(QLatin1String("ludo.sprite.")))
                requiresMapEventCaller = command.params.value(QStringLiteral("target"), QStringLiteral("self")).toString() == QLatin1String("self");
            else if (!conditionPredicates.isEmpty()) {
                for(const QVariantMap& predicate:conditionPredicates){
                    const QString kind=predicate.value(QStringLiteral("kind")).toString();
                    if(kind==QLatin1String("selfSwitch") ||
                       (kind==QLatin1String("routeRunning")&&predicate.value(QStringLiteral("target"),QStringLiteral("player")).toString()==QLatin1String("self")) ||
                       (kind==QLatin1String("distance")&&predicate.value(QStringLiteral("eventId")).toString().isEmpty())){requiresMapEventCaller=true;break;}
                }
            }
            if (requiresMapEventCaller) {
                if (contextCommon->trigger != CommonTrigger::None)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("ctx.self-event-in-common"),
                             commandLocation, QObject::tr("Este comando usa ‘Este Evento’, mas este Evento Comum é executado de forma independente e nunca possui um Map Event chamador."));
                else
                    addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.context.thisEvent.callerRequired"),
                             commandLocation, QObject::tr("Este comando usa ‘Este Evento’. Em um Evento Comum ele só possui alvo quando foi chamado por um Map Event; Autorun, Parallel ou execução independente não têm esse contexto."));
            }
        }
        if ((command.type == QLatin1String("variable.set") || command.type == QLatin1String("switch.set")) &&
            command.params.value(QStringLiteral("source")).toString() == QLatin1String("commonValue")) {
            CommonValueType actual = CommonValueType::Number;
            const QString valueId = command.params.value(QStringLiteral("commonValueId")).toString();
            if (!commonValueTypeForId(contextCommon, valueId, &actual))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.source.missing"), commandLocation,
                         QObject::tr("O comando referencia um parâmetro/local inexistente ou está fora de um Evento Comum."));
            else if (command.type == QLatin1String("variable.set") && actual != CommonValueType::Number)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.source.type"), commandLocation,
                         QObject::tr("Variável Global só pode receber um parâmetro/local do tipo Número."));
            else if (command.type == QLatin1String("switch.set") && actual != CommonValueType::Boolean)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.source.type"), commandLocation,
                         QObject::tr("Switch Global só pode receber um parâmetro/local Booleano."));
        }
        // RC2.43 — B+C+D compartilham o mesmo contrato de valor tipado.
        if (command.type == QLatin1String("switch.set")) {
            if (!switchExists(ed, command.params.value(QStringLiteral("id"), 1).toInt()))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.switch.target"), commandLocation, QObject::tr("O Switch Global de destino não existe."));
            const QString op = command.params.value(QStringLiteral("value"), QStringLiteral("on")).toString();
            if (op != QLatin1String("toggle")) {
                QVariantMap spec = command.params.value(QStringLiteral("sourceSpec")).toMap();
                if (!spec.isEmpty()) {
                    QString problem;
                    if (!commonSourceMatchesType(ed, spec, CommonValueType::Boolean, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.switch.source"), commandLocation, problem);
                }
            }
        }
        if (command.type == QLatin1String("variable.set")) {
            const int first = command.params.value(QStringLiteral("id"), 1).toInt();
            const int last = command.params.value(QStringLiteral("rangeEndId"), first).toInt();
            if (!variableExists(ed, first) || !variableExists(ed, last))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.variable.target"), commandLocation,
                         QObject::tr("A variável inicial/final do comando não existe."));
            const int normalizedLast = qMax(first, last);
            if (normalizedLast - first > 10000)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.variable.rangeSize"), commandLocation,
                         QObject::tr("A faixa de Variáveis não pode exceder 10.001 IDs."));
            else for (int variableId = first; variableId <= normalizedLast; ++variableId)
                if (!variableExists(ed, variableId)) {
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.variable.rangeHole"), commandLocation,
                             QObject::tr("A faixa inclui a Variável %1, que não está definida no projeto.").arg(variableId));
                    break;
                }
            QString problem;
            if (!commonSourceMatchesType(ed, normalizedCommonSourceSpec(command.params), CommonValueType::Number, contextCommon, &problem))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.variable.source"), commandLocation, problem);
        }
        if (command.type == QLatin1String("string.set")) {
            if (!stringExists(ed, command.params.value(QStringLiteral("id"), 1).toInt()))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.target"), commandLocation,
                         QObject::tr("A String Global de destino não existe."));
            const QString op = command.params.value(QStringLiteral("op"), QStringLiteral("=")).toString();
            const QSet<QString> validOps{QStringLiteral("="),QStringLiteral("+"),QStringLiteral("prepend"),QStringLiteral("insert"),
                                         QStringLiteral("replace"),QStringLiteral("remove"),QStringLiteral("clear"),
                                         QStringLiteral("trim"),QStringLiteral("upper"),QStringLiteral("lower"),
                                         QStringLiteral("substring"),QStringLiteral("charAt"),QStringLiteral("lineAt"),
                                         QStringLiteral("firstLine"),QStringLiteral("cutFirstLine"),QStringLiteral("cutFirstChar")};
            if (!validOps.contains(op))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.op"), commandLocation,
                         QObject::tr("A operação da String Global é desconhecida."));
            if (QSet<QString>{QStringLiteral("="),QStringLiteral("+"),QStringLiteral("prepend"),QStringLiteral("insert"),QStringLiteral("replace"),QStringLiteral("remove")}.contains(op)) {
                QString problem;
                if (!commonSourceMatchesType(ed, normalizedCommonSourceSpec(command.params), CommonValueType::Text, contextCommon, &problem))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.source"), commandLocation, problem);
            }
            if (op == QLatin1String("replace") && command.params.value(QStringLiteral("search")).toString().isEmpty())
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.search"), commandLocation, QObject::tr("Substituir texto precisa de um valor não vazio para procurar."));
            if (QSet<QString>{QStringLiteral("insert"),QStringLiteral("substring"),QStringLiteral("charAt"),QStringLiteral("lineAt")}.contains(op) &&
                command.params.value(QStringLiteral("index"), 0).toInt() < 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.index"), commandLocation, QObject::tr("O índice da operação de String não pode ser negativo."));
            if (op == QLatin1String("substring") && command.params.value(QStringLiteral("length"), 1).toInt() < 1)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.string.length"), commandLocation, QObject::tr("O tamanho do trecho precisa ser pelo menos 1."));
        }
        if (command.type == QLatin1String("variable.math")) {
            const int first = command.params.value(QStringLiteral("id"), 1).toInt();
            const int last = command.params.value(QStringLiteral("rangeEndId"), first).toInt();
            if (!variableExists(ed, first) || !variableExists(ed, last))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.target"), commandLocation,
                         QObject::tr("A variável inicial/final do cálculo não existe."));
            const int normalizedLast = qMax(first, last);
            if (normalizedLast - first > 10000)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.rangeSize"), commandLocation,
                         QObject::tr("A faixa do cálculo não pode exceder 10.001 IDs."));
            else for (int variableId = first; variableId <= normalizedLast; ++variableId)
                if (!variableExists(ed, variableId)) {
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.rangeHole"), commandLocation,
                             QObject::tr("A faixa do cálculo inclui a Variável %1, que não está definida no projeto.").arg(variableId));
                    break;
                }
            const QString op = command.params.value(QStringLiteral("operation"), QStringLiteral("add")).toString();
            const QSet<QString> validOps{QStringLiteral("add"),QStringLiteral("sub"),QStringLiteral("mul"),QStringLiteral("div"),
                                         QStringLiteral("mod"),QStringLiteral("min"),QStringLiteral("max"),QStringLiteral("pow"),
                                         QStringLiteral("sqrt"),QStringLiteral("abs"),QStringLiteral("round"),QStringLiteral("floor"),
                                         QStringLiteral("ceil"),QStringLiteral("sin"),QStringLiteral("cos"),QStringLiteral("atan2"),QStringLiteral("clamp"),
                                         QStringLiteral("expression")};
            if (!validOps.contains(op))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.op"), commandLocation, QObject::tr("A operação matemática é desconhecida."));
            if (op == QLatin1String("expression")) {
                const QVariantList terms = command.params.value(QStringLiteral("terms")).toList();
                if (terms.isEmpty() || terms.size() > 32)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.expression.size"), commandLocation,
                             QObject::tr("A expressão visual precisa ter entre 1 e 32 termos."));
                const QSet<QString> stepOps{QStringLiteral("add"),QStringLiteral("sub"),QStringLiteral("mul"),QStringLiteral("div"),
                                            QStringLiteral("mod"),QStringLiteral("min"),QStringLiteral("max"),QStringLiteral("pow")};
                const int limit = qMin(int(terms.size()), 32);
                for (int termIndex = 0; termIndex < limit; ++termIndex) {
                    const QVariantMap term = terms.at(termIndex).toMap();
                    if (termIndex > 0 && !stepOps.contains(term.value(QStringLiteral("op"), QStringLiteral("add")).toString()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.expression.op"), commandLocation,
                                 QObject::tr("A operação do termo %1 da expressão visual é inválida.").arg(termIndex + 1));
                    QString problem;
                    if (!commonSourceMatchesType(ed, term.value(QStringLiteral("sourceSpec")).toMap(), CommonValueType::Number, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.expression.source"), commandLocation,
                                 QObject::tr("Termo %1: %2").arg(termIndex + 1).arg(problem));
                }
            } else {
                const bool unary = QSet<QString>{QStringLiteral("sqrt"),QStringLiteral("abs"),QStringLiteral("round"),QStringLiteral("floor"),QStringLiteral("ceil"),QStringLiteral("sin"),QStringLiteral("cos")}.contains(op);
                for (const QString& field : {QStringLiteral("a"),QStringLiteral("b"),QStringLiteral("c")}) {
                    if (field == QLatin1String("b") && unary) continue;
                    if (field == QLatin1String("c") && op != QLatin1String("clamp")) continue;
                    QString problem;
                    if (!commonSourceMatchesType(ed, command.params.value(field).toMap(), CommonValueType::Number, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.math.source"), commandLocation, QObject::tr("Operando %1: %2").arg(field.toUpper(), problem));
                }
            }
        }
        if (command.type == QLatin1String("value.get")) {
            const QVariantMap query = command.params.value(QStringLiteral("query")).toMap();
            const QString key = query.value(QStringLiteral("key")).toString();
            const GameValueDescriptor* descriptor = gameValueDescriptor(key);
            if (!descriptor)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.gameValue.key"), commandLocation, QObject::tr("O Valor do Jogo selecionado é desconhecido."));
            else {
                const CommonValueType storedType = commonValueTypeFromId(command.params.value(QStringLiteral("valueType"), QStringLiteral("number")).toString());
                if (storedType != descriptor->type)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.gameValue.type"), commandLocation, QObject::tr("O tipo salvo do Valor do Jogo não corresponde ao catálogo da engine."));
                const QString queryProblem = gameValueQueryProblem(query);
                if (!queryProblem.isEmpty())
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.gameValue.query"), commandLocation, queryProblem);
                QString problem;
                if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), descriptor->type, contextCommon, &problem))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.gameValue.target"), commandLocation, problem);
            }
        }
        if (command.type.startsWith(QLatin1String("map.runtime."))) {
            const QString fixedMapId = command.params.value(QStringLiteral("mapId")).toString();
            const MapDoc* runtimeMap = fixedMapId.isEmpty() ? contextMap : ed.mapById(fixedMapId);
            if (!fixedMapId.isEmpty() && !runtimeMap)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.map"), commandLocation,
                         QObject::tr("O mapa selecionado para esta alteração não existe."));

            const auto validateNumber = [&](const QString& key, const QString& code) {
                const QVariant raw = command.params.value(key);
                const QVariantMap spec = raw.toMap();
                if (spec.isEmpty()) return true; // compatibilidade com comandos antigos/constantes crus
                QString problem;
                if (!commonSourceMatchesType(ed, spec, CommonValueType::Number, contextCommon, &problem)) {
                    addIssue(result, ValidationSeverity::Error, code, commandLocation,
                             QObject::tr("%1: %2").arg(key, problem));
                    return false;
                }
                return true;
            };
            const auto constantNumber = [&](const QString& key, int fallback, bool* isConstant = nullptr) {
                const QVariant raw = command.params.value(key);
                const QVariantMap spec = raw.toMap();
                if (spec.isEmpty()) { if (isConstant) *isConstant = true; return raw.isValid() ? raw.toInt() : fallback; }
                if (spec.value(QStringLiteral("source"), QStringLiteral("constant")).toString() == QLatin1String("constant")) {
                    if (isConstant) *isConstant = true;
                    return spec.value(QStringLiteral("value"), fallback).toInt();
                }
                if (isConstant) *isConstant = false;
                return fallback;
            };
            const auto validateLayer = [&](const QString& key, const QString& code) {
                const QString layerId = command.params.value(key).toString();
                if (layerId.isEmpty()) {
                    addIssue(result, ValidationSeverity::Error, code, commandLocation,
                             QObject::tr("Esta alteração precisa de uma camada de tiles."));
                    return false;
                }
                if (runtimeMap) {
                    const LayerPtr layer = runtimeMapLayerById(runtimeMap->layers, layerId);
                    if (!layer || layer->type != LayerType::Tile) {
                        addIssue(result, ValidationSeverity::Error, code, commandLocation,
                                 QObject::tr("A camada selecionada não existe mais ou não é uma camada de tiles."));
                        return false;
                    }
                } else if (fixedMapId.isEmpty() && contextCommon) {
                    addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.runtimeMap.dynamicLayer"), commandLocation,
                             QObject::tr("Este Common Event usa o mapa atual. O ID estável da camada será resolvido no mapa ativo; garanta que esse mapa possua a camada selecionada."));
                }
                return true;
            };
            const auto validateTile = [&](const QString& code) {
                const QString tilesetId = command.params.value(QStringLiteral("tilesetId")).toString();
                const Tileset* tileset = runtimeTilesetById(ed, tilesetId);
                if (!tileset) {
                    addIssue(result, ValidationSeverity::Error, code, commandLocation,
                             QObject::tr("O tileset selecionado para esta alteração não existe."));
                    return false;
                }
                const int tx = command.params.value(QStringLiteral("tx")).toInt();
                const int ty = command.params.value(QStringLiteral("ty")).toInt();
                if (!tileset->contains(tx, ty)) {
                    addIssue(result, ValidationSeverity::Error, code, commandLocation,
                             QObject::tr("O tile selecionado está fora dos limites do tileset."));
                    return false;
                }
                return true;
            };

            if (command.type == QLatin1String("map.runtime.tile")) {
                validateLayer(QStringLiteral("layerId"), QStringLiteral("event.runtimeMap.tile.layer"));
                validateNumber(QStringLiteral("x"), QStringLiteral("event.runtimeMap.tile.x"));
                validateNumber(QStringLiteral("y"), QStringLiteral("event.runtimeMap.tile.y"));
                if (!command.params.value(QStringLiteral("clear"), false).toBool())
                    validateTile(QStringLiteral("event.runtimeMap.tile.tile"));
            } else if (command.type == QLatin1String("map.runtime.fill")) {
                validateLayer(QStringLiteral("layerId"), QStringLiteral("event.runtimeMap.fill.layer"));
                for (const QString& key : {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("width"),QStringLiteral("height")})
                    validateNumber(key, QStringLiteral("event.runtimeMap.fill.%1").arg(key));
                for (const QString& key : {QStringLiteral("width"),QStringLiteral("height")}) {
                    bool constant = false; const int value = constantNumber(key, 1, &constant);
                    if (constant && (value < 1 || value > 512))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.fill.size"), commandLocation,
                                 QObject::tr("Largura e altura constantes precisam ficar entre 1 e 512 células."));
                }
                if (!command.params.value(QStringLiteral("clear"), false).toBool())
                    validateTile(QStringLiteral("event.runtimeMap.fill.tile"));
            } else if (command.type == QLatin1String("map.runtime.copy")) {
                validateLayer(QStringLiteral("sourceLayerId"), QStringLiteral("event.runtimeMap.copy.sourceLayer"));
                validateLayer(QStringLiteral("targetLayerId"), QStringLiteral("event.runtimeMap.copy.targetLayer"));
                for (const QString& key : {QStringLiteral("sourceX"),QStringLiteral("sourceY"),QStringLiteral("width"),QStringLiteral("height"),QStringLiteral("targetX"),QStringLiteral("targetY")})
                    validateNumber(key, QStringLiteral("event.runtimeMap.copy.%1").arg(key));
                for (const QString& key : {QStringLiteral("width"),QStringLiteral("height")}) {
                    bool constant = false; const int value = constantNumber(key, 1, &constant);
                    if (constant && (value < 1 || value > 512))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.copy.size"), commandLocation,
                                 QObject::tr("Largura e altura constantes precisam ficar entre 1 e 512 células."));
                }
            } else if (command.type == QLatin1String("map.runtime.passage")) {
                validateNumber(QStringLiteral("x"), QStringLiteral("event.runtimeMap.passage.x"));
                validateNumber(QStringLiteral("y"), QStringLiteral("event.runtimeMap.passage.y"));
                const int mask = command.params.value(QStringLiteral("mask"), -1).toInt();
                if (mask < -1 || mask > 15)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.passage.mask"), commandLocation,
                             QObject::tr("A máscara de passagem precisa ser Herdar (-1) ou ficar entre 0 e 15."));
            } else if (command.type == QLatin1String("map.runtime.terrain")) {
                validateNumber(QStringLiteral("x"), QStringLiteral("event.runtimeMap.terrain.x"));
                validateNumber(QStringLiteral("y"), QStringLiteral("event.runtimeMap.terrain.y"));
                validateNumber(QStringLiteral("terrain"), QStringLiteral("event.runtimeMap.terrain.value"));
                bool constant = false; const int terrain = constantNumber(QStringLiteral("terrain"), 0, &constant);
                if (constant && (terrain < 0 || terrain > 999999))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.terrain.range"), commandLocation,
                             QObject::tr("Terrain/Tag constante precisa ficar entre 0 e 999999."));
            } else if (command.type == QLatin1String("map.runtime.tileset")) {
                const QString source = command.params.value(QStringLiteral("sourceTilesetId")).toString();
                const QString target = command.params.value(QStringLiteral("targetTilesetId")).toString();
                const Tileset* sourceTileset = runtimeTilesetById(ed, source);
                const Tileset* targetTileset = target.isEmpty() ? nullptr : runtimeTilesetById(ed, target);
                if (!sourceTileset)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.tileset.source"), commandLocation,
                             QObject::tr("O tileset de origem do remapeamento não existe."));
                if (!target.isEmpty() && !targetTileset)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.tileset.target"), commandLocation,
                             QObject::tr("O tileset de destino do remapeamento não existe."));
                if (sourceTileset && targetTileset &&
                    (targetTileset->columns < sourceTileset->columns || targetTileset->rows < sourceTileset->rows))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.tileset.compatibility"), commandLocation,
                             QObject::tr("O tileset de destino precisa cobrir todos os índices X/Y do tileset de origem."));
            } else if (command.type == QLatin1String("map.runtime.reset")) {
                const QString scope = command.params.value(QStringLiteral("scope"), QStringLiteral("cell")).toString();
                if (!QSet<QString>{QStringLiteral("cell"),QStringLiteral("area"),QStringLiteral("tileset"),QStringLiteral("map")}.contains(scope))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.reset.scope"), commandLocation,
                             QObject::tr("A abrangência da restauração do mapa é desconhecida."));
                if (scope == QLatin1String("cell")) {
                    validateLayer(QStringLiteral("layerId"), QStringLiteral("event.runtimeMap.reset.layer"));
                    validateNumber(QStringLiteral("x"), QStringLiteral("event.runtimeMap.reset.x"));
                    validateNumber(QStringLiteral("y"), QStringLiteral("event.runtimeMap.reset.y"));
                } else if (scope == QLatin1String("area")) {
                    for (const QString& key : {QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("width"),QStringLiteral("height")})
                        validateNumber(key, QStringLiteral("event.runtimeMap.reset.%1").arg(key));
                    for (const QString& key : {QStringLiteral("width"),QStringLiteral("height")}) {
                        bool constant = false; const int value = constantNumber(key, 1, &constant);
                        if (constant && (value < 1 || value > 512))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.reset.size"), commandLocation,
                                     QObject::tr("Largura e altura constantes do reset precisam ficar entre 1 e 512 células."));
                    }
                } else if (scope == QLatin1String("tileset") && !runtimeTilesetById(ed, command.params.value(QStringLiteral("sourceTilesetId")).toString())) {
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.runtimeMap.reset.tileset"), commandLocation,
                             QObject::tr("O tileset cujo remapeamento será resetado não existe."));
                }
            }
        }
        if (command.type.startsWith(QLatin1String("database.")) &&
            command.type != QLatin1String("database.each.end")) {
            const QString databaseId = command.params.value(QStringLiteral("databaseId")).toString();
            const CustomDatabaseDefinition* database = ed.legacyImport.customDatabase(databaseId);
            if (!database) {
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.missing"), commandLocation,
                         QObject::tr("O Banco de Dados Personalizado selecionado não existe."));
            } else {
                const auto validateRecord = [&](const QString& specKey, const QString& idKey, const QString& code) {
                    const QVariantMap recordSpec = command.params.value(specKey).toMap();
                    if (!recordSpec.isEmpty()) {
                        QString problem;
                        if (!commonSourceMatchesType(ed, recordSpec, CommonValueType::Text, contextCommon, &problem)) {
                            addIssue(result, ValidationSeverity::Error, code, commandLocation,
                                     QObject::tr("Origem do registro: %1").arg(problem));
                            return false;
                        }
                        return true;
                    }
                    const QString recordId = command.params.value(idKey).toString();
                    if (!customDatabaseRecordById(*database, recordId)) {
                        addIssue(result, ValidationSeverity::Error, code, commandLocation,
                                 QObject::tr("O registro selecionado não existe mais neste banco."));
                        return false;
                    }
                    return true;
                };
                const auto fieldForCommand = [&]() -> const CustomDatabaseField* {
                    return customDatabaseFieldById(*database, command.params.value(QStringLiteral("fieldId")).toString());
                };
                if (command.type == QLatin1String("database.get")) {
                    validateRecord(QStringLiteral("recordSpec"), QStringLiteral("recordId"), QStringLiteral("event.database.get.record"));
                    const CustomDatabaseField* field = fieldForCommand();
                    if (!field) addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.get.field"), commandLocation, QObject::tr("O campo selecionado não existe mais neste banco."));
                    else {
                        QString problem;
                        if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), customDatabaseCommonType(field->type), contextCommon, &problem))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.get.target"), commandLocation, problem);
                    }
                } else if (command.type == QLatin1String("database.set")) {
                    if (database->mode != CustomDatabaseMode::Runtime)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.readonly"), commandLocation, QObject::tr("Este comando tenta alterar um banco Somente leitura."));
                    validateRecord(QStringLiteral("recordSpec"), QStringLiteral("recordId"), QStringLiteral("event.database.set.record"));
                    const CustomDatabaseField* field = fieldForCommand();
                    if (!field) addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.set.field"), commandLocation, QObject::tr("O campo selecionado não existe mais neste banco."));
                    else {
                        QString problem;
                        if (!commonSourceMatchesType(ed, command.params.value(QStringLiteral("sourceSpec")).toMap(), customDatabaseCommonType(field->type), contextCommon, &problem))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.set.source"), commandLocation, problem);
                    }
                } else if (command.type == QLatin1String("database.find")) {
                    const CustomDatabaseField* field = fieldForCommand();
                    if (!field) addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.find.field"), commandLocation, QObject::tr("O campo usado na procura não existe."));
                    else {
                        const QString op = command.params.value(QStringLiteral("operation"), QStringLiteral("equals")).toString();
                        QSet<QString> valid{QStringLiteral("equals"),QStringLiteral("notEquals")};
                        if (field->type == CustomDatabaseFieldType::Number)
                            valid.insert(QStringLiteral("less")); valid.insert(QStringLiteral("lessEqual")); valid.insert(QStringLiteral("greater")); valid.insert(QStringLiteral("greaterEqual"));
                        if (field->type == CustomDatabaseFieldType::Text)
                            valid.insert(QStringLiteral("contains")); valid.insert(QStringLiteral("startsWith")); valid.insert(QStringLiteral("endsWith"));
                        if (!valid.contains(op))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.find.operation"), commandLocation, QObject::tr("O operador de procura não é válido para o tipo deste campo."));
                        QString problem;
                        if (!commonSourceMatchesType(ed, command.params.value(QStringLiteral("sourceSpec")).toMap(), customDatabaseCommonType(field->type), contextCommon, &problem))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.find.source"), commandLocation, problem);
                        if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), CommonValueType::Text, contextCommon, &problem))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.find.target"), commandLocation, problem);
                    }
                } else if (command.type == QLatin1String("database.count")) {
                    QString problem;
                    if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), CommonValueType::Number, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.count.target"), commandLocation, problem);
                } else if (command.type == QLatin1String("database.exists")) {
                    validateRecord(QStringLiteral("recordSpec"), QStringLiteral("recordId"), QStringLiteral("event.database.exists.record"));
                    QString problem;
                    if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), CommonValueType::Boolean, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.exists.target"), commandLocation, problem);
                } else if (command.type == QLatin1String("database.copy")) {
                    if (database->mode != CustomDatabaseMode::Runtime)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.readonly"), commandLocation, QObject::tr("Copiar registro só pode alterar um banco Runtime."));
                    validateRecord(QStringLiteral("sourceRecordSpec"), QStringLiteral("sourceRecordId"), QStringLiteral("event.database.copy.source"));
                    validateRecord(QStringLiteral("targetRecordSpec"), QStringLiteral("targetRecordId"), QStringLiteral("event.database.copy.target"));
                } else if (command.type == QLatin1String("database.reset")) {
                    if (database->mode != CustomDatabaseMode::Runtime)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.readonly"), commandLocation, QObject::tr("Resetar registro só pode alterar um banco Runtime."));
                    validateRecord(QStringLiteral("recordSpec"), QStringLiteral("recordId"), QStringLiteral("event.database.reset.record"));
                } else if (command.type == QLatin1String("database.recordInfo")) {
                    validateRecord(QStringLiteral("recordSpec"), QStringLiteral("recordId"), QStringLiteral("event.database.info.record"));
                    const QString info = command.params.value(QStringLiteral("info"), QStringLiteral("id")).toString();
                    const CommonValueType infoType = info == QLatin1String("number") ? CommonValueType::Number : CommonValueType::Text;
                    if (!QSet<QString>{QStringLiteral("id"),QStringLiteral("name"),QStringLiteral("number")}.contains(info))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.info.kind"), commandLocation, QObject::tr("A informação do registro é desconhecida."));
                    QString problem;
                    if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), infoType, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.info.target"), commandLocation, problem);
                } else if (command.type == QLatin1String("database.each")) {
                    QString problem;
                    if (!valueTargetMatchesType(ed, command.params.value(QStringLiteral("target")).toMap(), CommonValueType::Text, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.each.target"), commandLocation, problem);
                }
            }
        }
        if (command.type == QLatin1String("repeat.begin")) {
            QVariantMap countSpec=command.params.value(QStringLiteral("countSpec")).toMap();
            if(countSpec.isEmpty())countSpec=QVariantMap{{QStringLiteral("source"),QStringLiteral("constant")},{QStringLiteral("value"),command.params.value(QStringLiteral("count"),1)}};
            QString problem;
            if(!commonSourceMatchesType(ed,countSpec,CommonValueType::Number,contextCommon,&problem))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.repeat.count"),commandLocation,problem);
            const QVariantMap target=command.params.value(QStringLiteral("indexTarget")).toMap();
            if(!target.isEmpty()&&!valueTargetMatchesType(ed,target,CommonValueType::Number,contextCommon,&problem))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.repeat.indexTarget"),commandLocation,problem);
        }
        if (command.type == QLatin1String("input.text")) {
            if (!stringExists(ed, command.params.value(QStringLiteral("stringId"), 1).toInt()))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.input.string"), commandLocation, QObject::tr("A String Global que recebe o texto não existe."));
            double maximum=0.0;
            if(staticNumberValue(command.params.value(QStringLiteral("maximumLength"),32),&maximum)&&(maximum<1||maximum>1024))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.input.textLength"), commandLocation, QObject::tr("O tamanho máximo da entrada de texto precisa ficar entre 1 e 1024."));
        }
        if(conditionCommand||command.type==QLatin1String("choice.show")){
            for(int predicateIndex=0;predicateIndex<conditionPredicates.size();++predicateIndex){
                const QVariantMap& predicate=conditionPredicates[predicateIndex];
                const QString kind=predicate.value(QStringLiteral("kind"),QStringLiteral("switch")).toString();
                const QString leafLocation=conditionPredicates.size()>1?QObject::tr("%1 / condição %2").arg(commandLocation).arg(predicateIndex+1):commandLocation;
                if(kind==QLatin1String("switch")){
                    if(!switchExists(ed,predicate.value(QStringLiteral("id"),1).toInt()))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.switch.missing"),leafLocation,QObject::tr("A condição referencia um Switch Global inexistente."));
                }else if(kind==QLatin1String("selfSwitch")){
                    if(!QSet<QString>{QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("C"),QStringLiteral("D")}.contains(predicate.value(QStringLiteral("letter"),QStringLiteral("A")).toString()))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.selfSwitch.letter"),leafLocation,QObject::tr("A condição usa uma letra de Self Switch inválida."));
                }else if(kind==QLatin1String("variable")){
                    if(!variableExists(ed,predicate.value(QStringLiteral("id"),1).toInt()))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.variable.missing"),leafLocation,QObject::tr("A condição referencia uma Variável Global inexistente."));
                    const QVariantMap right=predicate.value(QStringLiteral("rightSpec")).toMap();if(!right.isEmpty()){QString problem;if(!commonSourceMatchesType(ed,right,CommonValueType::Number,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.variable.source"),leafLocation,problem);}
                }else if(kind==QLatin1String("string")){
                    if(!stringExists(ed,predicate.value(QStringLiteral("id"),1).toInt()))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.string.condition.missing"),leafLocation,QObject::tr("A condição referencia uma String Global inexistente."));
                    const QString op=predicate.value(QStringLiteral("op"),QStringLiteral("==")).toString();if(!QSet<QString>{QStringLiteral("=="),QStringLiteral("!="),QStringLiteral("contains"),QStringLiteral("startsWith"),QStringLiteral("endsWith")}.contains(op))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.string.condition.op"),leafLocation,QObject::tr("O operador da condição de String é inválido."));
                    const QVariantMap right=predicate.value(QStringLiteral("rightSpec")).toMap();if(!right.isEmpty()){QString problem;if(!commonSourceMatchesType(ed,right,CommonValueType::Text,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.string.source"),leafLocation,problem);}
                }else if(kind==QLatin1String("commonValue")){
                    CommonValueType conditionType=CommonValueType::Number;if(!commonValueTypeForId(contextCommon,predicate.value(QStringLiteral("id")).toString(),&conditionType))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.condition.missing"),leafLocation,QObject::tr("A condição referencia um parâmetro/local inexistente ou está fora de um Evento Comum."));
                    else{const QString op=predicate.value(QStringLiteral("op"),QStringLiteral("==")).toString();const bool validOp=conditionType==CommonValueType::Number?QSet<QString>{QStringLiteral("=="),QStringLiteral("!="),QStringLiteral(">="),QStringLiteral("<="),QStringLiteral(">"),QStringLiteral("<")}.contains(op):conditionType==CommonValueType::Boolean?QSet<QString>{QStringLiteral("=="),QStringLiteral("!=")}.contains(op):QSet<QString>{QStringLiteral("=="),QStringLiteral("!="),QStringLiteral("contains"),QStringLiteral("startsWith"),QStringLiteral("endsWith")}.contains(op);if(!validOp)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.condition.op"),leafLocation,QObject::tr("O operador da condição não é válido para o tipo do parâmetro/local."));const QVariantMap right=predicate.value(QStringLiteral("rightSpec")).toMap();if(!right.isEmpty()){QString problem;if(!commonSourceMatchesType(ed,right,conditionType,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.common.source"),leafLocation,problem);}}
                }else if(kind==QLatin1String("button")){
                    bool ok=false;gameActionFromId(predicate.value(QStringLiteral("action"),QStringLiteral("confirm")).toString(),&ok);if(!ok)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.input.condition.action"),leafLocation,QObject::tr("A condição de Input referencia uma ação inexistente."));const QString state=predicate.value(QStringLiteral("state"),QStringLiteral("held")).toString();if(!QSet<QString>{QStringLiteral("pressed"),QStringLiteral("held"),QStringLiteral("released")}.contains(state))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.input.condition.state"),leafLocation,QObject::tr("O estado da condição de Input é inválido."));
                }else if(kind==QLatin1String("gold")){
                    const QVariantMap right=predicate.value(QStringLiteral("rightSpec")).toMap();if(!right.isEmpty()){QString problem;if(!commonSourceMatchesType(ed,right,CommonValueType::Number,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.gold.source"),leafLocation,problem);}
                }else if(kind==QLatin1String("item")){
                    const QVariantMap amount=predicate.value(QStringLiteral("amountSpec")).toMap();if(!amount.isEmpty()){QString problem;if(!commonSourceMatchesType(ed,amount,CommonValueType::Number,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.item.source"),leafLocation,problem);}
                }else if(kind==QLatin1String("random")){
                    const int chance=predicate.value(QStringLiteral("chance"),50).toInt();if(chance<0||chance>100)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.condition.random.range"),leafLocation,QObject::tr("A probabilidade da condição precisa ficar entre 0 e 100%."));
                }
            }
        }
        if(command.type==QLatin1String("wait.until")){
            double timeoutValue=0.0;const bool staticTimeout=staticNumberValue(command.params.value(QStringLiteral("timeoutFrames"),0),&timeoutValue);
            if(staticTimeout&&(timeoutValue<0||timeoutValue>360000))addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.waitUntil.timeout"),commandLocation,QObject::tr("O tempo limite de Esperar até será normalizado entre 0 e 360000 quadros."));
            bool hasRandom=false;for(const QVariantMap& predicate:conditionPredicates)if(predicate.value(QStringLiteral("kind")).toString()==QLatin1String("random")){hasRandom=true;break;}
            if(hasRandom&&staticTimeout&&timeoutValue<=0)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.waitUntil.randomInfinite"),commandLocation,QObject::tr("Esperar até usando Probabilidade sem tempo limite pode permanecer ativo indefinidamente."));
        }
        if(command.type==QLatin1String("label")){
            const QString name=command.params.value(QStringLiteral("name")).toString();
            if(duplicateLabels.contains(name))addIssue(result,ValidationSeverity::Error,
                QStringLiteral("event.label.duplicate"),commandLocation,
                QObject::tr("O rótulo “%1” está duplicado; um salto teria destino ambíguo.").arg(name));
        }
        if (command.type == QLatin1String("if")) ++ifDepth;
        else if (command.type == QLatin1String("else") && ifDepth <= 0)
            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.if.else"),
                     commandLocation, QObject::tr("Há um Senão sem condição aberta."));
        else if (command.type == QLatin1String("endIf")) {
            if (ifDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.if.end"),
                         commandLocation, QObject::tr("Há um fim de condição sem início."));
            else --ifDepth;
        } else if (command.type == QLatin1String("loop.begin")) ++loopDepth;
        else if (command.type == QLatin1String("repeat.begin")) ++repeatDepth;
        else if ((command.type == QLatin1String("loop.break") || command.type == QLatin1String("repeat.break")) && loopDepth <= 0 && repeatDepth <= 0)
            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.loop.break"),
                     commandLocation, QObject::tr("Sair da repetição está fora de uma repetição."));
        else if (command.type == QLatin1String("loop.end")) {
            if (loopDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.loop.end"),
                         commandLocation, QObject::tr("Há um fim de repetição contínua sem início."));
            else --loopDepth;
        } else if (command.type == QLatin1String("repeat.end")) {
            if (repeatDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.repeat.end"),
                         commandLocation, QObject::tr("Há um fim de Repetir N vezes sem início."));
            else --repeatDepth;
        }
        if (command.type == QLatin1String("database.each")) ++databaseEachDepth;
        else if (command.type == QLatin1String("database.each.end")) {
            if (databaseEachDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.each.end"),
                         commandLocation, QObject::tr("Há um fim de percurso de banco sem início."));
            else --databaseEachDepth;
        }
        if (command.type == QLatin1String("parallel.begin")) {
            if (parallelDepth > 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("ctx.parallel-nested"),
                         commandLocation, QObject::tr("Um bloco Parallel não pode ser aninhado em outro Parallel; extraia a tarefa interna ou execute-a sequencialmente."));
            ++parallelDepth;
            if (i + 1 < commands.size() && commands.at(i + 1).type == QLatin1String("parallel.end"))
                addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.parallel.empty"),
                         commandLocation, QObject::tr("O bloco Parallel está vazio e não inicia nenhuma tarefa."));
        } else if (command.type == QLatin1String("parallel.end")) {
            if (parallelDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.parallel.end"),
                         commandLocation, QObject::tr("Há um fim de Parallel sem início."));
            else --parallelDepth;
        }

        // Blocos 9+10: Begin/End são regiões estruturais reais. Nesting é
        // permitido, mas um End sem Begin ou Begin sem End não pode habilitar
        // um skip implícito sobre o restante do evento. Modos automáticos já
        // são rejeitados pelo contrato de page activation acima e não entram
        // no pareamento do fluxo Normal.
        if(command.executionMode==EventExecutionMode::Normal&&command.type==QLatin1String("ludo.cutscene.settings")){
            pendingCutsceneSettings=command.params;
            bool validAction=false;gameActionFromId(command.params.value(QStringLiteral("skipAction"),QStringLiteral("skipCutscene")).toString(),&validAction);
            if(!validAction)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.cutscene.settings.action"),commandLocation,QObject::tr("A ação de input configurada para pular a cutscene não existe."));
            const QString nesting=command.params.value(QStringLiteral("nesting"),QStringLiteral("allow")).toString();
            if(nesting!=QLatin1String("allow")&&nesting!=QLatin1String("warn")&&nesting!=QLatin1String("forbid"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.cutscene.settings.nesting"),commandLocation,QObject::tr("A política de aninhamento da cutscene é inválida."));
        } else if (command.executionMode == EventExecutionMode::Normal &&
            command.type == QLatin1String("ludo.cutscene.begin")) {
            const QString nesting=pendingCutsceneSettings.value(QStringLiteral("nesting"),QStringLiteral("allow")).toString();
            if(cutsceneDepth>0&&nesting==QLatin1String("forbid"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.cutscene.nesting.forbidden"),commandLocation,QObject::tr("Esta região proíbe aninhamento, mas foi iniciada dentro de outra cutscene."));
            else if(cutsceneDepth>0&&nesting==QLatin1String("warn"))addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.cutscene.nesting.warning"),commandLocation,QObject::tr("A região está aninhada; confirme se o primeiro skip deve fechar somente a região interna."));
            pendingCutsceneSettings.clear();
            ++cutsceneDepth;
        } else if (command.executionMode == EventExecutionMode::Normal &&
                   command.type == QLatin1String("ludo.cutscene.end")) {
            if (cutsceneDepth <= 0)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.cutscene.end"),
                         commandLocation, QObject::tr("Há um fim de cutscene pulável sem início correspondente."));
            else --cutsceneDepth;
        }
        if (command.type == QLatin1String("move.route")) {
            const MoveRoute route=moveRouteFromMap(command.params.value(QStringLiteral("route")).toMap());
            validateMoveRouteDefinition(route,commandLocation,result,false);
            if(contextMap)for(int ri=0;ri<route.commands.size();++ri){
                const MoveCommand& stepCommand=route.commands.at(ri);
                if(stepCommand.type!=QLatin1String("pathfind"))continue;
                const MoveRoutePathOptions path=moveRoutePathOptionsFromCommand(stepCommand);
                const QString pathLocation=QObject::tr("%1 / pathfinding %2").arg(commandLocation).arg(ri+1);
                if(path.targetKind==MoveRoutePathTargetKind::Cell&&!mapContains(*contextMap,path.cell))
                    addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.cell.bounds"),pathLocation,
                             QObject::tr("O tile de destino fica fora dos limites deste mapa."));
                if(path.targetKind==MoveRoutePathTargetKind::Event){
                    bool found=false;for(const MapEvent& ev:contextMap->events)if(ev.id==path.eventId){found=true;break;}
                    if(!found)addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.path.event.missing"),pathLocation,
                                       QObject::tr("O Evento usado como alvo do pathfinding não existe neste mapa."));
                }
            }
        } else if (command.type == QLatin1String("move.route.control")) {
            const QString target=command.params.value(QStringLiteral("target"),QStringLiteral("self")).toString().trimmed();
            const bool validTarget=target==QLatin1String("self")||target==QLatin1String("player")||
                (target.startsWith(QLatin1String("event:"))&&target.size()>6);
            if(!validTarget)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.control.target"),
                         commandLocation,QObject::tr("O controle de rota possui um alvo inválido."));
            const QString action=command.params.value(QStringLiteral("action"),QStringLiteral("pause")).toString();
            if(action!=QLatin1String("pause")&&action!=QLatin1String("resume")&&
               action!=QLatin1String("cancel")&&action!=QLatin1String("clearQueue"))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("move.route.control.action"),
                         commandLocation,QObject::tr("A ação de controle da rota é desconhecida."));
        } else if(command.type==QLatin1String("ludo.camera.move")||
                  command.type==QLatin1String("ludo.camera.moveOnly")||
                  command.type==QLatin1String("ludo.camera.zoomOnly")||
                  command.type==QLatin1String("ludo.camera.reset")||
                  command.type==QLatin1String("ludo.camera.restore")){
            const int duration=command.params.value(QStringLiteral("duration"),30).toInt();
            if(duration<0||duration>3600)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.camera.duration"),
                         commandLocation,QObject::tr("A duração da câmera precisa ficar entre 0 e 3600 quadros."));
            if(command.type==QLatin1String("ludo.camera.move")||command.type==QLatin1String("ludo.camera.zoomOnly")){
                const double zoom=command.params.value(QStringLiteral("zoom"),2.0).toDouble();
                if(!std::isfinite(zoom)||zoom<.25||zoom>8.0)
                    addIssue(result,ValidationSeverity::Error,QStringLiteral("event.camera.zoom"),
                             commandLocation,QObject::tr("O zoom da câmera precisa ficar entre 0,25× e 8×."));
            }
            if(command.type==QLatin1String("ludo.camera.move")||command.type==QLatin1String("ludo.camera.moveOnly")){
                const QString target=command.params.value(QStringLiteral("target"),QStringLiteral("player")).toString();
                const bool valid=target==QLatin1String("player")||target==QLatin1String("self")||
                    target==QLatin1String("position")||(target.startsWith(QLatin1String("event:"))&&target.size()>6);
                if(!valid)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.camera.target"),
                    commandLocation,QObject::tr("O alvo do movimento da câmera é inválido."));
            }
        } else if (command.type == QLatin1String("ludo.filter.chromaticAberration") ||
                   command.type == QLatin1String("ludo.filter.noise") ||
                   command.type == QLatin1String("ludo.filter.scanlines") ||
                   command.type == QLatin1String("ludo.filter.vignette") ||
                   command.type == QLatin1String("ludo.filter.blur") ||
                   command.type == QLatin1String("ludo.filter.tiltShift")) {
            const int slot=command.params.value(QStringLiteral("slot"),1).toInt();
            const int duration=command.params.value(QStringLiteral("duration"),30).toInt();
            const bool world=command.params.value(QStringLiteral("affectWorld"),true).toBool();
            const bool pictures=command.params.value(QStringLiteral("affectPictures"),false).toBool();
            const bool hud=command.params.value(QStringLiteral("affectHud"),false).toBool();
            if(slot<core::LudoFilterMinSlot||slot>core::LudoFilterMaxSlot)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.slot"),commandLocation,QObject::tr("O slot/ID do filtro precisa ficar entre 1 e 99."));
            if(!world&&!pictures&&!hud)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.scope.empty"),commandLocation,QObject::tr("Escolha pelo menos um escopo para o filtro: mapa/eventos, Pictures ou HUD."));
            if(duration<0||duration>3600)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.duration"),commandLocation,QObject::tr("A transição do filtro precisa ficar entre 0 e 3600 quadros."));
            auto finiteRange=[&](const QString& key,double fallback,double lo,double hi,const QString& issue,const QString& message){bool ok=false;const double value=command.params.value(key,fallback).toDouble(&ok);if(!ok||!std::isfinite(value)||value<lo||value>hi)addIssue(result,ValidationSeverity::Error,issue,commandLocation,message);};
            if(command.type==QLatin1String("ludo.filter.chromaticAberration")){
                const QString mode=command.params.value(QStringLiteral("mode"),QStringLiteral("lens")).toString();
                if(mode!=QLatin1String("normal")&&mode!=QLatin1String("lens"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.chromatic.mode"),commandLocation,QObject::tr("O modo da Aberração Cromática deve ser Normal ou Lente."));
                finiteRange(QStringLiteral("intensity"),4.0,0,32,QStringLiteral("event.filter.chromatic.intensity"),QObject::tr("A intensidade da Aberração Cromática precisa ficar entre 0 e 32 px."));
                finiteRange(QStringLiteral("edgeStart"),.55,0,.95,QStringLiteral("event.filter.chromatic.edge"),QObject::tr("O início da aberração nas bordas precisa ficar entre 0% e 95%."));
                finiteRange(QStringLiteral("falloff"),2,.25,8,QStringLiteral("event.filter.chromatic.falloff"),QObject::tr("A curva da lente precisa ficar entre 0,25 e 8."));
                finiteRange(QStringLiteral("mix"),1,0,100,QStringLiteral("event.filter.chromatic.mix"),QObject::tr("A mistura da Aberração Cromática precisa ficar entre 0 e 1 (ou 0% e 100% em projeto legado)."));
            } else if(command.type==QLatin1String("ludo.filter.noise")){
                const QString style=command.params.value(QStringLiteral("style"),QStringLiteral("legacy")).toString();
                if(style!=QLatin1String("legacy")&&style!=QLatin1String("film"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.noise.style"),commandLocation,QObject::tr("O tipo de Noise deve ser Legacy Block ou Film Grain."));
                const QString temporal=command.params.value(QStringLiteral("temporal"),QStringLiteral("flicker")).toString();
                if(temporal!=QLatin1String("static")&&temporal!=QLatin1String("flicker")&&temporal!=QLatin1String("drift")&&temporal!=QLatin1String("smooth"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.noise.temporal"),commandLocation,QObject::tr("O modo temporal do Noise deve ser Estático, Flicker, Drift ou Suave."));
                finiteRange(QStringLiteral("intensity"),.12,0,100,QStringLiteral("event.filter.noise.intensity"),QObject::tr("A intensidade do Noise precisa ficar entre 0 e 1 (ou 0% e 100%)."));
                finiteRange(QStringLiteral("size"),2,1,64,QStringLiteral("event.filter.noise.size"),QObject::tr("O tamanho do grão precisa ficar entre 1 e 64 px."));
                finiteRange(QStringLiteral("speed"),1,0,20,QStringLiteral("event.filter.noise.speed"),QObject::tr("A velocidade do Noise precisa ficar entre 0 e 20."));
                finiteRange(QStringLiteral("seed"),0,0,65535,QStringLiteral("event.filter.noise.seed"),QObject::tr("O seed do Noise precisa ficar entre 0 e 65535."));
                finiteRange(QStringLiteral("contrast"),1,.25,4,QStringLiteral("event.filter.noise.contrast"),QObject::tr("O contraste do Noise precisa ficar entre 0,25 e 4."));
                finiteRange(QStringLiteral("colorAmount"),0,0,100,QStringLiteral("event.filter.noise.color"),QObject::tr("A quantidade de ruído colorido precisa ficar entre 0 e 1 (ou 0% e 100%)."));
            } else if(command.type==QLatin1String("ludo.filter.scanlines")){
                finiteRange(QStringLiteral("intensity"),.28,0,100,QStringLiteral("event.filter.scanlines.intensity"),QObject::tr("A intensidade das Scanlines precisa ficar entre 0 e 1 (ou 0% e 100%)."));
                finiteRange(QStringLiteral("spacing"),3,2,32,QStringLiteral("event.filter.scanlines.spacing"),QObject::tr("O espaçamento das Scanlines precisa ficar entre 2 e 32 px."));
                const QString scanStyle=command.params.value(QStringLiteral("style"),QStringLiteral("legacy")).toString();
                if(scanStyle!=QLatin1String("legacy")&&scanStyle!=QLatin1String("softCrt")&&scanStyle!=QLatin1String("sharpCrt")&&scanStyle!=QLatin1String("interlaced"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.scanlines.style"),commandLocation,QObject::tr("O estilo de Scanlines deve ser Legacy, CRT suave, CRT nítido ou Entrelaçado."));
                finiteRange(QStringLiteral("thickness"),.50,.05,95,QStringLiteral("event.filter.scanlines.thickness"),QObject::tr("A espessura das Scanlines precisa ficar entre 5% e 95%."));
                finiteRange(QStringLiteral("softness"),.22,0,95,QStringLiteral("event.filter.scanlines.softness"),QObject::tr("A suavidade das Scanlines precisa ficar entre 0% e 95%."));
                finiteRange(QStringLiteral("phase"),0,-32,32,QStringLiteral("event.filter.scanlines.phase"),QObject::tr("A fase das Scanlines precisa ficar entre -32 e 32 px."));
                finiteRange(QStringLiteral("scrollSpeed"),0,-120,120,QStringLiteral("event.filter.scanlines.scroll"),QObject::tr("A velocidade de rolagem das Scanlines precisa ficar entre -120 e 120 px/s."));
                finiteRange(QStringLiteral("interlaceAmount"),.55,0,100,QStringLiteral("event.filter.scanlines.interlace-amount"),QObject::tr("A força do entrelaçamento precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("interlaceSpeed"),60,0,120,QStringLiteral("event.filter.scanlines.interlace-speed"),QObject::tr("A velocidade do entrelaçamento precisa ficar entre 0 e 120 Hz."));
                finiteRange(QStringLiteral("sweepSpeed"),.35,0,5,QStringLiteral("event.filter.scanlines.sweep-speed"),QObject::tr("A velocidade da faixa branca precisa ficar entre 0 e 5."));
                finiteRange(QStringLiteral("sweepWidth"),.055,.005,50,QStringLiteral("event.filter.scanlines.sweep-width"),QObject::tr("A largura da faixa precisa ficar entre 0,5% e 50%."));
                finiteRange(QStringLiteral("sweepSoftness"),1.0,0,100,QStringLiteral("event.filter.scanlines.sweep-softness"),QObject::tr("A suavidade da faixa precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("sweepRed"),255,0,255,QStringLiteral("event.filter.scanlines.sweep-red"),QObject::tr("O canal vermelho da faixa precisa ficar entre 0 e 255."));
                finiteRange(QStringLiteral("sweepGreen"),255,0,255,QStringLiteral("event.filter.scanlines.sweep-green"),QObject::tr("O canal verde da faixa precisa ficar entre 0 e 255."));
                finiteRange(QStringLiteral("sweepBlue"),255,0,255,QStringLiteral("event.filter.scanlines.sweep-blue"),QObject::tr("O canal azul da faixa precisa ficar entre 0 e 255."));
                finiteRange(QStringLiteral("sweepIntensity"),.45,0,100,QStringLiteral("event.filter.scanlines.sweep-intensity"),QObject::tr("O brilho da faixa branca precisa ficar entre 0 e 1 (ou 0% e 100%)."));
                finiteRange(QStringLiteral("sweepDelay"),0,0,3600,QStringLiteral("event.filter.scanlines.sweep-delay"),QObject::tr("O delay entre varreduras precisa ficar entre 0 e 3600 quadros."));
            } else if(command.type==QLatin1String("ludo.filter.vignette")){
                finiteRange(QStringLiteral("intensity"),.45,0,100,QStringLiteral("event.filter.vignette.intensity"),QObject::tr("A intensidade do Vignette precisa ficar entre 0 e 1 (ou 0% e 100%)."));
                finiteRange(QStringLiteral("radius"),.68,.05,100,QStringLiteral("event.filter.vignette.radius"),QObject::tr("O raio do Vignette precisa ficar entre 5% e 100%."));
                finiteRange(QStringLiteral("softness"),.28,.01,100,QStringLiteral("event.filter.vignette.softness"),QObject::tr("A suavidade do Vignette precisa ficar entre 1% e 100%."));
            } else if(command.type==QLatin1String("ludo.filter.blur")){
                finiteRange(QStringLiteral("radius"),4,0,24,QStringLiteral("event.filter.blur.radius"),QObject::tr("O raio do Blur precisa ficar entre 0 e 24 px."));
                finiteRange(QStringLiteral("strength"),1.0,0,100,QStringLiteral("event.filter.blur.strength"),QObject::tr("A força do Blur precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("edgePreservation"),0.0,0,100,QStringLiteral("event.filter.blur.edge"),QObject::tr("A preservação de bordas precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("angle"),0.0,-180,180,QStringLiteral("event.filter.blur.angle"),QObject::tr("O ângulo do Directional Blur precisa ficar entre -180° e 180°."));
                const QString direction=command.params.value(QStringLiteral("direction"),QStringLiteral("full")).toString();
                if(direction!=QLatin1String("horizontal")&&direction!=QLatin1String("vertical")&&direction!=QLatin1String("full"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.blur.direction"),commandLocation,QObject::tr("A direção do Blur deve ser Horizontal, Vertical ou Completo."));
                const QString style=command.params.value(QStringLiteral("style"),QStringLiteral("legacy")).toString();
                if(style!=QLatin1String("legacy")&&style!=QLatin1String("gaussianSoft")&&style!=QLatin1String("gaussianSharp")&&style!=QLatin1String("box")&&style!=QLatin1String("directional")&&style!=QLatin1String("pixel"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.blur.style"),commandLocation,QObject::tr("O estilo do Blur não é reconhecido."));
                const QString quality=command.params.value(QStringLiteral("quality"),QStringLiteral("medium")).toString();
                if(quality!=QLatin1String("low")&&quality!=QLatin1String("medium")&&quality!=QLatin1String("high")&&quality!=QLatin1String("ultra"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.blur.quality"),commandLocation,QObject::tr("A qualidade do Blur deve ser Baixa, Média, Alta ou Ultra."));
            } else {
                finiteRange(QStringLiteral("blur"),6,0,24,QStringLiteral("event.filter.tilt.blur"),QObject::tr("O Blur do Tilt-Shift precisa ficar entre 0 e 24 px."));
                finiteRange(QStringLiteral("centerY"),.5,0,100,QStringLiteral("event.filter.tilt.center"),QObject::tr("O centro do plano do Tilt-Shift precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("focusWidth"),.25,.01,100,QStringLiteral("event.filter.tilt.focus"),QObject::tr("A faixa em foco do Tilt-Shift precisa ficar entre 1% e 100%."));
                finiteRange(QStringLiteral("falloff"),.2,.01,100,QStringLiteral("event.filter.tilt.falloff"),QObject::tr("A transição de foco do Tilt-Shift precisa ficar entre 1% e 100%."));
                finiteRange(QStringLiteral("strength"),1.0,0,100,QStringLiteral("event.filter.tilt.strength"),QObject::tr("A força do Tilt-Shift precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("edgePreservation"),0.0,0,100,QStringLiteral("event.filter.tilt.edge"),QObject::tr("A preservação de bordas do Tilt-Shift precisa ficar entre 0% e 100%."));
                finiteRange(QStringLiteral("angle"),0.0,-180,180,QStringLiteral("event.filter.tilt.angle"),QObject::tr("O ângulo do plano de foco precisa ficar entre -180° e 180°."));
                finiteRange(QStringLiteral("upperBlur"),1.0,0,200,QStringLiteral("event.filter.tilt.upper"),QObject::tr("O blur acima do foco precisa ficar entre 0% e 200%."));
                finiteRange(QStringLiteral("lowerBlur"),1.0,0,200,QStringLiteral("event.filter.tilt.lower"),QObject::tr("O blur abaixo do foco precisa ficar entre 0% e 200%."));
                const QString style=command.params.value(QStringLiteral("style"),QStringLiteral("legacy")).toString();
                if(style!=QLatin1String("legacy")&&style!=QLatin1String("gaussianSoft")&&style!=QLatin1String("gaussianSharp")&&style!=QLatin1String("box"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.tilt.style"),commandLocation,QObject::tr("O estilo do Tilt-Shift não é reconhecido."));
                const QString quality=command.params.value(QStringLiteral("quality"),QStringLiteral("medium")).toString();
                if(quality!=QLatin1String("low")&&quality!=QLatin1String("medium")&&quality!=QLatin1String("high")&&quality!=QLatin1String("ultra"))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.tilt.quality"),commandLocation,QObject::tr("A qualidade do Tilt-Shift deve ser Baixa, Média, Alta ou Ultra."));
            }
        } else if (command.type == QLatin1String("ludo.filter.clear")) {
            const QString filter=command.params.value(QStringLiteral("filter"),QStringLiteral("all")).toString();
            const int slot=command.params.value(QStringLiteral("slot"),1).toInt();
            const int duration=command.params.value(QStringLiteral("duration"),30).toInt();
            core::LudoFilterType parsed;
            if(filter!=QLatin1String("all")&&!core::ludoFilterTypeFromId(filter,&parsed))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.clear.unknown"),commandLocation,QObject::tr("O filtro selecionado para remoção não existe no Ludo Filter System."));
            if(slot<core::LudoFilterMinSlot||slot>core::LudoFilterMaxSlot)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.slot"),commandLocation,QObject::tr("O slot/ID do filtro precisa ficar entre 1 e 99."));
            if(duration<0||duration>3600)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.filter.duration"),commandLocation,QObject::tr("A transição do filtro precisa ficar entre 0 e 3600 quadros."));
        } else if (command.type == QLatin1String("game.ui.open") ||
                   command.type == QLatin1String("game.ui.close")) {
            addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.ui.legacy"),
                     commandLocation,
                     QObject::tr("Este comando pertencia ao UI Designer removido. Substitua-o por Pictures, Escolhas e uma chamada de Evento Comum."));
        } else if (command.type == QLatin1String("map.transfer")) {
            const QString mapId = command.params.value(QStringLiteral("mapId")).toString();
            const MapDoc* destination = ed.mapById(mapId);
            if (!destination) {
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.transfer.map"),
                         commandLocation, QObject::tr("O mapa de destino do teletransporte não existe."));
                continue;
            }
            double x=0.0,y=0.0;
            if (staticNumberValue(command.params.value(QStringLiteral("x"),0),&x) &&
                staticNumberValue(command.params.value(QStringLiteral("y"),0),&y)) {
                const QPoint cell(qRound(x),qRound(y));
                if (!mapContains(*destination, cell))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.transfer.position"),
                             commandLocation,
                             QObject::tr("O destino (%1, %2) fica fora do mapa “%3”.")
                                 .arg(cell.x()).arg(cell.y()).arg(destination->name), true);
            }
        } else if (command.type == QLatin1String("map.event.call")) {
            const QString requestedMap=command.params.value(QStringLiteral("mapId")).toString();
            const QString mapId=requestedMap.isEmpty()&&contextMap?contextMap->id:requestedMap;
            const MapDoc* targetMap=ed.mapById(mapId);
            if(!targetMap) addIssue(result,ValidationSeverity::Error,QStringLiteral("event.mapCall.map"),commandLocation,QObject::tr("O mapa do Map Event chamado não existe."));
            else {
                if(contextMap&&targetMap->id!=contextMap->id)
                    addIssue(result,ValidationSeverity::Error,QStringLiteral("event.mapCall.crossMap"),commandLocation,QObject::tr("Chamar Evento do Mapa só funciona no mapa atual. Para usar outro mapa, transfira o jogador antes da chamada."));
                const QString eventId=command.params.value(QStringLiteral("eventId")).toString();
                const MapEvent* targetEvent=nullptr;for(const MapEvent& ev:targetMap->events)if(ev.id==eventId){targetEvent=&ev;break;}
                if(!targetEvent) addIssue(result,ValidationSeverity::Error,QStringLiteral("event.mapCall.event"),commandLocation,QObject::tr("O Map Event chamado não existe mais."));
                else {const int page=command.params.value(QStringLiteral("pageIndex"),-1).toInt();if(page < -1 || page>=targetEvent->pages.size())addIssue(result,ValidationSeverity::Error,QStringLiteral("event.mapCall.page"),commandLocation,QObject::tr("A página selecionada do Map Event não existe."));}
            }
        } else if (command.type == QLatin1String("common.reserve")) {
            const QString commonId=command.params.value(QStringLiteral("commonId")).toString();const int number=command.params.value(QStringLiteral("number")).toInt();
            const CommonEvent* targetCommon=!commonId.isEmpty()?ed.legacyImport.commonEventById(commonId):ed.legacyImport.commonEventByNumber(number);
            if(!targetCommon) addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.reserve.missing"),commandLocation,QObject::tr("O Evento Comum reservado não existe."));
            else {
                if(!commonId.isEmpty() && number > 0 && number != targetCommon->number)
                    addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.common.reserve.numberMismatch"),commandLocation,QObject::tr("A reserva usa o ID estável correto, mas o número legível está desatualizado (#%1 → #%2). Salvar/reabrir normaliza a referência.").arg(number).arg(targetCommon->number),true);
                const QVariantMap arguments=command.params.value(QStringLiteral("arguments")).toMap();QSet<QString> ids;
                for(const CommonEventParameter& parameter:targetCommon->parameters){ids.insert(parameter.id);if(parameter.required&&!arguments.contains(parameter.id))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.reserve.required"),commandLocation,QObject::tr("O parâmetro obrigatório “%1” não recebeu valor.").arg(parameter.name));else if(arguments.contains(parameter.id)){QString problem;if(!commonSourceMatchesType(ed,arguments.value(parameter.id).toMap(),parameter.type,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.reserve.type"),commandLocation,QObject::tr("Parâmetro “%1”: %2").arg(parameter.name,problem));}}
                for(auto it=arguments.constBegin();it!=arguments.constEnd();++it)if(!ids.contains(it.key()))addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.common.reserve.orphan"),commandLocation,QObject::tr("A reserva contém argumento de uma assinatura antiga."));
                const int priority=command.params.value(QStringLiteral("priority"),0).toInt();if(priority < -1000 || priority > 1000)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.common.reserve.priority"),commandLocation,QObject::tr("A prioridade da reserva precisa ficar entre -1000 e 1000."));
            }
        } else if (command.type == QLatin1String("flow.exit")) {
            const QString scope=command.params.value(QStringLiteral("scope"),QStringLiteral("frame")).toString();
            if(!QSet<QString>{QStringLiteral("frame"),QStringLiteral("map"),QStringLiteral("common"),QStringLiteral("all")}.contains(scope))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.flowExit.scope"),commandLocation,QObject::tr("O escopo de Return/Exit é inválido."));
            if(scope==QLatin1String("common")){
                if(!contextCommon)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.flowExit.commonOutside"),commandLocation,QObject::tr("Return/Exit do Evento Comum só pode ser criado dentro de um Evento Comum."));
                else if(contextCommon->returnValue.enabled&&!command.params.value(QStringLiteral("sourceSpec")).toMap().isEmpty()){QString problem;if(!commonSourceMatchesType(ed,command.params.value(QStringLiteral("sourceSpec")).toMap(),contextCommon->returnValue.type,contextCommon,&problem))addIssue(result,ValidationSeverity::Error,QStringLiteral("event.flowExit.returnType"),commandLocation,problem);}
            }
            if(contextCommon&&scope==QLatin1String("map"))
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("ctx.flow-event-in-common"),commandLocation,QObject::tr("Sair do Map Event dentro de um Evento Comum depende de um Map Event chamador; em Autorun, Parallel ou reserva independente não há frame de mapa para encerrar."));
        } else if (command.type == QLatin1String("common.call")) {
            const QString commonId = command.params.value(QStringLiteral("commonId")).toString();
            const int number = command.params.value(QStringLiteral("number")).toInt();
            const CommonEvent* targetCommon = !commonId.isEmpty() ? ed.legacyImport.commonEventById(commonId)
                                                                  : ed.legacyImport.commonEventByNumber(number);
            if (!targetCommon) {
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.missing"),
                         commandLocation, QObject::tr("O evento comum chamado não existe."));
            } else {
                if (!commonId.isEmpty() && number > 0 && number != targetCommon->number)
                    addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.common.numberMismatch"),
                             commandLocation,
                             QObject::tr("A chamada usa o ID estável correto, mas o número legível está desatualizado (#%1 → #%2). Salvar/reabrir o projeto normaliza essa referência.")
                                 .arg(number).arg(targetCommon->number), true);
                const QVariantMap arguments = command.params.value(QStringLiteral("arguments")).toMap();
                QSet<QString> parameterIds;
                for (const CommonEventParameter& parameter : targetCommon->parameters) {
                    parameterIds.insert(parameter.id);
                    if (parameter.required && !arguments.contains(parameter.id)) {
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.required"),
                                 commandLocation, QObject::tr("O parâmetro obrigatório “%1” não recebeu valor.").arg(parameter.name));
                        continue;
                    }
                    if (arguments.contains(parameter.id)) {
                        QString problem;
                        if (!commonSourceMatchesType(ed, arguments.value(parameter.id).toMap(), parameter.type, contextCommon, &problem))
                            addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.type"),
                                     commandLocation, QObject::tr("Parâmetro “%1”: %2").arg(parameter.name, problem));
                    }
                }
                for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
                    if (!parameterIds.contains(it.key()))
                        addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.common.argument.orphan"),
                                 commandLocation, QObject::tr("A chamada contém um argumento de uma assinatura antiga. Reabra o comando para remapear."));
                    const QVariantMap spec = it.value().toMap();
                    const QString sourceKind = spec.value(QStringLiteral("source"), QStringLiteral("constant")).toString();
                    if (sourceKind == QLatin1String("commonValue") &&
                        !commonValueExists(contextCommon, spec.value(QStringLiteral("commonValueId")).toString()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.local"),
                                 commandLocation, QObject::tr("Um argumento referencia um parâmetro/local que não existe neste Evento Comum."));
                    else if (sourceKind == QLatin1String("variable") &&
                             !variableExists(ed, spec.value(QStringLiteral("variableId")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.variable"),
                                 commandLocation, QObject::tr("Um argumento referencia uma variável global que não existe."));
                    else if (sourceKind == QLatin1String("switch") &&
                             !switchExists(ed, spec.value(QStringLiteral("switchId")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.switch"),
                                 commandLocation, QObject::tr("Um argumento referencia um interruptor global que não existe."));
                    else if (sourceKind == QLatin1String("string") &&
                             !stringExists(ed, spec.value(QStringLiteral("stringId")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.argument.string"),
                                 commandLocation, QObject::tr("Um argumento referencia uma String Global que não existe."));
                }
                const QVariantMap ret = command.params.value(QStringLiteral("returnTarget")).toMap();
                const QString retKind = ret.value(QStringLiteral("target"), QStringLiteral("none")).toString();
                if (retKind != QLatin1String("none") && !targetCommon->returnValue.enabled)
                    addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.common.return.unavailable"),
                             commandLocation, QObject::tr("A chamada tenta guardar um retorno, mas o Evento Comum chamado não possui saída habilitada."));
                if (retKind == QLatin1String("variable")) {
                    if (!variableExists(ed, ret.value(QStringLiteral("id")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.variable"),
                                 commandLocation, QObject::tr("A variável que recebe o retorno não existe."));
                    if (targetCommon->returnValue.enabled && targetCommon->returnValue.type != CommonValueType::Number)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.type"),
                                 commandLocation, QObject::tr("Variável Global só pode receber retorno do tipo Número."));
                }
                if (retKind == QLatin1String("switch")) {
                    if (!switchExists(ed, ret.value(QStringLiteral("id")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.switch"),
                                 commandLocation, QObject::tr("O interruptor que recebe o retorno não existe."));
                    if (targetCommon->returnValue.enabled && targetCommon->returnValue.type != CommonValueType::Boolean)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.type"),
                                 commandLocation, QObject::tr("Switch Global só pode receber retorno Booleano."));
                }
                if (retKind == QLatin1String("string")) {
                    if (!stringExists(ed, ret.value(QStringLiteral("id")).toInt()))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.string"),
                                 commandLocation, QObject::tr("A String Global que recebe o retorno não existe."));
                    if (targetCommon->returnValue.enabled && targetCommon->returnValue.type != CommonValueType::Text)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.type"),
                                 commandLocation, QObject::tr("String Global só pode receber retorno do tipo Texto."));
                }
                if (retKind == QLatin1String("commonValue")) {
                    CommonValueType receiverType = CommonValueType::Number;
                    if (!commonLocalTypeForId(contextCommon, ret.value(QStringLiteral("commonValueId")).toString(), &receiverType))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.local"),
                                 commandLocation, QObject::tr("A variável local que recebe o retorno não existe neste Evento Comum."));
                    else if (targetCommon->returnValue.enabled && receiverType != targetCommon->returnValue.type)
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.type"),
                                 commandLocation, QObject::tr("O tipo do parâmetro/local de destino não corresponde ao retorno."));
                }
                if (retKind != QLatin1String("none") && retKind != QLatin1String("variable") &&
                    retKind != QLatin1String("switch") && retKind != QLatin1String("string") &&
                    retKind != QLatin1String("commonValue"))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.target"),
                             commandLocation, QObject::tr("O destino do retorno é desconhecido."));
            }
        } else if (command.type == QLatin1String("common.local.set")) {
            CommonValueType targetType = CommonValueType::Number;
            const QString targetId = command.params.value(QStringLiteral("id")).toString();
            if (!contextCommon)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.local.outside"), commandLocation,
                         QObject::tr("Mudar parâmetro/local só pode ser usado dentro de um Evento Comum."));
            else if (!commonValueTypeForId(contextCommon, targetId, &targetType))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.local.missing"), commandLocation,
                         QObject::tr("O parâmetro/local de destino não existe mais na assinatura."));
            else {
                const QString op = command.params.value(QStringLiteral("op"), QStringLiteral("=")).toString();
                const bool validOp = targetType == CommonValueType::Number
                    ? QSet<QString>{QStringLiteral("="),QStringLiteral("+"),QStringLiteral("-"),QStringLiteral("*"),QStringLiteral("/"),QStringLiteral("%")}.contains(op)
                    : targetType == CommonValueType::Boolean
                        ? QSet<QString>{QStringLiteral("="),QStringLiteral("toggle")}.contains(op)
                        : QSet<QString>{QStringLiteral("="),QStringLiteral("+")}.contains(op);
                if (!validOp)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.local.op"), commandLocation,
                             QObject::tr("A operação não é válida para o tipo do parâmetro/local."));
                if (op != QLatin1String("toggle")) {
                    QString problem;
                    if (!commonSourceMatchesType(ed, normalizedCommonSourceSpec(command.params), targetType, contextCommon, &problem))
                        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.local.source"), commandLocation, problem);
                }
            }
        } else if (command.type == QLatin1String("common.return")) {
            if (!contextCommon)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.outside"), commandLocation,
                         QObject::tr("Retornar só pode ser usado dentro de um Evento Comum."));
            else if (!contextCommon->returnValue.enabled)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.disabled"), commandLocation,
                         QObject::tr("Este Evento Comum não possui retorno habilitado na Assinatura."));
            if (contextCommon && contextCommon->returnValue.enabled) {
                QString problem;
                if (!commonSourceMatchesType(ed, normalizedCommonSourceSpec(command.params),
                                             contextCommon->returnValue.type, contextCommon, &problem))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.common.return.source"),
                             commandLocation, problem);
            }
        } else if (command.type == QLatin1String("localization.set")) {
            const QString locale = command.params.value(QStringLiteral("locale")).toString().trimmed();
            const QStringList enabled = ed.legacyImport.localization.enabledLocaleCodes();
            bool found = false;
            for (const QString& code : enabled)
                if (code.compare(locale, Qt::CaseInsensitive) == 0) { found = true; break; }
            if (!ed.legacyImport.localization.enabled)
                addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.localization.disabled"),
                         commandLocation, QObject::tr("O comando altera idioma, mas a localização do projeto está desativada."));
            else if (locale.isEmpty() || !found)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.localization.locale"),
                         commandLocation, QObject::tr("O idioma escolhido neste comando não existe ou está desativado."));
        } else if (command.type == QLatin1String("battle.start")) {
            const QString id = command.params.value(QStringLiteral("troopId")).toString();
            bool found = false;
            for (const DatabaseRecord& troop : ed.legacyImport.database.value(QStringLiteral("troops")))
                if (troop.id == id) { found = true; break; }
            if (!found)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.troop.missing"),
                         commandLocation, QObject::tr("A tropa escolhida para a batalha não existe."));
        } else if (command.type == QLatin1String("jump")) {
            const QString label = command.params.value(QStringLiteral("label")).toString();
            if (label.isEmpty() || !labels.contains(label))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.label.missing"),
                         commandLocation, QObject::tr("O rótulo de destino não existe nesta lista."));
        } else if (command.type == QLatin1String("choice.show")) {
            QStringList choices = command.params.value(QStringLiteral("choices")).toStringList();
            if (choices.isEmpty())
                for (const QVariant& value : command.params.value(QStringLiteral("choices")).toList())
                    choices.push_back(value.toString());
            if (choices.size() < 2)
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.choice.options"),
                         commandLocation, QObject::tr("Uma escolha precisa de pelo menos duas opções."));
            if(choices.size()>8)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.limit"),
                         commandLocation,QObject::tr("Uma escolha pode exibir no máximo oito opções."));
            const QVariantList disabledChoices=command.params.value(QStringLiteral("disabledChoices")).toList();
            if(!disabledChoices.isEmpty()&&disabledChoices.size()!=choices.size())
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.disabled-count"),commandLocation,QObject::tr("A quantidade de estados bloqueados não corresponde às opções. As opções sem estado definido serão consideradas habilitadas."));
            bool anyEnabled=disabledChoices.isEmpty();for(int i=0;i<choices.size();++i)if(!disabledChoices.value(i).toBool()){anyEnabled=true;break;}
            if(!anyEnabled)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.all-disabled"),commandLocation,QObject::tr("Todas as escolhas estão bloqueadas; o jogador não poderá confirmar uma opção."));
            const QVariantList choiceConditions=command.params.value(QStringLiteral("conditions")).toList();
            if(!choiceConditions.isEmpty()&&choiceConditions.size()!=choices.size())addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.condition-count"),commandLocation,QObject::tr("A quantidade de condições não corresponde às opções; itens ausentes permanecem disponíveis."));
            for(int conditionIndex=0;conditionIndex<choiceConditions.size();++conditionIndex){const QVariantMap spec=choiceConditions.at(conditionIndex).toMap();QVariantMap tree=spec.value(QStringLiteral("conditionTree"),spec.value(QStringLiteral("condition"))).toMap();if(tree.isEmpty()&&isConditionTreeNode(spec))tree=spec;if(tree.isEmpty())continue;const QString problem=conditionTreeStructureProblem(tree,64);if(!problem.isEmpty())addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.condition-structure"),QObject::tr("%1 / condição da escolha %2").arg(commandLocation).arg(conditionIndex+1),problem);}
            const double timeLimit=command.params.value(QStringLiteral("timeLimit"),0.0).toDouble();if(!std::isfinite(timeLimit)||timeLimit<0.0)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.time-limit"),commandLocation,QObject::tr("O tempo limite não pode ser negativo."));
            if(command.params.contains(QStringLiteral("showDisabledReason"))&&command.params.value(QStringLiteral("showDisabledReason")).metaType().id()!=QMetaType::Bool)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.disabled-reason-flag"),commandLocation,QObject::tr("“Mostrar motivo de bloqueio” precisa ser verdadeiro ou falso. O valor será ajustado automaticamente."));
            const int timedDefault=command.params.value(QStringLiteral("defaultChoice"),-1).toInt();if(timedDefault < -1 || timedDefault >= choices.size())addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.timed-default"),commandLocation,QObject::tr("A escolha automática ao esgotar o tempo está fora da faixa."));
            if(timeLimit>0.0&&timedDefault<0&&command.params.value(QStringLiteral("cancelValue"),0).toInt()<0)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.timer-no-cancel"),commandLocation,QObject::tr("O tempo expira em Cancelar, mas cancelar está desativado. A primeira opção disponível será escolhida."));
            const QString layout=command.params.value(QStringLiteral("layout"),QStringLiteral("vertical")).toString();
            if(layout!=QLatin1String("vertical")&&layout!=QLatin1String("horizontal")&&layout!=QLatin1String("grid"))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.layout"),commandLocation,QObject::tr("O layout da escolha é inválido."));
            const int columns=command.params.value(QStringLiteral("columns"),1).toInt();if(columns<1||columns>8)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.columns"),commandLocation,QObject::tr("A quantidade de colunas será limitada entre 1 e 8."));
            for(const QString& key:{QStringLiteral("spacingX"),QStringLiteral("spacingY")}){const int value=command.params.value(key,key==QLatin1String("spacingX")?6:4).toInt();if(value<0||value>128)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.spacing"),commandLocation,QObject::tr("O espaçamento das escolhas será limitado entre 0 e 128 px."));}
            const QString alignment=command.params.value(QStringLiteral("alignment"),QStringLiteral("left")).toString();if(alignment!=QLatin1String("left")&&alignment!=QLatin1String("center")&&alignment!=QLatin1String("right"))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.alignment"),commandLocation,QObject::tr("O alinhamento das escolhas é inválido."));
            const QString boxMode=command.params.value(QStringLiteral("boxMode"),QStringLiteral("theme")).toString();if(boxMode!=QLatin1String("theme")&&boxMode!=QLatin1String("transparent"))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.box-mode"),commandLocation,QObject::tr("O modo da caixa de escolhas é inválido."));
            const QString family=command.params.value(QStringLiteral("fontFamily")).toString().trimmed();if(!family.isEmpty()&&!projectFontFamilyKnown(ed,family))
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("text.font.reference"),commandLocation,QObject::tr("A fonte ‘%1’ usada nas escolhas não está cadastrada nas Fontes do projeto; ela pode faltar no Player exportado.").arg(family));
            const int choiceFontSize=command.params.value(QStringLiteral("fontSize"),0).toInt();if(choiceFontSize<0||choiceFontSize>96)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.font-size"),commandLocation,QObject::tr("O tamanho da fonte das escolhas será normalizado."));
            const int variableId = command.params.value(QStringLiteral("resultVariable")).toInt();
            if (!variableExists(ed, variableId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.choice.variable"),
                         commandLocation, QObject::tr("A variável que recebe a escolha não existe."));
            const int cancelValue=command.params.value(QStringLiteral("cancelValue"),0).toInt();
            if(cancelValue != -1 && cancelValue != 0)
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.choice.cancel"),
                         commandLocation,QObject::tr("O resultado de cancelamento da escolha está fora da faixa válida."));
            const QVariantList branches=command.params.value(QStringLiteral("branches")).toList();
            if(!branches.isEmpty()&&branches.size()!=choices.size())
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.choice.branches"),
                         commandLocation,QObject::tr("A quantidade de ramificações não corresponde às opções; revise a escolha."));
            for(int branch=0;branch<branches.size();++branch){
                const QVector<EventCommand> nested=eventCommandsFromVariantList(branches.at(branch).toList());
                validateEventCommands(ed,nested,
                    QObject::tr("%1 / escolha %2").arg(commandLocation).arg(branch+1),result,contextMap,contextCommon,false);
            }
        } else if(command.type==QLatin1String("input.wait")){
            bool ok=false;gameActionFromId(command.params.value(QStringLiteral("action")).toString(),&ok);
            if(!ok)addIssue(result,ValidationSeverity::Error,QStringLiteral("event.input.action"),commandLocation,QObject::tr("A ação de Input escolhida não existe."));
            const QString state=command.params.value(QStringLiteral("state"),QStringLiteral("pressed")).toString();
            if(state!=QLatin1String("pressed")&&state!=QLatin1String("held")&&state!=QLatin1String("released"))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.input.state"),commandLocation,QObject::tr("O estado de Input é inválido."));
        } else if(command.type==QLatin1String("input.number")){
            const int variableId=command.params.value(QStringLiteral("variableId")).toInt();
            if(!variableExists(ed,variableId))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.input.variable"),
                         commandLocation,QObject::tr("A variável que recebe o número não existe."));
            double minimum=0.0,maximum=0.0;
            if(staticNumberValue(command.params.value(QStringLiteral("minimum"),0),&minimum)&&staticNumberValue(command.params.value(QStringLiteral("maximum"),9999),&maximum)&&minimum>maximum)
                addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.input.range"),
                         commandLocation,QObject::tr("O intervalo numérico está invertido; o player irá corrigi-lo."));
        } else if (command.type == QLatin1String("input.confirm")) {
            const int variableId = command.params.value(QStringLiteral("resultVariable")).toInt();
            if (!variableExists(ed, variableId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.confirm.variable"),
                         commandLocation, QObject::tr("A variável que recebe a confirmação não existe."));
        } else if (command.type == QLatin1String("input.item")) {
            const int variableId = command.params.value(QStringLiteral("variableId")).toInt();
            if (!variableExists(ed, variableId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.iteminput.variable"),
                         commandLocation, QObject::tr("A variável que recebe o item não existe."));
            const QString category = command.params.value(QStringLiteral("category"), QStringLiteral("items")).toString();
            const QSet<QString> validCategories{QStringLiteral("items"), QStringLiteral("weapons"),
                                                QStringLiteral("armors")};
            if (!validCategories.contains(category))
                addIssue(result, ValidationSeverity::Warning, QStringLiteral("event.iteminput.category"),
                         commandLocation, QObject::tr("A categoria da seleção de item é desconhecida."));
        } else if (command.type == QLatin1String("shop.open")) {
            QStringList itemIds = command.params.value(QStringLiteral("itemIds")).toStringList();
            if (itemIds.isEmpty())
                for (const QVariant& value : command.params.value(QStringLiteral("itemIds")).toList())
                    itemIds.push_back(value.toString());
            if (itemIds.isEmpty())
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.shop.empty"),
                         commandLocation, QObject::tr("A loja não possui produtos."));
            for (const QString& id : itemIds) {
                bool found = false;
                for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"),
                                                QStringLiteral("armors")})
                    for (const DatabaseRecord& record : ed.legacyImport.database.value(category))
                        if (record.id == id) { found = true; break; }
                if (!found)
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.shop.item"),
                             commandLocation, QObject::tr("A loja contém um produto apagado."));
            }
        } else if (command.type == QLatin1String("party.change") ||
                   command.type.startsWith(QLatin1String("actor."))) {
            const QString actorId = command.params.value(QStringLiteral("actorId")).toString();
            // ID vazio significa "todo o grupo" nos comandos que oferecem
            // essa escolha visual; não é uma referência quebrada.
            if ((!actorId.isEmpty() || command.type == QLatin1String("party.change") ||
                 command.type == QLatin1String("actor.equip")) &&
                !recordExists(ed, QStringLiteral("actors"), actorId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.actor.missing"),
                         commandLocation, QObject::tr("O personagem escolhido não existe."));
            if (command.type == QLatin1String("actor.equip")) {
                const QString itemId = command.params.value(QStringLiteral("itemId")).toString();
                if (!itemId.isEmpty() && !inventoryRecordExists(ed, itemId))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.equipment.missing"),
                             commandLocation, QObject::tr("O equipamento escolhido não existe."));
            } else if (command.type == QLatin1String("actor.state")) {
                const QString stateId = command.params.value(QStringLiteral("stateId")).toString();
                if (!recordExists(ed, QStringLiteral("states"), stateId))
                    addIssue(result, ValidationSeverity::Error, QStringLiteral("event.state.missing"),
                             commandLocation, QObject::tr("O estado escolhido não existe."));
            }
        } else if (command.type == QLatin1String("inventory.change")) {
            const QString itemId = command.params.value(QStringLiteral("itemId")).toString();
            if (!inventoryRecordExists(ed, itemId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.item.missing"),
                         commandLocation, QObject::tr("O item escolhido não existe."));
        } else if (command.type.startsWith(QLatin1String("quest."))) {
            const QString questId = command.params.value(QStringLiteral("questId")).toString();
            if (!recordExists(ed, QStringLiteral("quests"), questId))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.quest.missing"),
                         commandLocation, QObject::tr("A missão escolhida não existe."));
        } else if (command.type == QLatin1String("audio.footstep")) {
            const QString surfaceId=command.params.value(QStringLiteral("surfaceId")).toString().trimmed();
            if(!surfaceId.isEmpty()&&!ed.legacyImport.footstepSurfaceById(surfaceId))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("ref.missing-surface"),commandLocation,QObject::tr("A superfície de passos escolhida não existe."));
            const int volume=command.params.value(QStringLiteral("volume"),100).toInt();
            if(volume<0||volume>100)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.footstep.volume"),commandLocation,QObject::tr("O volume será limitado ao intervalo 0–100."),true);
        } else if (command.type.startsWith(QLatin1String("audio.")) &&
                   command.type != QLatin1String("audio.stop")) {
            const QString source = command.params.value(QStringLiteral("source")).toString();
            if (!source.isEmpty() && !assetExists(ed, source))
                addIssue(result, ValidationSeverity::Error, QStringLiteral("event.audio.missing"),
                         commandLocation, QObject::tr("O arquivo de áudio escolhido não existe."));
            const int volume=command.params.value(QStringLiteral("volume"),90).toInt();
            const int pitch=command.params.value(QStringLiteral("pitch"),100).toInt();
            const int pan=command.params.value(QStringLiteral("pan"),0).toInt();
            if(volume<0||volume>100)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.audio.volume"),commandLocation,QObject::tr("O volume será limitado ao intervalo 0–100."),true);
            if(pitch<50||pitch>200)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.audio.pitch"),commandLocation,QObject::tr("O tom e a velocidade serão ajustados para ficar entre 50% e 200%."),true);
            if(pan<-100||pan>100)addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.audio.pan"),commandLocation,QObject::tr("A posição estéreo será ajustada para ficar entre totalmente à esquerda e totalmente à direita."),true);
        } else if(command.type==QLatin1String("weather.set")){
            WeatherState weather;
            weather.setConfig(command.params.value(QStringLiteral("type")).toString(),
                              command.params.value(QStringLiteral("intensity"),50).toInt(),
                              command.params.value(QStringLiteral("thunderSe")).toString(),
                              command.params.value(QStringLiteral("thunderVolume"),90).toInt(), true);
            if(weather.kind==WeatherKind::Storm&&!weather.thunderSePath.isEmpty()&&
               !assetExists(ed,weather.thunderSePath))
                addIssue(result,ValidationSeverity::Error,QStringLiteral("event.weather.thunder"),
                         commandLocation,QObject::tr("O SE de trovão escolhido não existe."));
        } else if (command.type == QLatin1String("plugin.call")) {
            const PluginInvocationResolution resolution = resolvePluginInvocation(
                ed.legacyImport.plugins,
                command.params.value(QStringLiteral("pluginId")).toString(),
                command.params.value(QStringLiteral("commandId")).toString(),
                command.params.value(QStringLiteral("arguments")).toMap());
            if (!resolution.ready()) {
                QString issueCode = QStringLiteral("event.plugin.%1").arg(pluginInvocationStatusId(resolution.status));
                // Preserve stable diagnostic IDs used by navigation/older tooling while
                // the resolution itself is centralized by Plugin Contract 2.0.
                if (resolution.status == PluginInvocationStatus::MissingPlugin)
                    issueCode = QStringLiteral("ref.missing-plugin");
                else if (resolution.status == PluginInvocationStatus::DisabledPlugin)
                    issueCode = QStringLiteral("event.plugin.disabled");
                else if (resolution.status == PluginInvocationStatus::MissingCommand)
                    issueCode = QStringLiteral("event.plugin.command");
                addIssue(result, ValidationSeverity::Error, issueCode, commandLocation, resolution.error);
            } else {
                validateEventCommands(ed, resolution.expandedCommands,
                    commandLocation + QObject::tr(" / expansão do plugin"), result, contextMap, contextCommon, false);
            }
        }
    }
    if (ifDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.if.unclosed"), location,
                 QObject::tr("Há %1 condição(ões) sem fim.").arg(ifDepth));
    if (loopDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.loop.unclosed"), location,
                 QObject::tr("Há %1 repetição(ões) contínua(s) sem fim.").arg(loopDepth));
    if (repeatDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.repeat.unclosed"), location,
                 QObject::tr("Há %1 bloco(s) Repetir N vezes sem fim.").arg(repeatDepth));
    if (databaseEachDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.database.each.unclosed"), location,
                 QObject::tr("Há %1 percurso(s) de Banco de Dados sem fim.").arg(databaseEachDepth));
    if (parallelDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.parallel.unclosed"), location,
                 QObject::tr("Há %1 bloco(s) Parallel sem fim.").arg(parallelDepth));
    if (cutsceneDepth > 0)
        addIssue(result, ValidationSeverity::Error, QStringLiteral("event.cutscene.unclosed"), location,
                 QObject::tr("Há %1 região(ões) de cutscene pulável sem fim.").arg(cutsceneDepth));
    if(!pendingCutsceneSettings.isEmpty())
        addIssue(result,ValidationSeverity::Warning,QStringLiteral("event.cutscene.settings.unused"),location,QObject::tr("Há uma configuração de cutscene sem um Início posterior no mesmo fluxo."));
}

} // namespace core
