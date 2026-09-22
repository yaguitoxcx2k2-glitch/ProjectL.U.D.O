#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAction;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QMenuBar;
QT_END_NAMESPACE

namespace ui {

struct EditorActionEntry {
    QString key;
    QString label;
    QString category;
    QString shortcut;
    QString keywords;
    QAction* action = nullptr;
};

/// Gera o catálogo global diretamente dos menus existentes. Assim a Command
/// Palette não cria callbacks paralelos e sempre aciona a mesma QAction da UI.
QVector<EditorActionEntry> editorActionEntries(QMenuBar* menuBar);

class EditorActionPalette final : public QDialog
{
public:
    explicit EditorActionPalette(const QVector<EditorActionEntry>& entries,
                                 QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();
    void toggleFavorite();
    void updateFavoriteButton();

    QVector<EditorActionEntry> m_entries;
    QLineEdit* m_search = nullptr;
    QListWidget* m_results = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_favorite = nullptr;
};

} // namespace ui
