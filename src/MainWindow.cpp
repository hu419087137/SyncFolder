#include "MainWindow.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <QtGlobal>

#include "FolderSyncWorker.h"
#include "FolderWatcher.h"
#include "ui_MainWindow.h"

namespace
{
constexpr int kDebounceIntervalMs = 800;
constexpr int kPeriodicCheckIntervalMs = 3000;

bool isSameOrNestedPath(const QString &sourcePath, const QString &targetPath)
{
    const QString normalizedSourcePath = QDir::cleanPath(sourcePath);
    const QString normalizedTargetPath = QDir::cleanPath(targetPath);

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif

    if (normalizedSourcePath.compare(normalizedTargetPath, caseSensitivity) == 0) {
        return true;
    }

    const QString sourcePrefix = normalizedSourcePath + QDir::separator();
    const QString targetPrefix = normalizedTargetPath + QDir::separator();
    return normalizedSourcePath.startsWith(targetPrefix, caseSensitivity)
        || normalizedTargetPath.startsWith(sourcePrefix, caseSensitivity);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      _ui(new Ui::MainWindow),
      _debounceTimer(nullptr),
      _periodicCheckTimer(nullptr),
      _workerThread(nullptr),
      _folderSyncWorker(nullptr),
      _folderWatcher(nullptr),
      _isMonitoring(false),
      _isSyncRunning(false),
      _hasPendingSync(false)
{
    buildUi();

    _debounceTimer = new QTimer(this);
    _debounceTimer->setSingleShot(true);
    _debounceTimer->setInterval(kDebounceIntervalMs);
    connect(_debounceTimer, &QTimer::timeout, this, &MainWindow::slotDebounceTimeout);

    _periodicCheckTimer = new QTimer(this);
    _periodicCheckTimer->setInterval(kPeriodicCheckIntervalMs);
    connect(_periodicCheckTimer, &QTimer::timeout, this, &MainWindow::slotPeriodicCheck);

    _folderWatcher = new FolderWatcher(this);
    connect(_folderWatcher, &FolderWatcher::sigFolderChanged, this, &MainWindow::slotWatcherChanged);

    initializeWorker();
    updateControlState();
    appendLog(tr("程序已启动，请先选择 A 主目录和 B 备份目录。"));
}

MainWindow::~MainWindow()
{
    if (_workerThread != nullptr && _workerThread->isRunning()) {
        _workerThread->quit();
        _workerThread->wait();
    }

    delete _ui;
}

void MainWindow::slotBrowseSourceFolder()
{
    const QString currentPath = normalizePath(_ui->sourcePathEdit->text());
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 A 主目录"),
        currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!selectedPath.isEmpty()) {
        _ui->sourcePathEdit->setText(QDir::toNativeSeparators(selectedPath));
    }
}

void MainWindow::slotBrowseTargetFolder()
{
    const QString currentPath = normalizePath(_ui->targetPathEdit->text());
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 B 备份目录"),
        currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!selectedPath.isEmpty()) {
        _ui->targetPathEdit->setText(QDir::toNativeSeparators(selectedPath));
    }
}

void MainWindow::slotStartMonitoring()
{
    QString errorMessage;
    if (!validatePaths(&errorMessage)) {
        QMessageBox::warning(this, tr("路径无效"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    _isMonitoring = true;
    _hasPendingSync = false;
    _scheduledReason.clear();
    _pendingReason.clear();
    refreshWatcher();
    _periodicCheckTimer->start();
    updateControlState();

    _ui->statusLabel->setText(tr("状态：监控中"));
    _ui->detailLabel->setText(tr("已启动持续监控，等待首次同步。"));
    appendLog(tr("已启动监控模式，后续会持续保持 B 与 A 一致。"));
    requestSync(tr("启动监控后的首次同步"));
}

void MainWindow::slotStopMonitoring()
{
    _isMonitoring = false;
    _hasPendingSync = false;
    _scheduledReason.clear();
    _pendingReason.clear();
    _debounceTimer->stop();
    _periodicCheckTimer->stop();
    if (_folderWatcher != nullptr) {
        _folderWatcher->clear();
    }

    if (!_isSyncRunning) {
        _ui->statusLabel->setText(tr("状态：监控已停止"));
        _ui->detailLabel->setText(tr("持续监控已关闭，可继续手动同步。"));
    }

    appendLog(tr("已停止监控模式。"));
    updateControlState();
}

void MainWindow::slotManualSync()
{
    QString errorMessage;
    if (!validatePaths(&errorMessage)) {
        QMessageBox::warning(this, tr("路径无效"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    requestSync(tr("手动同步"));
}

void MainWindow::slotWatcherChanged(const QString &changedPath)
{
    if (!_isMonitoring) {
        return;
    }

    _scheduledReason = tr("检测到目录变化：%1").arg(changedPath);
    _debounceTimer->start();
    appendLog(tr("检测到目录变化，准备重新同步，path=%1").arg(changedPath));
}

void MainWindow::slotDebounceTimeout()
{
    const QString reason = _scheduledReason.isEmpty() ? tr("目录变化触发同步") : _scheduledReason;
    _scheduledReason.clear();
    requestSync(reason);
}

void MainWindow::slotPeriodicCheck()
{
    if (!_isMonitoring) {
        return;
    }

    if (_isSyncRunning) {
        _hasPendingSync = true;
        _pendingReason = tr("周期校验");
        return;
    }

    requestSync(tr("周期校验"));
}

void MainWindow::slotSyncStarted(int totalSteps, const QString &reason)
{
    _ui->progressBar->setRange(0, qMax(1, totalSteps));
    _ui->progressBar->setValue(0);
    _ui->progressBar->setFormat(totalSteps == 0 ? tr("无需同步") : tr("%v/%m"));
    _ui->statusLabel->setText(tr("状态：正在同步"));
    _ui->detailLabel->setText(tr("触发原因：%1").arg(reason));
    appendLog(tr("同步计划已生成，totalSteps=%1，reason=%2").arg(totalSteps).arg(reason));
}

void MainWindow::slotSyncProgress(int currentStep, int totalSteps, const QString &currentItem)
{
    _ui->progressBar->setRange(0, qMax(1, totalSteps));
    _ui->progressBar->setValue(currentStep);
    _ui->progressBar->setFormat(tr("%v/%m"));
    _ui->detailLabel->setText(currentItem);
}

void MainWindow::slotSyncFinished(bool success, const QString &summary)
{
    _isSyncRunning = false;

    _ui->progressBar->setRange(0, 1);
    _ui->progressBar->setValue(success ? 1 : 0);
    _ui->progressBar->setFormat(success ? tr("完成") : tr("失败"));
    _ui->statusLabel->setText(success
                                  ? (_isMonitoring ? tr("状态：监控中") : tr("状态：同步完成"))
                                  : (_isMonitoring ? tr("状态：监控中（上次失败）")
                                                   : tr("状态：同步失败")));
    _ui->detailLabel->setText(summary);
    appendLog(summary);

    if (_isMonitoring) {
        refreshWatcher();
    }

    updateControlState();

    if (_isMonitoring && _hasPendingSync) {
        const QString pendingReason =
            _pendingReason.isEmpty() ? tr("合并后的补偿同步") : _pendingReason;
        _hasPendingSync = false;
        _pendingReason.clear();

        QTimer::singleShot(200, this, [this, pendingReason]() {
            requestSync(pendingReason);
        });
    }
}

void MainWindow::slotAppendLog(const QString &message)
{
    appendLog(message);
}

void MainWindow::buildUi()
{
    _ui->setupUi(this);
    _ui->progressBar->setRange(0, 1);
    _ui->progressBar->setValue(0);
    _ui->progressBar->setFormat(tr("待同步"));

    connect(_ui->browseSourceButton, &QPushButton::clicked, this, &MainWindow::slotBrowseSourceFolder);
    connect(_ui->browseTargetButton, &QPushButton::clicked, this, &MainWindow::slotBrowseTargetFolder);
    connect(_ui->startMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStartMonitoring);
    connect(_ui->stopMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStopMonitoring);
    connect(_ui->manualSyncButton, &QPushButton::clicked, this, &MainWindow::slotManualSync);
}

void MainWindow::initializeWorker()
{
    _workerThread = new QThread(this);
    _folderSyncWorker = new FolderSyncWorker();
    _folderSyncWorker->moveToThread(_workerThread);

    connect(_workerThread, &QThread::finished, _folderSyncWorker, &QObject::deleteLater);
    connect(this, &MainWindow::sigStartSync, _folderSyncWorker, &FolderSyncWorker::slotStartSync,
            Qt::QueuedConnection);
    connect(_folderSyncWorker, &FolderSyncWorker::sigSyncStarted, this, &MainWindow::slotSyncStarted,
            Qt::QueuedConnection);
    connect(_folderSyncWorker, &FolderSyncWorker::sigSyncProgress, this, &MainWindow::slotSyncProgress,
            Qt::QueuedConnection);
    connect(_folderSyncWorker, &FolderSyncWorker::sigSyncFinished, this, &MainWindow::slotSyncFinished,
            Qt::QueuedConnection);
    connect(_folderSyncWorker, &FolderSyncWorker::sigLogMessage, this, &MainWindow::slotAppendLog,
            Qt::QueuedConnection);

    _workerThread->start();
}

void MainWindow::updateControlState()
{
    const bool canEditPaths = !_isMonitoring && !_isSyncRunning;

    _ui->sourcePathEdit->setEnabled(canEditPaths);
    _ui->targetPathEdit->setEnabled(canEditPaths);
    _ui->browseSourceButton->setEnabled(canEditPaths);
    _ui->browseTargetButton->setEnabled(canEditPaths);
    _ui->startMonitorButton->setEnabled(!_isMonitoring && !_isSyncRunning);
    _ui->stopMonitorButton->setEnabled(_isMonitoring);
    _ui->manualSyncButton->setEnabled(!_isSyncRunning);
}

void MainWindow::refreshWatcher()
{
    if (_folderWatcher == nullptr) {
        return;
    }

    if (!_isMonitoring) {
        _folderWatcher->clear();
        return;
    }

    _folderWatcher->setWatchedFolders(normalizePath(_ui->sourcePathEdit->text()),
                                      normalizePath(_ui->targetPathEdit->text()));
}

void MainWindow::appendLog(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    _ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void MainWindow::requestSync(const QString &reason)
{
    if (_isSyncRunning) {
        _hasPendingSync = true;
        _pendingReason = reason;
        appendLog(tr("当前同步仍在执行，已合并新的请求，reason=%1").arg(reason));
        return;
    }

    _isSyncRunning = true;
    updateControlState();
    _ui->statusLabel->setText(tr("状态：正在扫描目录"));
    _ui->detailLabel->setText(tr("触发原因：%1").arg(reason));
    _ui->progressBar->setRange(0, 0);
    _ui->progressBar->setFormat(tr("扫描中..."));

    emit sigStartSync(normalizePath(_ui->sourcePathEdit->text()),
                      normalizePath(_ui->targetPathEdit->text()),
                      reason);
}

QString MainWindow::normalizePath(const QString &path) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

bool MainWindow::validatePaths(QString *errorMessage) const
{
    const QString sourcePath = normalizePath(_ui->sourcePathEdit->text());
    const QString targetPath = normalizePath(_ui->targetPathEdit->text());

    if (sourcePath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请先选择 A 主目录。");
        }
        return false;
    }

    if (targetPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请先选择 B 备份目录。");
        }
        return false;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("A 主目录不存在或不是目录，path=%1").arg(sourcePath);
        }
        return false;
    }

    const QFileInfo targetInfo(targetPath);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("B 备份路径已存在且不是目录，path=%1").arg(targetPath);
        }
        return false;
    }

    if (isSameOrNestedPath(sourcePath, targetPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("A 和 B 不能相同，也不能互相嵌套，source=%1，target=%2")
                                .arg(sourcePath, targetPath);
        }
        return false;
    }

    return true;
}
