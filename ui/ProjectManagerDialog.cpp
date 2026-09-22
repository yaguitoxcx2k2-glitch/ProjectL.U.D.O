#include "ProjectManagerDialog.h"
#include "ProjectCreationWorkflow.h"
#include "ProjectDashboard.h"

#include "core/ProjectIO.h"
#include "core/AssetWorkflow.h"
#include "core/Version.h"
#include "Icons.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QSet>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {
namespace {

constexpr auto kProjectsKey = "projectManager/projects";
constexpr auto kSortKey = "projectManager/sort";

struct ProjectEntry {
    QString path;
    bool favorite = false;
    QDateTime lastOpened;
};

QString cleanPath(const QString& path)
{
    const QFileInfo fi(path);
    if (fi.exists()) return QDir::cleanPath(fi.canonicalFilePath());
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QVector<ProjectEntry> loadEntries()
{
    QVector<ProjectEntry> out;
    const QVariantList values = QSettings().value(QString::fromLatin1(kProjectsKey)).toList();
    QHash<QString,int> seen;
    auto preference=[](const ProjectEntry& e){
        const auto meta=ProjectDashboard::inspect(e.path,false);
        int score=meta.exists?10:0;
        if(!meta.rpgMakerProjectRoot.isEmpty()){
            score+=50;
            const QString integrated=cleanPath(QDir(meta.rpgMakerProjectRoot).filePath(QStringLiteral("LUDO/projeto.ludo")));
            if(integrated.compare(e.path,Qt::CaseInsensitive)==0)score+=100;
        }
        return score;
    };
    for (const QVariant& v : values) {
        const QVariantMap m = v.toMap();
        ProjectEntry e;
        e.path = cleanPath(m.value(QStringLiteral("path")).toString());
        e.favorite = m.value(QStringLiteral("favorite")).toBool();
        e.lastOpened = m.value(QStringLiteral("lastOpened")).toDateTime();
        if (e.path.isEmpty()) continue;
        const auto meta=ProjectDashboard::inspect(e.path,false);
        const QString key=meta.projectId.isEmpty()?QStringLiteral("path:")+e.path.toLower():QStringLiteral("id:")+meta.projectId;
        if(!seen.contains(key)){seen.insert(key,out.size());out.push_back(e);continue;}
        ProjectEntry& current=out[seen.value(key)];
        const bool favorite=current.favorite||e.favorite;
        const QDateTime opened=current.lastOpened>=e.lastOpened?current.lastOpened:e.lastOpened;
        if(preference(e)>preference(current))current=e;
        current.favorite=favorite;current.lastOpened=opened;
    }
    return out;
}

void saveEntries(const QVector<ProjectEntry>& entries)
{
    QVariantList values;
    values.reserve(entries.size());
    for (const ProjectEntry& e : entries) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), e.path);
        m.insert(QStringLiteral("favorite"), e.favorite);
        m.insert(QStringLiteral("lastOpened"), e.lastOpened);
        values.push_back(m);
    }
    QSettings().setValue(QString::fromLatin1(kProjectsKey), values);
}

bool copyDirectory(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    const QDir source(sourcePath);
    if (!source.exists()) {
        if (error) *error = QObject::tr("A pasta de origem não existe.");
        return false;
    }
    if (!QDir().mkpath(destinationPath)) {
        if (error) *error = QObject::tr("Não foi possível criar a pasta de destino.");
        return false;
    }
    const QFileInfoList entries = source.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
                                                        QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& fi : entries) {
        const QString dst = QDir(destinationPath).filePath(fi.fileName());
        if (fi.isDir()) {
            if (!copyDirectory(fi.absoluteFilePath(), dst, error)) return false;
        } else {
            if (QFileInfo::exists(dst)) QFile::remove(dst);
            if (!QFile::copy(fi.absoluteFilePath(), dst)) {
                if (error) *error = QObject::tr("Não foi possível copiar %1.").arg(fi.fileName());
                return false;
            }
        }
    }
    return true;
}

bool rewriteProjectIdentity(const QString& projectFile, const QString& newName, bool regenerateId, QString* error)
{
    QFile f(projectFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QObject::tr("Não foi possível abrir o projeto para atualizar o nome.");
        return false;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QObject::tr("O arquivo .ludo não possui JSON válido.");
        return false;
    }
    QJsonObject root = doc.object();
    root.insert(QStringLiteral("projectName"), newName);
    if (regenerateId)
        root.insert(QStringLiteral("projectId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    doc.setObject(root);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QObject::tr("Não foi possível salvar a identidade do projeto.");
        return false;
    }
    if (f.write(doc.toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = QObject::tr("Falha ao escrever o arquivo .ludo.");
        return false;
    }
    return true;
}

QString safeFolderName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\/:*?\"<>|]+")), QStringLiteral("_"));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("MeuMapa");
    return name;
}

} // namespace

ProjectManagerDialog::ProjectManagerDialog(core::Editor& editor, QWidget* parent)
    : QDialog(parent), m_editor(editor)
{
    setWindowTitle(tr("Início — LUDO Map Editor %1").arg(QString::fromLatin1(core::version::Editor)));
    setWindowIcon(QIcon(QStringLiteral(":/resources/ludo-engine-icon.png")));
    resize(1120, 660);
    setMinimumSize(820, 500);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto* titleRow = new QHBoxLayout;
    auto* titleBlock = new QVBoxLayout;
    auto* title = new QLabel(tr("LUDO"), this);
    QFont tf = title->font(); tf.setPointSize(tf.pointSize() + 6); tf.setBold(true); title->setFont(tf);
    auto* subtitle = new QLabel(tr("Seus projetos, o RPG Maker e a equipe em um só lugar."), this);
    subtitle->setProperty("uiRole", QStringLiteral("hint"));
    titleBlock->addWidget(title); titleBlock->addWidget(subtitle);
    titleRow->addLayout(titleBlock, 1);
    auto* newProject = new QPushButton(icons::get(QStringLiteral("new")), tr("Criar projeto"), this);
    auto* addProject = new QPushButton(icons::get(QStringLiteral("open")), tr("Abrir projeto LUDO…"), this);
    auto* openRpgMaker = new QPushButton(QIcon(QStringLiteral(":/ludo/icons/rpg-maker.png")), tr("Abrir RPG Maker…"), this);
    auto* team = new QPushButton(icons::get(QStringLiteral("team-online")), tr("Equipe"), this);
    newProject->setMinimumHeight(36); addProject->setMinimumHeight(36);
    titleRow->addWidget(team);titleRow->addWidget(openRpgMaker);titleRow->addWidget(addProject); titleRow->addWidget(newProject);
    root->addLayout(titleRow);

    auto* filterRow = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar por nome ou caminho…"));
    m_search->setClearButtonEnabled(true);
    m_search->setAccessibleName(tr("Buscar projetos"));
    m_search->setToolTip(tr("Buscar projetos (Ctrl+F)"));
    m_sort = new QComboBox(this);
    m_sort->addItem(tr("Mais recentes"), QStringLiteral("recent"));
    m_sort->addItem(tr("Favoritos primeiro"), QStringLiteral("favorite"));
    m_sort->addItem(tr("Nome A–Z"), QStringLiteral("name"));
    const QString storedSort = QSettings().value(QString::fromLatin1(kSortKey), QStringLiteral("recent")).toString();
    m_sort->setCurrentIndex(qMax(0, m_sort->findData(storedSort)));
    filterRow->addWidget(m_search, 1); filterRow->addWidget(m_sort);
    root->addLayout(filterRow);
    m_summary = new QLabel(this);
    m_summary->setProperty("uiRole", QStringLiteral("hint"));
    root->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({tr("★"), tr("Projeto"), tr("Estado"), tr("Versão"), tr("Última abertura"), tr("Local")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setAccessibleName(tr("Projetos recentes e favoritos"));
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    auto* content = new QSplitter(Qt::Horizontal, this);
    content->setChildrenCollapsible(false);
    content->addWidget(m_table);

    auto* details = new QWidget(content);
    details->setMinimumWidth(310);
    details->setMaximumWidth(440);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(16, 10, 10, 10);
    detailsLayout->setSpacing(9);

    m_detailTitle = new QLabel(tr("Comece por aqui"), details);
    QFont detailTitleFont = m_detailTitle->font();
    detailTitleFont.setPointSize(detailTitleFont.pointSize() + 3);
    detailTitleFont.setBold(true);
    m_detailTitle->setFont(detailTitleFont);
    m_detailTitle->setWordWrap(true);
    detailsLayout->addWidget(m_detailTitle);

    m_detailStatus = new QLabel(details);
    m_detailStatus->setWordWrap(true);
    m_detailStatus->setProperty("uiRole", QStringLiteral("hint"));
    detailsLayout->addWidget(m_detailStatus);

    m_detailMeta = new QLabel(details);
    m_detailMeta->setWordWrap(true);
    m_detailMeta->setProperty("uiRole", QStringLiteral("hint"));
    detailsLayout->addWidget(m_detailMeta);

    m_detailHealth = new QLabel(details);
    m_detailHealth->setWordWrap(true);
    detailsLayout->addWidget(m_detailHealth);

    m_detailRecovery = new QLabel(details);
    m_detailRecovery->setWordWrap(true);
    detailsLayout->addWidget(m_detailRecovery);

    m_detailPath = new QLabel(details);
    m_detailPath->setWordWrap(true);
    m_detailPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailPath->setProperty("uiRole", QStringLiteral("hint"));
    detailsLayout->addWidget(m_detailPath);

    m_onboarding = new QLabel(details);
    m_onboarding->setWordWrap(true);
    m_onboarding->setTextFormat(Qt::RichText);
    detailsLayout->addWidget(m_onboarding);
    detailsLayout->addStretch(1);

    content->addWidget(details);
    content->setStretchFactor(0, 1);
    content->setStretchFactor(1, 0);
    content->setSizes({760, 340});
    root->addWidget(content, 1);

    auto* actions = new QHBoxLayout;
    m_open = new QPushButton(tr("Abrir projeto"), this);
    m_locate = new QPushButton(tr("Localizar novamente…"), this);
    m_favorite = new QPushButton(tr("Favoritar"), this);
    m_duplicate = new QPushButton(tr("Duplicar…"), this);
    m_rename = new QPushButton(tr("Renomear…"), this);
    m_showFolder = new QPushButton(tr("Mostrar na pasta"), this);
    m_remove = new QPushButton(tr("Remover da lista"), this);
    m_delete = new QPushButton(tr("Excluir projeto…"), this);
    actions->addWidget(m_open);
    actions->addWidget(m_locate);
    actions->addWidget(m_favorite);
    actions->addWidget(m_duplicate);
    actions->addWidget(m_rename);
    actions->addWidget(m_showFolder);
    actions->addStretch(1);
    actions->addWidget(m_remove);
    actions->addWidget(m_delete);
    root->addLayout(actions);

    auto* footer = new QHBoxLayout;
    auto* version = new QLabel(tr("LUDO Map Editor %1").arg(QString::fromLatin1(core::version::Editor)), this);
    version->setProperty("uiRole", QStringLiteral("hint"));
    auto* close = new QPushButton(tr("Fechar"), this);
    footer->addWidget(version); footer->addStretch(1); footer->addWidget(close);
    root->addLayout(footer);

    connect(newProject, &QPushButton::clicked, this, &ProjectManagerDialog::createProject);
    connect(addProject, &QPushButton::clicked, this, &ProjectManagerDialog::addExistingProject);
    connect(team,&QPushButton::clicked,this,[this]{m_teamRequested=true;accept();});
    connect(openRpgMaker,&QPushButton::clicked,this,[this]{
        const QString root=QFileDialog::getExistingDirectory(this,tr("Abrir projeto RPG Maker MV ou MZ"));
        if(root.isEmpty())return;m_selectedRpgMakerRoot=root;accept();
    });
    connect(m_open, &QPushButton::clicked, this, &ProjectManagerDialog::openSelected);
    connect(m_locate, &QPushButton::clicked, this, &ProjectManagerDialog::locateMissingProject);
    connect(m_favorite, &QPushButton::clicked, this, &ProjectManagerDialog::toggleFavorite);
    connect(m_duplicate, &QPushButton::clicked, this, &ProjectManagerDialog::duplicateProject);
    connect(m_rename, &QPushButton::clicked, this, &ProjectManagerDialog::renameProject);
    connect(m_showFolder, &QPushButton::clicked, this, &ProjectManagerDialog::showProjectFolder);
    connect(m_remove, &QPushButton::clicked, this, &ProjectManagerDialog::removeFromList);
    connect(m_delete, &QPushButton::clicked, this, &ProjectManagerDialog::deleteProject);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildTable(); });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QString::fromLatin1(kSortKey), m_sort->currentData());
        rebuildTable();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        updateButtons();
        refreshDetails();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openSelected(); });
    connect(m_table, &QTableWidget::itemActivated, this, [this](QTableWidgetItem*) { openSelected(); });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int, int column) {
        if (column == 0) toggleFavorite();
    });
    auto* focusSearch = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(focusSearch, &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });

    rebuildTable();
    refreshDetails();
}

void ProjectManagerDialog::rememberProject(const QString& projectPath, bool touchLastOpened)
{
    if (projectPath.trimmed().isEmpty()) return;
    const QString path = cleanPath(projectPath);
    QVector<ProjectEntry> entries = loadEntries();
    const auto incomingMeta=ProjectDashboard::inspect(path,false);
    bool found = false;
    for (ProjectEntry& e : entries) {
        const auto existingMeta=ProjectDashboard::inspect(e.path,false);
        const bool sameIdentity=!incomingMeta.projectId.isEmpty()&&incomingMeta.projectId==existingMeta.projectId;
        if (sameIdentity||e.path.compare(path, Qt::CaseInsensitive) == 0) {
            // Uma cópia Team antiga não substitui o projeto que já vive dentro
            // da raiz RPG Maker. Caminho é localização; projectId é identidade.
            if(existingMeta.rpgMakerProjectRoot.isEmpty()&&!incomingMeta.rpgMakerProjectRoot.isEmpty())e.path=path;
            else if(!sameIdentity)e.path = path;
            if (touchLastOpened || !e.lastOpened.isValid()) e.lastOpened = QDateTime::currentDateTime();
            found = true;
            break;
        }
    }
    if (!found) entries.push_back({path, false, touchLastOpened ? QDateTime::currentDateTime() : QDateTime()});
    saveEntries(entries);
}

QString ProjectManagerDialog::projectPathForIdentity(const QString& projectId)
{
    if(projectId.isEmpty())return {};
    for(const ProjectEntry& entry:loadEntries()){
        const auto meta=ProjectDashboard::inspect(entry.path,false);
        if(meta.exists&&meta.projectId==projectId)return entry.path;
    }
    return {};
}

QString ProjectManagerDialog::currentProjectPath() const
{
    const auto selected = m_table->selectedItems();
    if (selected.isEmpty()) return {};
    return selected.first()->data(Qt::UserRole).toString();
}

void ProjectManagerDialog::rebuildTable()
{
    const QString selectedBefore = currentProjectPath();
    QVector<ProjectEntry> entries = loadEntries();
    const QString query = m_search->text().trimmed();
    const QString sort = m_sort->currentData().toString();

    auto metaFor = [](const ProjectEntry& e) { return ProjectDashboard::inspect(e.path, false); };
    if (sort == QLatin1String("name")) {
        std::stable_sort(entries.begin(), entries.end(), [&](const ProjectEntry& a, const ProjectEntry& b) {
            return QString::localeAwareCompare(metaFor(a).name, metaFor(b).name) < 0;
        });
    } else if (sort == QLatin1String("favorite")) {
        std::stable_sort(entries.begin(), entries.end(), [&](const ProjectEntry& a, const ProjectEntry& b) {
            if (a.favorite != b.favorite) return a.favorite > b.favorite;
            return a.lastOpened > b.lastOpened;
        });
    } else {
        std::stable_sort(entries.begin(), entries.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
            return a.lastOpened > b.lastOpened;
        });
    }

    m_table->setRowCount(0);
    int selectedRow = -1;
    int shownProjects = 0;
    int shownFavorites = 0;
    for (const ProjectEntry& e : entries) {
        const ProjectDashboardSnapshot meta = ProjectDashboard::inspect(e.path, false);
        if (!query.isEmpty() && !meta.name.contains(query, Qt::CaseInsensitive) &&
            !e.path.contains(query, Qt::CaseInsensitive)) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        ++shownProjects;
        if (e.favorite) ++shownFavorites;
        auto make = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setData(Qt::UserRole, e.path);
            if (!meta.exists) item->setForeground(QBrush(QColor(185, 90, 90)));
            m_table->setItem(row, col, item);
        };
        make(0, e.favorite ? QStringLiteral("★") : QStringLiteral("☆"));
        make(1, meta.name + (meta.exists ? QString() : tr("  [ausente]")));
        QString state = tr("Pronto");
        if (!meta.exists) state = tr("Ausente");
        else if (meta.formatVersion > core::version::ProjectFormat) state = tr("Versão nova");
        else if (meta.recovery.newerThanProject) state = tr("Recovery");
        make(2, state);
        make(3, meta.ludoVersion.isEmpty() ? QStringLiteral("—") : meta.ludoVersion);
        make(4, e.lastOpened.isValid() ? e.lastOpened.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"));
        make(5, QDir::toNativeSeparators(e.path));
        if (e.path.compare(selectedBefore, Qt::CaseInsensitive) == 0) selectedRow = row;
    }
    if (selectedRow >= 0) m_table->selectRow(selectedRow);
    else if (m_table->rowCount() > 0) m_table->selectRow(0);
    if (m_summary)
        m_summary->setText(tr("Projetos visíveis: %1 · favoritos: %2 · Enter abre · clique em ★ para favoritar")
                               .arg(shownProjects).arg(shownFavorites));
    updateButtons();
    refreshDetails();
}

void ProjectManagerDialog::refreshDetails()
{
    if (!m_detailTitle) return;
    const QString path = currentProjectPath();
    if (path.isEmpty()) {
        m_detailTitle->setText(tr("Comece por aqui"));
        m_detailStatus->setText(tr("Crie seu primeiro projeto de mapas ou adicione um projeto existente."));
        m_detailStatus->setProperty("uiRole", QStringLiteral("hint"));
        m_detailMeta->clear();
        m_detailHealth->clear();
        m_detailRecovery->clear();
        m_detailPath->clear();
        m_onboarding->setText(tr(
            "<b>Primeiros passos</b><br>"
            "1. Crie um <b>Novo projeto</b>.<br>"
            "2. Monte o cenário, colisões e prioridades no LUDO.<br>"
            "3. Use o menu <b>RPG Maker</b> para vincular/exportar o mapa.<br>"
            "4. Posicione eventos no RPG Maker usando o panorama de referência."));
        m_detailStatus->style()->unpolish(m_detailStatus);
        m_detailStatus->style()->polish(m_detailStatus);
        return;
    }

    const ProjectDashboardSnapshot snapshot = ProjectDashboard::inspect(path, true);
    m_detailTitle->setText(snapshot.name);
    m_detailPath->setText(QDir::toNativeSeparators(snapshot.path));

    QStringList meta;
    if (!snapshot.ludoVersion.isEmpty()) meta << tr("LUDO %1").arg(snapshot.ludoVersion);
    if (snapshot.formatVersion > 0) meta << tr("Formato %1").arg(snapshot.formatVersion);
    meta << tr("RPG Maker %1").arg(snapshot.targetEngine.toUpper());
    if (snapshot.modified.isValid())
        meta << tr("Editado %1").arg(snapshot.modified.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    m_detailMeta->setText(meta.join(QStringLiteral("  •  ")));

    if (!snapshot.exists) {
        m_detailStatus->setText(tr("Projeto não localizado. Use “Localizar novamente…” para reconectar a pasta."));
        m_detailStatus->setProperty("uiRole", QStringLiteral("warningText"));
        m_detailHealth->clear();
    } else if (!snapshot.healthAvailable) {
        m_detailStatus->setText(tr("Não foi possível verificar a saúde deste projeto."));
        m_detailStatus->setProperty("uiRole", QStringLiteral("warningText"));
        m_detailHealth->setText(snapshot.healthError);
    } else {
        const auto& validation = snapshot.health.validation;
        if (validation.errorCount > 0) {
            m_detailStatus->setText(tr("Atenção: há problemas que podem impedir a edição ou exportação do mapa."));
            m_detailStatus->setProperty("uiRole", QStringLiteral("warningText"));
        } else if (validation.warningCount > 0) {
            m_detailStatus->setText(tr("Projeto utilizável, com avisos para revisar."));
            m_detailStatus->setProperty("uiRole", QStringLiteral("warningText"));
        } else {
            m_detailStatus->setText(tr("Projeto pronto para continuar."));
            m_detailStatus->setProperty("uiRole", QStringLiteral("successText"));
        }
        m_detailHealth->setText(tr("<b>Saúde:</b> Erros: %1 · Avisos: %2 · Informações: %3")
                                    .arg(validation.errorCount)
                                    .arg(validation.warningCount)
                                    .arg(validation.infoCount));
    }
    m_detailStatus->style()->unpolish(m_detailStatus);
    m_detailStatus->style()->polish(m_detailStatus);

    if (snapshot.recovery.newerThanProject) {
        m_detailRecovery->setText(tr("<b>Recuperação disponível:</b> %1 — mais recente que a versão salva. "
                                     "Ao abrir, a LUDO oferecerá as opções de recuperação.")
                                      .arg(snapshot.recovery.recoveryModified.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
    } else {
        m_detailRecovery->setText(tr("<b>Recovery:</b> nenhuma cópia mais recente pendente."));
    }

    m_onboarding->setText(tr(
        "<b>Fluxo recomendado</b><br>"
        "Edite o mapa no LUDO<br>"
        "Exporte para o RPG Maker escolhido<br>"
        "Crie eventos e teste o jogo no RPG Maker"));
}

void ProjectManagerDialog::updateButtons()
{
    const QString path = currentProjectPath();
    const bool has = !path.isEmpty();
    const bool exists = has && QFileInfo::exists(path);
    m_open->setEnabled(exists);
    m_locate->setEnabled(has && !exists);
    m_favorite->setEnabled(has);
    m_duplicate->setEnabled(exists);
    m_rename->setEnabled(exists);
    m_showFolder->setEnabled(exists);
    m_remove->setEnabled(has);
    m_delete->setEnabled(exists);
    if (has) {
        bool favorite = false;
        for (const ProjectEntry& e : loadEntries())
            if (e.path.compare(path, Qt::CaseInsensitive) == 0) { favorite = e.favorite; break; }
        m_favorite->setText(favorite ? tr("Desfavoritar") : tr("Favoritar"));
    } else m_favorite->setText(tr("Favoritar"));
}

void ProjectManagerDialog::openSelected()
{
    const QString path = currentProjectPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    const ProjectDashboardSnapshot meta = ProjectDashboard::inspect(path, false);
    if (meta.formatVersion > core::version::ProjectFormat) {
        QMessageBox::critical(this, tr("Projeto mais novo que o Editor"),
            tr("Este projeto usa ProjectFormat %1, mas esta versão da LUDO suporta até ProjectFormat %2.\n\n"
               "Atualize o LUDO Map Editor antes de abrir este projeto para evitar perda de dados.")
                .arg(meta.formatVersion).arg(core::version::ProjectFormat));
        return;
    }
    const QString currentVersion = QString::fromLatin1(core::version::Editor);
    if (!meta.ludoVersion.isEmpty() && meta.ludoVersion != currentVersion) {
        const auto answer = QMessageBox::information(this, tr("Versão diferente da LUDO"),
            tr("Este projeto foi salvo na LUDO %1 e será aberto na LUDO %2.\n\n"
               "O formato atual continua compatível, mas é recomendado manter um backup do projeto antes de salvar em uma versão diferente.\n\n"
               "Deseja abrir agora?")
                .arg(meta.ludoVersion, currentVersion),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
    }
    // A lista de recentes só é tocada depois que o ProjectOpenWorkflow
    // realmente carregar o projeto com sucesso.
    m_selectedProjectAlreadyLoaded = false;
    m_selectedProjectPath = path;
    accept();
}

void ProjectManagerDialog::createProject()
{
    ProjectCreationWorkflowResult creation;
    if (!runProjectCreationWorkflow(m_editor, this, &creation)) return;

    m_selectedProjectAlreadyLoaded = true;
    m_selectedProjectPath = creation.project.projectPath;
    accept();
}

void ProjectManagerDialog::addExistingProject()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Adicionar projeto LUDO"), QString(),
                                                      tr("Projeto LUDO (*.ludo);;Projeto antigo (*.json);;Todos (*)"));
    if (path.isEmpty()) return;
    rememberProject(path);
    rebuildTable();
}

void ProjectManagerDialog::locateMissingProject()
{
    const QString oldPath = currentProjectPath();
    if (oldPath.isEmpty()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Localizar projeto novamente"), QFileInfo(oldPath).absolutePath(),
                                                      tr("Projeto LUDO (*.ludo);;Projeto antigo (*.json);;Todos (*)"));
    if (path.isEmpty()) return;
    QVector<ProjectEntry> entries = loadEntries();
    for (ProjectEntry& e : entries) {
        if (e.path.compare(oldPath, Qt::CaseInsensitive) == 0) {
            e.path = cleanPath(path);
            e.lastOpened = QDateTime::currentDateTime();
            break;
        }
    }
    saveEntries(entries);
    rebuildTable();
}

void ProjectManagerDialog::toggleFavorite()
{
    const QString path = currentProjectPath();
    if (path.isEmpty()) return;
    QVector<ProjectEntry> entries = loadEntries();
    for (ProjectEntry& e : entries)
        if (e.path.compare(path, Qt::CaseInsensitive) == 0) { e.favorite = !e.favorite; break; }
    saveEntries(entries);
    rebuildTable();
}

void ProjectManagerDialog::duplicateProject()
{
    const QString path = currentProjectPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    const ProjectDashboardSnapshot meta = ProjectDashboard::inspect(path, false);
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Duplicar projeto"), tr("Nome da cópia:"), QLineEdit::Normal,
                                                   meta.name + tr(" - Cópia"), &ok).trimmed();
    if (!ok || newName.isEmpty()) return;
    const QFileInfo projectFile(path);
    const QString sourceRoot = projectFile.absolutePath();
    const QString destinationRoot = QDir::cleanPath(QDir(sourceRoot).absoluteFilePath(QStringLiteral("../") + safeFolderName(newName)));
    if (QFileInfo::exists(destinationRoot)) {
        QMessageBox::warning(this, tr("Duplicar projeto"), tr("Já existe uma pasta com esse nome ao lado do projeto atual."));
        return;
    }
    QString error;
    if (!copyDirectory(sourceRoot, destinationRoot, &error)) {
        QDir(destinationRoot).removeRecursively();
        QMessageBox::warning(this, tr("Duplicar projeto"), error);
        return;
    }
    const QString copiedProject = QDir(destinationRoot).filePath(projectFile.fileName());
    if (!rewriteProjectIdentity(copiedProject, newName, true, &error)) {
        QMessageBox::warning(this, tr("Duplicar projeto"), error);
        return;
    }
    rememberProject(copiedProject, false);
    rebuildTable();
}

void ProjectManagerDialog::renameProject()
{
    const QString path = currentProjectPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    const ProjectDashboardSnapshot meta = ProjectDashboard::inspect(path, false);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renomear projeto"), tr("Novo nome do projeto:"), QLineEdit::Normal,
                                                meta.name, &ok).trimmed();
    if (!ok || name.isEmpty() || name == meta.name) return;
    QString error;
    if (!rewriteProjectIdentity(path, name, false, &error)) {
        QMessageBox::warning(this, tr("Renomear projeto"), error);
        return;
    }
    rebuildTable();
}

void ProjectManagerDialog::showProjectFolder()
{
    const QString path = currentProjectPath();
    if (path.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void ProjectManagerDialog::removeFromList()
{
    const QString path = currentProjectPath();
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remover da lista"),
                              tr("Remover este projeto da lista de recentes? Os arquivos não serão apagados.")) != QMessageBox::Yes) return;
    QVector<ProjectEntry> entries = loadEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ProjectEntry& e) {
        return e.path.compare(path, Qt::CaseInsensitive) == 0;
    }), entries.end());
    saveEntries(entries);
    rebuildTable();
}

void ProjectManagerDialog::deleteProject()
{
    const QString path = currentProjectPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    const ProjectDashboardSnapshot meta = ProjectDashboard::inspect(path, false);
    const QFileInfo projectInfo(path);
    const QString root = projectInfo.absolutePath();
    const QString cleanRoot = QDir::cleanPath(root);
    const QString home = QDir::cleanPath(QDir::homePath());
    const QString filesystemRoot = QDir(cleanRoot).rootPath();
    const bool dedicatedLudoFolder = projectInfo.fileName().compare(QStringLiteral("projeto.ludo"), Qt::CaseInsensitive) == 0
                                     && QDir(root).exists(QStringLiteral("Assets"));
    if (!dedicatedLudoFolder || cleanRoot == home || cleanRoot == QDir::cleanPath(filesystemRoot)) {
        QMessageBox::warning(this, tr("Excluir projeto"),
            tr("A LUDO não pode confirmar que esta pasta contém somente o projeto. Por segurança, use ‘Remover da lista’ e apague os arquivos manualmente se desejar."));
        return;
    }
    const auto answer = QMessageBox::warning(this, tr("Excluir projeto"),
        tr("Esta ação apagará permanentemente a pasta inteira do projeto:\n\n%1\n\nProjeto: %2\n\nDeseja continuar?")
            .arg(QDir::toNativeSeparators(root), meta.name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    if (!QDir(root).removeRecursively()) {
        QMessageBox::warning(this, tr("Excluir projeto"), tr("Não foi possível apagar todos os arquivos do projeto."));
        return;
    }
    QVector<ProjectEntry> entries = loadEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const ProjectEntry& e) {
        return e.path.compare(path, Qt::CaseInsensitive) == 0;
    }), entries.end());
    saveEntries(entries);
    rebuildTable();
}

} // namespace ui
