#pragma once

#include "NavigationCatalog.h"

#include <QDialog>
#include <QVector>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QEvent;
QT_END_NAMESPACE

namespace ui {

/// Ctrl+P: navegação rápida por tudo que possui um destino editorial no projeto.
/// O catálogo é compartilhado; o diálogo só apresenta e registra preferência local.
class QuickOpenDialog final : public QDialog
{
public:
    explicit QuickOpenDialog(const QVector<NavigationItem>& items, QWidget* parent = nullptr);

    NavigationItem selectedItem() const { return m_selected; }
    bool hasSelection() const { return !m_selected.key.isEmpty(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void acceptCurrent();
    void updateFavoriteButton();
    void toggleFavorite();

    QVector<NavigationItem> m_items;
    QLineEdit* m_search = nullptr;
    QComboBox* m_group = nullptr;
    QListWidget* m_results = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_favorite = nullptr;
    NavigationItem m_selected;
};

} // namespace ui
