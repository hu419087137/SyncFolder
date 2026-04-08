#pragma once

#include <QObject>

class QFileSystemWatcher;

/**
 * @brief FolderWatcher 负责递归监控目录树变化。
 *
 * QFileSystemWatcher 不会自动把新建子目录加入监听列表，所以这里封装一层，
 * 每次目录变化后都会重新刷新监听集合，确保后续变化也能继续被发现。
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
     * @brief 递归监控 A 和 B 两棵目录树。
     * @param sourcePath A 主目录绝对路径。
     * @param targetPath B 备份目录绝对路径。
     */
    void setWatchedFolders(const QString &sourcePath, const QString &targetPath);

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
    QStringList collectDirectoryPaths(const QString &rootPath) const;

    QFileSystemWatcher *_fileSystemWatcher;
    QString _sourcePath;
    QString _targetPath;
};
