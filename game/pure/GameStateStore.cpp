#include "GameStateStore.h"

#include <limits>

namespace game::pure {
namespace {
template<class Map, class Key>
bool contains(const Map& map, const Key& key) { return map.find(key) != map.end(); }

template<class T>
std::vector<T> sortedKeys(const std::unordered_map<T, bool>& map)
{
    std::vector<T> out; out.reserve(map.size());
    for (const auto& [key, value] : map) { (void)value; out.push_back(key); }
    std::sort(out.begin(), out.end());
    return out;
}
}

void GameStateStore::clear()
{
    m_switches.clear(); m_variables.clear(); m_strings.clear();
    m_selfSwitches.clear(); m_inventory.clear(); m_gold = 0;
}

bool GameStateStore::switchOn(int id) const { auto it=m_switches.find(id); return it!=m_switches.end() && it->second; }
void GameStateStore::setSwitch(int id, bool value) { if (id > 0) m_switches[id]=value; }
bool GameStateStore::hasSwitch(int id) const { return contains(m_switches,id); }
std::vector<int> GameStateStore::switchIds() const { std::vector<int> out; out.reserve(m_switches.size()); for (auto& p:m_switches) out.push_back(p.first); std::sort(out.begin(),out.end()); return out; }

int GameStateStore::variable(int id) const { auto it=m_variables.find(id); return it==m_variables.end()?0:it->second; }
void GameStateStore::setVariable(int id, int value) { if (id > 0) m_variables[id]=value; }
bool GameStateStore::hasVariable(int id) const { return contains(m_variables,id); }
std::vector<int> GameStateStore::variableIds() const { std::vector<int> out; out.reserve(m_variables.size()); for (auto& p:m_variables) out.push_back(p.first); std::sort(out.begin(),out.end()); return out; }

std::string GameStateStore::stringValue(int id) const { auto it=m_strings.find(id); return it==m_strings.end()?std::string{}:it->second; }
void GameStateStore::setStringValue(int id, std::string value) { if (id > 0) m_strings[id]=std::move(value); }
bool GameStateStore::hasString(int id) const { return contains(m_strings,id); }
std::vector<int> GameStateStore::stringIds() const { std::vector<int> out; out.reserve(m_strings.size()); for (auto& p:m_strings) out.push_back(p.first); std::sort(out.begin(),out.end()); return out; }

bool GameStateStore::selfSwitch(const std::string& key) const { auto it=m_selfSwitches.find(key); return it!=m_selfSwitches.end() && it->second; }
void GameStateStore::setSelfSwitch(std::string key, bool value) { if (!key.empty()) m_selfSwitches[std::move(key)]=value; }
std::vector<std::string> GameStateStore::selfSwitchKeys() const { std::vector<std::string> out; out.reserve(m_selfSwitches.size()); for (auto& p:m_selfSwitches) out.push_back(p.first); std::sort(out.begin(),out.end()); return out; }

void GameStateStore::setGold(int amount) noexcept { m_gold=std::clamp(amount,0,999999999); }
void GameStateStore::addGold(int amount) noexcept
{
    const std::int64_t total=std::int64_t(m_gold)+std::int64_t(amount);
    m_gold=int(std::clamp<std::int64_t>(total,0,999999999));
}

int GameStateStore::itemCount(const std::string& id) const { auto it=m_inventory.find(id); return it==m_inventory.end()?0:it->second; }
void GameStateStore::setItemCount(std::string id, int amount)
{
    if (id.empty()) return; amount=std::clamp(amount,0,9999);
    if (amount==0) m_inventory.erase(id); else m_inventory[std::move(id)]=amount;
}
void GameStateStore::addItem(const std::string& id, int amount)
{
    const std::int64_t total=std::int64_t(itemCount(id))+std::int64_t(amount);
    setItemCount(id,int(std::clamp<std::int64_t>(total,0,9999)));
}
std::vector<std::string> GameStateStore::inventoryIds() const { std::vector<std::string> out; out.reserve(m_inventory.size()); for (auto& p:m_inventory) out.push_back(p.first); std::sort(out.begin(),out.end()); return out; }

} // namespace game::pure
