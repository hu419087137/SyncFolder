#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

class FolderSyncWorker;
class HttpFolderSyncWorker;
class HttpSyncServer;
class FolderWatcher;
class QNetworkAccessManager;
class QNetworkReply;
class QStandardItemModel;
class QTableView;
class QTimer;
class QWidget;
class QEvent;
class QThread;
class QObject;

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
    void slotRemoveSelectedHttpClientPair();
    void slotPairSelectionChanged();
    void slotHttpClientPairSelectionChanged();
    void slotStartHttpServer();
    void slotStopHttpServer();
    void slotAddHttpSharedFolder();
    void slotEditHttpSharedFolder();
    void slotRemoveSelectedHttpSharedFolder();
    void slotFetchHttpClientSources();
    void slotHttpClientSourceReplyFinished();
    void slotBrowseHttpClientTargetRoot();
    void slotAddSelectedHttpClientSources();
    void slotStartMonitoring();
    void slotStopMonitoring();
    void slotSyncAllPairs();
    void slotSyncAllHttpClientPairs();
    void slotWatcherChanged(const QString &changedPath);
    void slotDebounceTimeout();
    void slotPeriodicCheck();
    void slotHttpServerStateChanged(bool isRunning, quint16 listenPort);

private:
    enum WorkerKind
    {
        E_LocalFolderSyncWorker = 0,
        E_HttpFolderSyncWorker = 1
    };

    struct FolderPairConfig
    {
        int sourceType = 0;
        QString sourcePath;
        QString sourceAccessToken;
        QString remoteSourceId;
        QString remoteSourceName;
        QString targetPath;
        QString statusText;
        bool isSyncEnabled = true;
        qint64 progressValue = 0;
        qint64 progressMaximum = 1;
    };

    struct HttpSharedFolderConfig
    {
        QString id;
        QString name;
        QString rootPath;
    };

    struct HttpRemoteSourceConfig
    {
        QString id;
        QString name;
        QString rootName;
    };

    struct RunningSyncContext
    {
        QThread *thread = nullptr;
        QObject *worker = nullptr;
        int workerKind = E_LocalFolderSyncWorker;
        qint64 currentStep = 0;
        qint64 totalSteps = 1;
        qint64 lastUiProgressValue = -1;
        qint64 lastUiProgressMaximum = -1;
        QString lastUiStatusText;
        QElapsedTimer uiProgressTimer;
        QString reason;
    };

    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void applyButtonStyles();
    void refreshHttpServerUi();
    void refreshHttpSharedFolderTable();
    void refreshHttpClientSourceTable();
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
    bool startPairSync(int pairIndex, const QString &reason, bool shouldKickoffImmediately = true);
    void kickoffPairSync(int pairIndex);
    void connectWorkerSignals(QObject *worker, int workerKind, int pairIndex, QThread *thread);
    bool invokeWorkerCancel(QObject *worker, int workerKind);
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
    QVector<int> buildEnabledPairIndexes(int sourceType) const;
    QVector<int> buildPairIndexesBySourceType(int sourceType) const;
    QVector<int> normalizePairIndexes(const QVector<int> &pairIndexes) const;
    int currentSelectedPairIndex() const;
    int currentSelectedHttpClientPairIndex() const;
    int currentSelectedPairIndex(const QTableView *tableView, const QVector<int> &pairIndexes) const;
    void selectPairRow(int pairIndex) const;
    void selectPairRow(QTableView *tableView,
                       QStandardItemModel *tableModel,
                       const QVector<int> &pairIndexes,
                       int pairIndex) const;
    void addPair();
    void editPair(int pairIndex);
    void syncPair(int pairIndex);
    void cancelPairSync(int pairIndex);
    void togglePairSync(int pairIndex);
    QWidget *createActionWidget(QTableView *ownerTableView, int pairIndex);
    bool isHttpSourcePair(const FolderPairConfig &pairConfig) const;
    QString buildSourceDisplayText(const FolderPairConfig &pairConfig) const;
    QString buildTargetDisplayText(const FolderPairConfig &pairConfig) const;
    QString buildPairStateText(const FolderPairConfig &pairConfig) const;
    QString buildPairDisplayText(const FolderPairConfig &pairConfig, int pairIndex) const;
    QString buildPairListSummaryText(const QVector<int> &pairIndexes) const;
    QString normalizeLocalPath(const QString &path) const;
    QString normalizeHttpSourceUrl(const QString &sourceUrl) const;
    QString buildHttpClientSourceListUrl(const QString &sourceUrl) const;
    QString buildHttpClientImportPath(const HttpRemoteSourceConfig &remoteSourceConfig) const;
    bool validateHttpSharedFolderConfig(const HttpSharedFolderConfig &sharedFolderConfig,
                                        int ignoredIndex,
                                        QString *errorMessage) const;
    bool editPairConfig(FolderPairConfig *pairConfig, int ignoredIndex, const QString &windowTitle) const;
    bool validatePairConfig(const FolderPairConfig &pairConfig,
                            int ignoredIndex,
                            QString *errorMessage) const;

    Ui::MainWindow *_ui;
    QStandardItemModel *_localPairTableModel;
    QStandardItemModel *_httpClientPairTableModel;
    QTimer *_debounceTimer;
    QTimer *_periodicCheckTimer;
    FolderWatcher *_folderWatcher;
    HttpSyncServer *_httpSyncServer;
    QNetworkAccessManager *_httpClientCatalogNetworkAccessManager;
    QPointer<QNetworkReply> _activeHttpClientCatalogReply;
    QVector<FolderPairConfig> _folderPairConfigs;
    QVector<HttpSharedFolderConfig> _httpSharedFolderConfigs;
    QVector<HttpRemoteSourceConfig> _httpRemoteSourceConfigs;
    QVector<int> _localPairIndexes;
    QVector<int> _httpClientPairIndexes;
    QHash<int, RunningSyncContext> _runningSyncContexts;
    QHash<int, QString> _pendingSyncReasons;
    bool _isMonitoring;
    int _compareMode;
    int _maxParallelSyncCount;
    QString _scheduledReason;
    QString _httpServerAccessToken;
    QString _httpClientServerUrl;
    QString _httpClientAccessToken;
    QString _httpClientTargetRootPath;
    quint16 _httpServerPort;
};
