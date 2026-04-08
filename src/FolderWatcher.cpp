#include "FolderWatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>

namespace
{
QString normalizePath(const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

void appendUniquePath(const QString &path, QSet<QString> *paths)
{
    if (!path.isEmpty()) {
        paths->insert(path);
    }
}
} // namespace

FolderWatcher::FolderWatcher(QObject *parent)
    : QObject(parent),
      _fileSystemWatcher(new QFileSystemWatcher(this))
{
    connect(_fileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, &FolderWatcher::slotDirectoryChanged);
}

void FolderWatcher::setWatchedFolders(const QString &sourcePath, const QString &targetPath)
{
    _sourcePath = normalizePath(sourcePath);
    _targetPath = normalizePath(targetPath);
    rebuildWatches();
}

void FolderWatcher::clear()
{
    const QStringList watchedDirectories = _fileSystemWatcher->directories();
    if (!watchedDirectories.isEmpty()) {
        _fileSystemWatcher->removePaths(watchedDirectories);
    }
}

void FolderWatcher::slotDirectoryChanged(const QString &changedPath)
{
    emit sigFolderChanged(changedPath);

    // 目录树变化后刷新监听集合，保证新建子目录也被纳入监控。
    QTimer::singleShot(150, this, [this]() {
        rebuildWatches();
    });
}

void FolderWatcher::rebuildWatches()
{
    clear();

    QSet<QString> watchPaths;
    appendUniquePath(_sourcePath, &watchPaths);
    appendUniquePath(_targetPath, &watchPaths);

    const QString sourceParentPath = QFileInfo(_sourcePath).dir().absolutePath();
    const QString targetParentPath = QFileInfo(_targetPath).dir().absolutePath();
    appendUniquePath(sourceParentPath, &watchPaths);
    appendUniquePath(targetParentPath, &watchPaths);

    const QStringList sourceDirectories = collectDirectoryPaths(_sourcePath);
    const QStringList targetDirectories = collectDirectoryPaths(_targetPath);
    for (const QString &directoryPath : sourceDirectories) {
        appendUniquePath(directoryPath, &watchPaths);
    }
    for (const QString &directoryPath : targetDirectories) {
        appendUniquePath(directoryPath, &watchPaths);
    }

    QStringList existingWatchPaths;
    existingWatchPaths.reserve(watchPaths.size());
    for (const QString &watchPath : watchPaths) {
        const QFileInfo fileInfo(watchPath);
        if (fileInfo.exists() && fileInfo.isDir()) {
            existingWatchPaths.append(watchPath);
        }
    }

    if (!existingWatchPaths.isEmpty()) {
        _fileSystemWatcher->addPaths(existingWatchPaths);
    }
}

QStringList FolderWatcher::collectDirectoryPaths(const QString &rootPath) const
{
    QStringList directoryPaths;

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return directoryPaths;
    }

    directoryPaths.append(rootPath);
    QDirIterator iterator(rootPath,
                          QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        directoryPaths.append(iterator.fileInfo().absoluteFilePath());
    }

    return directoryPaths;
}
