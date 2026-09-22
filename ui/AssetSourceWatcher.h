#pragma once
#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QHash>
#include <QSet>
namespace core { class Editor; }
namespace ui {
// One Assets observer for local and team projects. Hashing runs off the UI thread.
class AssetSourceWatcher final : public QObject {
public:
    explicit AssetSourceWatcher(core::Editor& editor, QObject* parent=nullptr);
    void rescan();
private:
    void scan();
    core::Editor& ed;
    QFileSystemWatcher watcher;
    QTimer debounce, fallback;
    QString root;
    QHash<QString, QString> stamps;
    QSet<QString> notifiedFiles;
    bool running=false;
    bool again=false;
};
}
