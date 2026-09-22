#include "UniversalAssetPicker.h"

#include "Icons.h"
#include "core/AssetDatabase.h"
#include "core/AssetWorkflow.h"
#include "core/AssetImportPipeline.h"
#include "core/ProjectIO.h"
#include "core/ResourceManager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <memory>

namespace ui {
namespace {

constexpr int PathRole = Qt::UserRole + 1;
constexpr int AssetIdRole = Qt::UserRole + 2;
constexpr int FolderRole = Qt::UserRole + 3;
constexpr int CategoryRole = Qt::UserRole + 4;

bool isImagePath(const QString& path)
{
    return core::AssetDatabase::typeForPath(path) == QLatin1String("image");
}

struct PickerSyncResult {
    bool ok = false;
    QString error;
    core::AssetDatabase database;
    QVector<core::AssetPathChange> changes;
};

struct PickerImportResult {
    QStringList copied;
    QVector<core::AssetImportOutcome> outcomes;
    QStringList warnings;
    QStringList errors;
    int requested = 0;
    bool cancelled = false;
};

QString normalizedAbsolute(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

const core::AssetCategoryInfo* findCategory(const QVector<core::AssetCategoryInfo>& categories,
                                            const QString& id)
{
    for (const core::AssetCategoryInfo& category : categories)
        if (category.id.compare(id, Qt::CaseInsensitive) == 0) return &category;
    return nullptr;
}

QString relativeInsideCategory(const core::AssetCategoryInfo& category, const QString& projectRelative)
{
    const QString normalized = core::AssetDatabase::normalizePath(projectRelative);
    QStringList prefixes{category.relativeFolder};
    prefixes.append(category.aliases);
    for (QString prefix : prefixes) {
        prefix = core::AssetDatabase::normalizePath(prefix);
        if (normalized.compare(prefix, Qt::CaseInsensitive) == 0) return QString();
        if (normalized.startsWith(prefix + QLatin1Char('/'), Qt::CaseInsensitive))
            return normalized.mid(prefix.size() + 1);
    }
    return QFileInfo(normalized).fileName();
}

QTreeWidgetItem* ensureChildFolder(QTreeWidgetItem* parent, const QString& name,
                                   const QString& absolutePath, const QString& categoryId,
                                   QTreeWidget* tree)
{
    const int childCount = parent ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* item = parent ? parent->child(i) : tree->topLevelItem(i);
        if (item->data(0, FolderRole).toBool() &&
            item->text(0).compare(name, Qt::CaseInsensitive) == 0) {
            if (item->data(0, PathRole).toString().isEmpty() && !absolutePath.isEmpty())
                item->setData(0, PathRole, absolutePath);
            return item;
        }
    }
    auto* item = new QTreeWidgetItem;
    item->setText(0, name);
    item->setIcon(0, tree->style()->standardIcon(QStyle::SP_DirIcon));
    item->setData(0, FolderRole, true);
    item->setData(0, PathRole, absolutePath);
    item->setData(0, CategoryRole, categoryId);
    if (parent) parent->addChild(item);
    else tree->addTopLevelItem(item);
    return item;
}

QTreeWidgetItem* ensureFolderChain(QTreeWidget* tree, QTreeWidgetItem* root,
                                   const QString& relativeFolder,
                                   const QString& absoluteBase,
                                   const QString& categoryId)
{
    QTreeWidgetItem* parent = root;
    QString accumulated;
    const QStringList parts = QDir::fromNativeSeparators(relativeFolder)
                                  .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        accumulated = accumulated.isEmpty() ? part : accumulated + QLatin1Char('/') + part;
        const QString absolute = absoluteBase.isEmpty() ? QString() : QDir(absoluteBase).filePath(accumulated);
        parent = ensureChildFolder(parent, part, absolute, categoryId, tree);
    }
    return parent ? parent : root;
}

} // namespace

UniversalAssetPickerDialog::UniversalAssetPickerDialog(core::Editor& editor,
                                                       const Request& request,
                                                       QWidget* parent)
    : QDialog(parent), m_ed(editor), m_request(request),
      m_scope(core::AssetPickerCatalog::scopeForContext(request.context, request.requiredMediaType))
{
    setWindowTitle(request.title.isEmpty() ? tr("Selecionar imagem") : request.title);
    resize(980, 650);
    setMinimumSize(760, 500);

    auto* outer = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Categoria:"), this));
    m_category = new QComboBox(this);
    m_category->setMinimumWidth(190);
    m_category->addItem(core::AssetPickerCatalog::aggregateLabel(m_scope.mediaType), QString());
    for (const core::AssetCategoryInfo& category : m_scope.categories)
        m_category->addItem(category.displayName, category.id);
    top->addWidget(m_category);
    top->addSpacing(12);
    top->addWidget(new QLabel(tr("Pesquisar:"), this));
    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Nome, pasta ou categoria…"));
    top->addWidget(m_search, 1);
    outer->addLayout(top);

    const int initialIndex = m_category->findData(m_scope.initialCategoryId);
    if (initialIndex >= 0) m_category->setCurrentIndex(initialIndex);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_tree = new QTreeWidget(split);
    m_tree->setHeaderLabel(tr("Imagens do projeto"));
    m_tree->setSelectionMode(request.multiple ? QAbstractItemView::ExtendedSelection
                                              : QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);

    auto* right = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(right);
    auto* previewToolbar = new QHBoxLayout;
    previewToolbar->addWidget(new QLabel(tr("Prévia"), right));
    previewToolbar->addStretch(1);
    previewToolbar->addWidget(new QLabel(tr("Zoom:"), right));
    m_zoom = new QComboBox(right);
    m_zoom->addItem(tr("Ajustar"), 0);
    m_zoom->addItem(QStringLiteral("50%"), 50);
    m_zoom->addItem(QStringLiteral("100%"), 100);
    m_zoom->addItem(QStringLiteral("200%"), 200);
    previewToolbar->addWidget(m_zoom);
    rightLayout->addLayout(previewToolbar);

    m_previewScroll = new QScrollArea(right);
    m_previewScroll->setWidgetResizable(false);
    m_preview = new QLabel(m_previewScroll);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(340, 340);
    m_preview->setText(tr("Selecione uma imagem"));
    m_previewScroll->setWidget(m_preview);
    rightLayout->addWidget(m_previewScroll, 1);

    m_path = new QLabel(right);
    m_path->setWordWrap(true);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_path);
    m_info = new QLabel(right);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_info);

    split->addWidget(m_tree);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    outer->addWidget(split, 1);

    auto* actions = new QHBoxLayout;
    m_external = new QPushButton(icons::get(QStringLiteral("open")), tr("Adicionar imagem externa…"), this);
    actions->addWidget(m_external);
    m_pixelArt2x = new QCheckBox(tr("Pixel art 16 px → 2x"), this);
    m_pixelArt2x->setToolTip(tr("Amplia a imagem em 2x com nearest-neighbor durante a importação, sem sobrescrever o original."));
    actions->addWidget(m_pixelArt2x);
    auto* refresh = new QPushButton(tr("Atualizar lista"), this);
    actions->addWidget(refresh);
    actions->addStretch(1);
    outer->addLayout(actions);

    auto* buttons = new QDialogButtonBox(this);
    auto* use = buttons->addButton(request.multiple ? tr("Usar imagens") : tr("Usar imagem"),
                                   QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    outer->addWidget(buttons);

    QString folderError;
    if (!core::AssetWorkflow::ensureProjectFolders(m_ed.projectRoot(), &folderError) && !folderError.isEmpty())
        QMessageBox::warning(this, tr("Assets"), folderError);
    synchronizeAssetDatabase(false);
    rebuildTree();
    if (!m_request.initialPath.trimmed().isEmpty()) {
        const QFileInfo initialInfo(m_request.initialPath);
        const QString initialAbsolute = initialInfo.isAbsolute()
            ? initialInfo.absoluteFilePath()
            : QDir(m_ed.projectRoot()).filePath(m_request.initialPath);
        selectAbsolutePath(initialAbsolute);
        refreshPreview();
    }

    connect(m_category, &QComboBox::currentIndexChanged, this, [this] { rebuildTree(); });
    connect(m_search, &QLineEdit::textChanged, this, [this] {
        QTimer::singleShot(120, this, [this] { rebuildTree(); });
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { refreshPreview(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!item || item->data(0, FolderRole).toBool()) return;
        if (selectionIsAcceptable()) accept();
    });
    connect(m_zoom, &QComboBox::currentIndexChanged, this, [this] { applyImageZoom(); });
    connect(m_external, &QPushButton::clicked, this, [this] { addExternalFiles(); });
    connect(refresh, &QPushButton::clicked, this, [this] {
        synchronizeAssetDatabase(true);
        rebuildTree();
    });
    connect(use, &QPushButton::clicked, this, [this] {
        if (!selectionIsAcceptable()) {
            QMessageBox::information(this, tr("Selecionar imagem"), tr("Selecione uma imagem compatível."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void UniversalAssetPickerDialog::synchronizeAssetDatabase(bool showErrors)
{
    if (m_ed.projectRoot().isEmpty()) return;
    const QString projectRoot = m_ed.projectRoot();
    const core::AssetDatabase databaseCopy = m_ed.assetDatabase;
    const QByteArray before = QJsonDocument(m_ed.assetDatabase.toJson()).toJson(QJsonDocument::Compact);

    QFutureWatcher<PickerSyncResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<PickerSyncResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([databaseCopy, projectRoot]() mutable {
        PickerSyncResult result;
        result.database = databaseCopy;
        result.ok = result.database.synchronize(projectRoot, &result.error);
        if (result.ok) result.changes = result.database.takePathChanges();
        return result;
    }));
    if (!watcher.isFinished()) loop.exec();
    const PickerSyncResult result = watcher.result();
    if (!result.ok) {
        if (showErrors)
            QMessageBox::warning(this, tr("Asset Database"),
                                 tr("Não foi possível atualizar os assets:\n%1").arg(result.error));
        return;
    }

    m_ed.assetDatabase = result.database;
    bool rewrote = false;
    for (const core::AssetPathChange& change : result.changes) {
        core::io::rewriteEditorAssetPath(m_ed, change.oldPath, change.newPath);
        rewrote = true;
    }
    const QByteArray after = QJsonDocument(m_ed.assetDatabase.toJson()).toJson(QJsonDocument::Compact);
    if (rewrote || before != after) {
        m_ed.projectDirty = true;
        emit m_ed.projectChanged();
    }
}

QString UniversalAssetPickerDialog::currentCategoryId() const
{
    return m_category ? m_category->currentData().toString() : QString();
}

void UniversalAssetPickerDialog::rebuildTree()
{
    if (!m_tree) return;
    const QString selectedBefore = selectedPath();
    m_tree->clear();

    const QString categoryId = currentCategoryId();
    const QString search = m_search ? m_search->text() : QString();
    const QVector<core::AssetRecord> records = core::AssetPickerCatalog::query(
        m_ed.assetDatabase, categoryId, m_scope.mediaType, search, false);

    auto categoryInfoFor = [this](const QString& id) -> const core::AssetCategoryInfo* {
        return findCategory(m_scope.categories, id);
    };

    QHash<QString, QTreeWidgetItem*> categoryRoots;
    if (categoryId.isEmpty()) {
        for (const core::AssetCategoryInfo& category : m_scope.categories) {
            auto* root = ensureChildFolder(nullptr, category.displayName,
                                           QDir(m_ed.projectRoot()).filePath(category.relativeFolder),
                                           category.id, m_tree);
            categoryRoots.insert(category.id.toLower(), root);
        }
    }

    // Pastas físicas canônicas/subpastas aparecem mesmo vazias quando uma
    // categoria específica está selecionada.
    if (!categoryId.isEmpty() && search.trimmed().isEmpty()) {
        if (const core::AssetCategoryInfo* category = categoryInfoFor(categoryId)) {
            QStringList roots{category->relativeFolder};
            roots.append(category->aliases);
            for (const QString& relativeRoot : roots) {
                const QString absoluteRoot = QDir(m_ed.projectRoot()).filePath(relativeRoot);
                if (!QFileInfo(absoluteRoot).isDir()) continue;
                QDirIterator it(absoluteRoot, QDir::Dirs | QDir::NoDotAndDotDot,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString absoluteDir = it.next();
                    const QString rel = QDir(absoluteRoot).relativeFilePath(absoluteDir);
                    ensureFolderChain(m_tree, nullptr, rel, absoluteRoot, category->id);
                }
            }
        }
    }

    for (const core::AssetRecord& record : records) {
        const core::AssetCategoryInfo* category = categoryInfoFor(record.category);
        QTreeWidgetItem* root = nullptr;
        QString relative = record.path;
        QString absoluteBase;
        if (category) {
            relative = relativeInsideCategory(*category, record.path);
            absoluteBase = QDir(m_ed.projectRoot()).filePath(category->relativeFolder);
            if (categoryId.isEmpty()) root = categoryRoots.value(category->id.toLower(), nullptr);
        }

        const QString folderPart = QFileInfo(relative).path() == QLatin1String(".")
                                       ? QString() : QFileInfo(relative).path();
        QTreeWidgetItem* parent = ensureFolderChain(m_tree, root, folderPart, absoluteBase,
                                                    record.category);
        auto* item = new QTreeWidgetItem;
        item->setText(0, QFileInfo(record.path).fileName());
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setData(0, FolderRole, false);
        item->setData(0, AssetIdRole, record.id);
        item->setData(0, CategoryRole, record.category);
        item->setData(0, PathRole, QDir(m_ed.projectRoot()).filePath(record.path));
        if (parent) parent->addChild(item);
        else m_tree->addTopLevelItem(item);
    }

    m_tree->sortItems(0, Qt::AscendingOrder);
    if (!search.trimmed().isEmpty()) m_tree->expandAll();
    else {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            m_tree->topLevelItem(i)->setExpanded(true);
    }

    if (!selectedBefore.isEmpty()) selectAbsolutePath(selectedBefore);
    if (m_tree->selectedItems().isEmpty()) {
        QTreeWidgetItemIterator it(m_tree);
        while (*it) {
            if (!(*it)->data(0, FolderRole).toBool()) {
                m_tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }
    refreshPreview();

    // Em visão agregada o destino de importação não é determinístico; o
    // usuário escolhe a categoria no DropDown antes de adicionar externamente.
    m_external->setEnabled(!currentCategoryId().isEmpty());
    m_external->setToolTip(currentCategoryId().isEmpty()
                               ? tr("Escolha uma categoria antes de adicionar um arquivo externo.")
                               : QString());
}

QStringList UniversalAssetPickerDialog::selectedPaths() const
{
    QStringList paths;
    if (!m_tree) return paths;
    for (QTreeWidgetItem* item : m_tree->selectedItems()) {
        if (!item || item->data(0, FolderRole).toBool()) continue;
        const QString path = item->data(0, PathRole).toString();
        if (!path.isEmpty() && !paths.contains(path)) paths.push_back(path);
    }
    return paths;
}

QString UniversalAssetPickerDialog::selectedPath() const
{
    return selectedPaths().value(0);
}

bool UniversalAssetPickerDialog::selectionIsAcceptable() const
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return false;
    if (!m_request.multiple && paths.size() != 1) return false;
    for (const QString& path : paths) {
        if (!m_request.requiredMediaType.trimmed().isEmpty() &&
            core::AssetDatabase::typeForPath(path).compare(m_request.requiredMediaType, Qt::CaseInsensitive) != 0)
            return false;
    }
    return true;
}

void UniversalAssetPickerDialog::refreshPreview()
{
    const quint64 generation = ++m_previewGeneration;
    m_previewImage = QImage();
    m_previewImagePath.clear();
    m_zoom->setEnabled(false);

    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (selected.isEmpty()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Selecione um arquivo"));
        m_path->clear();
        m_info->setText(tr("Arquivos registrados no projeto: %1.").arg(m_ed.assetDatabase.records().size()));
        return;
    }

    QTreeWidgetItem* item = selected.first();
    if (item->data(0, FolderRole).toBool()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Pasta"));
        m_path->setText(item->data(0, PathRole).toString());
        m_info->setText(tr("Selecione um arquivo ou use esta pasta como destino para um nova imagem externa."));
        return;
    }

    const QString path = item->data(0, PathRole).toString();
    const QString assetId = item->data(0, AssetIdRole).toString();
    const core::AssetRecord* record = m_ed.assetDatabase.recordById(assetId);
    const QFileInfo fi(path);
    m_path->setText(m_ed.projectRelativePath(path));
    if (record) {
        m_info->setText(tr("ID: %1\nCategoria: %2 • Tipo: %3 • %4 KB")
                            .arg(record->id, record->category, record->type)
                            .arg(record->size / 1024));
    } else {
        m_info->setText(tr("%1 KB").arg(fi.size() / 1024));
    }


    if (!isImagePath(path)) {
        m_preview->setPixmap(QPixmap());
        const QString kind = record ? record->type : core::AssetDatabase::typeForPath(path);
        m_preview->setText(tr("%1\n%2").arg(fi.fileName(), kind.toUpper()));
        return;
    }

    m_preview->setPixmap(QPixmap());
    m_preview->setText(tr("Carregando prévia…"));
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, path, generation] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewGeneration || selectedPath() != path) return;
        if (image.isNull()) {
            m_preview->setText(tr("Não foi possível visualizar esta imagem."));
            return;
        }
        m_previewImage = image;
        m_previewImagePath = path;
        m_zoom->setEnabled(true);
        const QString currentInfo = m_info->text();
        m_info->setText(currentInfo + tr("\nImagem: %1 × %2 px").arg(image.width()).arg(image.height()));
        applyImageZoom();
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QImageReader reader(path);
        const QSize original = reader.size();
        if (original.isValid() && (original.width() > 2048 || original.height() > 2048))
            reader.setScaledSize(original.scaled(2048, 2048, Qt::KeepAspectRatio));
        return reader.read();
    }));
}

void UniversalAssetPickerDialog::applyImageZoom()
{
    if (m_previewImage.isNull() || m_previewImagePath != selectedPath()) return;
    const int percent = m_zoom->currentData().toInt();
    QSize target;
    if (percent <= 0) {
        QSize available = m_previewScroll->viewport()->size() - QSize(20, 20);
        if (available.width() < 32 || available.height() < 32) available = QSize(340, 340);
        target = m_previewImage.size().scaled(available, Qt::KeepAspectRatio);
    } else {
        target = QSize(qMax(1, m_previewImage.width() * percent / 100),
                       qMax(1, m_previewImage.height() * percent / 100));
    }
    m_preview->setPixmap(QPixmap::fromImage(m_previewImage.scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_preview->resize(target);
}

QString UniversalAssetPickerDialog::currentDestinationDirectory() const
{
    const QString categoryId = currentCategoryId();
    const core::AssetCategoryInfo* category = findCategory(m_scope.categories, categoryId);
    if (!category) return QString();

    const QString canonicalRoot = QDir::cleanPath(QDir(m_ed.projectRoot()).filePath(category->relativeFolder));
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (!selected.isEmpty()) {
        QTreeWidgetItem* item = selected.first();
        QString candidate = item->data(0, PathRole).toString();
        if (!candidate.isEmpty()) {
            if (!item->data(0, FolderRole).toBool()) candidate = QFileInfo(candidate).absolutePath();
            candidate = QDir::cleanPath(candidate);
#ifdef Q_OS_WIN
            const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
            const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
            if (candidate.compare(canonicalRoot, cs) == 0 ||
                candidate.startsWith(canonicalRoot + QDir::separator(), cs))
                return candidate;
        }
    }
    return canonicalRoot;
}

QString UniversalAssetPickerDialog::imageFileDialogFilter() const
{
    return tr("Imagens (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;Todos os arquivos (*)");
}

void UniversalAssetPickerDialog::addExternalFiles()
{
    const QString destination = currentDestinationDirectory();
    if (destination.isEmpty()) {
        QMessageBox::information(this, tr("Adicionar imagem"),
                                 tr("Escolha primeiro uma categoria no menu."));
        return;
    }
    QDir().mkpath(destination);

    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Adicionar imagem ao projeto"), QString(), imageFileDialogFilter());
    if (files.isEmpty()) return;

    QStringList candidates;
    QStringList incompatible;
    for (const QString& file : files) {
        const QString fileType = core::AssetDatabase::typeForPath(file);
        if (fileType.compare(QStringLiteral("image"), Qt::CaseInsensitive) != 0) {
            incompatible << QFileInfo(file).fileName();
            continue;
        }
        candidates << file;
    }
    if (!incompatible.isEmpty()) {
        QMessageBox::information(this, tr("Adicionar imagem"),
                                 tr("Alguns arquivos não pertencem ao formato de imagem esperado e foram ignorados:\n%1")
                                     .arg(incompatible.join(QStringLiteral("\n"))));
    }
    if (candidates.isEmpty()) return;

    const QString projectRoot = m_ed.projectRoot();
    const core::AssetImportProfile profile = m_pixelArt2x && m_pixelArt2x->isChecked()
        ? core::AssetImportProfile::pixelArt16To2x()
        : core::AssetImportProfile::native();

    PickerImportResult result;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress(tr("Adicionando imagens…"), tr("Cancelar"), 0, candidates.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(150);
    connect(&progress, &QProgressDialog::canceled, this, [cancelled] {
        cancelled->store(true, std::memory_order_relaxed);
    });

    QFutureWatcher<PickerImportResult> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<PickerImportResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([candidates, projectRoot, destination, cancelled, &progress, profile] {
        PickerImportResult imported;
        imported.requested = candidates.size();
        for (int i = 0; i < candidates.size(); ++i) {
            if (cancelled->load(std::memory_order_relaxed)) { imported.cancelled = true; break; }
            const core::AssetImportOutcome outcome = core::importAssetIntoProject(
                candidates.at(i), projectRoot, destination, profile);
            imported.outcomes.push_back(outcome);
            if (outcome.ok()) imported.copied << outcome.outputPath;
            if (!outcome.warning.isEmpty()) imported.warnings << outcome.warning;
            if (!outcome.error.isEmpty()) imported.errors << outcome.error;
            QMetaObject::invokeMethod(&progress, [&progress, i] {
                progress.setValue(i + 1);
            }, Qt::QueuedConnection);
        }
        return imported;
    }));
    progress.show();
    if (!watcher.isFinished()) loop.exec();
    progress.close();
    result = watcher.result();

    if (!result.errors.isEmpty())
        QMessageBox::warning(this, tr("Adicionar imagem"), result.errors.mid(0, 8).join(QLatin1Char('\n')));

    const QStringList available = result.copied;
    if (available.isEmpty()) return;

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

    const QString firstCategory = core::AssetWorkflow::categoryIdForPath(m_ed.projectRelativePath(available.first()));
    const int comboIndex = m_category->findData(firstCategory);
    if (comboIndex >= 0) m_category->setCurrentIndex(comboIndex);
    rebuildTree();
    selectAbsolutePath(available.first());
}

void UniversalAssetPickerDialog::selectAbsolutePath(const QString& absolutePath)
{
    const QString wanted = normalizedAbsolute(absolutePath);
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        const QString candidate = item->data(0, PathRole).toString();
        if (!item->data(0, FolderRole).toBool() && !candidate.isEmpty() &&
            normalizedAbsolute(candidate).compare(wanted, Qt::CaseInsensitive) == 0) {
            m_tree->setCurrentItem(item);
            item->setSelected(true);
            m_tree->scrollToItem(item);
            return;
        }
        ++it;
    }
}

QString UniversalAssetPickerDialog::chooseOne(core::Editor& editor, QWidget* parent,
                                              const QString& context,
                                              const QString& requiredMediaType,
                                              const QString& title)
{
    if (editor.projectRoot().isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Assets"),
                                 QObject::tr("Salve o projeto em uma pasta antes de escolher assets."));
        return QString();
    }
    Request request;
    request.context = context;
    request.requiredMediaType = requiredMediaType;
    request.title = title;
    UniversalAssetPickerDialog dialog(editor, request, parent);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedPath() : QString();
}

QStringList UniversalAssetPickerDialog::chooseMany(core::Editor& editor, QWidget* parent,
                                                   const QString& context,
                                                   const QString& requiredMediaType,
                                                   const QString& title)
{
    if (editor.projectRoot().isEmpty()) return {};
    Request request;
    request.context = context;
    request.requiredMediaType = requiredMediaType;
    request.multiple = true;
    request.title = title;
    UniversalAssetPickerDialog dialog(editor, request, parent);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedPaths() : QStringList();
}


} // namespace ui
