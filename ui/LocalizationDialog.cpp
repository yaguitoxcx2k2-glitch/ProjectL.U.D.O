#include "LocalizationDialog.h"

#include "core/EventCommandCodec.h"
#include "core/Localization.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace ui {
namespace {
QString suggestedKey(const QString& prefix, const QString& seed, int fallbackNumber = 0)
{
    QString key = core::normalizeLocalizationKey(prefix + QLatin1Char('.') + seed);
    if (key.isEmpty() || key == prefix) key = core::normalizeLocalizationKey(prefix + QLatin1Char('.') + QString::number(fallbackNumber));
    return key;
}

QString shortTextKey(const core::LocalizationSettings& localization, QString kind)
{
    if (kind == QLatin1String("picture_text")) kind = QStringLiteral("picture");
    kind = core::normalizeLocalizationKey(kind);
    if (kind.isEmpty()) kind = QStringLiteral("text");
    const QString prefix = QStringLiteral("text.%1.").arg(kind);
    for (int number = 1; number < 1000000; ++number) {
        const QString key = prefix + QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
        if (!localization.texts.contains(key)) return key;
    }
    return prefix + QString::number(localization.texts.size() + 1);
}

int renameCommandKeys(QVector<core::EventCommand>& commands, const QString& oldKey,
                      const QString& newKey)
{
    int changed = 0;
    for (core::EventCommand& command : commands) {
        for (const QString& field : {QStringLiteral("localizationKey"),
                                     QStringLiteral("speakerLocalizationKey"),
                                     QStringLiteral("titleLocalizationKey"),
                                     QStringLiteral("promptLocalizationKey")}) {
            if (command.params.value(field).toString() == oldKey) {
                command.params[field] = newKey;
                ++changed;
            }
        }
        QStringList choices = command.params.value(QStringLiteral("choiceLocalizationKeys")).toStringList();
        bool choicesChanged = false;
        for (QString& key : choices) if (key == oldKey) { key = newKey; ++changed; choicesChanged = true; }
        if (choicesChanged) command.params[QStringLiteral("choiceLocalizationKeys")] = choices;

        if (command.type != QLatin1String("choice.show")) continue;
        QVariantList branches = command.params.value(QStringLiteral("branches")).toList();
        for (int branchIndex = 0; branchIndex < branches.size(); ++branchIndex) {
            QVector<core::EventCommand> nested = core::eventCommandsFromVariantList(
                branches.at(branchIndex).toList());
            changed += renameCommandKeys(nested, oldKey, newKey);
            branches[branchIndex] = core::eventCommandsToVariantList(nested);
        }
        command.params[QStringLiteral("branches")] = branches;
    }
    return changed;
}

int renameProjectKey(core::Editor& editor, const QString& oldKey, const QString& newKey)
{
    int changed = 0;
    auto replace = [&](QString& key) { if (key == oldKey) { key = newKey; ++changed; } };
    replace(editor.titleScreen.titleTextKey);
    replace(editor.cutsceneSkip.labelTextKey);
    auto renameWidget = [&](core::UiWidgetSettings& widget) {
        replace(widget.textKey); replace(widget.placeholderTextKey);
        for (QString& key : widget.itemTextKeys) replace(key);
    };
    for (auto it = editor.gameUi.widgets.begin(); it != editor.gameUi.widgets.end(); ++it)
        renameWidget(it.value());
    for (auto component = editor.gameUi.components.begin(); component != editor.gameUi.components.end(); ++component)
        for (auto widget = component.value().widgets.begin(); widget != component.value().widgets.end(); ++widget)
            renameWidget(widget.value());
    for (auto category = editor.database.begin(); category != editor.database.end(); ++category)
        for (core::DatabaseRecord& record : category.value()) {
            for (const QString& field : {QStringLiteral("nameTextKey"), QStringLiteral("descriptionTextKey")})
                if (record.data.value(field).toString() == oldKey) { record.data[field] = newKey; ++changed; }
        }
    for (auto& document : editor.docs)
        for (auto& event : document.events)
            for (auto& page : event.pages)
                changed += renameCommandKeys(page.commands, oldKey, newKey);
    for (auto& commonEvent : editor.commonEvents)
        changed += renameCommandKeys(commonEvent.commands, oldKey, newKey);
    return changed;
}
}

LocalizationDialog::LocalizationDialog(core::Editor& editor, QWidget* parent)
    : QDialog(parent), m_editor(editor), m_working(editor.localization)
{
    m_working.ensureDefaults();
    setWindowTitle(tr("Localização do jogo — Chaves de texto"));
    resize(1050, 680);
    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(tr("A localização é opcional. Os textos originais continuam disponíveis como alternativa. "
                                "Use chaves apenas onde quiser traduzir; se uma tradução estiver faltando, o jogo usa o texto original."), this);
    intro->setWordWrap(true); root->addWidget(intro);

    auto* settings = new QFormLayout;
    m_enabled = new QCheckBox(tr("Ativar localização neste projeto"), this); m_enabled->setChecked(m_working.enabled);
    m_defaultLocale = new QComboBox(this); m_fallbackLocale = new QComboBox(this);
    m_initialMode = new QComboBox(this);
    m_initialMode->addItem(tr("Idioma padrão do jogo"), QStringLiteral("default"));
    m_initialMode->addItem(tr("Detectar idioma do sistema"), QStringLiteral("system"));
    m_initialMode->addItem(tr("Último idioma escolhido pelo jogador"), QStringLiteral("last"));
    m_initialMode->setCurrentIndex(qMax(0, m_initialMode->findData(m_working.initialMode)));
    settings->addRow(m_enabled); settings->addRow(tr("Idioma padrão:"), m_defaultLocale);
    settings->addRow(tr("Idioma alternativo:"), m_fallbackLocale); settings->addRow(tr("Idioma inicial:"), m_initialMode);
    root->addLayout(settings);

    auto* localeButtons = new QHBoxLayout;
    auto* addLocaleButton = new QPushButton(tr("Adicionar idioma…"), this);
    auto* removeLocaleButton = new QPushButton(tr("Remover idioma"), this);
    auto* scanButton = new QPushButton(tr("Encontrar textos localizáveis"), this);
    auto* importButton = new QPushButton(tr("Importar CSV…"), this);
    auto* exportButton = new QPushButton(tr("Exportar CSV…"), this);
    localeButtons->addWidget(addLocaleButton); localeButtons->addWidget(removeLocaleButton);
    localeButtons->addSpacing(16); localeButtons->addWidget(scanButton); localeButtons->addStretch(1);
    localeButtons->addWidget(importButton); localeButtons->addWidget(exportButton); root->addLayout(localeButtons);

    auto* keyButtons = new QHBoxLayout;
    auto* addKeyButton = new QPushButton(tr("+ Chave"), this);
    auto* renameKeyButton = new QPushButton(tr("Renomear chave…"), this);
    auto* removeKeyButton = new QPushButton(tr("Remover chave"), this);
    keyButtons->addWidget(addKeyButton); keyButtons->addWidget(renameKeyButton);
    keyButtons->addWidget(removeKeyButton); keyButtons->addStretch(1);
    m_status = new QLabel(this); keyButtons->addWidget(m_status); root->addLayout(keyButtons);

    m_table = new QTableWidget(this); m_table->setAlternatingRowColors(true); m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true); m_table->verticalHeader()->setVisible(false); root->addWidget(m_table, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); root->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, [this]{
        syncTableToWorking();
        m_working.enabled = m_enabled->isChecked();
        m_working.defaultLocale = m_defaultLocale->currentData().toString();
        m_working.fallbackLocale = m_fallbackLocale->currentData().toString();
        m_working.initialMode = m_initialMode->currentData().toString();
        m_working.ensureDefaults();
        for (const auto& rename : std::as_const(m_pendingRenames))
            renameProjectKey(m_editor, rename.first, rename.second);
        m_editor.localization = m_working; m_editor.markDirty(); accept();
    });
    connect(addLocaleButton, &QPushButton::clicked, this, &LocalizationDialog::addLocale);
    connect(removeLocaleButton, &QPushButton::clicked, this, &LocalizationDialog::removeLocale);
    connect(addKeyButton, &QPushButton::clicked, this, &LocalizationDialog::addKey);
    connect(renameKeyButton, &QPushButton::clicked, this, &LocalizationDialog::renameKey);
    connect(removeKeyButton, &QPushButton::clicked, this, &LocalizationDialog::removeKey);
    connect(scanButton, &QPushButton::clicked, this, &LocalizationDialog::scanProjectTexts);
    connect(importButton, &QPushButton::clicked, this, &LocalizationDialog::importCsv);
    connect(exportButton, &QPushButton::clicked, this, &LocalizationDialog::exportCsv);
    connect(m_table, &QTableWidget::cellChanged, this, [this](int, int){ if (!m_refreshing) updateStatus(); });

    rebuildLocaleSelectors(); rebuildTable();
}

void LocalizationDialog::rebuildLocaleSelectors()
{
    const QString oldDefault = m_defaultLocale->currentData().toString().isEmpty() ? m_working.defaultLocale : m_defaultLocale->currentData().toString();
    const QString oldFallback = m_fallbackLocale->currentData().toString().isEmpty() ? m_working.fallbackLocale : m_fallbackLocale->currentData().toString();
    m_defaultLocale->clear(); m_fallbackLocale->clear();
    for (const auto& locale : m_working.locales) if (locale.enabled) {
        const QString label = QStringLiteral("%1 (%2)").arg(locale.name, locale.code);
        m_defaultLocale->addItem(label, locale.code); m_fallbackLocale->addItem(label, locale.code);
    }
    m_defaultLocale->setCurrentIndex(qMax(0, m_defaultLocale->findData(oldDefault)));
    m_fallbackLocale->setCurrentIndex(qMax(0, m_fallbackLocale->findData(oldFallback)));
}

void LocalizationDialog::syncTableToWorking()
{
    if (!m_table || m_table->columnCount() < 1) return;
    QHash<QString, QHash<QString, QString>> rebuilt;
    QStringList locales; for (int c = 1; c < m_table->columnCount(); ++c) locales << m_table->horizontalHeaderItem(c)->data(Qt::UserRole).toString();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QString key = core::normalizeLocalizationKey(m_table->item(r,0) ? m_table->item(r,0)->text() : QString()); if (key.isEmpty()) continue;
        for (int c = 1; c < m_table->columnCount(); ++c) if (auto* item=m_table->item(r,c)) rebuilt[key][locales.value(c-1)] = item->text();
    }
    m_working.texts = rebuilt;
}

QString LocalizationDialog::resolvedPendingKey(QString key) const
{
    for (const auto& rename : m_pendingRenames)
        if (key == rename.first) key = rename.second;
    return key;
}

void LocalizationDialog::rebuildTable()
{
    m_refreshing = true;
    const QStringList locales = m_working.enabledLocaleCodes();
    m_table->clear(); m_table->setColumnCount(1 + locales.size());
    QStringList headers{tr("Chave")}; headers += locales; m_table->setHorizontalHeaderLabels(headers);
    for (int c = 1; c < m_table->columnCount(); ++c) m_table->horizontalHeaderItem(c)->setData(Qt::UserRole, locales.at(c-1));
    QStringList keys = m_working.texts.keys(); std::sort(keys.begin(), keys.end()); m_table->setRowCount(keys.size());
    for (int r=0;r<keys.size();++r) {
        auto* keyItem=new QTableWidgetItem(keys.at(r));
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        keyItem->setToolTip(tr("Use 'Renomear chave…' para preservar todas as referências."));
        m_table->setItem(r,0,keyItem);
        for(int c=0;c<locales.size();++c)m_table->setItem(r,c+1,new QTableWidgetItem(m_working.texts.value(keys.at(r)).value(locales.at(c))));
    }
    m_table->resizeColumnToContents(0); m_refreshing=false; updateStatus();
}

void LocalizationDialog::updateStatus()
{
    syncTableToWorking();
    QStringList parts; for (const QString& locale : m_working.enabledLocaleCodes()) parts << QStringLiteral("%1 %2%").arg(locale).arg(m_working.completionPercent(locale));
    m_status->setText(tr("Chaves: %1 · %2").arg(m_working.texts.size()).arg(parts.join(QStringLiteral(" · "))));
}

void LocalizationDialog::addLocale()
{
    syncTableToWorking(); bool ok=false; QString code=QInputDialog::getText(this,tr("Adicionar idioma"),tr("Código (ex.: en-US):"),QLineEdit::Normal,QString(),&ok).trimmed().replace('_','-'); if(!ok||code.isEmpty())return;
    if(m_working.hasLocale(code)){QMessageBox::information(this,tr("Idioma"),tr("Esse idioma já existe."));return;}
    QString name=QInputDialog::getText(this,tr("Adicionar idioma"),tr("Nome amigável:"),QLineEdit::Normal,code,&ok).trimmed();if(!ok)return;
    m_working.locales.push_back({code,name.isEmpty()?code:name,true});rebuildLocaleSelectors();rebuildTable();
}

void LocalizationDialog::removeLocale()
{
    syncTableToWorking();
    if (m_working.locales.size() <= 1) { QMessageBox::warning(this,tr("Idioma"),tr("O projeto precisa manter pelo menos um idioma.")); return; }
    QStringList labels; QHash<QString, QString> codeByLabel;
    for (const auto& locale : m_working.locales) { const QString label=QStringLiteral("%1 (%2)").arg(locale.name,locale.code); labels<<label; codeByLabel.insert(label,locale.code); }
    bool ok=false; const QString chosen=QInputDialog::getItem(this,tr("Remover idioma"),tr("Idioma:"),labels,0,false,&ok);
    if(!ok||chosen.isEmpty())return; const QString code=codeByLabel.value(chosen); if(code.isEmpty())return;
    if(QMessageBox::question(this,tr("Remover idioma"),tr("Remover %1 do catálogo? As traduções dessa coluna serão apagadas.").arg(code))!=QMessageBox::Yes)return;
    for(int i=m_working.locales.size()-1;i>=0;--i)if(m_working.locales[i].code.compare(code,Qt::CaseInsensitive)==0)m_working.locales.remove(i);
    for(auto it=m_working.texts.begin();it!=m_working.texts.end();++it)it.value().remove(code);
    if(m_working.defaultLocale.compare(code,Qt::CaseInsensitive)==0)m_working.defaultLocale=m_working.locales.value(0).code;
    if(m_working.fallbackLocale.compare(code,Qt::CaseInsensitive)==0)m_working.fallbackLocale=m_working.defaultLocale;
    rebuildLocaleSelectors();rebuildTable();
}

void LocalizationDialog::addKey()
{
    syncTableToWorking(); bool ok=false; QString key=core::normalizeLocalizationKey(QInputDialog::getText(this,tr("Nova chave"),tr("Chave:"),QLineEdit::Normal,QStringLiteral("ui."),&ok)); if(!ok||key.isEmpty())return;
    if(m_working.texts.contains(key)){QMessageBox::information(this,tr("Chave"),tr("Essa chave já existe."));return;} m_working.texts.insert(key,{}); rebuildTable();
}

void LocalizationDialog::renameKey()
{
    syncTableToWorking();
    const int row = m_table->currentRow();
    if (row < 0) { QMessageBox::information(this,tr("Renomear chave"),tr("Selecione uma chave primeiro.")); return; }
    const QString oldKey = core::normalizeLocalizationKey(m_table->item(row,0)->text());
    bool ok = false;
    const QString requested = QInputDialog::getText(this, tr("Renomear chave"), tr("Nova chave:"),
        QLineEdit::Normal, oldKey, &ok);
    if (!ok) return;
    const QString newKey = core::normalizeLocalizationKey(requested);
    if (newKey.isEmpty()) { QMessageBox::warning(this,tr("Renomear chave"),tr("Informe uma chave válida.")); return; }
    if (newKey == oldKey) return;
    if (m_working.texts.contains(newKey)) {
        QMessageBox::warning(this,tr("Renomear chave"),tr("A chave %1 já existe.").arg(newKey)); return;
    }
    m_working.texts.insert(newKey, m_working.texts.take(oldKey));
    m_pendingRenames.push_back({oldKey, newKey});
    rebuildTable();
}

void LocalizationDialog::removeKey()
{
    syncTableToWorking(); const int row=m_table->currentRow(); if(row<0)return; const QString key=core::normalizeLocalizationKey(m_table->item(row,0)->text());
    if(QMessageBox::question(this,tr("Remover chave"),tr("Remover %1? As referências existentes continuarão usando o texto original.").arg(key))!=QMessageBox::Yes)return;
    m_working.texts.remove(key);rebuildTable();
}

void LocalizationDialog::scanProjectTexts()
{
    syncTableToWorking(); int added=0, bound=0; const QString base=m_working.defaultLocale;
    auto add=[&](QString key,const QString& text){key=core::normalizeLocalizationKey(key);if(key.isEmpty()||text.trimmed().isEmpty())return key;if(!m_working.texts.contains(key)){m_working.texts[key][base]=text;++added;}else if(m_working.texts[key].value(base).isEmpty())m_working.texts[key][base]=text;return key;};
    const QVector<QPair<QString,QString>> systemTexts = {
        {QStringLiteral("system.title.new_game"),tr("Novo Jogo")},{QStringLiteral("system.title.continue"),tr("Continuar")},{QStringLiteral("system.title.quit"),tr("Sair")},
        {QStringLiteral("system.menu.title"),tr("Menu")},{QStringLiteral("system.menu.status"),tr("Status")},{QStringLiteral("system.menu.items"),tr("Itens")},
        {QStringLiteral("system.menu.equipment"),tr("Equipamento")},{QStringLiteral("system.menu.skills"),tr("Habilidades")},{QStringLiteral("system.menu.quests"),tr("Missões")},
        {QStringLiteral("system.menu.save"),tr("Salvar")},{QStringLiteral("system.menu.load"),tr("Carregar")},{QStringLiteral("system.menu.settings"),tr("Configurações")},
        {QStringLiteral("system.menu.return_title"),tr("Voltar ao título")},{QStringLiteral("system.settings.title"),tr("Configurações")},
        {QStringLiteral("system.settings.language"),tr("Idioma")},{QStringLiteral("system.settings.ui_scale"),tr("Escala da UI/texto")},
        {QStringLiteral("system.settings.reduce_shake"),tr("Reduzir tremores")},{QStringLiteral("system.settings.reduce_flash"),tr("Reduzir flashes")},
        {QStringLiteral("system.settings.strong_focus"),tr("Foco reforçado")},{QStringLiteral("system.settings.language_changed"),tr("Idioma alterado.")},
        {QStringLiteral("system.settings.fullscreen"),tr("Tela cheia")},{QStringLiteral("system.settings.renderer"),tr("Renderizador")},
        {QStringLiteral("system.settings.master_volume"),tr("Volume geral")},{QStringLiteral("system.settings.voice_volume"),tr("Voz")},
        {QStringLiteral("system.settings.ui_volume"),tr("Volume da UI")},{QStringLiteral("system.settings.text_speed"),tr("Velocidade do texto")},
        {QStringLiteral("system.settings.window_opacity"),tr("Opacidade das janelas")},{QStringLiteral("system.settings.ui_animation"),tr("Animação da UI")},
        {QStringLiteral("system.settings.scale_filter"),tr("Filtro de escala")},{QStringLiteral("system.common.on"),tr("Ligado")},
        {QStringLiteral("system.common.off"),tr("Desligado")},{QStringLiteral("system.common.bilinear"),tr("Bilinear")},
        {QStringLiteral("system.common.nearest"),tr("Nearest")},
        {QStringLiteral("system.shop.title"),tr("Loja")},{QStringLiteral("system.shop.buy_title"),tr("Loja — Comprar")},
        {QStringLiteral("system.shop.sell_title"),tr("Loja — Vender")},{QStringLiteral("system.shop.buy"),tr("Comprar")},
        {QStringLiteral("system.shop.sell"),tr("Vender")},{QStringLiteral("system.shop.exit"),tr("Sair")},
        {QStringLiteral("system.shop.choose_mode"),tr("Escolha se deseja comprar ou vender itens.\n\nDinheiro: %1 G")},
        {QStringLiteral("system.shop.no_products"),tr("Nenhum produto disponível.")},
        {QStringLiteral("system.shop.detail"),tr("%1\n\n%2\n\nPreço unitário: %3 G\nQuantidade: %4\nVocê possui: %5\nTotal: %6 G")},
        {QStringLiteral("system.shop.entry"),tr("%1   %2 G   [×%3]")},
        {QStringLiteral("system.shop.insufficient_money"),tr("Dinheiro insuficiente.")},
        {QStringLiteral("system.shop.insufficient_quantity"),tr("Você não possui essa quantidade.")},
        {QStringLiteral("system.shop.bought"),tr("Comprado: %1 ×%2")},{QStringLiteral("system.shop.sold"),tr("Vendido: %1 ×%2")},
        {QStringLiteral("system.battle.result"),tr("Resultado")},{QStringLiteral("system.battle.round"),tr("Batalha — Rodada %1")},
        {QStringLiteral("system.battle.executing"),tr("Executando ação…")},{QStringLiteral("system.battle.turn"),tr("Turno de %1")},
        {QStringLiteral("system.battle.choose_enemy"),tr("Escolha o inimigo")},{QStringLiteral("system.battle.choose_skill"),tr("Escolha uma habilidade")},
        {QStringLiteral("system.battle.choose_item"),tr("Escolha um item")},{QStringLiteral("system.battle.choose_ally"),tr("Escolha um aliado")},
        {QStringLiteral("system.battle.attack"),tr("Atacar")},{QStringLiteral("system.battle.skill"),tr("Habilidade")},
        {QStringLiteral("system.battle.item"),tr("Item")},{QStringLiteral("system.battle.defend"),tr("Defender")},
        {QStringLiteral("system.battle.escape"),tr("Fugir")},{QStringLiteral("system.battle.continue"),tr("Continuar")}
    };
    for (const auto& pair : systemTexts) add(pair.first, pair.second);
    if(!m_editor.titleScreen.titleText.trimmed().isEmpty()){QString key=resolvedPendingKey(m_editor.titleScreen.titleTextKey);if(key.isEmpty())key=QStringLiteral("ui.title.title");key=add(key,m_editor.titleScreen.titleText);if(m_editor.titleScreen.titleTextKey.isEmpty()){m_editor.titleScreen.titleTextKey=key;++bound;}}
    if(!m_editor.cutsceneSkip.label.trimmed().isEmpty()){QString key=resolvedPendingKey(m_editor.cutsceneSkip.labelTextKey);if(key.isEmpty())key=QStringLiteral("system.cutscene.skip");key=add(key,m_editor.cutsceneSkip.label);if(m_editor.cutsceneSkip.labelTextKey.isEmpty()){m_editor.cutsceneSkip.labelTextKey=key;++bound;}}
    int wi=0;
    auto scanWidget=[&](core::UiWidgetSettings&w,const QString&prefix){
        ++wi;
        if(!w.text.trimmed().isEmpty()){QString key=resolvedPendingKey(w.textKey);if(key.isEmpty())key=suggestedKey(prefix,w.name,wi);key=add(key,w.text);if(w.textKey.isEmpty()){w.textKey=key;++bound;}}
        if(!w.placeholder.trimmed().isEmpty()){QString key=resolvedPendingKey(w.placeholderTextKey);if(key.isEmpty())key=suggestedKey(prefix,w.name+QStringLiteral(".placeholder"),wi);key=add(key,w.placeholder);if(w.placeholderTextKey.isEmpty()){w.placeholderTextKey=key;++bound;}}
        while(w.itemTextKeys.size()<w.items.size())w.itemTextKeys.push_back(QString());
        for(int i=0;i<w.items.size();++i){if(w.items.at(i).trimmed().isEmpty())continue;QString key=resolvedPendingKey(w.itemTextKeys.at(i));if(key.isEmpty())key=suggestedKey(prefix,w.name+QStringLiteral(".item.%1").arg(i+1),wi);key=add(key,w.items.at(i));if(w.itemTextKeys.at(i).isEmpty()){w.itemTextKeys[i]=key;++bound;}}
    };
    for(auto it=m_editor.gameUi.widgets.begin();it!=m_editor.gameUi.widgets.end();++it)scanWidget(it.value(),QStringLiteral("ui.%1").arg(it.value().screen));
    for(auto component=m_editor.gameUi.components.begin();component!=m_editor.gameUi.components.end();++component)
        for(auto widget=component.value().widgets.begin();widget!=component.value().widgets.end();++widget)
            scanWidget(widget.value(),QStringLiteral("ui.component.%1").arg(component.key()));
    for(auto dit=m_editor.database.begin();dit!=m_editor.database.end();++dit){int ri=0;for(auto&rec:dit.value()){++ri;if(!rec.name.trimmed().isEmpty()){QString key=resolvedPendingKey(rec.data.value("nameTextKey").toString());if(key.isEmpty())key=suggestedKey(QStringLiteral("db.%1.%2").arg(dit.key(),rec.number),QStringLiteral("name"),ri);key=add(key,rec.name);if(rec.data.value("nameTextKey").toString().isEmpty()){rec.data["nameTextKey"]=key;++bound;}}if(!rec.description.trimmed().isEmpty()){QString key=resolvedPendingKey(rec.data.value("descriptionTextKey").toString());if(key.isEmpty())key=suggestedKey(QStringLiteral("db.%1.%2").arg(dit.key(),rec.number),QStringLiteral("description"),ri);key=add(key,rec.description);if(rec.data.value("descriptionTextKey").toString().isEmpty()){rec.data["descriptionTextKey"]=key;++bound;}}}}
    std::function<void(QVector<core::EventCommand>&,const QString&)> scanCommands;
    scanCommands=[&](QVector<core::EventCommand>& commands,const QString& prefix){
        auto bindField=[&](core::EventCommand&cmd,const QString&field,const QString&kind){
            QString fallback=cmd.params.value(field).toString();
            if(cmd.type==QLatin1String("picture.text")&&field==QLatin1String("text"))
                fallback=cmd.params.value(QStringLiteral("rich")).toMap().value(QStringLiteral("text")).toString();
            if(fallback.trimmed().isEmpty())return;
            // message/subtitle/picture.text compartilham `localizationKey`
            // no runtime. Os demais campos usam chaves nomeadas para que
            // titulo, prompt e speaker possam coexistir no mesmo comando.
            const QString keyField=field==QLatin1String("text")
                ? QStringLiteral("localizationKey")
                : field+QStringLiteral("LocalizationKey");
            QString key=resolvedPendingKey(cmd.params.value(keyField).toString());
            if(key.isEmpty())key=shortTextKey(m_working,kind);
            key=add(key,fallback);
            if(cmd.params.value(keyField).toString().isEmpty()){cmd.params[keyField]=key;++bound;}
        };
        for(int ci=0;ci<commands.size();++ci){
            core::EventCommand&cmd=commands[ci];
            if(cmd.type==QLatin1String("message")||cmd.type==QLatin1String("subtitle.show")||cmd.type==QLatin1String("subtitle.enqueue")||cmd.type==QLatin1String("bubble.show")||cmd.type==QLatin1String("notification.show")){
                bindField(cmd,QStringLiteral("text"),cmd.type==QLatin1String("message")?QStringLiteral("message"):QStringLiteral("subtitle"));
                if(cmd.type!=QLatin1String("notification.show"))bindField(cmd,QStringLiteral("speaker"),QStringLiteral("speaker"));
            }else if(cmd.type==QLatin1String("picture.text")){
                bindField(cmd,QStringLiteral("text"),QStringLiteral("picture_text"));
            }else if(cmd.type==QLatin1String("input.number")||cmd.type==QLatin1String("input.text")||
                     cmd.type==QLatin1String("input.confirm")||cmd.type==QLatin1String("input.item")||
                     cmd.type==QLatin1String("inn.open")){
                bindField(cmd,QStringLiteral("title"),QStringLiteral("title"));
                bindField(cmd,QStringLiteral("prompt"),QStringLiteral("prompt"));
            }
            if(cmd.type!=QLatin1String("choice.show"))continue;
            QStringList choices=cmd.params.value(QStringLiteral("choices")).toStringList();
            if(choices.isEmpty())for(const QVariant&value:cmd.params.value(QStringLiteral("choices")).toList())choices.push_back(value.toString());
            QStringList keys=cmd.params.value(QStringLiteral("choiceLocalizationKeys")).toStringList();
            while(keys.size()<choices.size())keys.push_back(QString());
            bool changed=false;
            for(int i=0;i<choices.size();++i){
                if(choices.at(i).trimmed().isEmpty())continue;
                QString key=resolvedPendingKey(keys.at(i));
                if(key.isEmpty()){key=shortTextKey(m_working,QStringLiteral("choice"));changed=true;}
                keys[i]=add(key,choices.at(i));
            }
            if(changed){cmd.params[QStringLiteral("choiceLocalizationKeys")]=keys;++bound;}

            // As escolhas guardam comandos aninhados. Eles fazem parte do
            // jogo tanto quanto a pagina principal e precisam entrar no CSV.
            QVariantList branches=cmd.params.value(QStringLiteral("branches")).toList();
            for(int bi=0;bi<branches.size();++bi){
                QVector<core::EventCommand> nested=core::eventCommandsFromVariantList(branches[bi].toList());
                scanCommands(nested,QStringLiteral("%1.choice.%2").arg(prefix).arg(bi+1));
                branches[bi]=core::eventCommandsToVariantList(nested);
            }
            cmd.params[QStringLiteral("branches")]=branches;
        }
    };
    for(auto&doc:m_editor.docs)for(auto&ev:doc.events)for(int pi=0;pi<ev.pages.size();++pi)scanCommands(ev.pages[pi].commands,QStringLiteral("map.%1.event.%2").arg(doc.id,ev.id));
    for(auto&ce:m_editor.commonEvents)scanCommands(ce.commands,QStringLiteral("common.%1").arg(ce.number));
    if(bound>0)m_editor.markDirty(); rebuildTable(); QMessageBox::information(this,tr("Textos localizáveis"),tr("Novas chaves: %1.\nReferências vinculadas: %2.\n\nOs textos originais foram preservados como alternativa.").arg(added).arg(bound));
}

void LocalizationDialog::importCsv()
{
    if(!m_pendingRenames.isEmpty()){QMessageBox::information(this,tr("Importar CSV"),tr("Confirme as renomeações com OK e reabra a janela antes de importar um CSV."));return;}
    syncTableToWorking();const QString path=QFileDialog::getOpenFileName(this,tr("Importar traduções"),m_editor.projectRoot(),tr("CSV (*.csv)"));if(path.isEmpty())return;QStringList warnings;QString error;if(!core::importLocalizationCsv(m_working,path,&warnings,&error)){QMessageBox::critical(this,tr("Importar CSV"),error);return;}rebuildLocaleSelectors();rebuildTable();QString msg=tr("CSV importado com sucesso.");if(!warnings.isEmpty())msg+=QStringLiteral("\n\n")+warnings.mid(0,12).join(QLatin1Char('\n'));QMessageBox::information(this,tr("Importar CSV"),msg);
}

void LocalizationDialog::exportCsv()
{
    syncTableToWorking();QString path=QFileDialog::getSaveFileName(this,tr("Exportar traduções"),m_editor.projectRoot()+QStringLiteral("/localization.csv"),tr("CSV (*.csv)"));if(path.isEmpty())return;if(!path.endsWith(QStringLiteral(".csv"),Qt::CaseInsensitive))path+=QStringLiteral(".csv");QString error;if(!core::exportLocalizationCsv(m_working,path,&error))QMessageBox::critical(this,tr("Exportar CSV"),error);else QMessageBox::information(this,tr("Exportar CSV"),tr("Traduções exportadas."));
}

} // namespace ui
