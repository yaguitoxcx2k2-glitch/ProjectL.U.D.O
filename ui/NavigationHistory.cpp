#include "NavigationHistory.h"

#include <QSettings>
#include <QSet>

namespace ui {

QString NavigationHistory::recentKey(Domain domain)
{
    return domain == Domain::Targets ? QStringLiteral("navigation/recentTargets")
                                     : QStringLiteral("navigation/recentActions");
}

QString NavigationHistory::favoritesKey(Domain domain)
{
    return domain == Domain::Targets ? QStringLiteral("navigation/favoriteTargets")
                                     : QStringLiteral("navigation/favoriteActions");
}

QStringList NavigationHistory::recent(Domain domain)
{
    return QSettings().value(recentKey(domain)).toStringList();
}

QStringList NavigationHistory::favorites(Domain domain)
{
    return QSettings().value(favoritesKey(domain)).toStringList();
}

void NavigationHistory::remember(Domain domain, const QString& key, int limit)
{
    const QString clean = key.trimmed();
    if (clean.isEmpty()) return;
    QSettings settings;
    QStringList values = settings.value(recentKey(domain)).toStringList();
    values.removeAll(clean);
    values.prepend(clean);
    while (values.size() > qMax(1, limit)) values.removeLast();
    settings.setValue(recentKey(domain), values);
}

bool NavigationHistory::isFavorite(Domain domain, const QString& key)
{
    return favorites(domain).contains(key);
}

bool NavigationHistory::toggleFavorite(Domain domain, const QString& key)
{
    const QString clean = key.trimmed();
    if (clean.isEmpty()) return false;
    QSettings settings;
    QStringList values = settings.value(favoritesKey(domain)).toStringList();
    const bool wasFavorite = values.removeAll(clean) > 0;
    if (!wasFavorite) values.prepend(clean);
    settings.setValue(favoritesKey(domain), values);
    return !wasFavorite;
}

void NavigationHistory::prune(Domain domain, const QStringList& validKeys)
{
    QSet<QString> valid;
    for (const QString& value : validKeys) valid.insert(value);
    QSettings settings;
    for (const QString& key : {recentKey(domain), favoritesKey(domain)}) {
        QStringList values = settings.value(key).toStringList();
        QStringList clean;
        QSet<QString> seen;
        for (const QString& value : values) {
            if (!valid.contains(value) || seen.contains(value)) continue;
            seen.insert(value);
            clean.push_back(value);
        }
        settings.setValue(key, clean);
    }
}

} // namespace ui
