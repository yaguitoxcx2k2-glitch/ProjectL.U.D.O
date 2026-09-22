#include "CommandPaletteDialog.h"
#include "CommandPresetStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace ui {
namespace {
constexpr auto kRecentKey = "commandPicker/recentTypes";
constexpr auto kRecentStatsKey = "commandPicker/recentStats";
constexpr auto kFavoritesKey = "commandPicker/favoriteTypes";

QString searchableText(const CommandCatalogEntry& entry)
{
    QString aliases;
    const QString type = entry.type.toLower();
    if (type == QLatin1String("map.transfer"))
        aliases += QStringLiteral(" teleporte teleportar teleport transfer");
    if (type.startsWith(QLatin1String("audio.")))
        aliases += QStringLiteral(" som sound musica música music audio áudio");
    if (type.startsWith(QLatin1String("picture.")))
        aliases += QStringLiteral(" imagem picture sprite imagem2d");
    if (type.contains(QLatin1String("camera")))
        aliases += QStringLiteral(" camera câmera view viewport");
    if (type.contains(QLatin1String("variable")) || type.contains(QLatin1String("var.")))
        aliases += QStringLiteral(" variavel variável variable");
    if (type.contains(QLatin1String("switch")))
        aliases += QStringLiteral(" switch interruptor flag");
    return (entry.label + QLatin1Char(' ') + entry.path.join(QLatin1Char(' ')) +
            QLatin1Char(' ') + entry.type + aliases).toCaseFolded();
}
}

CommandPickerDialog::CommandPickerDialog(const QVector<CommandCatalogEntry>& entries, QWidget* parent)
    : QDialog(parent), m_entries(entries)
{
    setWindowTitle(tr("Adicionar comando"));
    resize(620, 520);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar comando, categoria ou tipo…"));
    m_search->setAccessibleName(tr("Buscar comando"));
    layout->addWidget(m_search);

    m_category = new QComboBox(this);
    m_category->addItem(tr("Todas as categorias"), QString());
    for (const QString& category : commandCatalogTopLevelCategories(m_entries))
        m_category->addItem(category, category);
    layout->addWidget(m_category);

    QSettings settings;
    const QStringList favorites = settings.value(QString::fromLatin1(kFavoritesKey)).toStringList();
    for (const QString& type : favorites) if (!type.isEmpty()) m_favoriteTypes.insert(type);
    if (settings.value(QString::fromLatin1(kRecentStatsKey)).toMap().isEmpty()) {
        const QStringList legacy = settings.value(QString::fromLatin1(kRecentKey)).toStringList();
        QVariantMap migrated; const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (int i = 0; i < legacy.size() && i < 20; ++i)
            migrated[legacy.at(i)] = QVariantMap{{QStringLiteral("count"), 1},
                                                 {QStringLiteral("last"), now - i}};
        if (!migrated.isEmpty()) settings.setValue(QString::fromLatin1(kRecentStatsKey), migrated);
    }
    reloadPresetEntries();

    auto* tools = new QHBoxLayout;
    m_favoritesOnly = new QCheckBox(tr("Somente favoritos"), this);
    m_favoriteButton = new QPushButton(tr("☆ Favoritar"), this);
    auto* importPresets = new QPushButton(tr("Importar presets…"), this);
    auto* exportPresets = new QPushButton(tr("Exportar presets…"), this);
    tools->addWidget(m_favoritesOnly); tools->addWidget(m_favoriteButton);
    tools->addStretch(1); tools->addWidget(importPresets); tools->addWidget(exportPresets);
    layout->addLayout(tools);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);
    m_list->setAccessibleName(tr("Resultados de comandos"));
    layout->addWidget(m_list, 1);

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setProperty("uiRole", QStringLiteral("hint"));
    layout->addWidget(m_details);

    m_hint = new QLabel(tr("Digite para filtrar · ↑/↓ navegam · Enter insere · Esc cancela"), this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_category, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { rebuild(); });
    connect(m_favoritesOnly, &QCheckBox::toggled, this, [this] { rebuild(); });
    connect(m_favoriteButton, &QPushButton::clicked, this, &CommandPickerDialog::toggleFavorite);
    connect(m_search, &QLineEdit::returnPressed, this, &CommandPickerDialog::acceptCurrent);
    connect(m_list, &QListWidget::itemActivated, this, [this] { acceptCurrent(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { acceptCurrent(); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        const QString type = m_list && m_list->currentItem()
            ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
        if (m_favoriteButton) m_favoriteButton->setText(m_favoriteTypes.contains(type)
            ? tr("★ Desfavoritar") : tr("☆ Favoritar"));
        if (!m_details) return;
        const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const CommandCatalogEntry& entry) {
            return entry.type == type;
        });
        if (it == m_entries.cend()) { m_details->clear(); return; }
        QStringList bits;
        bits << tr("Tipo: %1").arg(it->type);
        if (!it->path.isEmpty()) bits << tr("Categoria: %1").arg(it->path.join(QStringLiteral(" › ")));
        if (!it->shortcutKey.isEmpty()) bits << tr("Atalho: %1").arg(it->shortcutKey);
        if (!it->tags.isEmpty()) bits << tr("Tags: %1").arg(it->tags.join(QStringLiteral(", ")));
        m_details->setText(bits.join(QStringLiteral("   •   ")));
    });
    connect(importPresets, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Importar presets"), QString(),
                                                          tr("Presets LUDO (*.json)"));
        if (file.isEmpty()) return; QString error;
        if (!importCommandPresets(file, &error)) QMessageBox::warning(this, tr("Importar presets"), error);
        else { reloadPresetEntries(); rebuild(); }
    });
    connect(exportPresets, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getSaveFileName(this, tr("Exportar presets"),
                                                     QStringLiteral("LUDO_Command_Presets.json"),
                                                     tr("Presets LUDO (*.json)"));
        if (file.isEmpty()) return; if (!file.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) file += QStringLiteral(".json");
        QString error; if (!exportCommandPresets(file, &error)) QMessageBox::warning(this, tr("Exportar presets"), error);
    });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        QListWidgetItem* item = m_list->itemAt(point); if (!item) return;
        m_list->setCurrentItem(item); const QString type = item->data(Qt::UserRole).toString();
        QMenu menu(this); menu.addAction(m_favoriteTypes.contains(type) ? tr("★ Desfavoritar") : tr("☆ Favoritar"), this, &CommandPickerDialog::toggleFavorite);
        if (type.startsWith(QLatin1String("preset:"))) {
            menu.addSeparator(); menu.addAction(tr("Excluir preset"), this, [this, type] {
                removeCommandPreset(type.mid(7)); m_favoriteTypes.remove(type);
                QSettings().setValue(QString::fromLatin1(kFavoritesKey), m_favoriteTypes.values());
                reloadPresetEntries(); rebuild();
            });
        }
        menu.exec(m_list->viewport()->mapToGlobal(point));
    });

    rebuild();
    m_search->setFocus();
}

bool CommandPickerDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            const int count = m_list ? m_list->count() : 0;
            if (count > 0) {
                int row = m_list->currentRow();
                if (row < 0) row = 0;
                row += key->key() == Qt::Key_Down ? 1 : -1;
                row = qBound(0, row, count - 1);
                m_list->setCurrentRow(row);
                m_list->scrollToItem(m_list->currentItem());
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPickerDialog::keyPressEvent(QKeyEvent* event)
{
    if (!event) return;
    if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
        const int count = m_list ? m_list->count() : 0;
        if (count > 0) {
            int row = m_list->currentRow();
            if (row < 0) row = 0;
            row += event->key() == Qt::Key_Down ? 1 : -1;
            row = qBound(0, row, count - 1);
            m_list->setCurrentRow(row);
            m_list->scrollToItem(m_list->currentItem());
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        acceptCurrent();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void CommandPickerDialog::rebuild()
{
    const QString query = m_search->text().trimmed().toCaseFolded();
    const QString category = m_category ? m_category->currentData().toString() : QString();
    const QVariantMap recentStats = QSettings().value(QString::fromLatin1(kRecentStatsKey)).toMap();

    QVector<int> indexes;
    for (int i = 0; i < m_entries.size(); ++i) {
        const CommandCatalogEntry& entry = m_entries.at(i);
        if (m_favoritesOnly && m_favoritesOnly->isChecked() && !m_favoriteTypes.contains(entry.type)) continue;
        if (!category.isEmpty() && (entry.path.isEmpty() || entry.path.first() != category)) continue;
        if (query.isEmpty() || searchableText(entry).contains(query)) indexes.push_back(i);
    }
    if (query.isEmpty()) {
        std::stable_sort(indexes.begin(), indexes.end(), [&](int a, int b) {
            const QString at = m_entries.at(a).type, bt = m_entries.at(b).type;
            const bool af = m_favoriteTypes.contains(at), bf = m_favoriteTypes.contains(bt);
            if (af != bf) return af;
            const QVariantMap as = recentStats.value(at).toMap(), bs = recentStats.value(bt).toMap();
            const qint64 av = as.value(QStringLiteral("last")).toLongLong() + qMin(100, as.value(QStringLiteral("count")).toInt()) * 3600LL;
            const qint64 bv = bs.value(QStringLiteral("last")).toLongLong() + qMin(100, bs.value(QStringLiteral("count")).toInt()) * 3600LL;
            return av > bv;
        });
    }

    m_list->clear();
    for (int index : std::as_const(indexes)) {
        const CommandCatalogEntry& entry = m_entries.at(index);
        QString category = entry.path.join(QStringLiteral(" › "));
        if (m_favoriteTypes.contains(entry.type)) category = tr("Favoritos") + (category.isEmpty() ? QString() : QStringLiteral(" › ") + category);
        else if (query.isEmpty() && recentStats.contains(entry.type)) category = tr("Recentes") + (category.isEmpty() ? QString() : QStringLiteral(" › ") + category);
        const QString star = m_favoriteTypes.contains(entry.type) ? QStringLiteral("★ ") : QString();
        auto* item = new QListWidgetItem(category.isEmpty()
                                             ? star + entry.label
                                             : tr("%1%2\n%3").arg(star, entry.label, category), m_list);
        item->setData(Qt::UserRole, entry.type);
        item->setToolTip(entry.type);
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
    m_hint->setText(m_list->count() == 0
                        ? tr("Nenhum comando encontrado.")
                        : tr("Comandos: %1 · Enter insere · Esc cancela").arg(m_list->count()));
}

void CommandPickerDialog::acceptCurrent()
{
    QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    m_selectedType = item->data(Qt::UserRole).toString();
    if (m_selectedType.isEmpty()) return;
    remember(m_selectedType);
    accept();
}

void CommandPickerDialog::remember(const QString& type)
{
    QSettings settings;
    QVariantMap stats = settings.value(QString::fromLatin1(kRecentStatsKey)).toMap();
    QVariantMap current = stats.value(type).toMap();
    current[QStringLiteral("count")] = current.value(QStringLiteral("count")).toInt() + 1;
    current[QStringLiteral("last")] = QDateTime::currentSecsSinceEpoch(); stats[type] = current;
    QStringList keys = stats.keys();
    std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
        return stats.value(a).toMap().value(QStringLiteral("last")).toLongLong() >
               stats.value(b).toMap().value(QStringLiteral("last")).toLongLong();
    });
    while (keys.size() > 20) stats.remove(keys.takeLast());
    settings.setValue(QString::fromLatin1(kRecentStatsKey), stats);
    QStringList legacy = settings.value(QString::fromLatin1(kRecentKey)).toStringList();
    legacy.removeAll(type); legacy.prepend(type); while (legacy.size() > 20) legacy.removeLast();
    settings.setValue(QString::fromLatin1(kRecentKey), legacy);
}

void CommandPickerDialog::toggleFavorite()
{
    if (!m_list || !m_list->currentItem()) return;
    const QString type = m_list->currentItem()->data(Qt::UserRole).toString();
    if (type.isEmpty()) return;
    if (m_favoriteTypes.contains(type)) m_favoriteTypes.remove(type); else m_favoriteTypes.insert(type);
    QSettings().setValue(QString::fromLatin1(kFavoritesKey), m_favoriteTypes.values());
    rebuild();
}

void CommandPickerDialog::reloadPresetEntries()
{
    for (int i = m_entries.size() - 1; i >= 0; --i)
        if (m_entries.at(i).type.startsWith(QLatin1String("preset:"))) m_entries.removeAt(i);
    for (const CommandPreset& preset : loadCommandPresets()) {
        CommandCatalogEntry entry;
        entry.path = {tr("Presets"), preset.commandType};
        entry.label = preset.name;
        entry.type = QStringLiteral("preset:%1").arg(preset.id);
        m_entries.push_back(entry);
    }
}

QString CommandPickerDialog::execCommand(const QPoint& globalPos)
{
    if (!globalPos.isNull()) move(globalPos + QPoint(0, 4));
    m_selectedType.clear();
    return exec() == QDialog::Accepted ? m_selectedType : QString();
}

} // namespace ui
