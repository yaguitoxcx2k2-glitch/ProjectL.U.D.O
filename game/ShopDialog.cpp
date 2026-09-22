#include "ShopDialog.h"

#include "GameSession.h"
#include "GamepadInput.h"
#include "RpgSystem.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace game {
namespace {

const core::DatabaseRecord* shopRecord(const core::Editor& editor, const QString& id)
{
    for (const QString& category : {QStringLiteral("items"), QStringLiteral("weapons"),
                                    QStringLiteral("armors")})
        if (const core::DatabaseRecord* record = databaseRecord(editor, category, id)) return record;
    return nullptr;
}

} // namespace

ShopDialog::ShopDialog(GameSession& session, const QStringList& itemIds,
                       bool purchaseOnly, QWidget* parent)
    : QDialog(parent), m_session(session), m_itemIds(itemIds), m_purchaseOnly(purchaseOnly)
{
    setWindowTitle(tr("Loja"));
    resize(720, 500);
    auto* layout = new QVBoxLayout(this);
    m_gold = new QLabel(this);
    m_gold->setStyleSheet(QStringLiteral("font-weight:700;font-size:16px"));
    layout->addWidget(m_gold);
    auto* body = new QHBoxLayout;
    m_items = new QListWidget(this);
    body->addWidget(m_items, 1);
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    m_details = new QLabel(right);
    m_details->setWordWrap(true);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rightLayout->addWidget(m_details, 1);
    auto* form = new QFormLayout;
    m_quantity = new QSpinBox(right);
    m_quantity->setRange(1, 99);
    form->addRow(tr("Quantidade"), m_quantity);
    rightLayout->addLayout(form);
    m_buy = new QPushButton(tr("Comprar"), right);
    m_sell = new QPushButton(tr("Vender"), right);
    m_sell->setVisible(!m_purchaseOnly);
    rightLayout->addWidget(m_buy);
    rightLayout->addWidget(m_sell);
    body->addWidget(right, 1);
    layout->addLayout(body, 1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(m_items, &QListWidget::currentRowChanged, this, [this](int) { refresh(); });
    connect(m_quantity, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(m_buy, &QPushButton::clicked, this, &ShopDialog::buy);
    connect(m_sell, &QPushButton::clicked, this, &ShopDialog::sell);

    QStringList valid;
    for (const QString& id : m_itemIds)
        if (shopRecord(m_session.editor(), id) && !valid.contains(id)) valid.push_back(id);
    m_itemIds = valid;
    for (const QString& id : m_itemIds) {
        const core::DatabaseRecord* record = shopRecord(m_session.editor(), id);
        auto* item = new QListWidgetItem(record ? record->name : id, m_items);
        item->setData(Qt::UserRole, id);
    }
    if (m_items->count() > 0) m_items->setCurrentRow(0);
    refresh();
    m_items->setFocus();
    new GamepadDialogNavigator(this, m_session.editor().inputSystem);
}

QString ShopDialog::selectedId() const
{
    return m_items->currentItem() ? m_items->currentItem()->data(Qt::UserRole).toString() : QString();
}

void ShopDialog::refresh()
{
    m_gold->setText(tr("Dinheiro: %1 G").arg(m_session.state().gold()));
    const QString id = selectedId();
    const core::DatabaseRecord* record = shopRecord(m_session.editor(), id);
    if (!record) {
        m_details->setText(tr("Nenhum produto disponível."));
        m_buy->setEnabled(false);
        m_sell->setEnabled(false);
        return;
    }
    const int price = qMax(0, record->data.value(QStringLiteral("price"), 0).toInt());
    const int quantity = m_quantity->value();
    const qint64 total = qint64(price) * quantity;
    const int owned = m_session.state().itemCount(id);
    m_details->setText(tr("<h3>%1</h3><p>%2</p><p>Preço: <b>%3 G</b><br>Você possui: <b>%4</b><br>Total: <b>%5 G</b></p>")
                           .arg(record->name, record->description).arg(price).arg(owned).arg(total));
    m_buy->setEnabled(total <= m_session.state().gold());
    m_sell->setEnabled(!m_purchaseOnly && owned >= quantity);
}

void ShopDialog::buy()
{
    const QString id = selectedId();
    const core::DatabaseRecord* record = shopRecord(m_session.editor(), id);
    if (!record) return;
    const int quantity = m_quantity->value();
    const qint64 total = qint64(qMax(0, record->data.value(QStringLiteral("price")).toInt())) * quantity;
    if (total > m_session.state().gold()) {
        QMessageBox::information(this, tr("Loja"), tr("Dinheiro insuficiente."));
        return;
    }
    m_session.state().addGold(-int(total));
    m_session.state().addItem(id, quantity);
    refresh();
}

void ShopDialog::sell()
{
    const QString id = selectedId();
    const core::DatabaseRecord* record = shopRecord(m_session.editor(), id);
    if (!record || m_purchaseOnly) return;
    const int quantity = m_quantity->value();
    if (m_session.state().itemCount(id) < quantity) return;
    const int price = qMax(0, record->data.value(QStringLiteral("price")).toInt());
    const int unit = price > 0 ? qMax(1, price / 2) : 0;
    m_session.state().addItem(id, -quantity);
    m_session.state().addGold(unit * quantity);
    refresh();
}

} // namespace game
