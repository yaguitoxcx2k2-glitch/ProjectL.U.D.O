#include "UiDataBinding.h"

#include "game/GameState.h"
#include "game/RpgSystem.h"

#include <QRegularExpression>
#include <QtGlobal>

#include <cmath>
#include <functional>

namespace game::ui {
namespace {

const PartyMemberState* memberFor(const core::UiDataBindingSettings& binding,
                                  const GameState& state)
{
    const QString requestedActor = binding.actorId.trimmed();
    if (!requestedActor.isEmpty())
        return state.partyMember(requestedActor);
    return state.party().isEmpty() ? nullptr : &state.party().first();
}

QString actorName(const core::Editor& editor, const PartyMemberState* member)
{
    if (!member) return {};
    return databaseRecordName(editor, QStringLiteral("actors"), member->actorId, member->actorId);
}

QString actorClassName(const core::Editor& editor, const PartyMemberState* member)
{
    if (!member) return {};
    const core::DatabaseRecord* actor = databaseRecord(editor, QStringLiteral("actors"), member->actorId);
    if (!actor) return {};
    return databaseRecordName(editor, QStringLiteral("classes"),
                              actor->data.value(QStringLiteral("classId")).toString());
}

QString actorStatesText(const core::Editor& editor, const PartyMemberState* member)
{
    if (!member) return {};
    QStringList names;
    for (const QString& stateId : member->states) {
        const QString name = databaseRecordName(editor, QStringLiteral("states"), stateId, stateId);
        if (!name.isEmpty()) names.push_back(name);
    }
    return names.join(QStringLiteral(", "));
}

QVariant runtimeToken(const QString& rawToken,
                      const core::UiDataBindingSettings& binding,
                      const core::Editor& editor,
                      const GameState& state,
                      const QVariant& currentValue,
                      bool* ok)
{
    const QString token = rawToken.trimmed();
    if (token.compare(QStringLiteral("value"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = currentValue.isValid();
        return currentValue;
    }

    const PartyMemberState* member = memberFor(binding, state);
    const CombatStats stats = member ? memberStats(editor, *member) : CombatStats{};
    if (token.compare(QStringLiteral("Party.Gold"), Qt::CaseInsensitive) == 0) return state.gold();
    if (token.compare(QStringLiteral("Party.Size"), Qt::CaseInsensitive) == 0) return state.party().size();
    if (token.compare(QStringLiteral("Actor.Name"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return actorName(editor, member);
    }
    if (token.compare(QStringLiteral("Actor.Class"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return actorClassName(editor, member);
    }
    if (token.compare(QStringLiteral("Actor.Level"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(member->level) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.HP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(member->hp) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.MaxHP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(stats.maxHp) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.MP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(member->mp) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.MaxMP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(stats.maxMp) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.EXP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(member->experience) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.NextEXP"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(experienceToNextLevel(editor, *member)) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.HPPercent"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr;
        return member ? QVariant(qRound(100.0 * member->hp / qMax(1, stats.maxHp))) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.MPPercent"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr;
        return member ? QVariant(stats.maxMp > 0 ? qRound(100.0 * member->mp / stats.maxMp) : 0) : QVariant();
    }
    if (token.compare(QStringLiteral("Actor.States"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = member != nullptr; return member ? QVariant(actorStatesText(editor, member)) : QVariant();
    }

    static const QRegularExpression variableRe(QStringLiteral("^Variable\\s*:\\s*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression switchRe(QStringLiteral("^Switch\\s*:\\s*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression itemRe(QStringLiteral("^ItemCount\\s*:\\s*(.+)$"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = variableRe.match(token);
    if (match.hasMatch()) return state.variable(match.captured(1).toInt());
    match = switchRe.match(token);
    if (match.hasMatch()) return state.switchOn(match.captured(1).toInt());
    match = itemRe.match(token);
    if (match.hasMatch()) return state.itemCount(match.captured(1).trimmed());

    if (ok) *ok = false;
    return {};
}

QVariant previewToken(const QString& rawToken,
                      const core::UiDataBindingSettings& binding,
                      const core::UiDataPreviewSettings& preview,
                      const QVariant& currentValue,
                      bool* ok)
{
    Q_UNUSED(binding);
    const QString token = rawToken.trimmed();
    if (token.compare(QStringLiteral("value"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = currentValue.isValid();
        return currentValue;
    }
    if (token.compare(QStringLiteral("Party.Gold"), Qt::CaseInsensitive) == 0) return preview.gold;
    if (token.compare(QStringLiteral("Party.Size"), Qt::CaseInsensitive) == 0) return preview.partySize;
    if (token.compare(QStringLiteral("Actor.Name"), Qt::CaseInsensitive) == 0) return preview.actorName;
    if (token.compare(QStringLiteral("Actor.Class"), Qt::CaseInsensitive) == 0) return QStringLiteral("Classe");
    if (token.compare(QStringLiteral("Actor.Level"), Qt::CaseInsensitive) == 0) return preview.actorLevel;
    if (token.compare(QStringLiteral("Actor.HP"), Qt::CaseInsensitive) == 0) return preview.actorHp;
    if (token.compare(QStringLiteral("Actor.MaxHP"), Qt::CaseInsensitive) == 0) return preview.actorMaxHp;
    if (token.compare(QStringLiteral("Actor.MP"), Qt::CaseInsensitive) == 0) return preview.actorMp;
    if (token.compare(QStringLiteral("Actor.MaxMP"), Qt::CaseInsensitive) == 0) return preview.actorMaxMp;
    if (token.compare(QStringLiteral("Actor.EXP"), Qt::CaseInsensitive) == 0) return preview.actorExp;
    if (token.compare(QStringLiteral("Actor.NextEXP"), Qt::CaseInsensitive) == 0) return preview.actorNextExp;
    if (token.compare(QStringLiteral("Actor.HPPercent"), Qt::CaseInsensitive) == 0)
        return qRound(100.0 * preview.actorHp / qMax(1, preview.actorMaxHp));
    if (token.compare(QStringLiteral("Actor.MPPercent"), Qt::CaseInsensitive) == 0)
        return preview.actorMaxMp > 0 ? qRound(100.0 * preview.actorMp / preview.actorMaxMp) : 0;
    if (token.compare(QStringLiteral("Actor.States"), Qt::CaseInsensitive) == 0) return QStringLiteral("Normal");

    static const QRegularExpression variableRe(QStringLiteral("^Variable\\s*:\\s*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression switchRe(QStringLiteral("^Switch\\s*:\\s*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression itemRe(QStringLiteral("^ItemCount\\s*:\\s*(.+)$"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = variableRe.match(token);
    if (match.hasMatch()) return preview.variables.value(match.captured(1).toInt(), 0);
    match = switchRe.match(token);
    if (match.hasMatch()) return preview.switches.value(match.captured(1).toInt(), false);
    match = itemRe.match(token);
    if (match.hasMatch()) return preview.itemCounts.value(match.captured(1).trimmed(), 0);

    if (ok) *ok = false;
    return {};
}

QString renderTemplate(const QString& format, const QString& fallback,
                       const std::function<QVariant(const QString&, bool*)>& tokenResolver,
                       bool* ok)
{
    QString output = format.isEmpty() ? QStringLiteral("{value}") : format;
    static const QRegularExpression tokenRe(QStringLiteral("\\{([^{}]+)\\}"));
    int offset = 0;
    bool allOk = true;
    while (true) {
        const QRegularExpressionMatch match = tokenRe.match(output, offset);
        if (!match.hasMatch()) break;
        bool tokenOk = true;
        const QVariant value = tokenResolver(match.captured(1), &tokenOk);
        const QString replacement = tokenOk && value.isValid() ? value.toString()
            : (fallback.isEmpty() ? QStringLiteral("—") : fallback);
        allOk = allOk && tokenOk;
        output.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    if (ok) *ok = allOk;
    return output;
}

bool variantBool(const QVariant& value, bool* ok)
{
    if (!value.isValid()) { if (ok) *ok = false; return false; }
    if (value.metaType().id() == QMetaType::Bool) { if (ok) *ok = true; return value.toBool(); }
    bool numberOk = false;
    const double number = value.toDouble(&numberOk);
    if (numberOk) { if (ok) *ok = true; return !qFuzzyIsNull(number); }
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("on") || text == QLatin1String("yes") || text == QLatin1String("sim")) {
        if (ok) *ok = true; return true;
    }
    if (text == QLatin1String("false") || text == QLatin1String("off") || text == QLatin1String("no") || text == QLatin1String("nao") || text == QStringLiteral("não")) {
        if (ok) *ok = true; return false;
    }
    if (ok) *ok = false;
    return false;
}

double variantNumber(const QVariant& value, bool* ok)
{
    bool localOk = false;
    const double number = value.toDouble(&localOk);
    if (ok) *ok = localOk;
    return localOk && std::isfinite(number) ? number : 0.0;
}

QVariant fallbackValue(const core::UiDataBindingSettings& binding)
{
    return binding.fallback.isEmpty() ? QVariant() : QVariant(binding.fallback);
}

template <typename SourceFn, typename TextFn>
UiResolvedDataBindings resolveAll(const core::UiLayoutElementSettings& element,
                                  SourceFn&& sourceFn, TextFn&& textFn)
{
    UiResolvedDataBindings result;
    for (const core::UiDataBindingSettings& binding : element.dataBindings) {
        if (!binding.enabled || binding.property.trimmed().isEmpty()) continue;
        const QString property = binding.property.trimmed().toLower();
        bool sourceOk = true;
        QVariant value = sourceFn(binding, &sourceOk);
        if (!sourceOk || !value.isValid()) value = fallbackValue(binding);

        bool converted = true;
        if (property == QLatin1String("text")) {
            bool textOk = true;
            result.text = textFn(binding, &textOk);
            result.hasText = true;
            if (!textOk) result.warnings.push_back(QStringLiteral("text: token/fonte indisponível"));
        } else if (property == QLatin1String("value")) {
            result.value = variantNumber(value, &converted); result.hasValue = converted;
        } else if (property == QLatin1String("maximum")) {
            result.maximum = variantNumber(value, &converted); result.hasMaximum = converted;
        } else if (property == QLatin1String("progress")) {
            result.progress = qBound(0.0, variantNumber(value, &converted), 1.0); result.hasProgress = converted;
        } else if (property == QLatin1String("visible")) {
            result.visible = variantBool(value, &converted); result.hasVisible = converted;
        } else if (property == QLatin1String("enabled")) {
            result.enabled = variantBool(value, &converted); result.hasEnabled = converted;
        } else if (property == QLatin1String("opacity")) {
            result.opacity = qBound(0.0, variantNumber(value, &converted), 1.0); result.hasOpacity = converted;
        } else if (property == QLatin1String("image")) {
            result.imagePath = value.toString(); result.hasImage = !result.imagePath.isEmpty(); converted = result.hasImage;
        } else if (property == QLatin1String("color")) {
            const QColor color(value.toString());
            converted = color.isValid();
            if (converted) { result.color = color; result.hasColor = true; }
        }
        if (!converted)
            result.warnings.push_back(property + QStringLiteral(": valor inválido; usando comportamento padrão"));
    }
    result.warnings.removeDuplicates();
    return result;
}

} // namespace

QVariant UiDataBindingResolver::runtimeSourceValue(const core::UiDataBindingSettings& binding,
                                                   const core::Editor& editor,
                                                   const GameState& state,
                                                   bool* ok)
{
    if (ok) *ok = true;
    const QString source = binding.source.trimmed().toLower();
    const PartyMemberState* member = memberFor(binding, state);
    const CombatStats stats = member ? memberStats(editor, *member) : CombatStats{};

    if (source == QLatin1String("party.gold")) return state.gold();
    if (source == QLatin1String("party.size")) return state.party().size();
    if (source == QLatin1String("variable")) return state.variable(qMax(0, binding.id));
    if (source == QLatin1String("switch")) return state.switchOn(qMax(0, binding.id));
    if (source == QLatin1String("item.count")) return state.itemCount(binding.itemId.trimmed());
    if (source == QLatin1String("constant")) return binding.constantValue;
    if (source == QLatin1String("player.charset-path")) return editor.player.charsetPath;

    if (!member) { if (ok) *ok = false; return {}; }
    if (source == QLatin1String("actor.id")) return member->actorId;
    if (source == QLatin1String("actor.name")) return actorName(editor, member);
    if (source == QLatin1String("actor.class")) return actorClassName(editor, member);
    if (source == QLatin1String("actor.level")) return member->level;
    if (source == QLatin1String("actor.hp")) return member->hp;
    if (source == QLatin1String("actor.max-hp")) return stats.maxHp;
    if (source == QLatin1String("actor.mp")) return member->mp;
    if (source == QLatin1String("actor.max-mp")) return stats.maxMp;
    if (source == QLatin1String("actor.exp")) return member->experience;
    if (source == QLatin1String("actor.exp-next")) return experienceToNextLevel(editor, *member);
    if (source == QLatin1String("actor.hp-ratio")) return double(member->hp) / qMax(1, stats.maxHp);
    if (source == QLatin1String("actor.mp-ratio")) return stats.maxMp > 0 ? double(member->mp) / stats.maxMp : 0.0;
    if (source == QLatin1String("actor.state-count")) return member->states.size();
    if (source == QLatin1String("actor.states")) return actorStatesText(editor, member);
    if (source == QLatin1String("actor.graphic-path")) {
        const core::DatabaseRecord* actor = databaseRecord(editor, QStringLiteral("actors"), member->actorId);
        return actor ? actor->data.value(QStringLiteral("graphicPath")) : QVariant();
    }

    if (ok) *ok = false;
    return {};
}

QVariant UiDataBindingResolver::previewSourceValue(const core::UiDataBindingSettings& binding,
                                                   const core::UiDataPreviewSettings& preview,
                                                   bool* ok)
{
    if (ok) *ok = true;
    const QString source = binding.source.trimmed().toLower();
    if (source == QLatin1String("party.gold")) return preview.gold;
    if (source == QLatin1String("party.size")) return preview.partySize;
    if (source == QLatin1String("variable")) return preview.variables.value(qMax(0, binding.id), 0);
    if (source == QLatin1String("switch")) return preview.switches.value(qMax(0, binding.id), false);
    if (source == QLatin1String("constant")) return binding.constantValue;
    if (source == QLatin1String("actor.id")) return binding.actorId.isEmpty() ? QStringLiteral("actor-preview") : binding.actorId;
    if (source == QLatin1String("actor.name")) return preview.actorName;
    if (source == QLatin1String("actor.class")) return QStringLiteral("Classe");
    if (source == QLatin1String("actor.level")) return preview.actorLevel;
    if (source == QLatin1String("actor.hp")) return preview.actorHp;
    if (source == QLatin1String("actor.max-hp")) return preview.actorMaxHp;
    if (source == QLatin1String("actor.mp")) return preview.actorMp;
    if (source == QLatin1String("actor.max-mp")) return preview.actorMaxMp;
    if (source == QLatin1String("actor.exp")) return preview.actorExp;
    if (source == QLatin1String("actor.exp-next")) return preview.actorNextExp;
    if (source == QLatin1String("actor.hp-ratio")) return double(preview.actorHp) / qMax(1, preview.actorMaxHp);
    if (source == QLatin1String("actor.mp-ratio")) return preview.actorMaxMp > 0 ? double(preview.actorMp) / preview.actorMaxMp : 0.0;
    if (source == QLatin1String("actor.state-count")) return 0;
    if (source == QLatin1String("actor.states")) return QStringLiteral("Normal");
    if (source == QLatin1String("item.count")) return preview.itemCounts.value(binding.itemId.trimmed(), 0);
    if (source == QLatin1String("actor.graphic-path") || source == QLatin1String("player.charset-path")) {
        if (ok) *ok = false; return {};
    }
    if (ok) *ok = false;
    return {};
}

QString UiDataBindingResolver::runtimeText(const core::UiDataBindingSettings& binding,
                                           const core::Editor& editor,
                                           const GameState& state,
                                           bool* ok)
{
    bool sourceOk = true;
    QVariant current = runtimeSourceValue(binding, editor, state, &sourceOk);
    if (!sourceOk || !current.isValid()) current = fallbackValue(binding);
    bool templateOk = true;
    const QString text = renderTemplate(binding.format, binding.fallback,
        [&](const QString& token, bool* tokenOk) { return runtimeToken(token, binding, editor, state, current, tokenOk); },
        &templateOk);
    if (ok) *ok = sourceOk && templateOk;
    return text;
}

QString UiDataBindingResolver::previewText(const core::UiDataBindingSettings& binding,
                                           const core::UiDataPreviewSettings& preview,
                                           bool* ok)
{
    bool sourceOk = true;
    QVariant current = previewSourceValue(binding, preview, &sourceOk);
    if (!sourceOk || !current.isValid()) current = fallbackValue(binding);
    bool templateOk = true;
    const QString text = renderTemplate(binding.format, binding.fallback,
        [&](const QString& token, bool* tokenOk) { return previewToken(token, binding, preview, current, tokenOk); },
        &templateOk);
    if (ok) *ok = sourceOk && templateOk;
    return text;
}

UiResolvedDataBindings UiDataBindingResolver::resolveRuntime(const core::UiLayoutElementSettings& element,
                                                             const core::Editor& editor,
                                                             const GameState& state)
{
    return resolveAll(element,
        [&](const core::UiDataBindingSettings& binding, bool* ok) { return runtimeSourceValue(binding, editor, state, ok); },
        [&](const core::UiDataBindingSettings& binding, bool* ok) { return runtimeText(binding, editor, state, ok); });
}

UiResolvedDataBindings UiDataBindingResolver::resolvePreview(const core::UiLayoutElementSettings& element,
                                                             const core::UiDataPreviewSettings& preview)
{
    return resolveAll(element,
        [&](const core::UiDataBindingSettings& binding, bool* ok) { return previewSourceValue(binding, preview, ok); },
        [&](const core::UiDataBindingSettings& binding, bool* ok) { return previewText(binding, preview, ok); });
}

} // namespace game::ui
