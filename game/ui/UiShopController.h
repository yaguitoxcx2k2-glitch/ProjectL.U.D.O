#pragma once

#include "core/InputMap.h"
#include <QStringList>
#include <QVector>

namespace core { class Editor; struct DatabaseRecord; }
namespace game { class GameSession; class GameState; }

namespace game::ui {

enum class UiShopMode { ModeSelect, Buy, Sell };

class UiShopController {
public:
    explicit UiShopController(GameSession& session);

    void open(const QStringList& itemIds, bool purchaseOnly);
    void clear();
    bool active() const { return m_active; }
    bool handleAction(core::GameAction action);
    void selectIndex(int index);
    void refreshLocalization();

    UiShopMode mode() const { return m_mode; }
    int selected() const { return m_selected; }
    int quantity() const { return m_quantity; }
    int gold() const;
    QString title() const;
    QString detail() const;
    QString notice() const { return m_notice; }
    const QStringList& entries() const { return m_entries; }
    const core::Editor& editor() const;
    const GameState& state() const;

private:
    void rebuild();
    void activate();
    void back();
    const core::DatabaseRecord* selectedRecord() const;
    QString selectedId() const;
    void transact(bool buy);

    GameSession& m_session;
    QStringList m_itemIds;
    QStringList m_entries;
    bool m_purchaseOnly = false;
    bool m_active = false;
    UiShopMode m_mode = UiShopMode::ModeSelect;
    int m_selected = 0;
    int m_quantity = 1;
    QString m_notice;
};

} // namespace game::ui
