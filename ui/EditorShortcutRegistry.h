#pragma once

#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAction;
class QMenuBar;
QT_END_NAMESPACE

namespace ui {

struct EditorShortcutEntry {
    QString key;
    QString label;
    QString category;
    QString defaultShortcut;   ///< PortableText
    QString shortcut;          ///< PortableText, vazio = sem atalho
    QAction* action = nullptr;
};

/// Autoridade local dos atalhos do Editor. As ações continuam sendo as mesmas
/// QActions usadas pelos menus e pela Paleta de Ações; apenas a sequência de
/// teclas é sobreposta via QSettings.
class EditorShortcutRegistry final
{
public:
    static QVector<EditorShortcutEntry> entries(QMenuBar* menuBar);
    static void apply(QMenuBar* menuBar);
    static void saveOverride(const QString& key, const QString& portableSequence);
    static void clearOverride(const QString& key);
    static void clearAllOverrides();

    static QString conflictKey(const QVector<EditorShortcutEntry>& entries,
                               const QString& candidatePortable,
                               const QString& exceptKey = QString());
};

} // namespace ui
