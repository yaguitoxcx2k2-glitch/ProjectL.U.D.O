#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace game {

class GameSession;

class ShopDialog : public QDialog
{
public:
    ShopDialog(GameSession& session, const QStringList& itemIds,
               bool purchaseOnly, QWidget* parent = nullptr);

private:
    void refresh();
    void buy();
    void sell();
    QString selectedId() const;

    GameSession& m_session;
    QStringList m_itemIds;
    bool m_purchaseOnly = false;
    QLabel* m_gold = nullptr;
    QLabel* m_details = nullptr;
    QListWidget* m_items = nullptr;
    QSpinBox* m_quantity = nullptr;
    QPushButton* m_buy = nullptr;
    QPushButton* m_sell = nullptr;
};

} // namespace game
