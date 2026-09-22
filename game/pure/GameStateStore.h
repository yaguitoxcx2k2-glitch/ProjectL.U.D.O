#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::pure {

class GameStateStore {
public:
    void clear();

    bool switchOn(int id) const;
    void setSwitch(int id, bool value);
    bool hasSwitch(int id) const;
    std::vector<int> switchIds() const;

    int variable(int id) const;
    void setVariable(int id, int value);
    bool hasVariable(int id) const;
    std::vector<int> variableIds() const;

    std::string stringValue(int id) const;
    void setStringValue(int id, std::string value);
    bool hasString(int id) const;
    std::vector<int> stringIds() const;

    bool selfSwitch(const std::string& key) const;
    void setSelfSwitch(std::string key, bool value);
    std::vector<std::string> selfSwitchKeys() const;

    int gold() const noexcept { return m_gold; }
    void setGold(int amount) noexcept;
    void addGold(int amount) noexcept;

    int itemCount(const std::string& id) const;
    void setItemCount(std::string id, int amount);
    void addItem(const std::string& id, int amount);
    std::vector<std::string> inventoryIds() const;

    std::size_t switchCount() const noexcept { return m_switches.size(); }
    std::size_t variableCount() const noexcept { return m_variables.size(); }
    std::size_t stringCount() const noexcept { return m_strings.size(); }
    std::size_t selfSwitchCount() const noexcept { return m_selfSwitches.size(); }

private:
    std::unordered_map<int, bool> m_switches;
    std::unordered_map<int, int> m_variables;
    std::unordered_map<int, std::string> m_strings;
    std::unordered_map<std::string, bool> m_selfSwitches;
    std::unordered_map<std::string, int> m_inventory;
    int m_gold = 0;
};

} // namespace game::pure
