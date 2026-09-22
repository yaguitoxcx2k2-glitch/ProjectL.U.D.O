#include "AssetBrowser.h"
#include "UniversalAssetPicker.h"

#include "Icons.h"
#include "core/ResourceManager.h"
#include "core/ProjectIO.h"
#include "core/AssetWorkflow.h"
#include "core/AssetWorkflowMetadata.h"
#include "core/AssetImportPipeline.h"
#include "core/ProjectDependencyIndex.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>

namespace ui {

namespace {

QString uniqueDestination(const QString& dir, const QString& fileName)
{
    QFileInfo fi(fileName);
    QString candidate = QDir(dir).filePath(fi.fileName());
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(dir).filePath(QStringLiteral("%1_%2.%3")
                                           .arg(fi.completeBaseName()).arg(n++)
                                           .arg(fi.suffix()));
    }
    return candidate;
}

bool isImage(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("png") || s == QLatin1String("jpg") ||
           s == QLatin1String("jpeg") || s == QLatin1String("bmp") ||
           s == QLatin1String("webp") || s == QLatin1String("gif");
}

struct AssetBrowserSyncResult {
    bool ok = false;
    QString error;
    core::AssetDatabase database;
    QVector<core::AssetPathChange> changes;
};

struct AssetImportResult {
    QStringList copied;
    QVector<core::AssetImportOutcome> outcomes;
    QStringList warnings;
    QStringList errors;
    int requested = 0;
    bool cancelled = false;
};

} // namespace

bool AssetBrowserDialog::ensureProjectFolders(const QString& projectRoot, QString* error)
{
    // Compatibilidade de API: a fonte de verdade agora é o AssetWorkflow do
    // core, para Project Manager, Browser e futuros Pickers usarem o mesmo
    // contrato de pastas.
    return core::AssetWorkflow::ensureProjectFolders(projectRoot, error);
}

AssetBrowserDialog::AssetBrowserDialog(core::Editor& editor, Mode mode,
                                       const QString& initialFolder, QWidget* parent)
    : QDialog(parent), m_ed(editor), m_mode(mode)
{
    setWindowTitle(mode == Mode::Manage ? tr("Imagens do mapa") : tr("Escolher imagem"));
    resize(900, 590);
    auto* outer = new QVBoxLayout(this);

    auto* browserTools = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Buscar imagem…"));
    m_search->setClearButtonEnabled(true);
    m_category = new QComboBox(this);
    m_category->addItem(tr("Todas as imagens"), QString());
    for (const core::AssetCategoryInfo& category : core::AssetWorkflow::categories())
        m_category->addItem(category.displayName, category.id);
    browserTools->addWidget(m_search, 1);
    browserTools->addWidget(m_category);
    outer->addLayout(browserTools);

    auto* hint = new QLabel(
        tr("O LUDO mantém apenas imagens usadas na autoria do mapa: <b>Tilesets</b>, "
           "<b>Autotiles</b> e <b>Referências</b>. Cada imagem recebe um ID estável; "
           "mover ou renomear pelo navegador preserva as referências."), this);
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_model = new QFileSystemModel(split);
    m_model->setReadOnly(false);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_model->setNameFilterDisables(false);
    connect(m_model, &QFileSystemModel::fileRenamed, this,
            [this](const QString& path, const QString& oldName, const QString& newName) {
        const QDir dir(path);
        m_ed.resources().notifyAssetsChanged({
            m_ed.projectRelativePath(dir.filePath(oldName)),
            m_ed.projectRelativePath(dir.filePath(newName))
        });
        // QFileSystemModel conclui o rename antes deste sinal. Reconciliar no
        // próximo ciclo também cobre moves internos por drag/drop.
        QTimer::singleShot(0, this, [this] { synchronizeAssetDatabase(true); });
    });
    // InternalMove pode resultar em inserção/remoção de linhas em vez de um
    // fileRenamed simples. O debounce garante que drag/drop entre pastas também
    // preserve GUIDs sem re-hashear a árvore a cada sinal do QFileSystemModel.
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this] { scheduleAssetDatabaseSync(); });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this] { scheduleAssetDatabaseSync(); });
    m_model->setNameFilters({ QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                              QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
                              QStringLiteral("*.webp"), QStringLiteral("*.gif") });

    m_tree = new QTreeView(split);
    m_tree->setModel(m_model);
    connect(m_model,&QFileSystemModel::directoryLoaded,this,[this](const QString&){
        const auto internal=m_model->index(QDir(m_ed.assetsRoot()).filePath("LUDO/Team"));
        if(internal.isValid())m_tree->setRowHidden(internal.row(),internal.parent(),true);
    });
    m_tree->setSelectionMode(mode == Mode::SelectManyImages
                                 ? QAbstractItemView::ExtendedSelection
                                 : QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    for (int c = 1; c < 4; ++c) m_tree->hideColumn(c);

    auto* right = new QWidget(split);
    auto* rv = new QVBoxLayout(right);
    m_preview = new QLabel(right);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(300, 300);
    m_preview->setProperty("uiRole", QStringLiteral("previewCanvas"));
    rv->addWidget(m_preview, 1);
    m_path = new QLabel(right);
    m_path->setWordWrap(true);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rv->addWidget(m_path);
    m_assetInfo = new QLabel(right);
    m_assetInfo->setWordWrap(true);
    m_assetInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_assetInfo->setProperty("uiRole", QStringLiteral("hint"));
    rv->addWidget(m_assetInfo);

    auto* metadataRow = new QHBoxLayout;
    m_favorite = new QCheckBox(tr("★ Favorito"), right);
    m_tags = new QLineEdit(right);
    m_tags->setPlaceholderText(tr("Tags separadas por vírgula"));
    metadataRow->addWidget(m_favorite);
    metadataRow->addWidget(m_tags, 1);
    rv->addLayout(metadataRow);

    split->addWidget(m_tree);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    auto* actions = new QHBoxLayout;
    auto* folder = new QPushButton(icons::get(QStringLiteral("folder")), tr("Nova pasta…"), this);
    auto* import = new QPushButton(icons::get(QStringLiteral("open")), tr("Adicionar imagem externa…"), this);
    auto* remove = new QPushButton(icons::get(QStringLiteral("remove")), tr("Excluir"), this);
    m_missingAssets = new QPushButton(tr("Localizar ausentes…"), this);
    actions->addWidget(folder);
    actions->addWidget(import);
    m_pixelArt2x = new QCheckBox(tr("Pixel art 16 px → 2x"), this);
    m_pixelArt2x->setToolTip(tr("Ao adicionar uma imagem, amplia o arquivo inteiro em 2x com nearest-neighbor. "
                                  "Se a origem já estiver em Assets/, cria uma cópia sem sobrescrever o original."));
    actions->addWidget(m_pixelArt2x);
    actions->addWidget(remove);
    actions->addWidget(m_missingAssets);
    actions->addStretch(1);
    outer->addLayout(actions);

    auto* box = new QDialogButtonBox(this);
    if (mode == Mode::Manage) box->addButton(QDialogButtonBox::Close);
    else box->setStandardButtons(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
    outer->addWidget(box);

    QString err;
    ensureProjectFolders(m_ed.projectRoot(), &err);
    synchronizeAssetDatabase(false);
    const QString assetRoot = m_ed.assetsRoot();
    const QModelIndex rootIndex = m_model->setRootPath(assetRoot);
    m_tree->setRootIndex(rootIndex);
    const QString resolvedInitialFolder = core::AssetWorkflow::initialFolderForContext(initialFolder);
    QString initial = QDir(assetRoot).filePath(resolvedInitialFolder);
    if (!QFileInfo(initial).isDir()) initial = assetRoot;
    const QModelIndex initialIndex = m_model->index(initial);
    m_tree->setCurrentIndex(initialIndex);
    m_tree->expand(initialIndex);

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this] { refreshPreview(); refreshWorkflowMetadata(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { applyBrowserFilter(); });
    connect(m_category, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyBrowserFilter(); });
    connect(m_favorite, &QCheckBox::toggled, this, [this] { persistWorkflowMetadata(); });
    connect(m_tags, &QLineEdit::editingFinished, this, [this] { persistWorkflowMetadata(); });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        const QString p = m_model->filePath(idx);
        if (QFileInfo(p).isFile() && selectionIsAcceptable() && m_mode != Mode::Manage)
            accept();
    });
    connect(folder, &QPushButton::clicked, this, [this] { createFolder(); });
    connect(import, &QPushButton::clicked, this, [this] { importFiles(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeSelected(); });
    connect(m_missingAssets, &QPushButton::clicked, this, [this] { locateMissingAssets(); });
    connect(box, &QDialogButtonBox::accepted, this, [this] {
        if (m_mode == Mode::Manage) { accept(); return; }
        if (!selectionIsAcceptable()) {
            QMessageBox::information(this, tr("Escolher imagem"),
                                     tr("Selecione uma imagem compatível."));
            return;
        }
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshPreview();
    refreshWorkflowMetadata();
}

void AssetBrowserDialog::applyBrowserFilter()
{
    if (!m_model || !m_tree) return;
    const QString query = m_search ? m_search->text().trimmed() : QString();
    QStringList filters;
    const bool imagesOnly = true;
    if (imagesOnly) {
        const QStringList extensions = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("bmp"), QStringLiteral("webp"), QStringLiteral("gif")};
        for (const QString& ext : extensions)
            filters << (query.isEmpty() ? QStringLiteral("*.%1").arg(ext)
                                        : QStringLiteral("*%1*.%2").arg(query, ext));
    } else if (!query.isEmpty()) {
        filters << QStringLiteral("*%1*").arg(query);
    }
    m_model->setNameFilters(filters);

    const QString categoryId = m_category ? m_category->currentData().toString() : QString();
    QString rootPath = m_ed.assetsRoot();
    if (!categoryId.isEmpty())
        rootPath = QDir(m_ed.projectRoot()).filePath(core::AssetWorkflow::projectFolderForContext(categoryId));
    if (!QFileInfo(rootPath).isDir()) rootPath = m_ed.assetsRoot();
    const QModelIndex rootIndex = m_model->index(rootPath);
    m_tree->setRootIndex(rootIndex);
    if (rootIndex.isValid()) m_tree->expand(rootIndex);
}

void AssetBrowserDialog::refreshWorkflowMetadata()
{
    if (!m_favorite || !m_tags) return;
    const QString path = selectedPath();
    const bool previousFavoriteSignals = m_favorite->blockSignals(true);
    const bool previousTagSignals = m_tags->blockSignals(true);
    bool favorite = false;
    QStringList tags;
    if (!path.isEmpty()) {
        const core::AssetWorkflowMetadata workflow =
            core::assetWorkflowMetadata(m_ed.assetDatabase.metadataForPath(m_ed.projectRelativePath(path)));
        favorite = workflow.favorite;
        tags = workflow.tags;
    }
    m_favorite->setChecked(favorite);
    m_tags->setText(tags.join(QStringLiteral(", ")));
    m_favorite->setEnabled(!path.isEmpty());
    m_tags->setEnabled(!path.isEmpty());
    m_favorite->blockSignals(previousFavoriteSignals);
    m_tags->blockSignals(previousTagSignals);
}

void AssetBrowserDialog::persistWorkflowMetadata()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !m_favorite || !m_tags) return;
    const QString relative = m_ed.projectRelativePath(path);
    QJsonArray tags;
    const QStringList uniqueTags = core::normalizeAssetWorkflowTags(
        m_tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts));
    for (const QString& tag : uniqueTags) tags.append(tag);
    bool changed = false;
    const QJsonObject before = m_ed.assetDatabase.metadataForPath(relative);
    m_ed.assetDatabase.setMetadataValueForPath(relative, QStringLiteral("workflowFavorite"), m_favorite->isChecked());
    m_ed.assetDatabase.setMetadataValueForPath(relative, QStringLiteral("workflowTags"), tags);
    changed = before != m_ed.assetDatabase.metadataForPath(relative);
    if (changed) m_ed.markDirty();
    refreshPreview();
}

QString AssetBrowserDialog::destinationDirectory() const
{
    if (!m_tree || !m_model) return m_ed.assetsRoot();
    const QString p = m_model->filePath(m_tree->currentIndex());
    QFileInfo fi(p);
    if (fi.isDir()) return fi.absoluteFilePath();
    return fi.absolutePath();
}

QStringList AssetBrowserDialog::selectedPaths() const
{
    QStringList out;
    if (!m_tree || !m_model || !m_tree->selectionModel()) return out;
    for (const QModelIndex& idx : m_tree->selectionModel()->selectedRows(0)) {
        const QString p = m_model->filePath(idx);
        if (QFileInfo(p).isFile() && !out.contains(p)) out << p;
    }
    return out;
}

QString AssetBrowserDialog::selectedPath() const
{
    return selectedPaths().value(0);
}

bool AssetBrowserDialog::selectionIsAcceptable() const
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return false;
    if (m_mode == Mode::SelectOneImage || m_mode == Mode::SelectManyImages) {
        for (const QString& p : paths) if (!isImage(p)) return false;
    }
    return m_mode == Mode::SelectManyImages || paths.size() == 1;
}

void AssetBrowserDialog::refreshPreview()
{
    const QString p = selectedPath();
    const quint64 generation = ++m_previewGeneration;
    if (p.isEmpty()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Selecione um arquivo"));
        m_path->setText(destinationDirectory());
        if (m_assetInfo)
            m_assetInfo->setText(tr("Arquivos registrados: %1 • ausentes: %2")
                                     .arg(m_ed.assetDatabase.records().size())
                                     .arg(m_ed.assetDatabase.missingRecords().size()));
        updateAssetDatabaseStatus();
        return;
    }
    const QString relative = m_ed.projectRelativePath(p);
    m_path->setText(relative);
    if (const core::AssetRecord* record = m_ed.assetDatabase.recordByPath(relative)) {
        const int usageCount = m_dependencies.incomingCount(core::ProjectDependencyIndex::assetKey(record->id));
        m_assetInfo->setText(tr("ID: %1\nTipo: %2  •  Categoria: %3  •  SHA-256: %4…\nLocais que usam este arquivo: %5%6")
                                 .arg(record->id, record->type,
                                      record->category.isEmpty() ? QStringLiteral("other") : record->category,
                                      record->sha256.left(12))
                                 .arg(usageCount)
                                 .arg(usageCount == 0 ? tr(" — candidato a limpeza") : QString()));
    } else {
        m_assetInfo->setText(tr("Ainda não registrado no Asset Database."));
    }
    updateAssetDatabaseStatus();

    const QFileInfo fi(p);
    const QString cacheKey = QStringLiteral("%1|%2|300")
                                 .arg(fi.absoluteFilePath())
                                 .arg(fi.lastModified().toMSecsSinceEpoch());
    const auto cached = m_previewCache.constFind(cacheKey);
    if (cached != m_previewCache.constEnd()) {
        m_preview->setText(QString());
        m_preview->setPixmap(QPixmap::fromImage(cached.value()));
        return;
    }

    if (!isImage(p)) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("%1\n%2 KB")
                               .arg(fi.fileName())
                               .arg(fi.size() / 1024));
        return;
    }

    m_preview->setPixmap(QPixmap());
    m_preview->setText(tr("Carregando prévia…"));
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, p, cacheKey, generation] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewGeneration || selectedPath() != p) return;
        if (image.isNull()) {
            m_preview->setPixmap(QPixmap());
            m_preview->setText(tr("%1\n%2 KB")
                                   .arg(QFileInfo(p).fileName())
                                   .arg(QFileInfo(p).size() / 1024));
            return;
        }
        m_previewCache.insert(cacheKey, image);
        m_previewCacheOrder.removeAll(cacheKey);
        m_previewCacheOrder.push_back(cacheKey);
        while (m_previewCacheOrder.size() > 32) {
            const QString oldest = m_previewCacheOrder.takeFirst();
            m_previewCache.remove(oldest);
        }
        m_preview->setText(QString());
        // QPixmap continua sendo criado exclusivamente na UI thread.
        m_preview->setPixmap(QPixmap::fromImage(image));
    });
    watcher->setFuture(QtConcurrent::run([p] {
        QImage image(p);
        if (image.isNull()) return QImage();
        return image.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }));
}

void AssetBrowserDialog::createFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nova pasta"), tr("Nome:"),
                                                QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString created = QDir(destinationDirectory()).filePath(name);
    if (!QDir(destinationDirectory()).mkdir(name)) {
        QMessageBox::warning(this, tr("Nova pasta"), tr("Não foi possível criar a pasta."));
        return;
    }
    m_ed.resources().notifyAssetsChanged({m_ed.projectRelativePath(created)});
}

void AssetBrowserDialog::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Adicionar imagem ao projeto"), QString(),
        tr("Imagens (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;Todos os arquivos (*)"));
    if (files.isEmpty()) return;

    const QString dest = destinationDirectory();
    const QString projectRoot = m_ed.projectRoot();
    const core::AssetImportProfile profile = m_pixelArt2x && m_pixelArt2x->isChecked()
        ? core::AssetImportProfile::pixelArt16To2x()
        : core::AssetImportProfile::native();

    AssetImportResult result;
    QProgressDialog progress(tr("Adicionando imagens…"), tr("Cancelar"), 0, files.size(), this);
    progress.setWindowTitle(tr("Adicionar imagem"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, this, [cancelled] {
        cancelled->store(true, std::memory_order_relaxed);
    });

    const int totalFiles = files.size();
    QFutureWatcher<AssetImportResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<AssetImportResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([files, projectRoot, dest, totalFiles, cancelled, &progress, profile] {
        AssetImportResult imported;
        imported.requested = files.size();
        for (int i = 0; i < files.size(); ++i) {
            if (cancelled->load(std::memory_order_relaxed)) { imported.cancelled = true; break; }
            const core::AssetImportOutcome outcome = core::importAssetIntoProject(
                files.at(i), projectRoot, dest, profile);
            imported.outcomes.push_back(outcome);
            if (outcome.ok()) imported.copied << outcome.outputPath;
            if (!outcome.warning.isEmpty()) imported.warnings << outcome.warning;
            if (!outcome.error.isEmpty()) imported.errors << outcome.error;
            QMetaObject::invokeMethod(&progress, [&progress, i, totalFiles] {
                progress.setValue(i + 1);
                progress.setLabelText(QObject::tr("Adicionando imagens… %1/%2").arg(i + 1).arg(totalFiles));
            }, Qt::QueuedConnection);
        }
        return imported;
    }));
    progress.show();
    if (!watcher.isFinished()) loop.exec();
    progress.close();
    result = watcher.result();

    if ((!result.errors.isEmpty() || result.copied.size() != result.requested) && !result.cancelled)
        QMessageBox::warning(this, tr("Adicionar imagem"),
                             tr("Arquivos preparados: %1 de %2.%3")
                                 .arg(result.copied.size()).arg(result.requested)
                                 .arg(result.errors.isEmpty() ? QString()
                                      : QStringLiteral("\n\n") + result.errors.mid(0, 8).join(QLatin1Char('\n'))));

    const QStringList available = result.copied;
    if (!available.isEmpty()) {
        QStringList relative;
        for (const QString& path : available) relative << m_ed.projectRelativePath(path);
        m_ed.resources().notifyAssetsChanged(relative);
        synchronizeAssetDatabase(true);
        bool metadataChanged = false;
        int transformedCount = 0;
        QStringList transformedSummary;
        for (const core::AssetImportOutcome& outcome : result.outcomes) {
            if (!outcome.ok() || !outcome.transformed) continue;
            ++transformedCount;
            const QString rel = m_ed.projectRelativePath(outcome.outputPath);
            const QJsonObject beforeMeta = m_ed.assetDatabase.metadataForPath(rel);
            if (m_ed.assetDatabase.setMetadataValueForPath(
                    rel, QStringLiteral("assetImport"),
                    core::assetImportMetadata(profile, outcome)) &&
                beforeMeta != m_ed.assetDatabase.metadataForPath(rel))
                metadataChanged = true;
            if (outcome.sourceSize.isValid() && outcome.outputSize.isValid())
                transformedSummary << tr("%1: %2×%3 → %4×%5")
                    .arg(QFileInfo(outcome.outputPath).fileName())
                    .arg(outcome.sourceSize.width()).arg(outcome.sourceSize.height())
                    .arg(outcome.outputSize.width()).arg(outcome.outputSize.height());
        }
        if (metadataChanged) {
            m_ed.projectDirty = true;
            emit m_ed.projectChanged();
        }
        if (!result.warnings.isEmpty())
            QMessageBox::information(this, tr("Importação de imagens"), result.warnings.mid(0, 8).join(QLatin1Char('\n')));
        else if (transformedCount > 0)
            QMessageBox::information(this, tr("Importação 2x"),
                                     tr("Imagens ampliadas com vizinho mais próximo: %1.\n\n%2")
                                         .arg(transformedCount)
                                         .arg(transformedSummary.mid(0, 8).join(QLatin1Char('\n'))));
        m_tree->setCurrentIndex(m_model->index(available.first()));
    }
}

void AssetBrowserDialog::removeSelected()
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    const QString p = m_model->filePath(idx);
    if (QFileInfo(p).absoluteFilePath() == QFileInfo(m_ed.assetsRoot()).absoluteFilePath()) return;
    const QString relative = m_ed.projectRelativePath(p);
    int usageCount = 0;
    if (const core::AssetRecord* record = m_ed.assetDatabase.recordByPath(relative)) {
        usageCount = m_dependencies.incomingCount(core::ProjectDependencyIndex::assetKey(record->id));
    }
    const QString question = usageCount > 0
        ? tr("Remover “%1” do projeto?\n\nUsos encontrados: %2. Se você remover este arquivo, essas referências deixarão de funcionar.")
              .arg(QFileInfo(p).fileName()).arg(usageCount)
        : tr("Excluir “%1” do projeto?\n\nNenhuma referência ativa foi encontrada pelo grafo de dependências.")
              .arg(QFileInfo(p).fileName());
    if (QMessageBox::question(this, tr("Excluir imagem"), question) != QMessageBox::Yes) return;
    if (!m_model->remove(idx)) {
        QMessageBox::warning(this, tr("Excluir imagem"), tr("Não foi possível excluir o item."));
        return;
    }
    m_ed.resources().notifyAssetsChanged({relative});
    QTimer::singleShot(0, this, [this] { synchronizeAssetDatabase(true); });
}

void AssetBrowserDialog::synchronizeAssetDatabase(bool showErrors)
{
    if (m_ed.projectRoot().isEmpty()) return;
    if (m_assetSyncRunning) { m_assetSyncAgain = true; return; }
    m_assetSyncRunning = true;

    const QByteArray before = QJsonDocument(m_ed.assetDatabase.toJson()).toJson(QJsonDocument::Compact);
    const QString projectRoot = m_ed.projectRoot();
    const core::AssetDatabase databaseCopy = m_ed.assetDatabase;

    QFutureWatcher<AssetBrowserSyncResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<AssetBrowserSyncResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([databaseCopy, projectRoot]() mutable {
        AssetBrowserSyncResult result;
        result.database = databaseCopy;
        result.ok = result.database.synchronize(projectRoot, &result.error);
        if (result.ok) result.changes = result.database.takePathChanges();
        return result;
    }));
    if (!watcher.isFinished()) loop.exec();
    const AssetBrowserSyncResult result = watcher.result();
    m_assetSyncRunning = false;

    if (!result.ok) {
        if (showErrors)
            QMessageBox::warning(this, tr("Asset Database"),
                                 tr("Não foi possível atualizar o banco de assets:\n%1").arg(result.error));
    } else {
        m_ed.assetDatabase = result.database;
        for (const core::AssetPathChange& change : result.changes)
            core::io::rewriteEditorAssetPath(m_ed, change.oldPath, change.newPath);
        const QByteArray after = QJsonDocument(m_ed.assetDatabase.toJson()).toJson(QJsonDocument::Compact);
        if (before != after) {
            m_ed.projectDirty = true;
            emit m_ed.projectChanged();
        }
        refreshDependencySnapshot();
        updateAssetDatabaseStatus();
        refreshPreview();
    }

    if (m_assetSyncAgain) {
        m_assetSyncAgain = false;
        QTimer::singleShot(0, this, [this] { synchronizeAssetDatabase(false); });
    }
}

void AssetBrowserDialog::scheduleAssetDatabaseSync()
{
    if (m_assetSyncPending) return;
    m_assetSyncPending = true;
    QTimer::singleShot(80, this, [this] {
        m_assetSyncPending = false;
        synchronizeAssetDatabase(false);
    });
}

void AssetBrowserDialog::refreshDependencySnapshot()
{
    m_dependencies = core::ProjectDependencyIndex::build(m_ed);
}

void AssetBrowserDialog::updateAssetDatabaseStatus()
{
    if (!m_missingAssets) return;
    const int missing = m_ed.assetDatabase.missingRecords().size();
    m_missingAssets->setText(missing > 0
        ? tr("Localizar ausentes… (%1)").arg(missing)
        : tr("Localizar ausentes…"));
    m_missingAssets->setEnabled(missing > 0);
}

void AssetBrowserDialog::locateMissingAssets()
{
    const QVector<core::AssetRecord> missing = m_ed.assetDatabase.missingRecords();
    if (missing.isEmpty()) {
        QMessageBox::information(this, tr("Imagens ausentes"), tr("Nenhuma imagem registrada está ausente."));
        return;
    }

    int repaired = 0;
    for (const core::AssetRecord& record : missing) {
        const auto answer = QMessageBox::question(
            this, tr("Localizar imagem ausente"),
            tr("A imagem abaixo não foi encontrada:\n\n%1\nID: %2\n\nDeseja localizar este arquivo agora?")
                .arg(record.path, record.id),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Cancel) break;
        if (answer != QMessageBox::Yes) continue;

        const QString chosen = QFileDialog::getOpenFileName(
            this, tr("Localizar %1").arg(QFileInfo(record.path).fileName()), QString(),
            tr("Imagens (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
        if (chosen.isEmpty()) continue;

        QString projectRelative = m_ed.projectRelativePath(chosen);
        if (!projectRelative.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive)) {
            const QString recoveredDir = QDir(m_ed.assetsRoot()).filePath(QStringLiteral("Recovered"));
            QDir().mkpath(recoveredDir);
            const QString target = uniqueDestination(recoveredDir, QFileInfo(chosen).fileName());
            if (!QFile::copy(chosen, target)) {
                QMessageBox::warning(this, tr("Localizar imagem"),
                                     tr("Não foi possível copiar o arquivo para Assets/Recovered."));
                continue;
            }
            projectRelative = m_ed.projectRelativePath(target);
        }

        QString error;
        if (!m_ed.assetDatabase.rebind(record.id, projectRelative, m_ed.projectRoot(), &error)) {
            QMessageBox::warning(this, tr("Localizar imagem"), error);
            continue;
        }
        const QVector<core::AssetPathChange> changes = m_ed.assetDatabase.takePathChanges();
        for (const core::AssetPathChange& change : changes)
            core::io::rewriteEditorAssetPath(m_ed, change.oldPath, change.newPath);
        ++repaired;
    }

    if (repaired > 0) {
        m_ed.projectDirty = true;
        m_ed.resources().notifyAssetsChanged();
        emit m_ed.projectChanged();
        QMessageBox::information(this, tr("Imagens reparadas"),
                                 tr("Arquivos reassociados mantendo o mesmo ID: %1.").arg(repaired));
    }
    synchronizeAssetDatabase(true);
}

QString AssetBrowserDialog::chooseImage(core::Editor& ed, QWidget* parent,
                                        const QString& initialFolder)
{
    return UniversalAssetPickerDialog::chooseOne(
        ed, parent, initialFolder, QStringLiteral("image"), QObject::tr("Selecionar imagem"));
}


QStringList AssetBrowserDialog::chooseImages(core::Editor& ed, QWidget* parent,
                                             const QString& initialFolder)
{
    return UniversalAssetPickerDialog::chooseMany(
        ed, parent, initialFolder, QStringLiteral("image"), QObject::tr("Selecionar imagens"));
}


void AssetBrowserDialog::manage(core::Editor& ed, QWidget* parent)
{
    if (ed.projectRoot().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Assets"),
                                 QObject::tr("Crie ou salve o projeto em uma pasta primeiro."));
        return;
    }
    AssetBrowserDialog(ed, Mode::Manage, QString(), parent).exec();
}

} // namespace ui
