#include "MainWindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRectF>
#include <QSet>
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
constexpr int kDefaultMaxParallelSyncCount = 2;

enum class ButtonStyleKind
{
    E_Primary,
    E_Success,
    E_Warning,
    E_Danger,
    E_Neutral
};

bool isDarkPalette(const QPalette &palette);

QColor buildButtonBaseColor(ButtonStyleKind buttonStyleKind, const QPalette &palette)
{
    const bool darkTheme = isDarkPalette(palette);
    switch (buttonStyleKind) {
    case ButtonStyleKind::E_Primary:
        return darkTheme ? QColor(76, 132, 255) : QColor(85, 124, 214);
    case ButtonStyleKind::E_Success:
        return darkTheme ? QColor(56, 170, 114) : QColor(48, 152, 98);
    case ButtonStyleKind::E_Warning:
        return darkTheme ? QColor(218, 132, 69) : QColor(221, 110, 54);
    case ButtonStyleKind::E_Danger:
        return darkTheme ? QColor(207, 92, 92) : QColor(202, 77, 77);
    case ButtonStyleKind::E_Neutral:
        return darkTheme ? QColor(98, 108, 120) : QColor(116, 122, 132);
    }

    return darkTheme ? QColor(98, 108, 120) : QColor(116, 122, 132);
}

QString buildActionButtonStyle(ButtonStyleKind buttonStyleKind, const QPalette &palette)
{
    const QColor baseColor = buildButtonBaseColor(buttonStyleKind, palette);
    const QColor hoverColor = baseColor.lighter(108);
    const QColor pressedColor = baseColor.darker(108);
    const bool darkTheme = isDarkPalette(palette);
    const QColor disabledBg = darkTheme ? QColor(76, 83, 92) : QColor(184, 193, 203);
    const QColor disabledFg = darkTheme ? QColor(170, 178, 186) : QColor(221, 227, 234);
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
               " color: %4;"
               " background-color: %5;"
               "}")
        .arg(baseColor.name(), hoverColor.name(), pressedColor.name(), disabledFg.name(), disabledBg.name());
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

QString stripProgressSuffix(const QString &statusText)
{
    auto stripByParentheses = [&statusText](QChar openParenthesis, QChar closeParenthesis) {
        const int openIndex = statusText.lastIndexOf(openParenthesis);
        if (openIndex < 0 || !statusText.endsWith(closeParenthesis)) {
            return statusText;
        }

        const QString progressText =
            statusText.mid(openIndex + 1, statusText.size() - openIndex - 2).trimmed();
        const QStringList progressParts = progressText.split(QLatin1Char('/'));
        if (progressParts.size() != 2) {
            return statusText;
        }

        bool isCurrentStepValid = false;
        bool isTotalStepValid = false;
        progressParts.at(0).trimmed().toLongLong(&isCurrentStepValid);
        progressParts.at(1).trimmed().toLongLong(&isTotalStepValid);
        if (!isCurrentStepValid || !isTotalStepValid) {
            return statusText;
        }

        return statusText.left(openIndex).trimmed();
    };

    const QString fullWidthResult = stripByParentheses(QChar(0xff08), QChar(0xff09));
    if (fullWidthResult != statusText) {
        return fullWidthResult;
    }

    return stripByParentheses(QLatin1Char('('), QLatin1Char(')'));
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
        const qint64 rawProgressMaximum = index.data(E_ProgressMaximumRole).toLongLong();
        const bool isBusyProgress = rawProgressMaximum <= 0;
        const qint64 progressMaximum = isBusyProgress ? 0 : rawProgressMaximum;
        const qint64 progressValue = isBusyProgress
            ? 0
            : qBound<qint64>(0, index.data(E_ProgressValueRole).toLongLong(), progressMaximum);
        const bool isSyncEnabled = index.data(E_IsSyncEnabledRole).toBool();
        const QColor progressColor = buildProgressBarColor(text, isSyncEnabled, viewOption.palette);
        const QColor textColor = option.state & QStyle::State_Selected
            ? viewOption.palette.color(QPalette::HighlightedText)
            : buildStatusTextColor(text, isSyncEnabled, viewOption.palette);
        const QRect contentRect = option.rect.adjusted(6, 4, -6, -4);
        const bool shouldShowPercent = !isBusyProgress && (progressMaximum > 1 || progressValue > 0);
        const int progressWidth = qMin(110, qMax(82, contentRect.width() / 4));
        const int percentWidth = shouldShowPercent ? 42 : 0;
        const int progressHeight = 10;
        const int progressTop = contentRect.top() + (contentRect.height() - progressHeight) / 2;
        const QRect progressRect(contentRect.left(), progressTop, progressWidth, progressHeight);
        const QRect percentRect(progressRect.right() + 6, contentRect.top(), percentWidth, contentRect.height());
        const QRect textRect = contentRect.adjusted(progressWidth + percentWidth + 12, 0, 0, 0);
        const int percentValue = shouldShowPercent
            ? qBound(0,
                     qRound(static_cast<double>(progressValue) * 100.0 / static_cast<double>(progressMaximum)),
                     100)
            : 0;
        const QString percentText = QStringLiteral("%1%").arg(percentValue);

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

        if (isBusyProgress) {
            QRectF busyFillRect = progressTrackRect.adjusted(1.0, 1.0, -1.0, -1.0);
            const qreal busyWidth = qMin(progressTrackRect.width(),
                                         qMax<qreal>(18.0, progressTrackRect.width() * 0.42));
            busyFillRect.setWidth(busyWidth);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(progressColor.red(), progressColor.green(), progressColor.blue(), 170));
            painter->drawRoundedRect(busyFillRect, 6, 6);
        } else if (progressValue > 0) {
            QRectF progressFillRect = progressTrackRect;
            const qreal fillWidth = progressTrackRect.width() * static_cast<qreal>(progressValue)
                / static_cast<qreal>(progressMaximum);
            progressFillRect.setWidth(qMax<qreal>(1.0, fillWidth));
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(progressColor.red(), progressColor.green(), progressColor.blue(), 200));
            painter->drawRoundedRect(progressFillRect, 6, 6);
        }

        if (shouldShowPercent) {
            QFont percentFont = viewOption.font;
            percentFont.setPointSizeF(qMax(8.0, percentFont.pointSizeF() - 1.0));
            painter->setFont(percentFont);
            painter->setPen(textColor);
            painter->drawText(percentRect, Qt::AlignVCenter | Qt::AlignLeft, percentText);
            painter->setFont(viewOption.font);
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
      _folderWatcher(nullptr),
      _isMonitoring(false),
      _isFastCompareEnabled(true),
      _maxParallelSyncCount(kDefaultMaxParallelSyncCount)
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

    loadSettings();
    updateControlState();

    if (_folderPairConfigs.isEmpty()) {
        appendLog(tr("程序已启动，请先新增至少一组同步对。"));
    }
}

MainWindow::~MainWindow()
{
    saveSettings();

    for (auto it = _runningSyncContexts.begin(); it != _runningSyncContexts.end(); ++it) {
        if (it.value().thread != nullptr && it.value().thread->isRunning()) {
            it.value().thread->quit();
            it.value().thread->wait();
        }
    }

    delete _ui;
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);

    if (event == nullptr) {
        return;
    }

    if (event->type() != QEvent::PaletteChange && event->type() != QEvent::ApplicationPaletteChange
        && event->type() != QEvent::StyleChange) {
        return;
    }

    if (_ui == nullptr || _ui->addPairButton == nullptr) {
        return;
    }

    applyButtonStyles();
    if (_ui != nullptr && _ui->pairTableView != nullptr) {
        refreshActionWidgets();
        _ui->pairTableView->viewport()->update();
    }
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

    if (runningSyncCount() > 0) {
        QMessageBox::information(this, tr("无法删除"), tr("存在正在执行的同步任务时，暂不支持删除同步对。"));
        return;
    }

    const QString pairText = buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex);
    removePairIndexFromQueues(pairIndex);
    _folderPairConfigs.removeAt(pairIndex);
    saveSettings();
    refreshPairTable();
    appendLog(tr("已删除同步对：%1").arg(pairText));

    if (_folderPairConfigs.isEmpty()) {
        if (_isMonitoring) {
            appendLog(tr("同步对列表已清空，自动停止监控。"));
            slotStopMonitoring();
        }
        return;
    }

    selectPairRow(qMin(pairIndex, _folderPairConfigs.size() - 1));
    if (_isMonitoring) {
        refreshWatcher();
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
    appendLog(tr("已启动全部监控，后续会持续保持所有已启用的 B 目录与对应 A 目录一致。"));
    updateControlState();
    QTimer::singleShot(0, this, [this]() {
        if (!_isMonitoring) {
            return;
        }

        refreshWatcher();
        _periodicCheckTimer->start();
        requestSyncAll(tr("启动监控后的首次同步"));
    });
}

void MainWindow::slotStopMonitoring()
{
    _isMonitoring = false;
    _scheduledReason.clear();
    _debounceTimer->stop();
    _periodicCheckTimer->stop();
    if (_folderWatcher != nullptr) {
        _folderWatcher->clear();
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

    const QVector<int> enabledPairIndexes = buildEnabledPairIndexes();
    const int disabledPairCount = _folderPairConfigs.size() - enabledPairIndexes.size();
    appendLog(tr("手动同步全部：本次纳入 %1 组已启用同步对，跳过 %2 组已暂停同步对。")
                  .arg(enabledPairIndexes.size())
                  .arg(disabledPairCount));
    appendLog(tr("手动同步全部目录：%1").arg(buildPairListSummaryText(enabledPairIndexes)));

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

void MainWindow::buildUi()
{
    _ui->setupUi(this);
    _ui->maxParallelSyncComboBox->addItem(tr("并发 1 组"), 1);
    _ui->maxParallelSyncComboBox->addItem(tr("并发 2 组"), 2);
    _ui->maxParallelSyncComboBox->addItem(tr("并发 3 组"), 3);
    _ui->maxParallelSyncComboBox->addItem(tr("并发 4 组"), 4);
    _ui->maxParallelSyncComboBox->setToolTip(tr("限制同步全部时同时运行的任务数，机械硬盘或移动盘建议 1-2。"));
    _ui->fastCompareCheckBox->setToolTip(
        tr("启用后，文件大小和修改时间一致时跳过全文读取；适合大文件多的目录。"));

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
    _ui->pairTableView->setAlternatingRowColors(false);
    applyButtonStyles();

    connect(_ui->addPairButton, &QPushButton::clicked, this, &MainWindow::slotAddPair);
    connect(_ui->removePairButton, &QPushButton::clicked, this, &MainWindow::slotRemoveSelectedPair);
    connect(_ui->pairTableView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this]() { slotPairSelectionChanged(); });
    connect(_ui->startMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStartMonitoring);
    connect(_ui->stopMonitorButton, &QPushButton::clicked, this, &MainWindow::slotStopMonitoring);
    connect(_ui->syncAllPairsButton, &QPushButton::clicked, this, &MainWindow::slotSyncAllPairs);
    connect(_ui->maxParallelSyncComboBox,
            &QComboBox::currentIndexChanged,
            this,
            [this](int index) {
                const int selectedCount = _ui->maxParallelSyncComboBox->itemData(index).toInt();
                _maxParallelSyncCount = qBound(1, selectedCount, 4);
                saveSettings();
                appendLog(tr("已设置最大并发同步数：%1 组。").arg(_maxParallelSyncCount));
                startQueuedSyncs();
                refreshActionWidgets();
                updateControlState();
            });
    connect(_ui->fastCompareCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        _isFastCompareEnabled = checked;
        saveSettings();
        appendLog(checked ? tr("已启用快速比对模式。") : tr("已关闭快速比对模式。"));
    });
}

void MainWindow::applyButtonStyles()
{
    if (_ui == nullptr || _ui->addPairButton == nullptr) {
        return;
    }

    const QPalette palette = this->palette();
    _ui->addPairButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->removePairButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Danger, palette));
    _ui->startMonitorButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->stopMonitorButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Warning, palette));
    _ui->syncAllPairsButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Success, palette));

    _ui->addPairButton->setCursor(Qt::PointingHandCursor);
    _ui->removePairButton->setCursor(Qt::PointingHandCursor);
    _ui->startMonitorButton->setCursor(Qt::PointingHandCursor);
    _ui->stopMonitorButton->setCursor(Qt::PointingHandCursor);
    _ui->syncAllPairsButton->setCursor(Qt::PointingHandCursor);
    _ui->maxParallelSyncComboBox->setCursor(Qt::PointingHandCursor);
    _ui->fastCompareCheckBox->setCursor(Qt::PointingHandCursor);
}

void MainWindow::loadSettings()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    _isFastCompareEnabled = settings.value(QStringLiteral("syncOptions/fastCompareEnabled"), true).toBool();
    _maxParallelSyncCount =
        qBound(1, settings.value(QStringLiteral("syncOptions/maxParallelSyncCount"),
                                 kDefaultMaxParallelSyncCount)
                      .toInt(),
               4);
    {
        const QSignalBlocker blocker(_ui->maxParallelSyncComboBox);
        const int maxParallelIndex = _ui->maxParallelSyncComboBox->findData(_maxParallelSyncCount);
        _ui->maxParallelSyncComboBox->setCurrentIndex(maxParallelIndex >= 0 ? maxParallelIndex : 1);
    }
    {
        const QSignalBlocker blocker(_ui->fastCompareCheckBox);
        _ui->fastCompareCheckBox->setChecked(_isFastCompareEnabled);
    }

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
    if (!_folderPairConfigs.isEmpty()) {
        selectPairRow(0);
        appendLog(tr("已恢复 %1 组历史同步路径，配置文件：%2")
                      .arg(_folderPairConfigs.size())
                      .arg(QDir::toNativeSeparators(settingsFilePath())));
    }
}

void MainWindow::saveSettings() const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("syncOptions/fastCompareEnabled"), _isFastCompareEnabled);
    settings.setValue(QStringLiteral("syncOptions/maxParallelSyncCount"), _maxParallelSyncCount);
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
    watchedFolders.reserve(enabledPairIndexes.size());
    for (int pairIndex : enabledPairIndexes) {
        const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
        // 只监听 A 主目录，避免程序同步 B 时反复触发自身监控。
        watchedFolders.append(pairConfig.sourcePath);
    }

    _folderWatcher->setWatchedFolders(watchedFolders);
}

void MainWindow::updateControlState()
{
    const bool hasPairs = !_folderPairConfigs.isEmpty();
    const bool hasSelection = currentSelectedPairIndex() >= 0;
    const bool hasEnabledPairs = !buildEnabledPairIndexes().isEmpty();
    const bool hasRunningSync = runningSyncCount() > 0;

    _ui->pairTableView->setEnabled(hasPairs);
    _ui->addPairButton->setEnabled(true);
    _ui->removePairButton->setEnabled(!hasRunningSync && hasSelection);
    _ui->startMonitorButton->setEnabled(!_isMonitoring && hasEnabledPairs);
    _ui->stopMonitorButton->setEnabled(_isMonitoring);
    _ui->syncAllPairsButton->setEnabled(hasEnabledPairs);
}

void MainWindow::appendLog(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    _ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void MainWindow::removePairIndexFromQueues(int pairIndex)
{
    if (pairIndex < 0) {
        return;
    }

    QHash<int, QString> shiftedReasons;
    for (auto it = _pendingSyncReasons.cbegin(); it != _pendingSyncReasons.cend(); ++it) {
        if (it.key() == pairIndex) {
            continue;
        }

        shiftedReasons.insert(it.key() > pairIndex ? it.key() - 1 : it.key(), it.value());
    }
    _pendingSyncReasons = shiftedReasons;
}

void MainWindow::startQueuedSyncs()
{
    if (_pendingSyncReasons.isEmpty()) {
        return;
    }

    const int availableSyncCount = maxParallelSyncCount() - runningSyncCount();
    if (availableSyncCount <= 0) {
        return;
    }

    QVector<int> pendingPairIndexes;
    pendingPairIndexes.reserve(_pendingSyncReasons.size());
    for (auto it = _pendingSyncReasons.cbegin(); it != _pendingSyncReasons.cend(); ++it) {
        pendingPairIndexes.append(it.key());
    }
    std::sort(pendingPairIndexes.begin(), pendingPairIndexes.end());

    int startedPairCount = 0;
    for (int pairIndex : pendingPairIndexes) {
        if (startedPairCount >= availableSyncCount) {
            break;
        }

        if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size() || isPairSyncRunning(pairIndex)) {
            continue;
        }

        const QString pendingReason = _pendingSyncReasons.take(pairIndex);
        startPairSync(pairIndex, pendingReason);
        ++startedPairCount;
    }
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
    const qint64 progressMaximum = pairConfig.progressMaximum <= 0 ? 0 : pairConfig.progressMaximum;
    const qint64 progressValue = progressMaximum <= 0 ? 0 : qBound<qint64>(0, pairConfig.progressValue, progressMaximum);
    const QString stateText = buildPairStateText(pairConfig);
    const QString tooltipText = progressMaximum > 0
        ? tr("%1\n进度：%2/%3").arg(stateText).arg(progressValue).arg(progressMaximum)
        : tr("%1\n进度：处理中").arg(stateText);

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

void MainWindow::updatePairProgress(int pairIndex, qint64 progressValue, qint64 progressMaximum)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (progressMaximum <= 0) {
        _folderPairConfigs[pairIndex].progressMaximum = 0;
        _folderPairConfigs[pairIndex].progressValue = 0;
    } else {
        _folderPairConfigs[pairIndex].progressMaximum = progressMaximum;
        _folderPairConfigs[pairIndex].progressValue =
            qBound<qint64>(0, progressValue, _folderPairConfigs[pairIndex].progressMaximum);
    }
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
        return;
    }

    QVector<int> pendingPairIndexes;
    QVector<int> readyPairIndexes;
    pendingPairIndexes.reserve(normalizedIndexes.size());
    readyPairIndexes.reserve(normalizedIndexes.size());
    int availableSyncCount = maxParallelSyncCount() - runningSyncCount();

    for (int pairIndex : normalizedIndexes) {
        if (isPairSyncRunning(pairIndex)) {
            _pendingSyncReasons.insert(pairIndex, reason);
            pendingPairIndexes.append(pairIndex);
            continue;
        }

        if (availableSyncCount > 0) {
            readyPairIndexes.append(pairIndex);
            --availableSyncCount;
        } else {
            _pendingSyncReasons.insert(pairIndex, reason);
            updatePairProgress(pairIndex, 0, 0);
            updatePairStatus(pairIndex, tr("等待执行（同步队列）"));
            pendingPairIndexes.append(pairIndex);
        }
    }

    for (int pairIndex : readyPairIndexes) {
        startPairSync(pairIndex, reason);
    }

    const int startedPairCount = readyPairIndexes.size();
    const int pendingPairCount = pendingPairIndexes.size();

    if (startedPairCount > 0 || pendingPairCount > 0) {
        appendLog(tr("已发起并行同步请求：立即启动 %1 组，待当前任务完成后补同步 %2 组，reason=%3")
                      .arg(startedPairCount)
                      .arg(pendingPairCount)
                      .arg(reason));
        if (!readyPairIndexes.isEmpty()) {
            appendLog(tr("已立即启动：%1").arg(buildPairListSummaryText(readyPairIndexes)));
        }
    }
    if (pendingPairCount > 0) {
        appendLog(tr("已加入待同步队列：%1").arg(buildPairListSummaryText(pendingPairIndexes)));
    }

    startQueuedSyncs();
    refreshActionWidgets();
    updateControlState();
}

void MainWindow::startPairSync(int pairIndex, const QString &reason)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size() || isPairSyncRunning(pairIndex)) {
        return;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
    auto *thread = new QThread(this);
    auto *worker = new FolderSyncWorker();
    RunningSyncContext syncContext;
    syncContext.thread = thread;
    syncContext.worker = worker;
    syncContext.currentStep = 0;
    syncContext.totalSteps = 0;
    syncContext.reason = reason;
    _runningSyncContexts.insert(pairIndex, syncContext);

    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker,
            &FolderSyncWorker::sigSyncStarted,
            this,
            [this, pairIndex](qint64 totalSteps,
                              int removeFileCount,
                              int addFileCount,
                              int updateFileCount,
                              const QString &syncReason) {
                handlePairSyncStarted(pairIndex,
                                      totalSteps,
                                      removeFileCount,
                                      addFileCount,
                                      updateFileCount,
                                      syncReason);
            },
            Qt::QueuedConnection);
    connect(worker,
            &FolderSyncWorker::sigSyncProgress,
            this,
            [this, pairIndex](qint64 currentStep, qint64 totalSteps, const QString &currentItem) {
                handlePairSyncProgress(pairIndex, currentStep, totalSteps, currentItem);
            },
            Qt::QueuedConnection);
    connect(worker,
            &FolderSyncWorker::sigSyncFinished,
            this,
            [this, pairIndex, thread](bool success, const QString &summary) {
                handlePairSyncFinished(pairIndex, success, summary);
                if (thread->isRunning()) {
                    thread->quit();
                }
            },
            Qt::QueuedConnection);
    connect(worker,
            &FolderSyncWorker::sigLogMessage,
            this,
            [this, pairIndex](const QString &message) {
                appendLog(tr("第 %1 组：%2").arg(pairIndex + 1).arg(message));
            },
            Qt::QueuedConnection);

    updatePairProgress(pairIndex, 0, 0);
    updatePairStatus(pairIndex, tr("准备扫描"));
    refreshActionWidgets();
    updateControlState();

    thread->start();
    QMetaObject::invokeMethod(worker,
                              "slotStartSync",
                              Qt::QueuedConnection,
                              Q_ARG(QString, pairConfig.sourcePath),
                              Q_ARG(QString, pairConfig.targetPath),
                              Q_ARG(QString, reason),
                              Q_ARG(bool, _isFastCompareEnabled));
}

void MainWindow::handlePairSyncStarted(int pairIndex,
                                       qint64 totalSteps,
                                       int removeFileCount,
                                       int addFileCount,
                                       int updateFileCount,
                                       const QString &reason)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    RunningSyncContext &syncContext = _runningSyncContexts[pairIndex];
    syncContext.currentStep = 0;
    syncContext.totalSteps = qMax<qint64>(1, totalSteps);
    syncContext.reason = reason;

    updatePairStatus(pairIndex,
                     tr("待删 %1 / 待增 %2 / 待同步 %3")
                         .arg(removeFileCount)
                         .arg(addFileCount)
                         .arg(updateFileCount));
    updatePairProgress(pairIndex, 0, qMax<qint64>(1, totalSteps));
    appendLog(tr("开始并行同步：%1，待删除文件 %2，待新增文件 %3，待同步文件 %4。")
                  .arg(buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex))
                  .arg(removeFileCount)
                  .arg(addFileCount)
                  .arg(updateFileCount));
}

void MainWindow::handlePairSyncProgress(int pairIndex,
                                        qint64 currentStep,
                                        qint64 totalSteps,
                                        const QString &currentItem)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size() || !_runningSyncContexts.contains(pairIndex)) {
        return;
    }

    RunningSyncContext &syncContext = _runningSyncContexts[pairIndex];
    syncContext.currentStep = currentStep;
    syncContext.totalSteps = totalSteps <= 0 ? 0 : qMax<qint64>(1, totalSteps);
    updatePairProgress(pairIndex, currentStep, totalSteps);
    updatePairStatus(pairIndex, currentItem);
}

void MainWindow::handlePairSyncFinished(int pairIndex, bool success, const QString &summary)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        _runningSyncContexts.remove(pairIndex);
        return;
    }

    _runningSyncContexts.remove(pairIndex);

    const QString pairText = buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex);
    const QString statusText = summary.contains(tr("取消"))
        ? tr("最近一次：已取消")
        : (success ? (summary.contains(tr("无需同步")) ? tr("最近一次：已一致") : tr("最近一次：成功"))
                   : tr("最近一次：失败"));
    updatePairStatus(pairIndex, statusText);
    updatePairProgress(pairIndex, 0, 1);
    appendLog(tr("并行同步完成：%1，result=%2").arg(pairText, success ? tr("成功") : tr("失败")));

    refreshActionWidgets();
    updateControlState();

    if (_pendingSyncReasons.contains(pairIndex)) {
        const QString pendingReason = _pendingSyncReasons.take(pairIndex);
        QTimer::singleShot(0, this, [this, pairIndex, pendingReason]() {
            _pendingSyncReasons.insert(pairIndex, pendingReason);
            startQueuedSyncs();
        });
        return;
    }

    startQueuedSyncs();
}

bool MainWindow::isPairSyncRunning(int pairIndex) const
{
    return _runningSyncContexts.contains(pairIndex);
}

bool MainWindow::isPairSyncQueued(int pairIndex) const
{
    return _pendingSyncReasons.contains(pairIndex);
}

int MainWindow::runningSyncCount() const
{
    return _runningSyncContexts.size();
}

int MainWindow::maxParallelSyncCount() const
{
    return qBound(1, _maxParallelSyncCount, 4);
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
    }

    updateControlState();
}

void MainWindow::editPair(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (isPairSyncRunning(pairIndex) || isPairSyncQueued(pairIndex)) {
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
        }
    }

    updateControlState();
}

void MainWindow::syncPair(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (isPairSyncRunning(pairIndex) || isPairSyncQueued(pairIndex)) {
        return;
    }

    selectPairRow(pairIndex);
    requestSyncByIndexes(
        QVector<int>{pairIndex},
        _folderPairConfigs.at(pairIndex).isSyncEnabled ? tr("手动同步单组") : tr("手动同步单组（已暂停自动同步）"));
}

void MainWindow::cancelPairSync(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (isPairSyncQueued(pairIndex)) {
        _pendingSyncReasons.remove(pairIndex);
        updatePairStatus(pairIndex, tr("已取消排队"));
        updatePairProgress(pairIndex, 0, 1);
        appendLog(tr("已取消排队同步：%1").arg(buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex)));
        refreshActionWidgets();
        updateControlState();
        startQueuedSyncs();
        return;
    }

    if (!isPairSyncRunning(pairIndex)) {
        return;
    }

    RunningSyncContext &syncContext = _runningSyncContexts[pairIndex];
    if (syncContext.thread != nullptr) {
        syncContext.thread->requestInterruption();
    }
    if (syncContext.worker != nullptr) {
        syncContext.worker->slotCancelSync();
    }

    updatePairStatus(pairIndex, tr("正在取消"));
    appendLog(tr("已请求取消同步：%1").arg(buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex)));
    refreshActionWidgets();
    updateControlState();
}

void MainWindow::togglePairSync(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (isPairSyncRunning(pairIndex) || isPairSyncQueued(pairIndex)) {
        return;
    }

    FolderPairConfig &pairConfig = _folderPairConfigs[pairIndex];
    const bool hadPendingSync = _pendingSyncReasons.contains(pairIndex);
    pairConfig.isSyncEnabled = !pairConfig.isSyncEnabled;
    if (!pairConfig.isSyncEnabled) {
        removePairIndexFromQueues(pairIndex);
        if (hadPendingSync) {
            pairConfig.statusText = tr("待同步");
            pairConfig.progressValue = 0;
            pairConfig.progressMaximum = 1;
        }
    }
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
        }
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
    const QPalette palette = container->palette();
    const bool isCurrentPairRunning = isPairSyncRunning(pairIndex);
    const bool isCurrentPairQueued = isPairSyncQueued(pairIndex);
    const bool isCurrentPairBusy = isCurrentPairRunning || isCurrentPairQueued;

    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);
    layout->addWidget(editButton);
    layout->addWidget(syncButton);
    layout->addWidget(toggleButton);
    layout->addStretch();

    editButton->setCursor(Qt::PointingHandCursor);
    syncButton->setCursor(Qt::PointingHandCursor);
    toggleButton->setCursor(Qt::PointingHandCursor);
    editButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    syncButton->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Success, palette));
    toggleButton->setStyleSheet(buildActionButtonStyle(_folderPairConfigs.at(pairIndex).isSyncEnabled
                                                           ? ButtonStyleKind::E_Warning
                                                           : ButtonStyleKind::E_Neutral,
                                                       palette));
    editButton->setToolTip(tr("编辑这一组 A/B 路径"));
    syncButton->setToolTip(tr("立即同步这一组路径"));
    toggleButton->setToolTip(_folderPairConfigs.at(pairIndex).isSyncEnabled ? tr("暂停这一组的自动同步和“同步全部”")
                                                                            : tr("恢复这一组的自动同步和“同步全部”"));
    if (isCurrentPairRunning) {
        syncButton->setText(tr("取消"));
        syncButton->setToolTip(tr("请求取消当前同步任务"));
    } else if (isCurrentPairQueued) {
        syncButton->setText(tr("取消排队"));
        syncButton->setToolTip(tr("从同步队列中移除这一组"));
    }

    editButton->setEnabled(!isCurrentPairBusy);
    syncButton->setEnabled(true);
    toggleButton->setEnabled(!isCurrentPairBusy);

    connect(editButton, &QPushButton::clicked, container, [this, pairIndex]() {
        editPair(pairIndex);
    });
    connect(syncButton, &QPushButton::clicked, container, [this, pairIndex]() {
        if (isPairSyncRunning(pairIndex) || isPairSyncQueued(pairIndex)) {
            cancelPairSync(pairIndex);
            return;
        }

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
    const QString rawStatusText = pairConfig.statusText.isEmpty() ? tr("待同步") : pairConfig.statusText;
    const QString statusText = stripProgressSuffix(rawStatusText);
    return tr("%1 | %2").arg(enableText, statusText);
}

QString MainWindow::buildPairDisplayText(const FolderPairConfig &pairConfig, int pairIndex) const
{
    return tr("第 %1 组 [%2 -> %3]")
        .arg(pairIndex + 1)
        .arg(QDir::toNativeSeparators(pairConfig.sourcePath))
        .arg(QDir::toNativeSeparators(pairConfig.targetPath));
}

QString MainWindow::buildPairListSummaryText(const QVector<int> &pairIndexes) const
{
    if (pairIndexes.isEmpty()) {
        return tr("无");
    }

    QStringList pairTexts;
    pairTexts.reserve(pairIndexes.size());
    for (int pairIndex : pairIndexes) {
        if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
            continue;
        }

        pairTexts.append(buildPairDisplayText(_folderPairConfigs.at(pairIndex), pairIndex));
    }

    return pairTexts.isEmpty() ? tr("无") : pairTexts.join(tr("； "));
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
