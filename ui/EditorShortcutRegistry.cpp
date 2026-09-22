#include "EditorShortcutRegistry.h"

#include "EditorActionPalette.h"

#include <QAction>
#include <QKeySequence>
#include <QMenuBar>
#include <QObject>
#include <QSettings>
#include <QSet>
#include <QWidget>

namespace ui {
namespace {
constexpr auto kDefaultProperty = "ludoDefaultShortcut";

QString settingsKey(const QString& actionKey)
{
    return QStringLiteral("shortcuts/overrides/") + actionKey;
}

QString legacySettingsKey(const QString& actionKey)
{
    if (!actionKey.startsWith(QStringLiteral("action:rpgmaker."))) return {};
    QString legacy = actionKey;
    legacy.replace(QStringLiteral("action:rpgmaker."), QStringLiteral("action:rpgmakermz."));
    return settingsKey(legacy);
}

QString portable(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

QKeySequence sequenceFromPortable(const QString& text)
{
    return QKeySequence::fromString(text.trimmed(), QKeySequence::PortableText);
}
}

QVector<EditorShortcutEntry> EditorShortcutRegistry::entries(QMenuBar* menuBar)
{
    QVector<EditorShortcutEntry> result;
    if (!menuBar) return result;
    QSettings settings;
    QSet<QAction*> seen;
    const QVector<EditorActionEntry> actions = editorActionEntries(menuBar);
    result.reserve(actions.size() + 16);
    const auto append = [&](QAction* action, const QString& key, const QString& label,
                            const QString& category, QVector<EditorShortcutEntry>& target) {
        if (!action || key.isEmpty() || seen.contains(action)) return;
        seen.insert(action);
        QVariant storedDefault = action->property(kDefaultProperty);
        if (!storedDefault.isValid()) {
            storedDefault = portable(action->shortcut());
            action->setProperty(kDefaultProperty, storedDefault);
        }
        EditorShortcutEntry entry;
        entry.key = key;
        entry.label = label;
        entry.category = category;
        entry.defaultShortcut = storedDefault.toString();
        const QString settingsPath = settingsKey(entry.key);
        if (settings.contains(settingsPath)) {
            entry.shortcut = settings.value(settingsPath).toString();
        } else {
            const QString legacyPath = legacySettingsKey(entry.key);
            if (!legacyPath.isEmpty() && settings.contains(legacyPath)) {
                entry.shortcut = settings.value(legacyPath).toString();
                settings.setValue(settingsPath, entry.shortcut);
            } else {
                entry.shortcut = entry.defaultShortcut;
            }
        }
        entry.action = action;
        target.push_back(entry);
    };

    for (const EditorActionEntry& source : actions)
        append(source.action, source.key, source.label, source.category, result);

    // Ações globais/toolbar que não participam da Action Palette (por exemplo
    // Ctrl+Tab ou ferramentas B/E/F/R) também podem ser personalizadas.
    if (QWidget* window = menuBar->window()) {
        for (QAction* action : window->findChildren<QAction*>()) {
            if (!action || seen.contains(action) || action->isSeparator()) continue;
            QString label = action->text();
            label.remove(QLatin1Char('&'));
            label = label.section(QLatin1Char('\t'), 0, 0).trimmed();
            if (label.isEmpty() || (action->shortcut().isEmpty() && !action->property(kDefaultProperty).isValid())) continue;
            const QString explicitId = action->property("paletteId").toString().trimmed();
            const QString key = !explicitId.isEmpty()
                ? QStringLiteral("action:%1").arg(explicitId)
                : QStringLiteral("action:global/%1").arg(label.toCaseFolded());
            append(action, key, label, QObject::tr("Ferramentas / Navegação"), result);
        }
    }
    return result;
}

void EditorShortcutRegistry::apply(QMenuBar* menuBar)
{
    for (const EditorShortcutEntry& entry : entries(menuBar)) {
        if (!entry.action) continue;
        entry.action->setShortcut(sequenceFromPortable(entry.shortcut));
    }
}

void EditorShortcutRegistry::saveOverride(const QString& key, const QString& portableSequence)
{
    if (key.trimmed().isEmpty()) return;
    QSettings().setValue(settingsKey(key), portableSequence.trimmed());
}

void EditorShortcutRegistry::clearOverride(const QString& key)
{
    if (key.trimmed().isEmpty()) return;
    QSettings().remove(settingsKey(key));
}

void EditorShortcutRegistry::clearAllOverrides()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("shortcuts/overrides"));
    settings.remove(QString());
    settings.endGroup();
}

QString EditorShortcutRegistry::conflictKey(const QVector<EditorShortcutEntry>& values,
                                            const QString& candidatePortable,
                                            const QString& exceptKey)
{
    const QKeySequence candidate = sequenceFromPortable(candidatePortable);
    if (candidate.isEmpty()) return {};
    for (const EditorShortcutEntry& entry : values) {
        if (entry.key == exceptKey || entry.shortcut.trimmed().isEmpty()) continue;
        const QKeySequence existing = sequenceFromPortable(entry.shortcut);
        if (!existing.isEmpty() && candidate.matches(existing) == QKeySequence::ExactMatch)
            return entry.key;
    }
    return {};
}

} // namespace ui
