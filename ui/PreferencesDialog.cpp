#include "PreferencesDialog.h"

#include "EditorUiPreferences.h"
#include "EditorUiPrimitives.h"
#include "EditorShortcutRegistry.h"
#include "Icons.h"
#include "core/Editor.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {

PreferencesDialog::PreferencesDialog(core::Editor& editorRef, QMenuBar* menuBar, QWidget* parent)
    : QDialog(parent), ed(editorRef)
{
    setWindowTitle(tr("Preferências do Editor"));
    setMinimumSize(620, 500);
    setAccessibleName(tr("Preferências do Editor"));

    const EditorUiPreferences initial = EditorUiPreferences::load();

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(primitives::infoBanner(
        tr("Estas opções pertencem a esta instalação do LUDO Map Editor. Elas não alteram o arquivo do projeto nem o projeto do RPG Maker vinculado."), this));

    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs, 1);

    // ---------------------------------------------------------- Interface
    auto* interfacePage = new QWidget(tabs);
    auto* interfaceLayout = new QVBoxLayout(interfacePage);
    interfaceLayout->addWidget(primitives::sectionTitle(tr("Aparência do Editor"), interfacePage));
    auto* interfaceForm = new QFormLayout;

    auto* scale = new QComboBox(interfacePage);
    for (int value : {100, 110, 125, 150})
        scale->addItem(QStringLiteral("%1%").arg(value), value);
    scale->setCurrentIndex(qMax(0, scale->findData(initial.uiScalePercent)));
    scale->setAccessibleName(tr("Escala da interface"));

    interfaceForm->addRow(tr("Escala da interface:"), scale);
    interfaceLayout->addLayout(interfaceForm);
    interfaceLayout->addWidget(primitives::hintLabel(
        tr("O LUDO usa tema escuro fixo para manter contraste consistente com os ícones. A escala passa a valer após reiniciar a LUDO."), interfacePage));
    interfaceLayout->addStretch(1);
    tabs->addTab(interfacePage, tr("Interface"));

    // ------------------------------------------------------------- Ícones
    auto* iconsPage = new QWidget(tabs);
    auto* iconsLayout = new QVBoxLayout(iconsPage);
    iconsLayout->addWidget(primitives::sectionTitle(tr("Ícones da interface"), iconsPage));
    iconsLayout->addWidget(primitives::hintLabel(
        tr("Troque os ícones do editor sem substituir arquivos do programa. Ao confirmar, o LUDO copia as imagens para a configuração local desta instalação."),
        iconsPage));

    auto* iconTop = new QHBoxLayout;
    auto* iconSearch = new QLineEdit(iconsPage);
    iconSearch->setPlaceholderText(tr("Buscar ação ou nome do ícone…"));
    iconSearch->setClearButtonEnabled(true);
    iconSearch->setAccessibleName(tr("Buscar ícone"));
    iconTop->addWidget(iconSearch, 1);
    auto* onlyMissingIcons = new QCheckBox(tr("Só pendentes"), iconsPage);
    onlyMissingIcons->setToolTip(tr("Mostrar apenas ações que ainda não possuem PNG/SVG dedicado de fábrica."));
    iconTop->addWidget(onlyMissingIcons);
    auto* iconSize = new QSpinBox(iconsPage);
    iconSize->setRange(16, 64);
    iconSize->setSingleStep(2);
    iconSize->setValue(icons::toolbarIconSize());
    iconSize->setSuffix(tr(" px"));
    iconSize->setToolTip(tr("Tamanho dos ícones das barras principais. A alteração é aplicada sem reiniciar."));
    iconTop->addWidget(new QLabel(tr("Tamanho:"), iconsPage));
    iconTop->addWidget(iconSize);
    iconsLayout->addLayout(iconTop);

    const QVector<icons::Descriptor> iconCatalog = icons::catalog();
    const int iconKeyRole = Qt::UserRole;
    const int iconPathRole = Qt::UserRole + 1;
    const int iconStateRole = Qt::UserRole + 2; // 0=inalterado, 1=trocar, 2=restaurar
    const int iconMissingRole = Qt::UserRole + 3;
    auto* iconTable = new QTableWidget(iconCatalog.size(), 4, iconsPage);
    iconTable->setHorizontalHeaderLabels({tr("Ícone"), tr("Ação"), tr("Origem"), tr("Status")});
    iconTable->verticalHeader()->setVisible(false);
    iconTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    iconTable->setSelectionMode(QAbstractItemView::SingleSelection);
    iconTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    iconTable->setIconSize(QSize(40, 40));
    iconTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    iconTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    iconTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    iconTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    for (int row = 0; row < iconCatalog.size(); ++row) {
        const auto& descriptor = iconCatalog.at(row);
        const QString customPath = icons::customIconPath(descriptor.key);
        auto* preview = new QTableWidgetItem;
        preview->setIcon(QIcon(icons::pixmap(descriptor.key, 40)));
        preview->setData(iconKeyRole, descriptor.key);
        preview->setData(iconPathRole, customPath);
        preview->setData(iconStateRole, 0);
        const bool missingArtwork = !icons::hasDedicatedArtwork(descriptor.key);
        preview->setData(iconMissingRole, missingArtwork);
        preview->setToolTip(descriptor.key);
        iconTable->setItem(row, 0, preview);

        auto* label = new QTableWidgetItem(descriptor.label);
        label->setToolTip(tr("Chave interna: %1").arg(descriptor.key));
        iconTable->setItem(row, 1, label);

        auto* source = new QTableWidgetItem(customPath.isEmpty() ? tr("Padrão") : tr("Personalizado"));
        if (!customPath.isEmpty()) source->setToolTip(customPath);
        iconTable->setItem(row, 2, source);
        auto* status = new QTableWidgetItem;
        if (!customPath.isEmpty()) status->setText(tr("Personalizado"));
        else if (missingArtwork) status->setText(tr("CRIAR ÍCONE"));
        else status->setText(tr("OK"));
        status->setToolTip(missingArtwork
            ? tr("Ainda não existe PNG/SVG dedicado para esta ação. O editor usa um fallback até você criar/importar um ícone.")
            : tr("Existe arte dedicada de fábrica para esta ação."));
        iconTable->setItem(row, 3, status);
        iconTable->setRowHeight(row, 46);
    }
    if (iconTable->rowCount() > 0) iconTable->selectRow(0);
    iconsLayout->addWidget(iconTable, 1);

    const auto filterIcons = [=] {
        const QString query = iconSearch->text().trimmed();
        for (int row = 0; row < iconTable->rowCount(); ++row) {
            const QString key = iconTable->item(row, 0)->data(iconKeyRole).toString();
            const QString label = iconTable->item(row, 1)->text();
            auto* preview = iconTable->item(row, 0);
            const bool missingFactory = preview->data(iconMissingRole).toBool();
            const bool pendingArtwork = missingFactory && preview->data(iconPathRole).toString().isEmpty();
            const bool textMatch = query.isEmpty() || key.contains(query, Qt::CaseInsensitive) ||
                                   label.contains(query, Qt::CaseInsensitive);
            iconTable->setRowHidden(row, !textMatch || (onlyMissingIcons->isChecked() && !pendingArtwork));
        }
    };
    connect(iconSearch, &QLineEdit::textChanged, this, [filterIcons](const QString&) { filterIcons(); });
    connect(onlyMissingIcons, &QCheckBox::toggled, this, [filterIcons](bool) { filterIcons(); });

    auto* iconButtons = new QHBoxLayout;
    auto* changeIcon = new QPushButton(tr("Alterar ícone…"), iconsPage);
    auto* resetIcon = new QPushButton(tr("Restaurar selecionado"), iconsPage);
    auto* importIconPreset = new QPushButton(icons::get(QStringLiteral("icon-preset-import")), tr("Importar preset…"), iconsPage);
    auto* exportIconPreset = new QPushButton(icons::get(QStringLiteral("icon-preset-export")), tr("Exportar preset…"), iconsPage);
    auto* resetAllIcons = new QPushButton(tr("Restaurar todos"), iconsPage);
    iconButtons->addWidget(changeIcon);
    iconButtons->addWidget(resetIcon);
    iconButtons->addStretch(1);
    iconButtons->addWidget(importIconPreset);
    iconButtons->addWidget(exportIconPreset);
    iconButtons->addWidget(resetAllIcons);
    iconsLayout->addLayout(iconButtons);

    const auto chooseIcon = [=, this] {
        const int row = iconTable->currentRow();
        if (row < 0) return;
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Escolher ícone"), QString(),
            tr("Imagens (*.png *.jpg *.jpeg *.webp *.bmp *.ico *.svg *.svgz);;Todos os arquivos (*.*)"));
        if (file.isEmpty()) return;
        const QPixmap previewPixmap = icons::filePixmap(file, 40);
        const QFileInfo info(file);
        if (previewPixmap.isNull() || !info.exists()) {
            QMessageBox::warning(this, tr("Ícone inválido"), tr("O arquivo selecionado não pôde ser usado como ícone."));
            return;
        }
        auto* preview = iconTable->item(row, 0);
        preview->setIcon(QIcon(previewPixmap));
        preview->setData(iconPathRole, file);
        preview->setData(iconStateRole, 1);
        auto* source = iconTable->item(row, 2);
        source->setText(tr("Novo: %1").arg(info.fileName()));
        source->setToolTip(file);
        iconTable->item(row, 3)->setText(tr("Personalizado ao confirmar"));
        filterIcons();
    };
    connect(changeIcon, &QPushButton::clicked, this, chooseIcon);
    connect(iconTable, &QTableWidget::cellDoubleClicked, this, [chooseIcon](int, int) { chooseIcon(); });

    connect(resetIcon, &QPushButton::clicked, this, [=] {
        const int row = iconTable->currentRow();
        if (row < 0) return;
        auto* preview = iconTable->item(row, 0);
        const QString key = preview->data(iconKeyRole).toString();
        preview->setIcon(QIcon(icons::defaultPixmap(key, 40)));
        preview->setData(iconPathRole, QString());
        preview->setData(iconStateRole, 2);
        iconTable->item(row, 2)->setText(tr("Padrão (ao confirmar)"));
        iconTable->item(row, 2)->setToolTip(QString());
        iconTable->item(row, 3)->setText(preview->data(iconMissingRole).toBool() ? tr("CRIAR ÍCONE") : tr("OK"));
        filterIcons();
    });
    connect(resetAllIcons, &QPushButton::clicked, this, [=] {
        if (QMessageBox::question(this, tr("Restaurar ícones"),
                                  tr("Restaurar todos os ícones personalizados para o padrão ao confirmar?"),
                                  QMessageBox::Yes | QMessageBox::Cancel,
                                  QMessageBox::Cancel) != QMessageBox::Yes) return;
        for (int row = 0; row < iconTable->rowCount(); ++row) {
            auto* preview = iconTable->item(row, 0);
            const QString key = preview->data(iconKeyRole).toString();
            preview->setIcon(QIcon(icons::defaultPixmap(key, 40)));
            preview->setData(iconPathRole, QString());
            preview->setData(iconStateRole, 2);
            iconTable->item(row, 2)->setText(tr("Padrão (ao confirmar)"));
            iconTable->item(row, 2)->setToolTip(QString());
            iconTable->item(row, 3)->setText(preview->data(iconMissingRole).toBool() ? tr("CRIAR ÍCONE") : tr("OK"));
        }
        filterIcons();
    });
    const auto reloadIconRows = [=] {
        for (int row = 0; row < iconTable->rowCount(); ++row) {
            auto* preview = iconTable->item(row, 0);
            const QString key = preview->data(iconKeyRole).toString();
            const QString customPath = icons::customIconPath(key);
            preview->setIcon(QIcon(icons::pixmap(key, 40)));
            preview->setData(iconPathRole, customPath);
            preview->setData(iconStateRole, 0);
            auto* source = iconTable->item(row, 2);
            source->setText(customPath.isEmpty() ? tr("Padrão") : tr("Personalizado"));
            source->setToolTip(customPath);
            auto* status = iconTable->item(row, 3);
            status->setText(!customPath.isEmpty() ? tr("Personalizado")
                                                  : (preview->data(iconMissingRole).toBool() ? tr("CRIAR ÍCONE") : tr("OK")));
        }
        iconSize->setValue(icons::toolbarIconSize());
        filterIcons();
    };
    connect(exportIconPreset, &QPushButton::clicked, this, [=, this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar preset de ícones"),
                                                           QStringLiteral("LUDO_Icons.ludoicons"),
                                                           tr("Preset de ícones LUDO (*.ludoicons);;Todos os arquivos (*.*)"));
        if (path.isEmpty()) return;
        QString error;
        if (!icons::exportPreset(path, &error)) {
            QMessageBox::warning(this, tr("Não foi possível exportar"), error);
            return;
        }
        QMessageBox::information(this, tr("Preset exportado"),
                                 tr("Preset de ícones salvo. Ele incorpora os arquivos personalizados e pode ser importado em outra instalação/projeto."));
    });
    connect(importIconPreset, &QPushButton::clicked, this, [=, this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar preset de ícones"), QString(),
                                                           tr("Preset de ícones LUDO (*.ludoicons *.json);;Todos os arquivos (*.*)"));
        if (path.isEmpty()) return;
        if (QMessageBox::question(this, tr("Importar preset de ícones"),
                                  tr("O preset substituirá o conjunto atual de ícones personalizados desta instalação. Continuar?"),
                                  QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        QString error;
        if (!icons::importPreset(path, &error)) {
            QMessageBox::warning(this, tr("Não foi possível importar"), error);
            return;
        }
        icons::refreshApplicationIcons(window());
        emit ed.layersChanged();
        reloadIconRows();
        QMessageBox::information(this, tr("Preset importado"), tr("Os ícones do preset foram aplicados."));
    });

    tabs->addTab(iconsPage, tr("Ícones"));

    // -------------------------------------------------------------- Mapa
    auto* mapPage = new QWidget(tabs);
    auto* mapLayout = new QVBoxLayout(mapPage);
    mapLayout->addWidget(primitives::sectionTitle(tr("Canvas e edição de mapas"), mapPage));
    auto* mapForm = new QFormLayout;

    auto* ghost = new QCheckBox(tr("Mostrar prévia da pintura"), mapPage);
    ghost->setChecked(ed.session.ghostPreview);
    auto* grid = new QCheckBox(tr("Mostrar grade"), mapPage);
    grid->setChecked(ed.session.showGrid);
    auto* multi = new QCheckBox(tr("Multigrade (grade da camada ativa)"), mapPage);
    multi->setChecked(ed.session.multigrid);

    auto makeColorButton = [this](const QColor& initialColor, QWidget* parentWidget) {
        auto* b = new QPushButton(parentWidget);
        b->setProperty("color", initialColor);
        const auto refresh = [b] {
            const QColor c = b->property("color").value<QColor>();
            b->setText(c.name(QColor::HexArgb));
            b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid palette(mid);padding:4px 10px;}")
                                 .arg(c.name(QColor::HexArgb), c.lightness() < 120 ? QStringLiteral("#fff") : QStringLiteral("#111")));
        };
        refresh();
        connect(b, &QPushButton::clicked, this, [this, b, refresh] {
            const QColor c = QColorDialog::getColor(b->property("color").value<QColor>(), this,
                                                     tr("Escolher cor"), QColorDialog::ShowAlphaChannel);
            if (c.isValid()) { b->setProperty("color", c); refresh(); }
        });
        return b;
    };

    auto* gridColor = makeColorButton(ed.session.gridColor, mapPage);
    auto* checker = new QCheckBox(tr("Fundo quadriculado de transparência"), mapPage);
    checker->setChecked(ed.session.checkerboardBackground);
    auto* checkerA = makeColorButton(ed.session.checkerColorA, mapPage);
    auto* checkerB = makeColorButton(ed.session.checkerColorB, mapPage);
    auto* checkerColors = new QWidget(mapPage);
    auto* checkerColorsLay = new QHBoxLayout(checkerColors);
    checkerColorsLay->setContentsMargins(0, 0, 0, 0);
    checkerColorsLay->addWidget(checkerA);
    checkerColorsLay->addWidget(checkerB);

    auto* snap = new QSpinBox(mapPage);
    snap->setRange(1, 512);
    snap->setValue(ed.session.snapGridSize);
    snap->setSuffix(tr(" px"));
    auto* dim = new QSpinBox(mapPage);
    dim->setRange(0, 100);
    dim->setValue(int(ed.session.focusDim * 100));
    dim->setSuffix(QStringLiteral(" %"));
    dim->setToolTip(tr("No modo “Focar”, define a visibilidade do fundo escurecido e a opacidade das camadas que ficam acima do foco. A camada selecionada permanece normal."));

    mapForm->addRow(ghost);
    mapForm->addRow(grid);
    mapForm->addRow(multi);
    mapForm->addRow(tr("Cor da grade:"), gridColor);
    mapForm->addRow(checker);
    mapForm->addRow(tr("Cores do fundo transparente:"), checkerColors);
    mapForm->addRow(tr("Snap de objetos/imagens:"), snap);
    mapForm->addRow(tr("Intensidade fora do foco:"), dim);
    mapLayout->addLayout(mapForm);
    mapLayout->addStretch(1);
    tabs->addTab(mapPage, tr("Mapa"));

    // ----------------------------------------------------------- Atalhos
    const QVector<EditorShortcutEntry> shortcutEntries = EditorShortcutRegistry::entries(menuBar);
    auto* shortcutsPage = new QWidget(tabs);
    auto* shortcutsLayout = new QVBoxLayout(shortcutsPage);
    shortcutsLayout->addWidget(primitives::sectionTitle(tr("Atalhos do Editor"), shortcutsPage));
    shortcutsLayout->addWidget(primitives::hintLabel(
        tr("Personalize as teclas das mesmas ações usadas pelos menus e pela Paleta de Ações. Conflitos são bloqueados."), shortcutsPage));
    auto* shortcutSearch = new QLineEdit(shortcutsPage);
    shortcutSearch->setPlaceholderText(tr("Buscar ação ou menu…"));
    shortcutSearch->setClearButtonEnabled(true);
    shortcutSearch->setAccessibleName(tr("Buscar atalho"));
    shortcutsLayout->addWidget(shortcutSearch);
    auto* shortcutTable = new QTableWidget(shortcutEntries.size(), 3, shortcutsPage);
    shortcutTable->setHorizontalHeaderLabels({tr("Ação"), tr("Menu"), tr("Atalho")});
    shortcutTable->verticalHeader()->setVisible(false);
    shortcutTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shortcutTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    shortcutTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    for (int row = 0; row < shortcutEntries.size(); ++row) {
        const EditorShortcutEntry& entry = shortcutEntries.at(row);
        auto* labelItem = new QTableWidgetItem(entry.label);
        labelItem->setData(Qt::UserRole, entry.key);
        labelItem->setData(Qt::UserRole + 1, entry.defaultShortcut);
        shortcutTable->setItem(row, 0, labelItem);
        shortcutTable->setItem(row, 1, new QTableWidgetItem(entry.category));
        auto* keyEdit = new QKeySequenceEdit(QKeySequence::fromString(entry.shortcut, QKeySequence::PortableText), shortcutTable);
        keyEdit->setMaximumSequenceLength(1);
        keyEdit->setClearButtonEnabled(true);
        shortcutTable->setCellWidget(row, 2, keyEdit);
    }
    shortcutsLayout->addWidget(shortcutTable, 1);
    connect(shortcutSearch, &QLineEdit::textChanged, this, [shortcutTable](const QString& text) {
        const QString query = text.trimmed();
        for (int row = 0; row < shortcutTable->rowCount(); ++row) {
            const QString haystack = shortcutTable->item(row, 0)->text() + QLatin1Char(' ') + shortcutTable->item(row, 1)->text();
            shortcutTable->setRowHidden(row, !query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive));
        }
    });
    auto* shortcutButtons = new QHBoxLayout;
    auto* resetSelectedShortcut = new QPushButton(tr("Restaurar selecionado"), shortcutsPage);
    auto* resetAllShortcuts = new QPushButton(tr("Restaurar todos"), shortcutsPage);
    shortcutButtons->addStretch(1);
    shortcutButtons->addWidget(resetSelectedShortcut);
    shortcutButtons->addWidget(resetAllShortcuts);
    shortcutsLayout->addLayout(shortcutButtons);
    connect(resetSelectedShortcut, &QPushButton::clicked, this, [shortcutTable] {
        const int row = shortcutTable->currentRow();
        if (row < 0) return;
        auto* item = shortcutTable->item(row, 0);
        auto* edit = qobject_cast<QKeySequenceEdit*>(shortcutTable->cellWidget(row, 2));
        if (item && edit) edit->setKeySequence(QKeySequence::fromString(item->data(Qt::UserRole + 1).toString(), QKeySequence::PortableText));
    });
    connect(resetAllShortcuts, &QPushButton::clicked, this, [shortcutTable] {
        for (int row = 0; row < shortcutTable->rowCount(); ++row) {
            auto* item = shortcutTable->item(row, 0);
            auto* edit = qobject_cast<QKeySequenceEdit*>(shortcutTable->cellWidget(row, 2));
            if (item && edit) edit->setKeySequence(QKeySequence::fromString(item->data(Qt::UserRole + 1).toString(), QKeySequence::PortableText));
        }
    });
    tabs->addTab(shortcutsPage, tr("Atalhos"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        EditorUiPreferences updated = initial;
        updated.theme = QStringLiteral("dark");
        updated.uiScalePercent = scale->currentData().toInt();
        updated.ghostPreview = ghost->isChecked();
        updated.showGrid = grid->isChecked();
        updated.multigrid = multi->isChecked();
        updated.gridColor = gridColor->property("color").value<QColor>();
        updated.checkerboardBackground = checker->isChecked();
        updated.checkerColorA = checkerA->property("color").value<QColor>();
        updated.checkerColorB = checkerB->property("color").value<QColor>();
        updated.snapGridSize = snap->value();
        updated.focusDim = dim->value() / 100.0;

        QVector<EditorShortcutEntry> pending = shortcutEntries;
        for (int row = 0; row < shortcutTable->rowCount() && row < pending.size(); ++row) {
            if (auto* edit = qobject_cast<QKeySequenceEdit*>(shortcutTable->cellWidget(row, 2)))
                pending[row].shortcut = edit->keySequence().toString(QKeySequence::PortableText);
        }
        // Só bloqueia conflitos introduzidos nesta edição. Conflitos históricos
        // entre defaults não podem impedir o usuário de alterar outra preferência.
        for (int row = 0; row < pending.size(); ++row) {
            if (row < shortcutEntries.size() && pending[row].shortcut == shortcutEntries[row].shortcut) continue;
            const QString conflict = EditorShortcutRegistry::conflictKey(pending, pending[row].shortcut, pending[row].key);
            if (conflict.isEmpty()) continue;
            QString other;
            for (const auto& candidate : pending) if (candidate.key == conflict) { other = candidate.label; break; }
            QMessageBox::warning(this, tr("Atalho em conflito"),
                                 tr("O atalho de “%1” também está sendo usado por “%2”. Escolha outra combinação.").arg(pending[row].label, other));
            return;
        }
        // Valida e aplica as mudanças de ícones somente depois que todas as
        // outras validações do diálogo passaram. Assim Cancelar nunca altera a UI.
        for (int row = 0; row < iconTable->rowCount(); ++row) {
            auto* item = iconTable->item(row, 0);
            if (!item || item->data(iconStateRole).toInt() != 1) continue;
            const QString key = item->data(iconKeyRole).toString();
            const QString path = item->data(iconPathRole).toString();
            QString error;
            if (!icons::setCustomIcon(key, path, &error)) {
                QMessageBox::warning(this, tr("Não foi possível alterar o ícone"), error);
                return;
            }
        }
        for (int row = 0; row < iconTable->rowCount(); ++row) {
            auto* item = iconTable->item(row, 0);
            if (!item || item->data(iconStateRole).toInt() != 2) continue;
            icons::clearCustomIcon(item->data(iconKeyRole).toString());
        }
        icons::setToolbarIconSize(iconSize->value());

        for (const EditorShortcutEntry& entry : pending) {
            if (entry.shortcut == entry.defaultShortcut) EditorShortcutRegistry::clearOverride(entry.key);
            else EditorShortcutRegistry::saveOverride(entry.key, entry.shortcut);
        }
        EditorShortcutRegistry::apply(menuBar);
        updated.save();

        m_restartRequired = updated.uiScalePercent != initial.uiScalePercent;

        ed.session.ghostPreview = updated.ghostPreview;
        ed.session.showGrid = updated.showGrid;
        ed.session.multigrid = updated.multigrid;
        ed.session.gridColor = updated.gridColor;
        ed.session.checkerboardBackground = updated.checkerboardBackground;
        ed.session.checkerColorA = updated.checkerColorA;
        ed.session.checkerColorB = updated.checkerColorB;
        ed.session.snapGridSize = updated.snapGridSize;
        ed.session.focusDim = updated.focusDim;
        emit ed.mapChanged();
        accept();
    });
}

} // namespace ui
