#pragma once

#include <QMainWindow>
#include <QSet>
#include <QVector>

class FolderSyncWorker;
class FolderWatcher;
class QStandardItemModel;
class QThread;
class QTimer;
class QWidget;

namespace Ui
{
class MainWindow;
}

/**
 * @brief MainWindow 提供多组文件夹同步项目的主界面。
 *
 * 界面负责维护多组 A/B 同步对、启动和停止整体监控、展示当前进度和运行日志，
 * 具体的文件同步动作依然交给后台线程中的同步工作对象执行。
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
    void slotAddPair();
    void slotRemoveSelectedPair();
    void slotPairSelectionChanged();
    void slotStartMonitoring();
    void slotStopMonitoring();
    void slotSyncAllPairs();
    void slotWatcherChanged(const QString &changedPath);
    void slotDebounceTimeout();
    void slotPeriodicCheck();
    void slotSyncStarted(int totalSteps,
                         int removeFileCount,
                         int addFileCount,
                         int updateFileCount,
                         const QString &reason);
    void slotSyncProgress(int currentStep, int totalSteps, const QString &currentItem);
    void slotSyncFinished(bool success, const QString &summary);
    void slotAppendLog(const QString &message);

private:
    struct FolderPairConfig
    {
        QString sourcePath;
        QString targetPath;
        QString statusText;
        bool isSyncEnabled = true;
        int progressValue = 0;
        int progressMaximum = 1;
    };

    void buildUi();
    void initializeWorker();
    void loadSettings();
    void saveSettings() const;
    void refreshPairTable();
    void refreshActionWidgets();
    void refreshWatcher();
    void updateControlState();
    void appendLog(const QString &message);
    void refreshStatusCell(int pairIndex);
    void updatePairStatus(int pairIndex, const QString &statusText);
    void updatePairProgress(int pairIndex, int progressValue, int progressMaximum);
    void requestSyncAll(const QString &reason);
    void requestSyncByIndexes(const QVector<int> &pairIndexes, const QString &reason);
    void startNextPairSync();
    void finishCurrentBatch();
    QVector<int> buildAllPairIndexes() const;
    QVector<int> buildEnabledPairIndexes() const;
    QVector<int> normalizePairIndexes(const QVector<int> &pairIndexes) const;
    int currentSelectedPairIndex() const;
    void selectPairRow(int pairIndex) const;
    void addPair();
    void editPair(int pairIndex);
    void syncPair(int pairIndex);
    void togglePairSync(int pairIndex);
    QWidget *createActionWidget(int pairIndex);
    QString buildPairStateText(const FolderPairConfig &pairConfig) const;
    QString buildPairDisplayText(const FolderPairConfig &pairConfig, int pairIndex) const;
    QString buildBatchSummary() const;
    QString normalizePath(const QString &path) const;
    bool editPairConfig(FolderPairConfig *pairConfig, int ignoredIndex, const QString &windowTitle) const;
    bool validatePairConfig(const FolderPairConfig &pairConfig,
                            int ignoredIndex,
                            QString *errorMessage) const;
    bool validateConfiguration(QString *errorMessage) const;

    Ui::MainWindow *_ui;
    QStandardItemModel *_pairTableModel;
    QTimer *_debounceTimer;
    QTimer *_periodicCheckTimer;
    QThread *_workerThread;
    FolderSyncWorker *_folderSyncWorker;
    FolderWatcher *_folderWatcher;
    QVector<FolderPairConfig> _folderPairConfigs;
    QVector<int> _syncQueue;
    QSet<int> _pendingSyncPairIndexes;
    bool _isMonitoring;
    bool _isSyncRunning;
    bool _hasPendingSync;
    int _currentSyncPairIndex;
    int _currentBatchTotalPairs;
    int _currentBatchFinishedPairs;
    int _currentBatchFailedPairs;
    QString _scheduledReason;
    QString _currentBatchReason;
    QString _pendingReason;
};
