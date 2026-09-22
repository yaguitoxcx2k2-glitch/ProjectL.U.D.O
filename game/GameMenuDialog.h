#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

namespace game {

class GameSession;

/// Menu jogável no estilo RPG clássico: status, itens, equipamentos,
/// save/load e opções de sistema em uma única interface.
class GameMenuDialog : public QDialog
{
public:
    enum ResultCode { ReturnToTitle = 2 };

    explicit GameMenuDialog(GameSession& session, bool standalone, QWidget* parent = nullptr);

private:
    void rebuildAll();
    void rebuildParty();
    void rebuildInventory();
    void rebuildEquipment();
    void rebuildQuests();
    void refreshStatusDetails();
    void refreshItemDetails();
    void refreshEquipmentDetails();
    void refreshQuestDetails();
    void useSelectedItem();
    void applyEquipment();
    void openSaveLoad(bool loadOnly);
    QString memberName(const QString& actorId) const;

    GameSession& m_session;
    bool m_standalone = false;
    QLabel* m_summary = nullptr;
    QListWidget* m_partyList = nullptr;
    QLabel* m_statusDetails = nullptr;
    QListWidget* m_inventoryList = nullptr;
    QLabel* m_itemDetails = nullptr;
    QComboBox* m_itemTarget = nullptr;
    QPushButton* m_useItem = nullptr;
    QComboBox* m_equipActor = nullptr;
    QComboBox* m_weapon = nullptr;
    QComboBox* m_armor = nullptr;
    QComboBox* m_accessory = nullptr;
    QLabel* m_equipDetails = nullptr;
    QListWidget* m_questList = nullptr;
    QLabel* m_questDetails = nullptr;
};

} // namespace game
