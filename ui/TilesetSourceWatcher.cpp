#include "TilesetSourceWatcher.h"

#include "core/Editor.h"
#include "core/Renderer.h"
#include "core/ResourceManager.h"
#include "core/TilesetOps.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <utility>

namespace ui {

TilesetSourceWatcher::TilesetSourceWatcher(core::Editor& editor, QObject* parent)
    : QObject(parent), ed(editor)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(220);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        queueFile(path);
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        inspectDirectory(path);
    });
    connect(&m_debounce, &QTimer::timeout, this, [this] { processPending(); });
    connect(&ed, &core::Editor::tilesetsChanged, this, [this] {
        if (m_processing) return;
        QTimer::singleShot(0, this, [this] { rescan(); });
    });

    rescan();
}

QString TilesetSourceWatcher::keyForPath(const QString& path)
{
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    clean = clean.toLower();
#endif
    return clean;
}

TilesetSourceWatcher::FileStamp TilesetSourceWatcher::stampForPath(const QString& path)
{
    const QFileInfo info(path);
    FileStamp s;
    s.exists = info.isFile();
    if (s.exists) {
        s.size = info.size();
        s.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
    return s;
}

void TilesetSourceWatcher::rescan()
{
    if (m_processing) return;

    const QStringList oldFiles = m_watcher.files();
    if (!oldFiles.isEmpty()) m_watcher.removePaths(oldFiles);
    const QStringList oldDirs = m_watcher.directories();
    if (!oldDirs.isEmpty()) m_watcher.removePaths(oldDirs);

    m_sourceFiles.clear();
    m_stamps.clear();
    QSet<QString> directories;

    for (int i = 0; i < ed.tilesets.size(); ++i) {
        const QStringList sources = core::tilesetSourceFiles(ed, i);
        for (const QString& raw : sources) {
            // Backings do Team Sync são representações canônicas recebidas da
            // equipe, não fontes de autoria para o Live Reload. Observá-los
            // aqui poderia reaplicar chroma/recorte ao atlas já processado e
            // ainda gerar um ciclo Asset Sync -> watcher -> Asset Sync.
            const QString relative = ed.projectRelativePath(raw);
            if (QDir::fromNativeSeparators(relative).startsWith(
                    QStringLiteral("Assets/LUDO/Team/Tilesets/"), Qt::CaseInsensitive))
                continue;
            const QString absolute = QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
            const QString key = keyForPath(absolute);
            if (key.isEmpty() || m_sourceFiles.contains(key)) continue;
            m_sourceFiles.insert(key);
            m_stamps.insert(key, stampForPath(absolute));
            const QFileInfo info(absolute);
            directories.insert(QDir::cleanPath(info.absolutePath()));
            if (info.isFile()) m_watcher.addPath(absolute);
        }
    }

    for (const QString& directory : directories)
        if (QFileInfo(directory).isDir()) m_watcher.addPath(directory);
}

void TilesetSourceWatcher::queueFile(const QString& path)
{
    const QString key = keyForPath(path);
    if (!m_sourceFiles.contains(key)) return;
    m_pending.insert(key);
    m_debounce.start();
}

void TilesetSourceWatcher::inspectDirectory(const QString& directory)
{
    const QString dirKey = keyForPath(directory);
    for (const QString& sourceKey : std::as_const(m_sourceFiles)) {
        const QFileInfo info(sourceKey);
        if (keyForPath(info.absolutePath()) != dirKey) continue;
        const FileStamp now = stampForPath(sourceKey);
        if (m_stamps.value(sourceKey) != now) {
            m_stamps.insert(sourceKey, now);
            m_pending.insert(sourceKey);
        }
    }
    if (!m_pending.isEmpty()) m_debounce.start();
}

void TilesetSourceWatcher::processPending()
{
    if (m_processing || m_pending.isEmpty()) return;
    m_processing = true;
    const QSet<QString> pending = std::exchange(m_pending, QSet<QString>{});

    QSet<int> changedTilesets;
    QStringList updatedFiles;
    QStringList errors;

    for (const QString& sourcePath : pending) {
        // Muitos editores salvam com arquivo temporário + rename. Se o arquivo
        // ainda não voltou, aguardamos outro evento do diretório em vez de
        // substituir o atlas por uma imagem vazia.
        if (!QFileInfo(sourcePath).isFile()) continue;
        for (int i = 0; i < ed.tilesets.size(); ++i) {
            QString error;
            QStringList updated;
            if (core::reloadTilesetSources(ed, i, sourcePath, &error, &updated)) {
                changedTilesets.insert(i);
                updatedFiles.append(updated);
            } else if (!error.trimmed().isEmpty()) {
                errors.push_back(error);
            }
        }
        m_stamps.insert(sourcePath, stampForPath(sourcePath));
    }

    if (!changedTilesets.isEmpty()) {
        for (int idx : std::as_const(changedTilesets)) core::pixmapCache().invalidate(idx);
        ed.markDirty();
        QStringList sharedPaths;
        for (const QString& absolute : std::as_const(updatedFiles)) {
            const QString relative = ed.projectRelativePath(absolute);
            if (relative.startsWith(QStringLiteral("Assets/"), Qt::CaseInsensitive))
                sharedPaths.push_back(relative);
        }
        sharedPaths.removeDuplicates();
        if (!sharedPaths.isEmpty()) ed.resources().notifyAssetsChanged(sharedPaths);
        emit ed.tilesetsChanged();
        emit ed.mapChanged();
        if (m_status) {
            updatedFiles.removeDuplicates();
            m_status(QObject::tr("Tileset atualizado automaticamente: %1")
                         .arg(updatedFiles.isEmpty() ? QObject::tr("imagem fonte")
                                                     : QFileInfo(updatedFiles.first()).fileName()));
        }
    } else if (!errors.isEmpty() && m_status) {
        errors.removeDuplicates();
        m_status(QObject::tr("Tileset não atualizado: %1").arg(errors.first()));
    }

    m_processing = false;
    // QFileSystemWatcher deixa de observar um arquivo quando ele é substituído
    // por rename. Reconstruir a lista aqui também cobre esse caso.
    rescan();
}

} // namespace ui
