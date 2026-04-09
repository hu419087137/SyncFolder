#pragma once

#include <QObject>
#include <QStringList>

class QFileSystemWatcher;

/**
 * @brief FolderWatcher 负责轻量监控多组目录变化。
 *
 * 为了避免大目录树一次性注册过多监听句柄，这里只监听每个主目录本身及其父目录。
 * 深层文件变化最终仍会通过主窗口中的周期校验被发现并补同步。
 */
class FolderWatcher : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造目录监控对象。
     * @param parent QObject 父对象。
     */
    explicit FolderWatcher(QObject *parent = nullptr);

    /**
     * @brief 监控多组主目录及其父目录。
     * @param folderPaths 需要纳入监控的目录绝对路径列表。
     */
    void setWatchedFolders(const QStringList &folderPaths);

    /**
     * @brief 清空当前所有监听路径。
     */
    void clear();

signals:
    /**
     * @brief 目录树中检测到变化。
     * @param changedPath 发生变化的目录路径。
     */
    void sigFolderChanged(const QString &changedPath);

private slots:
    void slotDirectoryChanged(const QString &changedPath);

private:
    void rebuildWatches();

    QFileSystemWatcher *_fileSystemWatcher;
    QStringList _folderPaths;
};
