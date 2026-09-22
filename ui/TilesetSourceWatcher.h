#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <functional>

namespace core { class Editor; }

namespace ui {

/// Mantém tilesets vinculados às imagens de Assets/Tilesets (ou a fontes
/// externas explicitamente escolhidas). O watcher observa arquivo + diretório
/// para sobreviver a editores que salvam por replace/rename atômico.
class TilesetSourceWatcher final : public QObject
{
public:
    explicit TilesetSourceWatcher(core::Editor& editor, QObject* parent = nullptr);

    void rescan();
    void setStatusHandler(std::function<void(const QString&)> handler) { m_status = std::move(handler); }

private:
    struct FileStamp {
        qint64 size = -1;
        qint64 mtimeMs = -1;
        bool exists = false;
        bool operator==(const FileStamp& other) const {
            return size == other.size && mtimeMs == other.mtimeMs && exists == other.exists;
        }
        bool operator!=(const FileStamp& other) const { return !(*this == other); }
    };

    static QString keyForPath(const QString& path);
    static FileStamp stampForPath(const QString& path);
    void queueFile(const QString& path);
    void inspectDirectory(const QString& directory);
    void processPending();

    core::Editor& ed;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QSet<QString> m_sourceFiles;
    QHash<QString, FileStamp> m_stamps;
    QSet<QString> m_pending;
    std::function<void(const QString&)> m_status;
    bool m_processing = false;
};

} // namespace ui
