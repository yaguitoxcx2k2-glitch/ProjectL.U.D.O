#include "EditorActionPalette.h"

#include "EditorUiPrimitives.h"
#include "NavigationHistory.h"

#include <algorithm>

#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ui {
namespace {
constexpr int RoleKey = Qt::UserRole;

QString cleanLabel(QString text)
{
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("…"), QString());
    return text.trimmed();
}

QString actionKey(QAction* action, const QString& category, const QString& label)
{
    const QString explicitId = action ? action->property("paletteId").toString().trimmed() : QString();
    if (!explicitId.isEmpty()) return QStringLiteral("action:%1").arg(explicitId);
    return QStringLiteral("action:%1/%2").arg(category.toCaseFolded(), label.toCaseFolded());
}

void appendMenu(QMenu* menu, const QStringList& path, QVector<EditorActionEntry>& out)
{
    if (!menu) return;
    QStringList currentPath = path;
    const QString own = cleanLabel(menu->title());
    if (!own.isEmpty()) currentPath.push_back(own);

    for (QAction* action : menu->actions()) {
        if (!action || action->isSeparator() || action->property("excludeFromPalette").toBool()) continue;
        if (QMenu* sub = action->menu()) {
            appendMenu(sub, currentPath, out);
            continue;
        }
        const QString label = cleanLabel(action->text());
        if (label.isEmpty()) continue;
        EditorActionEntry entry;
        entry.label = label;
        entry.category = currentPath.join(QStringLiteral(" › "));
        entry.shortcut = action->shortcut().toString(QKeySequence::NativeText);
        entry.key = actionKey(action, entry.category, label);
        entry.keywords = action->toolTip() + QLatin1Char(' ') + action->statusTip();
        entry.action = action;
        out.push_back(entry);
    }
}

QStringList validKeys(const QVector<EditorActionEntry>& entries)
{
    QStringList keys;
    keys.reserve(entries.size());
    for (const EditorActionEntry& entry : entries) keys.push_back(entry.key);
    return keys;
}

int actionScore(const EditorActionEntry& entry, const QString& query)
{
    const QString q = query.trimmed().toCaseFolded();
    if (q.isEmpty()) return 0;
    const QString label = entry.label.toCaseFolded();
    const QString haystack = label + QLatin1Char(' ') + entry.category.toCaseFolded() +
                             QLatin1Char(' ') + entry.shortcut.toCaseFolded() +
                             QLatin1Char(' ') + entry.keywords.toCaseFolded();
    const QStringList tokens = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString& token : tokens)
        if (!haystack.contains(token)) return -1;
    int score = label == q ? 400 : label.startsWith(q) ? 300 : label.contains(q) ? 220 : 0;
    if (entry.category.toCaseFolded().contains(q)) score += 80;
    for (const QString& token : tokens)
        if (label.startsWith(token)) score += 25;
    return score;
}

QString displayText(const EditorActionEntry& entry, bool favorite)
{
    QString detail = entry.category;
    if (!entry.shortcut.isEmpty()) detail += QStringLiteral(" · ") + entry.shortcut;
    return (favorite ? QStringLiteral("★  ") : QStringLiteral("   ")) +
           entry.label + QLatin1Char('\n') + detail;
}
}

QVector<EditorActionEntry> editorActionEntries(QMenuBar* menuBar)
{
    QVector<EditorActionEntry> out;
    if (!menuBar) return out;
    for (QAction* action : menuBar->actions()) {
        if (!action || !action->menu()) continue;
        appendMenu(action->menu(), {}, out);
    }
    return out;
}

EditorActionPalette::EditorActionPalette(const QVector<EditorActionEntry>& entries, QWidget* parent)
    : QDialog(parent), m_entries(entries)
{
    setWindowTitle(tr("Paleta de ações"));
    resize(720, 540);
    setModal(true);
    NavigationHistory::prune(NavigationHistory::Domain::Actions, validKeys(m_entries));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(8);
    root->addWidget(primitives::hintLabel(
        tr("Execute qualquer ação dos menus sem memorizar onde ela fica."), this));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Digite uma ação, menu ou atalho…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Buscar ação do Editor"));
    root->addWidget(m_search);

    m_results = new QListWidget(this);
    m_results->setAccessibleName(tr("Resultados da Paleta de ações"));
    root->addWidget(m_results, 1);

    auto* footer = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setProperty("uiRole", QStringLiteral("hint"));
    m_favorite = new QPushButton(tr("Favoritar"), this);
    auto* run = new QPushButton(tr("Executar"), this);
    run->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancelar"), this);
    footer->addWidget(m_status, 1);
    footer->addWidget(m_favorite);
    footer->addWidget(cancel);
    footer->addWidget(run);
    root->addLayout(footer);

    m_search->installEventFilter(this);
    m_results->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &EditorActionPalette::rebuild);
    connect(m_results, &QListWidget::itemSelectionChanged, this, &EditorActionPalette::updateFavoriteButton);
    connect(m_results, &QListWidget::itemActivated, this, [this] { acceptCurrent(); });
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this] { acceptCurrent(); });
    connect(m_favorite, &QPushButton::clicked, this, &EditorActionPalette::toggleFavorite);
    connect(run, &QPushButton::clicked, this, &EditorActionPalette::acceptCurrent);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    rebuild();
    m_search->setFocus();
}

bool EditorActionPalette::eventFilter(QObject* watched, QEvent* event)
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

void EditorActionPalette::rebuild()
{
    const QString currentKey = m_results->currentItem()
        ? m_results->currentItem()->data(RoleKey).toString() : QString();
    const QString query = m_search->text();
    const QStringList recent = NavigationHistory::recent(NavigationHistory::Domain::Actions);
    const QStringList favorites = NavigationHistory::favorites(NavigationHistory::Domain::Actions);

    struct Match { int index = -1; int score = 0; };
    QVector<Match> matches;
    for (int i = 0; i < m_entries.size(); ++i) {
        const int score = actionScore(m_entries.at(i), query);
        if (score >= 0) matches.push_back({i, score});
    }
    std::stable_sort(matches.begin(), matches.end(), [&](const Match& a, const Match& b) {
        const EditorActionEntry& ea = m_entries.at(a.index);
        const EditorActionEntry& eb = m_entries.at(b.index);
        if (query.trimmed().isEmpty()) {
            const bool fa = favorites.contains(ea.key), fb = favorites.contains(eb.key);
            if (fa != fb) return fa > fb;
            const int ra = recent.indexOf(ea.key), rb = recent.indexOf(eb.key);
            const int va = ra < 0 ? 100000 : ra, vb = rb < 0 ? 100000 : rb;
            if (va != vb) return va < vb;
        } else if (a.score != b.score) return a.score > b.score;
        if (ea.category != eb.category) return ea.category < eb.category;
        return QString::localeAwareCompare(ea.label, eb.label) < 0;
    });

    m_results->clear();
    int selectRow = -1;
    for (const Match& match : matches) {
        const EditorActionEntry& entry = m_entries.at(match.index);
        auto* row = new QListWidgetItem(displayText(entry, favorites.contains(entry.key)), m_results);
        row->setData(RoleKey, entry.key);
        if (!entry.action || !entry.action->isEnabled()) row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
        if (entry.key == currentKey) selectRow = m_results->count() - 1;
    }
    if (selectRow < 0 && m_results->count() > 0) selectRow = 0;
    if (selectRow >= 0) m_results->setCurrentRow(selectRow);
    m_status->setText(m_results->count() == 0
                          ? tr("Nenhuma ação encontrada.")
                          : tr("Ações: %1 · Enter executa · Ctrl+D favorita").arg(m_results->count()));
    updateFavoriteButton();
}

void EditorActionPalette::acceptCurrent()
{
    QListWidgetItem* row = m_results->currentItem();
    if (!row || !(row->flags() & Qt::ItemIsEnabled)) return;
    const QString key = row->data(RoleKey).toString();
    for (const EditorActionEntry& entry : m_entries) {
        if (entry.key != key || !entry.action || !entry.action->isEnabled()) continue;
        NavigationHistory::remember(NavigationHistory::Domain::Actions, key);
        QAction* action = entry.action;
        accept();
        action->trigger();
        return;
    }
}

void EditorActionPalette::toggleFavorite()
{
    QListWidgetItem* row = m_results->currentItem();
    if (!row) return;
    const QString key = row->data(RoleKey).toString();
    if (key.isEmpty()) return;
    NavigationHistory::toggleFavorite(NavigationHistory::Domain::Actions, key);
    rebuild();
}

void EditorActionPalette::updateFavoriteButton()
{
    const QString key = m_results->currentItem()
        ? m_results->currentItem()->data(RoleKey).toString() : QString();
    const bool has = !key.isEmpty();
    m_favorite->setEnabled(has);
    m_favorite->setText(has && NavigationHistory::isFavorite(NavigationHistory::Domain::Actions, key)
                            ? tr("Desfavoritar") : tr("Favoritar"));
}

} // namespace ui
