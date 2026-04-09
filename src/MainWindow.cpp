#include "MainWindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>

#include "FolderSyncWorker.h"
#include "FolderWatcher.h"
#include "PairEditDialog.h"
#include "ui_MainWindow.h"

namespace
{
constexpr int kDebounceIntervalMs = 800;
constexpr int kPeriodicCheckIntervalMs = 3000;
constexpr int kSourceColumn = 0;
constexpr int kTargetColumn = 1;
constexpr int kStatusColumn = 2;
constexpr int kActionColumn = 3;
constexpr int kPathColumnMinimumWidth = 220;
constexpr int kStatusColumnWidth = 360;
constexpr int kActionColumnWidth = 270;
constexpr int kTableRowHeight = 48;

QString buildActionButtonStyle(const QColor &baseColor)
{
    const QColor hoverColor = baseColor.lighter(108);
    const QColor pressedColor = baseColor.darker(108);
    return QStringLiteral(
               "QPushButton {"
               " border: none;"
               " border-radius: 6px;"
               " padding: 4px 10px;"
               " min-height: 28px;"
               " color: white;"
               " background-color: %1;"
               "}"
               "QPushButton:hover {"
               " background-color: %2;"
               "}"
               "QPushButton:pressed {"
               " background-color: %3;"
               "}"
               "QPushButton:disabled {"
               " color: #dde3ea;"
               " background-color: #b8c1cb;"
               "}")
        .arg(baseColor.name(), hoverColor.name(), pressedColor.name());
}

enum StatusDataRole
{
    E_ProgressValueRole = Qt::UserRole + 1,
    E_ProgressMaximumRole,
    E_IsSyncEnabledRole
};

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

QColor buildProgressBarColor(const QString &statusText, bool isSyncEnabled, const QPalette &palette)
{
    const bool isDarkTheme = isDarkPalette(palette);
    if (!isSyncEnabled) {
        return isDarkTheme ? QColor(120, 126, 136) : QColor(160, 160, 160);
    }

    if (statusText.contains(QStringLiteral("失败"))) {
        return isDarkTheme ? QColor(255, 128, 118) : QColor(220, 90, 78);
    }

    if (statusText.contains(QStringLiteral("成功")) || statusText.contains(QStringLiteral("已一致"))) {
        return isDarkTheme ? QColor(88, 196, 128) : QColor(70, 163, 104);
    }

    if (statusText.contains(QStringLiteral("排队")) || statusText.contains(QStringLiteral("等待执行"))) {
        return isDarkTheme ? QColor(236, 190, 92) : QColor(224, 167, 57);
    }

    if (statusText.contains(QStringLiteral("待删")) || statusText.contains(QStringLiteral("待增"))
        || statusText.contains(QStringLiteral("待同步"))) {
        return isDarkTheme ? QColor(110, 156, 255) : QColor(75, 127, 239);
    }

    return isDarkTheme ? QColor(145, 165, 190) : QColor(93, 120, 148);
}

QColor buildStatusTextColor(const QString &statusText, bool isSyncEnabled, const QPalette &palette)
{
    const bool isDarkTheme = isDarkPalette(palette);
    if (!isSyncEnabled) {
        return isDarkTheme ? QColor(148, 154, 164) : QColor(125, 125, 125);
    }

    if (statusText.contains(QStringLiteral("失败"))) {
        return isDarkTheme ? QColor(255, 158, 146) : QColor(180, 55, 45);
    }

    if (statusText.contains(QStringLiteral("成功")) || statusText.contains(QStringLiteral("已一致"))) {
        return isDarkTheme ? QColor(126, 226, 160) : QColor(39, 106, 68);
    }

    if (statusText.contains(QStringLiteral("排队")) || statusText.contains(QStringLiteral("等待执行"))) {
        return isDarkTheme ? QColor(245, 205, 112) : QColor(150, 100, 17);
    }

    return isDarkTheme ? palette.color(QPalette::Text) : QColor(48, 60, 78);
}

class PairStatusDelegate : public QStyledItemDelegate
{
public:
    explicit PairStatusDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem viewOption(option);
        initStyleOption(&viewOption, index);
        const QWidget *widget = viewOption.widget;
        QStyle *style = widget != nullptr ? widget->style() : QApplication::style();

        const QString text = index.data(Qt::DisplayRole).toString();
        const int progressValue = index.data(E_ProgressValueRole).toInt();
        const int progressMaximum = qMax(1, index.data(E_ProgressMaximumRole).toInt());
        const bool isSyncEnabled = index.data(E_IsSyncEnabledRole).toBool();
        const QColor progressColor = buildProgressBarColor(text, isSyncEnabled, viewOption.palette);
        const QColor textColor = option.state & QStyle::State_Selected
            ? viewOption.palette.color(QPalette::HighlightedText)
            : buildStatusTextColor(text, isSyncEnabled, viewOption.palette);
        const QRect contentRect = option.rect.adjusted(6, 4, -6, -4);
        const int progressWidth = qMin(130, qMax(96, contentRect.width() / 3));
        const int progressHeight = 8;
        const int progressTop = contentRect.top() + (contentRect.height() - progressHeight) / 2;
        const QRect progressRect(contentRect.left(), progressTop, progressWidth, progressHeight);
        const QRect textRect = contentRect.adjusted(progressWidth + 8, 0, 0, 0);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QStyleOptionViewItem backgroundOption(viewOption);
        backgroundOption.text.clear();
        backgroundOption.icon = QIcon();
        backgroundOption.features &= ~QStyleOptionViewItem::HasDisplay;
        backgroundOption.features &= ~QStyleOptionViewItem::HasDecoration;
        style->drawControl(QStyle::CE_ItemViewItem, &backgroundOption, painter, widget);

        const QRectF progressTrackRect = progressRect.adjusted(0, 0, -1, -1);
        const QColor trackColor = option.state & QStyle::State_Selected
            ? viewOption.palette.color(QPalette::HighlightedText).lighter(140)
            : viewOption.palette.color(QPalette::Mid);
        painter->setPen(QPen(trackColor, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(progressTrackRect, 4, 4);

        if (progressValue > 0) {
            QRectF progressFillRect = progressTrackRect;
            progressFillRect.setWidth(progressTrackRect.width() * progressValue / progressMaximum);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(progressColor.red(), progressColor.green(), progressColor.blue(), 200));
            painter->drawRoundedRect(progressFillRect, 6, 6);
        }

        painter->setPen(textColor);
        painter->drawText(textRect,
                          Qt::AlignVCenter | Qt::TextSingleLine,
                          viewOption.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize itemSize = QStyledItemDelegate::sizeHint(option, index);
        itemSize.setHeight(qMax(itemSize.height(), 28));
        itemSize.setWidth(qMax(itemSize.width(), kStatusColumnWidth));
        return itemSize;
    }
};

QString settingsFilePath()
{
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::homePath();
    }

    QDir().mkpath(appDataPath);
    return QDir(appDataPath).filePath(QStringLiteral("SyncFolder.ini"));
}

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
      _pairTableModel(nullptr),
      _debounceTimer(nullptr),
      _periodicCheckTimer(nullptr),
      _workerThread(nullptr),
      _folderSyncWorker(nullptr),
      _folderWatcher(nullptr),
      _isMonitoring(false),
      _isSyncRunning(false),
      _hasPendingSync(false),
      _currentSyncPairIndex(-1),
      _currentBatchTotalPairs(0),
      _currentBatchFinishedPairs(0),
      _currentBatchFailedPairs(0)
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
    loadSettings();
    updateControlState();

    if (_folderPairConfigs.isEmpty()) {
        appendLog(tr("程序已启动，请先新增至少一组同步对。"));
    }
}

MainWindow::~MainWindow()
{
    saveSettings();

    if (_workerThread != nullptr && _workerThread->isRunning()) {
        _workerThread->quit();
        _workerThread->wait();
    }

    delete _ui;
}

void MainWindow::slotAddPair()
{
    addPair();
}

void MainWindow::slotRemoveSelectedPair()
{
    const int pairIndex = currentSelectedPairIndex();
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        QMessageBox::information(this, tr("未选择同步对"), tr("请先在表格中选择一组需要删除的同步对。"));
        return;
    }

    const QString pairText = buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex);
    _folderPairConfigs.removeAt(pairIndex);
    saveSettings();
    refreshPairTable();
    appendLog(tr("已删除同步对：%1").arg(pairText));

    if (_folderPairConfigs.isEmpty()) {
        if (_isMonitoring) {
            appendLog(tr("同步对列表已清空，自动停止监控。"));
            slotStopMonitoring();
        } else {
            _ui->statusLabel->setText(tr("状态：空闲"));
            _ui->detailLabel->setText(tr("请先新增至少一组同步对。"));
        }
        return;
    }

    selectPairRow(qMin(pairIndex, _folderPairConfigs.size() - 1));
    if (_isMonitoring) {
        refreshWatcher();
        if (buildEnabledPairIndexes().isEmpty()) {
            _ui->statusLabel->setText(tr("状态：监控中（无启用项）"));
            _ui->detailLabel->setText(tr("所有同步对都已暂停，当前不会自动同步。"));
        }
    }

    updateControlState();
}

void MainWindow::slotPairSelectionChanged()
{
    updateControlState();
}

void MainWindow::slotStartMonitoring()
{
    QString errorMessage;
    if (!validateConfiguration(&errorMessage)) {
        QMessageBox::warning(this, tr("无法启动监控"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    _isMonitoring = true;
    _scheduledReason.clear();
    _pendingReason.clear();
    _pendingSyncPairIndexes.clear();
    _hasPendingSync = false;
    refreshWatcher();
    _periodicCheckTimer->start();

    _ui->statusLabel->setText(tr("状态：监控中"));
    _ui->detailLabel->setText(tr("已启动全部监控，准备执行首次同步。"));
    appendLog(tr("已启动全部监控，后续会持续保持所有已启用的 B 目录与对应 A 目录一致。"));
    updateControlState();

    requestSyncAll(tr("启动监控后的首次同步"));
}

void MainWindow::slotStopMonitoring()
{
    _isMonitoring = false;
    _scheduledReason.clear();
    _pendingReason.clear();
    _pendingSyncPairIndexes.clear();
    _hasPendingSync = false;
    _debounceTimer->stop();
    _periodicCheckTimer->stop();
    if (_folderWatcher != nullptr) {
        _folderWatcher->clear();
    }

    if (!_isSyncRunning) {
        _ui->statusLabel->setText(tr("状态：监控已停止"));
        _ui->detailLabel->setText(tr("持续监控已关闭，可以继续手动同步任意同步对。"));
    }

    appendLog(tr("已停止监控。"));
    updateControlState();
}

void MainWindow::slotSyncAllPairs()
{
    QString errorMessage;
    if (!validateConfiguration(&errorMessage)) {
        QMessageBox::warning(this, tr("无法同步"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    requestSyncAll(tr("手动同步全部已启用同步对"));
}

void MainWindow::slotWatcherChanged(const QString &changedPath)
{
    if (!_isMonitoring) {
        return;
    }

    _scheduledReason = tr("检测到目录变化：%1").arg(changedPath);
    _debounceTimer->start();
    appendLog(tr("检测到目录变化，准备重新同步全部已启用同步对，path=%1").arg(changedPath));
}

void MainWindow::slotDebounceTimeout()
{
    const QString reason = _scheduledReason.isEmpty() ? tr("目录变化触发同步") : _scheduledReason;
    _scheduledReason.clear();
    requestSyncAll(reason);
}

void MainWindow::slotPeriodicCheck()
{
    if (_isMonitoring) {
        requestSyncAll(tr("周期校验"));
    }
}

void MainWindow::slotSyncStarted(int totalSteps,
                                 int removeFileCount,
                                 int addFileCount,
                                 int updateFileCount,
                                 const QString &reason)
{
    if (_currentSyncPairIndex < 0 || _currentSyncPairIndex >= _folderPairConfigs.size()) {
        return;
    }

    const QString pairText = buildPairDisplayText(_folderPairConfigs.at(_currentSyncPairIndex), _currentSyncPairIndex);
    const QString planPreviewText =
        tr("待删 %1 / 待增 %2 / 待同步 %3").arg(removeFileCount).arg(addFileCount).arg(updateFileCount);

    updatePairStatus(_currentSyncPairIndex, planPreviewText);
    updatePairProgress(_currentSyncPairIndex, 0, qMax(1, totalSteps));
    _ui->progressBar->setRange(0, qMax(1, totalSteps));
    _ui->progressBar->setValue(0);
    _ui->progressBar->setFormat(totalSteps == 0 ? tr("无需同步") : tr("%v/%m"));
    _ui->statusLabel->setText(
        tr("状态：正在同步第 %1/%2 组").arg(_currentBatchFinishedPairs + 1).arg(_currentBatchTotalPairs));
    _ui->detailLabel->setText(
        tr("当前同步对：%1\n触发原因：%2\n备份前检查：待删除文件 %3，待新增文件 %4，待同步文件 %5。")
            .arg(pairText)
            .arg(reason)
            .arg(removeFileCount)
            .arg(addFileCount)
            .arg(updateFileCount));
    appendLog(tr("开始同步：%1，待删除文件 %2，待新增文件 %3，待同步文件 %4。")
                  .arg(pairText)
                  .arg(removeFileCount)
                  .arg(addFileCount)
                  .arg(updateFileCount));
}

void MainWindow::slotSyncProgress(int currentStep, int totalSteps, const QString &currentItem)
{
    if (_currentSyncPairIndex < 0 || _currentSyncPairIndex >= _folderPairConfigs.size()) {
        return;
    }

    const QString pairText = buildPairDisplayText(_folderPairConfigs.at(_currentSyncPairIndex), _currentSyncPairIndex);
    updatePairProgress(_currentSyncPairIndex, currentStep, totalSteps);
    _ui->progressBar->setRange(0, qMax(1, totalSteps));
    _ui->progressBar->setValue(currentStep);
    _ui->progressBar->setFormat(tr("%v/%m"));
    _ui->detailLabel->setText(tr("第 %1/%2 组：%3\n%4")
                                  .arg(_currentBatchFinishedPairs + 1)
                                  .arg(_currentBatchTotalPairs)
                                  .arg(pairText)
                                  .arg(currentItem));
}

void MainWindow::slotSyncFinished(bool success, const QString &summary)
{
    if (_currentSyncPairIndex >= 0 && _currentSyncPairIndex < _folderPairConfigs.size()) {
        const QString pairText =
            buildPairDisplayText(_folderPairConfigs.at(_currentSyncPairIndex), _currentSyncPairIndex);
        const QString statusText =
            success ? (summary.contains(tr("无需同步")) ? tr("最近一次：已一致") : tr("最近一次：成功"))
                    : tr("最近一次：失败");
        updatePairStatus(_currentSyncPairIndex, statusText);
        updatePairProgress(_currentSyncPairIndex, 0, 1);
        appendLog(tr("同步对完成：%1，result=%2").arg(pairText, success ? tr("成功") : tr("失败")));
    }

    ++_currentBatchFinishedPairs;
    if (!success) {
        ++_currentBatchFailedPairs;
    }

    if (!_syncQueue.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            startNextPairSync();
        });
        return;
    }

    finishCurrentBatch();
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

    _pairTableModel = new QStandardItemModel(this);
    _pairTableModel->setColumnCount(4);
    _pairTableModel->setHorizontalHeaderLabels(
        QStringList{tr("A 主目录"), tr("B 备份目录"), tr("状态 / 进度"), tr("操作")});

    _ui->pairTableView->setModel(_pairTableModel);
    _ui->pairTableView->setWordWrap(false);
    _ui->pairTableView->setTextElideMode(Qt::ElideMiddle);
    _ui->pairTableView->setShowGrid(false);
    _ui->pairTableView->horizontalHeader()->setStretchLastSection(false);
    _ui->pairTableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    _ui->pairTableView->horizontalHeader()->setMinimumSectionSize(96);
    _ui->pairTableView->horizontalHeader()->setSectionResizeMode(kSourceColumn, QHeaderView::Stretch);
    _ui->pairTableView->horizontalHeader()->setSectionResizeMode(kTargetColumn, QHeaderView::Stretch);
    _ui->pairTableView->horizontalHeader()->setSectionResizeMode(kStatusColumn, QHeaderView::Interactive);
    _ui->pairTableView->horizontalHeader()->setSectionResizeMode(kActionColumn, QHeaderView::Fixed);
    _ui->pairTableView->setColumnWidth(kSourceColumn, kPathColumnMinimumWidth);
    _ui->pairTableView->setColumnWidth(kTargetColumn, kPathColumnMinimumWidth);
    _ui->pairTableView->setItemDelegateForColumn(kStatusColumn, new PairStatusDelegate(_ui->pairTableView));
    _ui->pairTableView->setColumnWidth(kStatusColumn, kStatusColumnWidth);
    _ui->pairTableView->setColumnWidth(kActionColumn, kActionColumnWidth);
    _ui->pairTableView->verticalHeader()->setVisible(false);
    _ui->pairTableView->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    _ui->pairTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    _ui->pairTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    _ui->pairTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _ui->pairTableView->setAlternatingRowColors(true);

    connect(_ui->addPairButton, &QPushButton::clicked, this, &MainWindow::slotAddPair);
    connect(_ui->removePairButton, &QPushButton::clicked, this, &MainWindow::slotRemoveSelectedPair);
    connect(_ui->pairTableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this]() { slotPairSelectionChanged(); });
    connect(_ui->startMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStartMonitoring);
    connect(_ui->stopMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStopMonitoring);
    connect(_ui->syncAllPairsButton, &QPushButton::clicked, this, &MainWindow::slotSyncAllPairs);
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

void MainWindow::loadSettings()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const int pairCount = settings.beginReadArray(QStringLiteral("folderPairs"));
    for (int index = 0; index < pairCount; ++index) {
        settings.setArrayIndex(index);

        FolderPairConfig pairConfig;
        pairConfig.sourcePath = normalizePath(settings.value(QStringLiteral("sourcePath")).toString());
        pairConfig.targetPath = normalizePath(settings.value(QStringLiteral("targetPath")).toString());
        pairConfig.statusText = tr("待同步");
        pairConfig.isSyncEnabled = settings.value(QStringLiteral("isSyncEnabled"), true).toBool();

        QString errorMessage;
        if (!validatePairConfig(pairConfig, -1, &errorMessage)) {
            appendLog(tr("已忽略无效历史配置，第 %1 组，error=%2").arg(index + 1).arg(errorMessage));
            continue;
        }

        _folderPairConfigs.append(pairConfig);
    }
    settings.endArray();

    refreshPairTable();
    if (_folderPairConfigs.isEmpty()) {
        _ui->statusLabel->setText(tr("状态：空闲"));
        _ui->detailLabel->setText(tr("请先新增至少一组同步对。"));
        return;
    }

    selectPairRow(0);
    _ui->statusLabel->setText(tr("状态：空闲"));
    _ui->detailLabel->setText(tr("已恢复 %1 组历史同步路径，可立即开始同步或启动监控。").arg(_folderPairConfigs.size()));
    appendLog(tr("已恢复 %1 组历史同步路径，配置文件：%2")
                  .arg(_folderPairConfigs.size())
                  .arg(QDir::toNativeSeparators(settingsFilePath())));
}

void MainWindow::saveSettings() const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("folderPairs"));
    settings.beginWriteArray(QStringLiteral("folderPairs"));
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("sourcePath"), _folderPairConfigs.at(index).sourcePath);
        settings.setValue(QStringLiteral("targetPath"), _folderPairConfigs.at(index).targetPath);
        settings.setValue(QStringLiteral("isSyncEnabled"), _folderPairConfigs.at(index).isSyncEnabled);
    }
    settings.endArray();
}

void MainWindow::refreshPairTable()
{
    const int currentIndex = currentSelectedPairIndex();
    const int rowToSelect = _folderPairConfigs.isEmpty() ? -1 : qBound(0, currentIndex, _folderPairConfigs.size() - 1);
    QItemSelectionModel *selectionModel = _ui->pairTableView->selectionModel();

    auto rebuildRows = [this, rowToSelect, selectionModel]() {
        _pairTableModel->setRowCount(0);

        for (int row = 0; row < _folderPairConfigs.size(); ++row) {
            const FolderPairConfig &pairConfig = _folderPairConfigs.at(row);
            const QString sourcePathText = QDir::toNativeSeparators(pairConfig.sourcePath);
            const QString targetPathText = QDir::toNativeSeparators(pairConfig.targetPath);

            auto *sourceItem = new QStandardItem(sourcePathText);
            auto *targetItem = new QStandardItem(targetPathText);
            auto *statusItem = new QStandardItem;
            auto *actionItem = new QStandardItem;

            sourceItem->setEditable(false);
            targetItem->setEditable(false);
            statusItem->setEditable(false);
            actionItem->setEditable(false);
            sourceItem->setToolTip(sourcePathText);
            targetItem->setToolTip(targetPathText);

            _pairTableModel->setItem(row, kSourceColumn, sourceItem);
            _pairTableModel->setItem(row, kTargetColumn, targetItem);
            _pairTableModel->setItem(row, kStatusColumn, statusItem);
            _pairTableModel->setItem(row, kActionColumn, actionItem);
            refreshStatusCell(row);
        }

        if (rowToSelect >= 0) {
            const QModelIndex currentModelIndex = _pairTableModel->index(rowToSelect, kSourceColumn);
            _ui->pairTableView->setCurrentIndex(currentModelIndex);
            _ui->pairTableView->selectRow(rowToSelect);
        } else if (selectionModel != nullptr) {
            selectionModel->clearSelection();
        }
    };

    if (selectionModel != nullptr) {
        const QSignalBlocker blocker(selectionModel);
        rebuildRows();
    } else {
        rebuildRows();
    }

    refreshActionWidgets();
    updateControlState();
}

void MainWindow::refreshActionWidgets()
{
    for (int row = 0; row < _pairTableModel->rowCount(); ++row) {
        const QModelIndex actionIndex = _pairTableModel->index(row, kActionColumn);
        if (!actionIndex.isValid()) {
            continue;
        }

        _ui->pairTableView->setIndexWidget(actionIndex, createActionWidget(row));
    }

    _ui->pairTableView->setColumnWidth(kSourceColumn, kPathColumnMinimumWidth);
    _ui->pairTableView->setColumnWidth(kTargetColumn, kPathColumnMinimumWidth);
    _ui->pairTableView->setColumnWidth(kStatusColumn, kStatusColumnWidth);
    _ui->pairTableView->setColumnWidth(kActionColumn, kActionColumnWidth);
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

    const QVector<int> enabledPairIndexes = buildEnabledPairIndexes();
    if (enabledPairIndexes.isEmpty()) {
        _folderWatcher->clear();
        return;
    }

    QStringList watchedFolders;
    watchedFolders.reserve(enabledPairIndexes.size() * 2);
    for (int pairIndex : enabledPairIndexes) {
        const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
        watchedFolders.append(pairConfig.sourcePath);
        watchedFolders.append(pairConfig.targetPath);
    }

    _folderWatcher->setWatchedFolders(watchedFolders);
}

void MainWindow::updateControlState()
{
    const bool canEdit = !_isSyncRunning;
    const bool hasPairs = !_folderPairConfigs.isEmpty();
    const bool hasSelection = currentSelectedPairIndex() >= 0;
    const bool hasEnabledPairs = !buildEnabledPairIndexes().isEmpty();

    _ui->pairTableView->setEnabled(canEdit);
    _ui->addPairButton->setEnabled(canEdit);
    _ui->removePairButton->setEnabled(canEdit && hasSelection);
    _ui->startMonitorButton->setEnabled(!_isMonitoring && !_isSyncRunning && hasEnabledPairs);
    _ui->stopMonitorButton->setEnabled(_isMonitoring);
    _ui->syncAllPairsButton->setEnabled(!_isSyncRunning && hasEnabledPairs);

    if (!hasPairs && !_isSyncRunning && !_isMonitoring) {
        _ui->statusLabel->setText(tr("状态：空闲"));
        _ui->detailLabel->setText(tr("请先新增至少一组同步对。"));
    }
}

void MainWindow::appendLog(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    _ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void MainWindow::refreshStatusCell(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    QStandardItem *statusItem = _pairTableModel->item(pairIndex, kStatusColumn);
    if (statusItem == nullptr) {
        return;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
    const int progressMaximum = qMax(1, pairConfig.progressMaximum);
    const int progressValue = qBound(0, pairConfig.progressValue, progressMaximum);
    const QString stateText = buildPairStateText(pairConfig);
    const QString tooltipText = tr("%1\n进度：%2/%3").arg(stateText).arg(progressValue).arg(progressMaximum);

    statusItem->setData(stateText, Qt::DisplayRole);
    statusItem->setData(progressValue, E_ProgressValueRole);
    statusItem->setData(progressMaximum, E_ProgressMaximumRole);
    statusItem->setData(pairConfig.isSyncEnabled, E_IsSyncEnabledRole);
    statusItem->setToolTip(tooltipText);
}

void MainWindow::updatePairStatus(int pairIndex, const QString &statusText)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    _folderPairConfigs[pairIndex].statusText = statusText;
    refreshStatusCell(pairIndex);
}

void MainWindow::updatePairProgress(int pairIndex, int progressValue, int progressMaximum)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    _folderPairConfigs[pairIndex].progressMaximum = qMax(1, progressMaximum);
    _folderPairConfigs[pairIndex].progressValue =
        qBound(0, progressValue, _folderPairConfigs[pairIndex].progressMaximum);
    refreshStatusCell(pairIndex);
}

void MainWindow::requestSyncAll(const QString &reason)
{
    requestSyncByIndexes(buildEnabledPairIndexes(), reason);
}

void MainWindow::requestSyncByIndexes(const QVector<int> &pairIndexes, const QString &reason)
{
    const QVector<int> normalizedIndexes = normalizePairIndexes(pairIndexes);
    if (normalizedIndexes.isEmpty()) {
        _ui->progressBar->setRange(0, 1);
        _ui->progressBar->setValue(0);
        _ui->progressBar->setFormat(tr("待同步"));
        if (_isMonitoring) {
            _ui->statusLabel->setText(tr("状态：监控中（无启用项）"));
            _ui->detailLabel->setText(tr("当前没有已启用的同步对。"));
        } else {
            _ui->statusLabel->setText(tr("状态：空闲"));
            _ui->detailLabel->setText(tr("当前没有可同步的配置，可通过“恢复同步”重新启用某一行。"));
        }
        updateControlState();
        return;
    }

    if (_isSyncRunning) {
        _hasPendingSync = true;
        for (int pairIndex : normalizedIndexes) {
            _pendingSyncPairIndexes.insert(pairIndex);
        }
        _pendingReason = reason;
        appendLog(tr("当前仍有同步任务在执行，已合并新的请求，reason=%1，count=%2")
                      .arg(reason)
                      .arg(normalizedIndexes.size()));
        return;
    }

    _syncQueue = normalizedIndexes;
    _currentBatchReason = reason;
    _currentBatchTotalPairs = _syncQueue.size();
    _currentBatchFinishedPairs = 0;
    _currentBatchFailedPairs = 0;
    _currentSyncPairIndex = -1;
    _isSyncRunning = true;

    for (int pairIndex : _syncQueue) {
        updatePairProgress(pairIndex, 0, 1);
        updatePairStatus(pairIndex, tr("排队中"));
    }

    updateControlState();
    _ui->statusLabel->setText(tr("状态：准备同步"));
    _ui->detailLabel->setText(tr("本轮共 %1 组，触发原因：%2").arg(_currentBatchTotalPairs).arg(reason));
    _ui->progressBar->setRange(0, 0);
    _ui->progressBar->setFormat(tr("准备中..."));
    startNextPairSync();
}

void MainWindow::startNextPairSync()
{
    if (_syncQueue.isEmpty()) {
        finishCurrentBatch();
        return;
    }

    _currentSyncPairIndex = _syncQueue.takeFirst();
    if (_currentSyncPairIndex < 0 || _currentSyncPairIndex >= _folderPairConfigs.size()) {
        ++_currentBatchFinishedPairs;
        ++_currentBatchFailedPairs;
        QTimer::singleShot(0, this, [this]() {
            startNextPairSync();
        });
        return;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(_currentSyncPairIndex);
    updatePairStatus(_currentSyncPairIndex, tr("等待执行"));
    emit sigStartSync(pairConfig.sourcePath,
                      pairConfig.targetPath,
                      tr("%1 | 第 %2/%3 组")
                          .arg(_currentBatchReason)
                          .arg(_currentBatchFinishedPairs + 1)
                          .arg(_currentBatchTotalPairs));
}

void MainWindow::finishCurrentBatch()
{
    const bool allSuccess = _currentBatchFailedPairs == 0;
    const QString batchSummary = buildBatchSummary();

    _isSyncRunning = false;
    _currentSyncPairIndex = -1;
    _syncQueue.clear();

    _ui->progressBar->setRange(0, 1);
    _ui->progressBar->setValue(allSuccess ? 1 : 0);
    _ui->progressBar->setFormat(allSuccess ? tr("完成") : tr("存在失败"));
    _ui->statusLabel->setText(allSuccess
                                  ? (_isMonitoring ? tr("状态：监控中") : tr("状态：同步完成"))
                                  : (_isMonitoring ? tr("状态：监控中（部分失败）")
                                                   : tr("状态：同步完成（部分失败）")));
    _ui->detailLabel->setText(batchSummary);
    appendLog(batchSummary);

    if (_isMonitoring) {
        refreshWatcher();
        if (buildEnabledPairIndexes().isEmpty()) {
            _ui->statusLabel->setText(tr("状态：监控中（无启用项）"));
            _ui->detailLabel->setText(tr("所有同步对都已暂停，当前不会自动同步。"));
        }
    }

    updateControlState();

    if (_hasPendingSync) {
        QVector<int> pendingIndexes;
        pendingIndexes.reserve(_pendingSyncPairIndexes.size());
        for (int pairIndex : _pendingSyncPairIndexes) {
            pendingIndexes.append(pairIndex);
        }

        const QString pendingReason = _pendingReason.isEmpty() ? tr("补偿同步") : _pendingReason;
        _hasPendingSync = false;
        _pendingReason.clear();
        _pendingSyncPairIndexes.clear();

        QTimer::singleShot(200, this, [this, pendingIndexes, pendingReason]() {
            requestSyncByIndexes(pendingIndexes, pendingReason);
        });
    }
}

QVector<int> MainWindow::buildAllPairIndexes() const
{
    QVector<int> pairIndexes;
    pairIndexes.reserve(_folderPairConfigs.size());
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        pairIndexes.append(index);
    }
    return pairIndexes;
}

QVector<int> MainWindow::buildEnabledPairIndexes() const
{
    QVector<int> pairIndexes;
    pairIndexes.reserve(_folderPairConfigs.size());
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        if (_folderPairConfigs.at(index).isSyncEnabled) {
            pairIndexes.append(index);
        }
    }
    return pairIndexes;
}

QVector<int> MainWindow::normalizePairIndexes(const QVector<int> &pairIndexes) const
{
    QSet<int> uniqueIndexes;
    for (int pairIndex : pairIndexes) {
        if (pairIndex >= 0 && pairIndex < _folderPairConfigs.size()) {
            uniqueIndexes.insert(pairIndex);
        }
    }

    QVector<int> normalizedIndexes;
    normalizedIndexes.reserve(uniqueIndexes.size());
    for (int pairIndex : uniqueIndexes) {
        normalizedIndexes.append(pairIndex);
    }

    std::sort(normalizedIndexes.begin(), normalizedIndexes.end());
    return normalizedIndexes;
}

int MainWindow::currentSelectedPairIndex() const
{
    if (_ui->pairTableView->selectionModel() == nullptr) {
        return -1;
    }

    const QModelIndexList selectedRows = _ui->pairTableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        return -1;
    }

    return selectedRows.first().row();
}

void MainWindow::selectPairRow(int pairIndex) const
{
    if (pairIndex < 0 || pairIndex >= _pairTableModel->rowCount()) {
        return;
    }

    const QModelIndex currentModelIndex = _pairTableModel->index(pairIndex, kSourceColumn);
    if (!currentModelIndex.isValid()) {
        return;
    }

    _ui->pairTableView->setCurrentIndex(currentModelIndex);
    _ui->pairTableView->selectRow(pairIndex);
    _ui->pairTableView->scrollTo(currentModelIndex);
}

void MainWindow::addPair()
{
    FolderPairConfig pairConfig;
    const int newPairIndex = static_cast<int>(_folderPairConfigs.size());
    pairConfig.statusText = tr("待同步");
    pairConfig.isSyncEnabled = true;
    if (!editPairConfig(&pairConfig, -1, tr("新增同步对"))) {
        return;
    }

    _folderPairConfigs.append(pairConfig);
    saveSettings();
    refreshPairTable();
    selectPairRow(newPairIndex);

    const QString pairText = buildPairDisplayText(pairConfig, newPairIndex);
    appendLog(tr("已新增同步对：%1").arg(pairText));

    if (_isMonitoring) {
        refreshWatcher();
        requestSyncByIndexes(QVector<int>{newPairIndex}, tr("监控中新增同步对"));
    } else {
        _ui->statusLabel->setText(tr("状态：空闲"));
        _ui->detailLabel->setText(tr("已新增一组同步对，可继续添加，或直接执行同步。"));
    }

    updateControlState();
}

void MainWindow::editPair(int pairIndex)
{
    if (_isSyncRunning || pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    FolderPairConfig updatedConfig = _folderPairConfigs.at(pairIndex);
    const FolderPairConfig originalConfig = updatedConfig;
    if (!editPairConfig(&updatedConfig, pairIndex, tr("编辑同步对"))) {
        return;
    }

    const bool pathChanged = updatedConfig.sourcePath != originalConfig.sourcePath
        || updatedConfig.targetPath != originalConfig.targetPath;
    if (pathChanged) {
        updatedConfig.statusText = tr("待同步");
        updatedConfig.progressValue = 0;
        updatedConfig.progressMaximum = 1;
    }

    _folderPairConfigs[pairIndex] = updatedConfig;
    saveSettings();
    refreshPairTable();
    selectPairRow(pairIndex);

    appendLog(tr("已更新同步对：%1").arg(buildPairDisplayText(updatedConfig, pairIndex)));

    if (_isMonitoring) {
        refreshWatcher();
        if (updatedConfig.isSyncEnabled) {
            requestSyncByIndexes(QVector<int>{pairIndex}, tr("监控中编辑同步对后重新同步"));
        } else if (buildEnabledPairIndexes().isEmpty()) {
            _ui->statusLabel->setText(tr("状态：监控中（无启用项）"));
            _ui->detailLabel->setText(tr("所有同步对都已暂停，当前不会自动同步。"));
        }
    } else {
        _ui->statusLabel->setText(tr("状态：空闲"));
        _ui->detailLabel->setText(tr("同步对已更新，可直接用行内按钮发起同步。"));
    }

    updateControlState();
}

void MainWindow::syncPair(int pairIndex)
{
    if (_isSyncRunning || pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    selectPairRow(pairIndex);
    requestSyncByIndexes(
        QVector<int>{pairIndex},
        _folderPairConfigs.at(pairIndex).isSyncEnabled ? tr("手动同步单组") : tr("手动同步单组（已暂停自动同步）"));
}

void MainWindow::togglePairSync(int pairIndex)
{
    if (_isSyncRunning || pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    FolderPairConfig &pairConfig = _folderPairConfigs[pairIndex];
    pairConfig.isSyncEnabled = !pairConfig.isSyncEnabled;
    saveSettings();
    updatePairStatus(pairIndex, pairConfig.statusText);
    refreshActionWidgets();
    selectPairRow(pairIndex);

    const QString pairText = buildPairDisplayText(pairConfig, pairIndex);
    appendLog(pairConfig.isSyncEnabled ? tr("已恢复同步：%1").arg(pairText) : tr("已取消同步：%1").arg(pairText));

    if (_isMonitoring) {
        refreshWatcher();
        if (pairConfig.isSyncEnabled) {
            requestSyncByIndexes(QVector<int>{pairIndex}, tr("恢复同步后校正"));
        } else if (buildEnabledPairIndexes().isEmpty()) {
            _ui->statusLabel->setText(tr("状态：监控中（无启用项）"));
            _ui->detailLabel->setText(tr("所有同步对都已暂停，当前不会自动同步。"));
        } else {
            _ui->statusLabel->setText(tr("状态：监控中"));
            _ui->detailLabel->setText(tr("已暂停当前同步对的自动同步，其余已启用项仍继续监控。"));
        }
    } else {
        _ui->statusLabel->setText(tr("状态：空闲"));
        _ui->detailLabel->setText(pairConfig.isSyncEnabled ? tr("该同步对已恢复参与“同步全部”和自动监控。")
                                                           : tr("该同步对已暂停，不再参与“同步全部”和自动监控。"));
    }

    updateControlState();
}

QWidget *MainWindow::createActionWidget(int pairIndex)
{
    auto *container = new QWidget(_ui->pairTableView);
    auto *layout = new QHBoxLayout(container);
    auto *editButton = new QPushButton(tr("编辑"), container);
    auto *syncButton = new QPushButton(tr("同步"), container);
    auto *toggleButton =
        new QPushButton(_folderPairConfigs.at(pairIndex).isSyncEnabled ? tr("取消同步") : tr("恢复同步"), container);

    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);
    layout->addWidget(editButton);
    layout->addWidget(syncButton);
    layout->addWidget(toggleButton);
    layout->addStretch();

    editButton->setCursor(Qt::PointingHandCursor);
    syncButton->setCursor(Qt::PointingHandCursor);
    toggleButton->setCursor(Qt::PointingHandCursor);
    editButton->setStyleSheet(buildActionButtonStyle(QColor(85, 124, 214)));
    syncButton->setStyleSheet(buildActionButtonStyle(QColor(48, 152, 98)));
    toggleButton->setStyleSheet(buildActionButtonStyle(_folderPairConfigs.at(pairIndex).isSyncEnabled
                                                           ? QColor(221, 110, 54)
                                                           : QColor(116, 122, 132)));
    editButton->setToolTip(tr("编辑这一组 A/B 路径"));
    syncButton->setToolTip(tr("立即同步这一组路径"));
    toggleButton->setToolTip(_folderPairConfigs.at(pairIndex).isSyncEnabled ? tr("暂停这一组的自动同步和“同步全部”")
                                                                            : tr("恢复这一组的自动同步和“同步全部”"));
    editButton->setEnabled(!_isSyncRunning);
    syncButton->setEnabled(!_isSyncRunning);
    toggleButton->setEnabled(!_isSyncRunning);

    connect(editButton, &QPushButton::clicked, container, [this, pairIndex]() {
        editPair(pairIndex);
    });
    connect(syncButton, &QPushButton::clicked, container, [this, pairIndex]() {
        syncPair(pairIndex);
    });
    connect(toggleButton, &QPushButton::clicked, container, [this, pairIndex]() {
        togglePairSync(pairIndex);
    });

    return container;
}

QString MainWindow::buildPairStateText(const FolderPairConfig &pairConfig) const
{
    const QString enableText = pairConfig.isSyncEnabled ? tr("已启用") : tr("已暂停");
    const QString statusText = pairConfig.statusText.isEmpty() ? tr("待同步") : pairConfig.statusText;
    return tr("%1 | %2").arg(enableText, statusText);
}

QString MainWindow::buildPairDisplayText(const FolderPairConfig &pairConfig, int pairIndex) const
{
    return tr("第 %1 组 [%2 -> %3]")
        .arg(pairIndex + 1)
        .arg(QDir::toNativeSeparators(pairConfig.sourcePath))
        .arg(QDir::toNativeSeparators(pairConfig.targetPath));
}

QString MainWindow::buildBatchSummary() const
{
    const int successCount = _currentBatchTotalPairs - _currentBatchFailedPairs;
    return tr("本轮同步结束，触发原因：%1。共 %2 组：成功 %3，失败 %4。")
        .arg(_currentBatchReason)
        .arg(_currentBatchTotalPairs)
        .arg(successCount)
        .arg(_currentBatchFailedPairs);
}

QString MainWindow::normalizePath(const QString &path) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

bool MainWindow::editPairConfig(FolderPairConfig *pairConfig, int ignoredIndex, const QString &windowTitle) const
{
    if (pairConfig == nullptr) {
        return false;
    }

    PairEditDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(windowTitle);
    dialog.setSourcePath(pairConfig->sourcePath);
    dialog.setTargetPath(pairConfig->targetPath);

    while (dialog.exec() == QDialog::Accepted) {
        FolderPairConfig updatedConfig = *pairConfig;
        updatedConfig.sourcePath = normalizePath(dialog.sourcePath());
        updatedConfig.targetPath = normalizePath(dialog.targetPath());
        if (updatedConfig.statusText.isEmpty()) {
            updatedConfig.statusText = tr("待同步");
        }

        QString errorMessage;
        if (validatePairConfig(updatedConfig, ignoredIndex, &errorMessage)) {
            *pairConfig = updatedConfig;
            return true;
        }

        QMessageBox::warning(const_cast<MainWindow *>(this), tr("配置无效"), errorMessage);
    }

    return false;
}

bool MainWindow::validatePairConfig(const FolderPairConfig &pairConfig,
                                    int ignoredIndex,
                                    QString *errorMessage) const
{
    if (pairConfig.sourcePath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请先填写 A 主目录。");
        }
        return false;
    }

    if (pairConfig.targetPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请先填写 B 备份目录。");
        }
        return false;
    }

    const QFileInfo sourceInfo(pairConfig.sourcePath);
    if (sourceInfo.exists() && !sourceInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("A 主路径已存在但不是目录，path=%1").arg(pairConfig.sourcePath);
        }
        return false;
    }

    const QFileInfo targetInfo(pairConfig.targetPath);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("B 备份路径已存在但不是目录，path=%1").arg(pairConfig.targetPath);
        }
        return false;
    }

    if (isSameOrNestedPath(pairConfig.sourcePath, pairConfig.targetPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("同一组中的 A 和 B 不能相同，也不能互相嵌套，source=%1，target=%2")
                                .arg(pairConfig.sourcePath, pairConfig.targetPath);
        }
        return false;
    }

    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        if (index == ignoredIndex) {
            continue;
        }

        const FolderPairConfig &existingPair = _folderPairConfigs.at(index);
        const bool targetConflict = isSameOrNestedPath(pairConfig.targetPath, existingPair.targetPath)
            || isSameOrNestedPath(pairConfig.targetPath, existingPair.sourcePath)
            || isSameOrNestedPath(pairConfig.sourcePath, existingPair.targetPath);
        if (!targetConflict) {
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage =
                tr("为避免多组同步互相影响，任意备份目录都不能与其他同步对的主目录或备份目录重叠。"
                   "currentSource=%1，currentTarget=%2，otherSource=%3，otherTarget=%4")
                    .arg(pairConfig.sourcePath, pairConfig.targetPath, existingPair.sourcePath, existingPair.targetPath);
        }
        return false;
    }

    return true;
}

bool MainWindow::validateConfiguration(QString *errorMessage) const
{
    if (_folderPairConfigs.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请至少新增一组同步对。");
        }
        return false;
    }

    if (buildEnabledPairIndexes().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("当前没有已启用的同步对，请先恢复至少一组后再执行该操作。");
        }
        return false;
    }

    return true;
}
