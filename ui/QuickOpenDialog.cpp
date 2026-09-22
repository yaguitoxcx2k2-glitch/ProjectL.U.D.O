#include "QuickOpenDialog.h"

#include "EditorUiPrimitives.h"
#include "NavigationHistory.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace ui {
namespace {
constexpr int RoleKey = Qt::UserRole;

QStringList validKeys(const QVector<NavigationItem>& items)
{
    QStringList keys;
    keys.reserve(items.size());
    for (const NavigationItem& item : items) keys.push_back(item.key);
    return keys;
}

QString itemText(const NavigationItem& item, bool favorite)
{
    const QString prefix = favorite ? QStringLiteral("★  ") : QStringLiteral("   ");
    QString second = navigationKindLabel(item.kind);
    if (!item.context.trimmed().isEmpty()) second += QStringLiteral(" · ") + item.context;
    if (item.missing) second += QObject::tr(" · ausente");
    return prefix + item.label + QLatin1Char('\n') + second;
}
}

QuickOpenDialog::QuickOpenDialog(const QVector<NavigationItem>& items, QWidget* parent)
    : QDialog(parent), m_items(items)
{
    setWindowTitle(tr("Abrir rápido"));
    resize(760, 560);
    setModal(true);

    NavigationHistory::prune(NavigationHistory::Domain::Targets, validKeys(m_items));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(8);
    root->addWidget(primitives::hintLabel(
        tr("Encontre mapas, eventos, dados, tilesets e arquivos sem navegar por várias janelas."), this));

    auto* searchRow = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Digite um nome, caminho ou tipo…"));
    m_search->setAccessibleName(tr("Buscar destino no projeto"));
    m_group = new QComboBox(this);
    m_group->addItem(tr("Tudo"), QStringLiteral("all"));
    m_group->addItem(tr("Mapas"), QStringLiteral("maps"));
    m_group->addItem(tr("Eventos"), QStringLiteral("events"));
    m_group->addItem(tr("Dados"), QStringLiteral("data"));
    m_group->addItem(tr("Arquivos"), QStringLiteral("assets"));
    m_group->addItem(tr("Tilesets"), QStringLiteral("tilesets"));
    m_group->addItem(tr("Extensões"), QStringLiteral("plugins"));
    m_group->setAccessibleName(tr("Filtrar tipo de destino"));
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_group);
    root->addLayout(searchRow);

    m_results = new QListWidget(this);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setAccessibleName(tr("Resultados de Abrir rápido"));
    root->addWidget(m_results, 1);

    auto* footer = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setProperty("uiRole", QStringLiteral("hint"));
    m_favorite = new QPushButton(tr("Favoritar"), this);
    m_favorite->setAccessibleName(tr("Favoritar destino selecionado"));
    auto* open = new QPushButton(tr("Abrir"), this);
    open->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancelar"), this);
    footer->addWidget(m_status, 1);
    footer->addWidget(m_favorite);
    footer->addWidget(cancel);
    footer->addWidget(open);
    root->addLayout(footer);

    m_search->installEventFilter(this);
    m_results->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &QuickOpenDialog::rebuild);
    connect(m_group, &QComboBox::currentIndexChanged, this, [this](int) { rebuild(); });
    connect(m_results, &QListWidget::itemSelectionChanged, this, &QuickOpenDialog::updateFavoriteButton);
    connect(m_results, &QListWidget::itemActivated, this, [this] { acceptCurrent(); });
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this] { acceptCurrent(); });
    connect(m_favorite, &QPushButton::clicked, this, &QuickOpenDialog::toggleFavorite);
    connect(open, &QPushButton::clicked, this, &QuickOpenDialog::acceptCurrent);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    rebuild();
    m_search->setFocus();
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (!event || event->type() != QEvent::KeyPress) return QDialog::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (watched == m_search && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
        const int count = m_results->count();
        if (count > 0) {
            int row = m_results->currentRow();
            if (row < 0) row = 0;
            row = qBound(0, row + (key->key() == Qt::Key_Down ? 1 : -1), count - 1);
            m_results->setCurrentRow(row);
            m_results->scrollToItem(m_results->currentItem());
        }
        return true;
    }
    if ((watched == m_search || watched == m_results) &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
        acceptCurrent();
        return true;
    }
    if ((key->modifiers() & Qt::ControlModifier) && key->key() == Qt::Key_D) {
        toggleFavorite();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::rebuild()
{
    const QString currentKey = m_results->currentItem()
        ? m_results->currentItem()->data(RoleKey).toString() : QString();
    const QVector<int> indexes = rankedNavigationMatches(
        m_items, m_search->text(), m_group->currentData().toString());
    const QStringList recent = NavigationHistory::recent(NavigationHistory::Domain::Targets);
    const QStringList favorites = NavigationHistory::favorites(NavigationHistory::Domain::Targets);

    QVector<int> ordered = indexes;
    if (m_search->text().trimmed().isEmpty()) {
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) {
            const NavigationItem& ia = m_items.at(a);
            const NavigationItem& ib = m_items.at(b);
            const bool fa = favorites.contains(ia.key);
            const bool fb = favorites.contains(ib.key);
            if (fa != fb) return fa > fb;
            const int ra = recent.indexOf(ia.key);
            const int rb = recent.indexOf(ib.key);
            const int va = ra < 0 ? 100000 : ra;
            const int vb = rb < 0 ? 100000 : rb;
            if (va != vb) return va < vb;
            return QString::localeAwareCompare(ia.label, ib.label) < 0;
        });
    }

    m_results->clear();
    int selectRow = -1;
    for (int index : ordered) {
        const NavigationItem& item = m_items.at(index);
        auto* row = new QListWidgetItem(itemText(item, favorites.contains(item.key)), m_results);
        row->setData(RoleKey, item.key);
        row->setToolTip(item.context.isEmpty() ? item.key : item.context);
        if (item.key == currentKey) selectRow = m_results->count() - 1;
    }
    if (selectRow < 0 && m_results->count() > 0) selectRow = 0;
    if (selectRow >= 0) m_results->setCurrentRow(selectRow);
    m_status->setText(m_results->count() == 0
                          ? tr("Nenhum destino encontrado.")
                          : tr("Resultados: %1 · Enter abre · Ctrl+D adiciona aos favoritos").arg(m_results->count()));
    updateFavoriteButton();
}

void QuickOpenDialog::acceptCurrent()
{
    QListWidgetItem* row = m_results->currentItem();
    if (!row || !(row->flags() & Qt::ItemIsEnabled)) return;
    const QString key = row->data(RoleKey).toString();
    for (const NavigationItem& item : m_items) {
        if (item.key != key) continue;
        m_selected = item;
        NavigationHistory::remember(NavigationHistory::Domain::Targets, key);
        accept();
        return;
    }
}

void QuickOpenDialog::updateFavoriteButton()
{
    const QString key = m_results->currentItem()
        ? m_results->currentItem()->data(RoleKey).toString() : QString();
    const bool has = !key.isEmpty();
    m_favorite->setEnabled(has);
    m_favorite->setText(has && NavigationHistory::isFavorite(NavigationHistory::Domain::Targets, key)
                            ? tr("Desfavoritar") : tr("Favoritar"));
}

void QuickOpenDialog::toggleFavorite()
{
    QListWidgetItem* row = m_results->currentItem();
    if (!row) return;
    const QString key = row->data(RoleKey).toString();
    if (key.isEmpty()) return;
    NavigationHistory::toggleFavorite(NavigationHistory::Domain::Targets, key);
    rebuild();
}

} // namespace ui
