#pragma once

#include <QMainWindow>

class FolderSyncWorker;
class FolderWatcher;
class QThread;
class QTimer;
namespace Ui
{
class MainWindow;
}

/**
 * @brief MainWindow 提供文件夹同步项目的主界面。
 *
 * 界面负责目录选择、启动和停止监控、显示进度条以及输出日志，
 * 所有真正的同步动作都交给后台线程执行。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /**
     * @brief 构造主窗口。
     * @param parent QWidget 父对象。
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief 析构主窗口，并安全回收后台线程。
     */
    ~MainWindow() override;

signals:
    /**
     * @brief 发送后台同步请求。
     * @param sourcePath A 主目录绝对路径。
     * @param targetPath B 备份目录绝对路径。
     * @param reason 触发同步的原因。
     */
    void sigStartSync(const QString &sourcePath, const QString &targetPath, const QString &reason);

private slots:
    void slotBrowseSourceFolder();
    void slotBrowseTargetFolder();
    void slotStartMonitoring();
    void slotStopMonitoring();
    void slotManualSync();
    void slotWatcherChanged(const QString &changedPath);
    void slotDebounceTimeout();
    void slotPeriodicCheck();
    void slotSyncStarted(int totalSteps, const QString &reason);
    void slotSyncProgress(int currentStep, int totalSteps, const QString &currentItem);
    void slotSyncFinished(bool success, const QString &summary);
    void slotAppendLog(const QString &message);

private:
    void buildUi();
    void initializeWorker();
    void updateControlState();
    void refreshWatcher();
    void appendLog(const QString &message);
    void requestSync(const QString &reason);
    QString normalizePath(const QString &path) const;
    bool validatePaths(QString *errorMessage) const;

    Ui::MainWindow *_ui;
    QTimer *_debounceTimer;
    QTimer *_periodicCheckTimer;
    QThread *_workerThread;
    FolderSyncWorker *_folderSyncWorker;
    FolderWatcher *_folderWatcher;
    bool _isMonitoring;
    bool _isSyncRunning;
    bool _hasPendingSync;
    QString _scheduledReason;
    QString _pendingReason;
};
