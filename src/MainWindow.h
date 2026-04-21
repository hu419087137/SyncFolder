#pragma once

#include <QHash>
#include <QMainWindow>
#include <QVector>

class FolderSyncWorker;
class FolderWatcher;
class QStandardItemModel;
class QTimer;
class QWidget;
class QEvent;
class QThread;

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

protected:
    void changeEvent(QEvent *event) override;

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

private:
    struct FolderPairConfig
    {
        QString sourcePath;
        QString targetPath;
        QString statusText;
        bool isSyncEnabled = true;
        qint64 progressValue = 0;
        qint64 progressMaximum = 1;
    };

    struct RunningSyncContext
    {
        QThread *thread = nullptr;
        qint64 currentStep = 0;
        qint64 totalSteps = 1;
        QString reason;
    };

    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void applyButtonStyles();
    void refreshPairTable();
    void refreshActionWidgets();
    void refreshWatcher();
    void updateControlState();
    void appendLog(const QString &message);
    void removePairIndexFromQueues(int pairIndex);
    void startQueuedSyncs();
    void refreshStatusCell(int pairIndex);
    void updatePairStatus(int pairIndex, const QString &statusText);
    void updatePairProgress(int pairIndex, qint64 progressValue, qint64 progressMaximum);
    void requestSyncAll(const QString &reason);
    void requestSyncByIndexes(const QVector<int> &pairIndexes, const QString &reason);
    void startPairSync(int pairIndex, const QString &reason);
    void handlePairSyncStarted(int pairIndex,
                               qint64 totalSteps,
                               int removeFileCount,
                               int addFileCount,
                               int updateFileCount,
                               const QString &reason);
    void handlePairSyncProgress(int pairIndex,
                                qint64 currentStep,
                                qint64 totalSteps,
                                const QString &currentItem);
    void handlePairSyncFinished(int pairIndex, bool success, const QString &summary);
    bool isPairSyncRunning(int pairIndex) const;
    bool isPairSyncQueued(int pairIndex) const;
    int runningSyncCount() const;
    int maxParallelSyncCount() const;
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
    QString buildPairListSummaryText(const QVector<int> &pairIndexes) const;
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
    FolderWatcher *_folderWatcher;
    QVector<FolderPairConfig> _folderPairConfigs;
    QHash<int, RunningSyncContext> _runningSyncContexts;
    QHash<int, QString> _pendingSyncReasons;
    bool _isMonitoring;
    bool _isFastCompareEnabled;
    QString _scheduledReason;
};
