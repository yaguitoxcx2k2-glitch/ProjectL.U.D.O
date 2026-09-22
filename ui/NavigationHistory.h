#pragma once

#include <QString>
#include <QStringList>

namespace ui {

/// Autoridade única de recência/favoritos do Editor. É estado local da
/// instalação e nunca entra no ProjectFormat.
class NavigationHistory final
{
public:
    enum class Domain { Targets, Actions };

    static QStringList recent(Domain domain);
    static QStringList favorites(Domain domain);
    static void remember(Domain domain, const QString& key, int limit = 30);
    static bool isFavorite(Domain domain, const QString& key);
    static bool toggleFavorite(Domain domain, const QString& key);
    static void prune(Domain domain, const QStringList& validKeys);

private:
    static QString recentKey(Domain domain);
    static QString favoritesKey(Domain domain);
};

} // namespace ui
