#include "UiShopController.h"

#include "game/GameSession.h"
#include "game/RpgSystem.h"
#include "game/ui/UiDataBinding.h"
#include <QCoreApplication>

namespace game::ui {
namespace {
QString st(const char* s){return QCoreApplication::translate("UiShopController",s);}
QString shopTr(const GameSession& session,const QString& key,const char* fallback)
{return core::resolvePlayerText(session.editor().localization,key,st(fallback));}
bool initialScreenStateEnabled(const core::Editor& ed, const QString& screen, const QString& elementId)
{
    for (auto it=ed.gameUi.screenStates.cbegin(); it!=ed.gameUi.screenStates.cend(); ++it) {
        if (!it.value().initial || it.value().screen != screen) continue;
        const auto ov=it.value().elements.constFind(elementId);
        return ov==it.value().elements.cend() || !ov.value().hasEnabled || ov.value().enabled;
    }
    return true;
}
QString recordCategory(const core::Editor& ed, const QString& id)
{
    for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"), QStringLiteral("armors")})
        if (databaseRecord(ed, category, id)) return category;
    return {};
}

QString localizedRecordName(const core::Editor& ed, const core::DatabaseRecord& record)
{
    const QString category = recordCategory(ed, record.id);
    return category.isEmpty() ? record.name : databaseRecordName(ed, category, record.id, record.name);
}

QString localizedRecordDescription(const core::Editor& ed, const core::DatabaseRecord& record)
{
    const QString category = recordCategory(ed, record.id);
    return category.isEmpty() ? record.description : databaseRecordDescription(ed, category, record.id, record.description);
}
}

UiShopController::UiShopController(GameSession& session):m_session(session){}

const core::Editor& UiShopController::editor() const { return m_session.editor(); }
const GameState& UiShopController::state() const { return m_session.state(); }

void UiShopController::open(const QStringList& itemIds,bool purchaseOnly){clear();m_purchaseOnly=purchaseOnly;for(const QString&id:itemIds)if(databaseRecord(m_session.editor(),QStringLiteral("items"),id)||databaseRecord(m_session.editor(),QStringLiteral("weapons"),id)||databaseRecord(m_session.editor(),QStringLiteral("armors"),id)){if(!m_itemIds.contains(id))m_itemIds.push_back(id);}m_active=true;m_mode=purchaseOnly?UiShopMode::Buy:UiShopMode::ModeSelect;rebuild();}
void UiShopController::clear(){m_itemIds.clear();m_entries.clear();m_purchaseOnly=false;m_active=false;m_mode=UiShopMode::ModeSelect;m_selected=0;m_quantity=1;m_notice.clear();}
int UiShopController::gold()const{return m_session.state().gold();}
QString UiShopController::title()const{if(m_mode==UiShopMode::Buy)return shopTr(m_session,QStringLiteral("system.shop.buy_title"),"Loja — Comprar");if(m_mode==UiShopMode::Sell)return shopTr(m_session,QStringLiteral("system.shop.sell_title"),"Loja — Vender");return shopTr(m_session,QStringLiteral("system.shop.title"),"Loja");}
QString UiShopController::selectedId()const{if(m_mode==UiShopMode::ModeSelect)return QString();return m_selected>=0&&m_selected<m_itemIds.size()?m_itemIds[m_selected]:QString();}
const core::DatabaseRecord* UiShopController::selectedRecord()const{const QString id=selectedId();if(id.isEmpty())return nullptr;for(const QString&cat:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})if(const auto*r=databaseRecord(m_session.editor(),cat,id))return r;return nullptr;}
QString UiShopController::detail()const{if(m_mode==UiShopMode::ModeSelect)return shopTr(m_session,QStringLiteral("system.shop.choose_mode"),"Escolha se deseja comprar ou vender itens.\n\nDinheiro: %1 G").arg(gold());const auto*r=selectedRecord();if(!r)return shopTr(m_session,QStringLiteral("system.shop.no_products"),"Nenhum produto disponível.");const int price=qMax(0,r->data.value(QStringLiteral("price"),0).toInt());const int unit=m_mode==UiShopMode::Buy?price:(price>0?qMax(1,price/2):0);const int owned=m_session.state().itemCount(r->id);return shopTr(m_session,QStringLiteral("system.shop.detail"),"%1\n\n%2\n\nPreço unitário: %3 G\nQuantidade: %4\nVocê possui: %5\nTotal: %6 G").arg(localizedRecordName(m_session.editor(), *r), localizedRecordDescription(m_session.editor(), *r)).arg(unit).arg(m_quantity).arg(owned).arg(qint64(unit)*m_quantity);}
void UiShopController::rebuild(){m_entries.clear();if(m_mode==UiShopMode::ModeSelect){m_entries={shopTr(m_session,QStringLiteral("system.shop.buy"),"Comprar"),shopTr(m_session,QStringLiteral("system.shop.sell"),"Vender"),shopTr(m_session,QStringLiteral("system.shop.exit"),"Sair")};}else{for(const QString&id:m_itemIds){const core::DatabaseRecord*r=nullptr;for(const QString&cat:{QStringLiteral("items"),QStringLiteral("weapons"),QStringLiteral("armors")})if((r=databaseRecord(m_session.editor(),cat,id)))break;if(!r){m_entries.push_back(id);continue;}const int price=qMax(0,r->data.value(QStringLiteral("price"),0).toInt());const int shown=m_mode==UiShopMode::Buy?price:(price>0?qMax(1,price/2):0);m_entries.push_back(shopTr(m_session,QStringLiteral("system.shop.entry"),"%1   %2 G   [×%3]").arg(localizedRecordName(m_session.editor(), *r)).arg(shown).arg(m_session.state().itemCount(id)));}}m_selected=qBound(0,m_selected,qMax(0,m_entries.size()-1));m_quantity=qBound(1,m_quantity,99);}
void UiShopController::selectIndex(int index){if(m_entries.isEmpty())return;m_selected=qBound(0,index,m_entries.size()-1);}
void UiShopController::refreshLocalization(){if(m_active)rebuild();}

bool UiShopController::handleAction(core::GameAction a){if(!m_active)return false;const auto listMeta=m_session.editor().gameUi.layoutElements.value(QStringLiteral("shop.list"));const auto listData=UiDataBindingResolver::resolveRuntime(listMeta,m_session.editor(),m_session.state());const bool listEnabled=(!listData.hasEnabled||listData.enabled)&&initialScreenStateEnabled(m_session.editor(),QStringLiteral("shop"),QStringLiteral("shop.list"));if(!listEnabled&&a!=core::GameAction::Cancel&&a!=core::GameAction::Quit)return true;if(a==core::GameAction::Up){if(!m_entries.isEmpty())m_selected=(m_selected-1+m_entries.size())%m_entries.size();return true;}if(a==core::GameAction::Down){if(!m_entries.isEmpty())m_selected=(m_selected+1)%m_entries.size();return true;}if(a==core::GameAction::Left&&m_mode!=UiShopMode::ModeSelect){m_quantity=qMax(1,m_quantity-1);return true;}if(a==core::GameAction::Right&&m_mode!=UiShopMode::ModeSelect){m_quantity=qMin(99,m_quantity+1);return true;}if(a==core::GameAction::Confirm){activate();return true;}if(a==core::GameAction::Cancel||a==core::GameAction::Quit){back();return true;}return false;}
void UiShopController::activate(){m_notice.clear();if(m_mode==UiShopMode::ModeSelect){if(m_selected==0){m_mode=UiShopMode::Buy;m_selected=0;}else if(m_selected==1){m_mode=UiShopMode::Sell;m_selected=0;}else{m_active=false;return;}m_quantity=1;rebuild();return;}transact(m_mode==UiShopMode::Buy);}
void UiShopController::back(){m_notice.clear();if(m_purchaseOnly||m_mode==UiShopMode::ModeSelect){m_active=false;return;}m_mode=UiShopMode::ModeSelect;m_selected=0;m_quantity=1;rebuild();}
void UiShopController::transact(bool buy){const auto*r=selectedRecord();if(!r)return;const int base=qMax(0,r->data.value(QStringLiteral("price"),0).toInt());const int unit=buy?base:(base>0?qMax(1,base/2):0);const qint64 total=qint64(unit)*m_quantity;if(buy){if(total>m_session.state().gold()){m_notice=shopTr(m_session,QStringLiteral("system.shop.insufficient_money"),"Dinheiro insuficiente.");return;}m_session.state().addGold(-int(total));m_session.state().addItem(r->id,m_quantity);m_notice=shopTr(m_session,QStringLiteral("system.shop.bought"),"Comprado: %1 ×%2").arg(localizedRecordName(m_session.editor(), *r)).arg(m_quantity);}else{if(m_session.state().itemCount(r->id)<m_quantity){m_notice=shopTr(m_session,QStringLiteral("system.shop.insufficient_quantity"),"Você não possui essa quantidade.");return;}m_session.state().addItem(r->id,-m_quantity);m_session.state().addGold(int(total));m_notice=shopTr(m_session,QStringLiteral("system.shop.sold"),"Vendido: %1 ×%2").arg(localizedRecordName(m_session.editor(), *r)).arg(m_quantity);}rebuild();}

} // namespace game::ui
