#pragma once

#include "CommandCatalog.h"

#include <QDialog>
#include <QPoint>
#include <QSet>
#include <QVector>

QT_BEGIN_NAMESPACE
class QLabel;
class QCheckBox;
class QComboBox;
class QEvent;
class QKeyEvent;
class QLineEdit;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

namespace ui {

/// Picker único de comandos do Event Editor. CommandCatalog continua sendo a
/// fonte de descoberta; CommandRegistry continua sendo a autoridade runtime.
class CommandPickerDialog final : public QDialog
{
public:
    explicit CommandPickerDialog(const QVector<CommandCatalogEntry>& entries,
                                 QWidget* parent = nullptr);

    /// Abre próximo ao ponto de ancoragem e retorna o canonical type.
    QString execCommand(const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();
    void remember(const QString& type);
    void toggleFavorite();
    void reloadPresetEntries();

    QVector<CommandCatalogEntry> m_entries;
    QLineEdit*   m_search = nullptr;
    QComboBox*    m_category = nullptr;
    QListWidget* m_list = nullptr;
    QLabel*      m_hint = nullptr;
    QLabel*      m_details = nullptr;
    QCheckBox*   m_favoritesOnly = nullptr;
    QPushButton* m_favoriteButton = nullptr;
    QSet<QString> m_favoriteTypes;
    QString      m_selectedType;
};

} // namespace ui
