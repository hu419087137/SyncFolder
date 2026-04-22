#include "MainWindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>

#include "FolderSyncWorker.h"
#include "FolderWatcher.h"
#include "HttpClientTabWidget.h"
#include "HttpFolderSyncWorker.h"
#include "HttpServerTabWidget.h"
#include "HttpSyncServer.h"
#include "LocalSyncTabWidget.h"
#include "PairEditDialog.h"
#include "SyncTypes.h"
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
constexpr int kSharedFolderNameColumn = 0;
constexpr int kSharedFolderPathColumn = 1;
constexpr int kAvailableSourceCheckColumn = 0;
constexpr int kAvailableSourceNameColumn = 1;
constexpr int kAvailableSourceIdColumn = 2;
constexpr int kAvailableSourceRootNameColumn = 3;
constexpr int kAutoMaxParallelSyncCount = 0;
constexpr int kDefaultMaxParallelSyncCount = kAutoMaxParallelSyncCount;
constexpr int kDefaultCompareMode = 1;
constexpr qint64 kProgressUiUpdateIntervalMs = 300;

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

QString normalizeHttpSourceUrlInternal(const QString &sourceUrl)
{
    const QString trimmedUrl = sourceUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        return QString();
    }

    QUrl url = QUrl::fromUserInput(trimmedUrl);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        return QString();
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return QString();
    }

    url.setFragment(QString());
    url.setQuery(QString());

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.size() > 1) {
        path.chop(1);
    }
    if (path.endsWith(QStringLiteral("/api/v1/manifest"))) {
        path.chop(QStringLiteral("/manifest").size());
    } else if (path.endsWith(QStringLiteral("/api/v1/file"))) {
        path.chop(QStringLiteral("/file").size());
    } else if (path.endsWith(QStringLiteral("/api/v1/sources"))) {
        path.chop(QStringLiteral("/sources").size());
    }
    if (!path.endsWith(QStringLiteral("/api/v1"))) {
        if (path.isEmpty() || path == QStringLiteral("/")) {
            path = QStringLiteral("/api/v1");
        } else {
            path += QStringLiteral("/api/v1");
        }
    }
    url.setPath(path);
    return url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      _ui(new Ui::MainWindow),
      _localPairTableModel(nullptr),
      _httpClientPairTableModel(nullptr),
      _debounceTimer(nullptr),
      _periodicCheckTimer(nullptr),
      _folderWatcher(nullptr),
      _httpSyncServer(nullptr),
      _httpClientCatalogNetworkAccessManager(nullptr),
      _isMonitoring(false),
      _compareMode(kDefaultCompareMode),
      _maxParallelSyncCount(kDefaultMaxParallelSyncCount),
      _httpServerPort(8086)
{
    qRegisterMetaType<FolderSyncWorker::CompareMode>("FolderSyncWorker::CompareMode");

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

    _httpSyncServer = new HttpSyncServer(this);
    connect(_httpSyncServer, &HttpSyncServer::sigLogMessage, this, &MainWindow::appendLog);
    connect(_httpSyncServer,
            &HttpSyncServer::sigServerStateChanged,
            this,
            &MainWindow::slotHttpServerStateChanged);

    _httpClientCatalogNetworkAccessManager = new QNetworkAccessManager(this);

    loadSettings();
    refreshHttpServerUi();
    refreshHttpSharedFolderTable();
    refreshHttpClientSourceTable();
    updateControlState();

    if (_folderPairConfigs.isEmpty()) {
        appendLog(tr("程序已启动，请先新增至少一组同步对。"));
    }
}

MainWindow::~MainWindow()
{
    saveSettings();

    if (_activeHttpClientCatalogReply != nullptr) {
        _activeHttpClientCatalogReply->abort();
    }

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

    if (_ui == nullptr || _ui->localSyncTab == nullptr) {
        return;
    }

    applyButtonStyles();
    if (_ui != nullptr && _ui->localSyncTab->pairTableView() != nullptr) {
        refreshActionWidgets();
        _ui->localSyncTab->pairTableView()->viewport()->update();
        _ui->httpClientTab->pairTableView()->viewport()->update();
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

void MainWindow::slotRemoveSelectedHttpClientPair()
{
    const int pairIndex = currentSelectedHttpClientPairIndex();
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        QMessageBox::information(this, tr("未选择客户端任务"), tr("请先在 HTTP 客户端列表中选择一组需要删除的任务。"));
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
    appendLog(tr("已删除 HTTP 客户端任务：%1").arg(pairText));
    updateControlState();
}

void MainWindow::slotPairSelectionChanged()
{
    updateControlState();
}

void MainWindow::slotHttpClientPairSelectionChanged()
{
    updateControlState();
}

void MainWindow::slotStartHttpServer()
{
    _httpServerAccessToken = _ui->httpServerTab->httpServerTokenEdit()->text().trimmed();
    _httpServerPort = static_cast<quint16>(_ui->httpServerTab->httpServerPortSpinBox()->value());

    QVector<HttpSyncServer::SharedFolderConfig> serverSharedFolderConfigs;
    serverSharedFolderConfigs.reserve(_httpSharedFolderConfigs.size());
    for (const HttpSharedFolderConfig &sharedFolderConfig : _httpSharedFolderConfigs) {
        serverSharedFolderConfigs.append({sharedFolderConfig.id,
                                          sharedFolderConfig.name,
                                          sharedFolderConfig.rootPath});
    }

    QString errorMessage;
    if (!_httpSyncServer->startServer(serverSharedFolderConfigs,
                                      _httpServerPort,
                                      _httpServerAccessToken,
                                      &errorMessage)) {
        QMessageBox::warning(this, tr("无法启动 HTTP 服务"), errorMessage);
        appendLog(errorMessage);
        refreshHttpServerUi();
        return;
    }

    saveSettings();
    refreshHttpServerUi();
}

void MainWindow::slotStopHttpServer()
{
    _httpSyncServer->stopServer();
    refreshHttpServerUi();
}

void MainWindow::slotAddHttpSharedFolder()
{
    HttpSharedFolderConfig sharedFolderConfig;
    sharedFolderConfig.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sharedFolderConfig.name = QInputDialog::getText(this, tr("新增共享目录"), tr("目录名称"));
    if (sharedFolderConfig.name.trimmed().isEmpty()) {
        return;
    }

    const QString selectedPath = QFileDialog::getExistingDirectory(this,
                                                                   tr("选择共享目录"),
                                                                   QDir::homePath());
    if (selectedPath.isEmpty()) {
        return;
    }

    sharedFolderConfig.name = sharedFolderConfig.name.trimmed();
    sharedFolderConfig.rootPath = normalizeLocalPath(selectedPath);
    QString errorMessage;
    if (!validateHttpSharedFolderConfig(sharedFolderConfig, -1, &errorMessage)) {
        QMessageBox::warning(this, tr("共享目录无效"), errorMessage);
        return;
    }

    _httpSharedFolderConfigs.append(sharedFolderConfig);
    saveSettings();
    refreshHttpSharedFolderTable();
    appendLog(tr("已新增 HTTP 共享目录：%1 -> %2")
                  .arg(sharedFolderConfig.name, QDir::toNativeSeparators(sharedFolderConfig.rootPath)));
}

void MainWindow::slotEditHttpSharedFolder()
{
    const int row = _ui->httpServerTab->sharedFolderTableWidget()->currentRow();
    if (row < 0 || row >= _httpSharedFolderConfigs.size()) {
        return;
    }

    HttpSharedFolderConfig updatedConfig = _httpSharedFolderConfigs.at(row);
    const QString updatedName =
        QInputDialog::getText(this, tr("编辑共享目录"), tr("目录名称"), QLineEdit::Normal, updatedConfig.name);
    if (updatedName.trimmed().isEmpty()) {
        return;
    }

    const QString selectedPath = QFileDialog::getExistingDirectory(this,
                                                                   tr("选择共享目录"),
                                                                   updatedConfig.rootPath);
    if (selectedPath.isEmpty()) {
        return;
    }

    updatedConfig.name = updatedName.trimmed();
    updatedConfig.rootPath = normalizeLocalPath(selectedPath);
    QString errorMessage;
    if (!validateHttpSharedFolderConfig(updatedConfig, row, &errorMessage)) {
        QMessageBox::warning(this, tr("共享目录无效"), errorMessage);
        return;
    }

    _httpSharedFolderConfigs[row] = updatedConfig;
    saveSettings();
    refreshHttpSharedFolderTable();
    appendLog(tr("已更新 HTTP 共享目录：%1 -> %2")
                  .arg(updatedConfig.name, QDir::toNativeSeparators(updatedConfig.rootPath)));
}

void MainWindow::slotRemoveSelectedHttpSharedFolder()
{
    const int row = _ui->httpServerTab->sharedFolderTableWidget()->currentRow();
    if (row < 0 || row >= _httpSharedFolderConfigs.size()) {
        return;
    }

    const HttpSharedFolderConfig sharedFolderConfig = _httpSharedFolderConfigs.at(row);
    _httpSharedFolderConfigs.removeAt(row);
    saveSettings();
    refreshHttpSharedFolderTable();
    appendLog(tr("已删除 HTTP 共享目录：%1").arg(sharedFolderConfig.name));
}

void MainWindow::slotFetchHttpClientSources()
{
    if (_activeHttpClientCatalogReply != nullptr) {
        _activeHttpClientCatalogReply->abort();
        _activeHttpClientCatalogReply.clear();
    }

    _httpClientServerUrl = normalizeHttpSourceUrl(_ui->httpClientTab->serverUrlEdit()->text());
    _httpClientAccessToken = _ui->httpClientTab->accessTokenEdit()->text().trimmed();
    _ui->httpClientTab->serverUrlEdit()->setText(_httpClientServerUrl);
    if (_httpClientServerUrl.isEmpty()) {
        QMessageBox::warning(this, tr("地址无效"), tr("请先填写有效的 HTTP 服务地址。"));
        return;
    }

    QNetworkRequest request(QUrl(buildHttpClientSourceListUrl(_httpClientServerUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    if (!_httpClientAccessToken.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QStringLiteral("Bearer %1").arg(_httpClientAccessToken).toUtf8());
    }

    _activeHttpClientCatalogReply = _httpClientCatalogNetworkAccessManager->get(request);
    connect(_activeHttpClientCatalogReply,
            &QNetworkReply::finished,
            this,
            &MainWindow::slotHttpClientSourceReplyFinished);
    _ui->httpClientTab->fetchSourcesButton()->setEnabled(false);
    appendLog(tr("正在获取远端可同步目录列表：%1").arg(_httpClientServerUrl));
}

void MainWindow::slotHttpClientSourceReplyFinished()
{
    if (_activeHttpClientCatalogReply == nullptr) {
        _ui->httpClientTab->fetchSourcesButton()->setEnabled(true);
        return;
    }

    QNetworkReply *reply = _activeHttpClientCatalogReply;
    _activeHttpClientCatalogReply.clear();
    _ui->httpClientTab->fetchSourcesButton()->setEnabled(true);
    const QByteArray responseBody = reply->readAll();
    const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString errorMessage;
    if (reply->error() != QNetworkReply::NoError) {
        errorMessage = tr("获取远端目录列表失败，httpStatus=%1，error=%2")
                           .arg(httpStatusCode)
                           .arg(reply->errorString());
    } else if (httpStatusCode < 200 || httpStatusCode >= 300) {
        errorMessage = tr("获取远端目录列表失败，httpStatus=%1").arg(httpStatusCode);
    }

    if (!errorMessage.isEmpty()) {
        reply->deleteLater();
        QMessageBox::warning(this, tr("获取目录列表失败"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(responseBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDocument.isObject()) {
        reply->deleteLater();
        errorMessage = tr("解析远端目录列表失败，error=%1").arg(parseError.errorString());
        QMessageBox::warning(this, tr("解析失败"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    _httpRemoteSourceConfigs.clear();
    const QJsonArray sourcesArray = jsonDocument.object().value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &sourceValue : sourcesArray) {
        if (!sourceValue.isObject()) {
            continue;
        }

        const QJsonObject sourceObject = sourceValue.toObject();
        HttpRemoteSourceConfig remoteSourceConfig;
        remoteSourceConfig.id = sourceObject.value(QStringLiteral("id")).toString().trimmed();
        remoteSourceConfig.name = sourceObject.value(QStringLiteral("name")).toString().trimmed();
        remoteSourceConfig.rootName = sourceObject.value(QStringLiteral("rootName")).toString().trimmed();
        if (remoteSourceConfig.id.isEmpty()) {
            continue;
        }
        if (remoteSourceConfig.name.isEmpty()) {
            remoteSourceConfig.name = remoteSourceConfig.rootName;
        }
        if (remoteSourceConfig.name.isEmpty()) {
            remoteSourceConfig.name = remoteSourceConfig.id;
        }
        _httpRemoteSourceConfigs.append(remoteSourceConfig);
    }
    reply->deleteLater();
    refreshHttpClientSourceTable();
    updateControlState();
    appendLog(tr("已获取远端可同步目录 %1 项。").arg(_httpRemoteSourceConfigs.size()));
}

void MainWindow::slotBrowseHttpClientTargetRoot()
{
    const QString currentPath = normalizeLocalPath(_ui->httpClientTab->targetRootEdit()->text());
    const QString selectedPath = QFileDialog::getExistingDirectory(this,
                                                                   tr("选择本地备份根目录"),
                                                                   currentPath.isEmpty()
                                                                       ? QDir::homePath()
                                                                       : currentPath);
    if (selectedPath.isEmpty()) {
        return;
    }

    _httpClientTargetRootPath = normalizeLocalPath(selectedPath);
    _ui->httpClientTab->targetRootEdit()->setText(QDir::toNativeSeparators(_httpClientTargetRootPath));
}

void MainWindow::slotAddSelectedHttpClientSources()
{
    _httpClientServerUrl = normalizeHttpSourceUrl(_ui->httpClientTab->serverUrlEdit()->text());
    _httpClientAccessToken = _ui->httpClientTab->accessTokenEdit()->text().trimmed();
    _httpClientTargetRootPath = normalizeLocalPath(_ui->httpClientTab->targetRootEdit()->text());
    _ui->httpClientTab->serverUrlEdit()->setText(_httpClientServerUrl);
    _ui->httpClientTab->targetRootEdit()->setText(QDir::toNativeSeparators(_httpClientTargetRootPath));
    if (_httpClientServerUrl.isEmpty() || _httpClientTargetRootPath.isEmpty()) {
        QMessageBox::warning(this, tr("信息不完整"), tr("请先填写远端 HTTP 地址和本地备份根目录。"));
        return;
    }

    QVector<FolderPairConfig> createdPairs;
    QSet<QString> selectedTargetPathKeys;
    auto buildTargetPathKey = [](const QString &targetPath) {
        QString targetPathKey = QDir::cleanPath(targetPath);
#ifdef Q_OS_WIN
        targetPathKey = targetPathKey.toLower();
#endif
        return targetPathKey;
    };

    QTableWidget *tableWidget = _ui->httpClientTab->availableSourcesTableWidget();
    for (int row = 0; row < _httpRemoteSourceConfigs.size(); ++row) {
        QTableWidgetItem *checkedItem = tableWidget->item(row, kAvailableSourceCheckColumn);
        if (checkedItem == nullptr || checkedItem->checkState() != Qt::Checked) {
            continue;
        }

        const HttpRemoteSourceConfig &remoteSourceConfig = _httpRemoteSourceConfigs.at(row);
        FolderPairConfig pairConfig;
        pairConfig.sourceType = E_HttpDirectorySource;
        pairConfig.sourcePath = _httpClientServerUrl;
        pairConfig.sourceAccessToken = _httpClientAccessToken;
        pairConfig.remoteSourceId = remoteSourceConfig.id;
        pairConfig.remoteSourceName = remoteSourceConfig.name;
        pairConfig.targetPath = buildHttpClientImportPath(remoteSourceConfig);
        QString targetPathKey = buildTargetPathKey(pairConfig.targetPath);
        if (selectedTargetPathKeys.contains(targetPathKey)) {
            const QFileInfo targetInfo(pairConfig.targetPath);
            const QString baseFolderName = targetInfo.fileName().isEmpty()
                ? remoteSourceConfig.id
                : targetInfo.fileName();
            for (int suffix = 2; suffix <= 999; ++suffix) {
                const QString candidatePath =
                    QDir(_httpClientTargetRootPath)
                        .absoluteFilePath(QStringLiteral("%1_%2").arg(baseFolderName).arg(suffix));
                const QString candidateKey = buildTargetPathKey(candidatePath);
                if (selectedTargetPathKeys.contains(candidateKey)) {
                    continue;
                }

                pairConfig.targetPath = candidatePath;
                targetPathKey = candidateKey;
                break;
            }
        }
        pairConfig.statusText = tr("待同步");
        pairConfig.isSyncEnabled = true;
        QString errorMessage;
        if (!validatePairConfig(pairConfig, -1, &errorMessage)) {
            appendLog(tr("已跳过客户端目录导入：%1，error=%2").arg(remoteSourceConfig.name, errorMessage));
            continue;
        }
        selectedTargetPathKeys.insert(targetPathKey);
        createdPairs.append(pairConfig);
    }

    if (createdPairs.isEmpty()) {
        QMessageBox::information(this, tr("没有可导入目录"), tr("请至少勾选一个有效目录后再添加。"));
        return;
    }

    for (const FolderPairConfig &pairConfig : createdPairs) {
        _folderPairConfigs.append(pairConfig);
    }
    saveSettings();
    refreshPairTable();
    appendLog(tr("已新增 HTTP 客户端同步任务 %1 组。").arg(createdPairs.size()));
    selectPairRow(_folderPairConfigs.size() - 1);
}

void MainWindow::slotStartMonitoring()
{
    const QVector<int> enabledLocalPairIndexes = buildEnabledPairIndexes(E_LocalDirectorySource);
    if (enabledLocalPairIndexes.isEmpty()) {
        const QString errorMessage = tr("当前没有已启用的本地同步对，请先新增或恢复至少一组本地同步对。");
        QMessageBox::warning(this, tr("无法启动监控"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    _isMonitoring = true;
    _scheduledReason.clear();
    appendLog(tr("已启动本地目录监控，后续会持续保持已启用的本地 B 目录与对应 A 目录一致。"));
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
    const QVector<int> enabledLocalPairIndexes = buildEnabledPairIndexes(E_LocalDirectorySource);
    if (enabledLocalPairIndexes.isEmpty()) {
        const QString errorMessage = tr("当前没有已启用的本地同步对，请先新增或恢复至少一组本地同步对。");
        QMessageBox::warning(this, tr("无法同步"), errorMessage);
        appendLog(errorMessage);
        return;
    }

    const int disabledPairCount = _localPairIndexes.size() - enabledLocalPairIndexes.size();
    appendLog(tr("手动同步全部本地任务：本次纳入 %1 组已启用同步对，跳过 %2 组已暂停同步对。")
                  .arg(enabledLocalPairIndexes.size())
                  .arg(disabledPairCount));
    appendLog(tr("手动同步全部本地目录：%1").arg(buildPairListSummaryText(enabledLocalPairIndexes)));

    requestSyncAll(tr("手动同步全部已启用本地同步对"));
}

void MainWindow::slotSyncAllHttpClientPairs()
{
    const QVector<int> enabledHttpPairIndexes = buildEnabledPairIndexes(E_HttpDirectorySource);
    if (enabledHttpPairIndexes.isEmpty()) {
        QMessageBox::information(this, tr("没有可同步任务"), tr("当前没有已启用的 HTTP 客户端任务。"));
        return;
    }

    appendLog(tr("手动同步全部 HTTP 客户端任务：%1").arg(buildPairListSummaryText(enabledHttpPairIndexes)));
    requestSyncByIndexes(enabledHttpPairIndexes, tr("手动同步全部 HTTP 客户端任务"));
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

void MainWindow::slotHttpServerStateChanged(bool isRunning, quint16 listenPort)
{
    Q_UNUSED(listenPort)
    if (isRunning) {
        _httpServerPort = _httpSyncServer->listenPort();
    }
    refreshHttpServerUi();
}

void MainWindow::buildUi()
{
    _ui->setupUi(this);
    _ui->localSyncTab->maxParallelSyncComboBox()->addItem(tr("并发自动"), kAutoMaxParallelSyncCount);
    _ui->localSyncTab->maxParallelSyncComboBox()->addItem(tr("并发 1 组"), 1);
    _ui->localSyncTab->maxParallelSyncComboBox()->addItem(tr("并发 2 组"), 2);
    _ui->localSyncTab->maxParallelSyncComboBox()->addItem(tr("并发 3 组"), 3);
    _ui->localSyncTab->maxParallelSyncComboBox()->addItem(tr("并发 4 组"), 4);
    _ui->localSyncTab->maxParallelSyncComboBox()->setToolTip(
        tr("自动模式会尽量让同步全部中的任务同时起跑；机械硬盘或移动盘可手动改成 1-2。"));
    _ui->localSyncTab->compareModeComboBox()->addItem(tr("严格比对"), FolderSyncWorker::E_StrictCompare);
    _ui->localSyncTab->compareModeComboBox()->addItem(tr("快速比对"), FolderSyncWorker::E_FastCompare);
    _ui->localSyncTab->compareModeComboBox()->addItem(tr("极速比对"), FolderSyncWorker::E_TurboCompare);
    _ui->localSyncTab->compareModeComboBox()->setToolTip(
        tr("严格比对最准确；快速比对在大小和修改时间一致时跳过全文；极速比对只按大小和时间判断。"));
    _ui->httpServerTab->httpServerPortSpinBox()->setRange(1, 65535);
    _ui->httpServerTab->httpServerPortSpinBox()->setValue(_httpServerPort);
    _ui->httpServerTab->httpServerTokenEdit()->setPlaceholderText(tr("可选：访问令牌，留空则不鉴权"));
    _ui->httpClientTab->serverUrlEdit()->setPlaceholderText(tr("例如 http://192.168.1.10:8086/api/v1"));
    _ui->httpClientTab->accessTokenEdit()->setPlaceholderText(tr("可选：远端 HTTP 服务访问令牌"));
    _ui->httpClientTab->targetRootEdit()->setPlaceholderText(tr("选择本机用于存放 HTTP 目录备份的根目录"));

    _localPairTableModel = new QStandardItemModel(this);
    _localPairTableModel->setColumnCount(4);
    _localPairTableModel->setHorizontalHeaderLabels(
        QStringList{tr("同步源"), tr("B 备份目录"), tr("状态 / 进度"), tr("操作")});

    _httpClientPairTableModel = new QStandardItemModel(this);
    _httpClientPairTableModel->setColumnCount(4);
    _httpClientPairTableModel->setHorizontalHeaderLabels(
        QStringList{tr("同步源"), tr("B 备份目录"), tr("状态 / 进度"), tr("操作")});

    auto initPairTableView = [this](QTableView *tableView, QStandardItemModel *tableModel) {
        tableView->setModel(tableModel);
        tableView->setWordWrap(false);
        tableView->setTextElideMode(Qt::ElideMiddle);
        tableView->setShowGrid(false);
        tableView->horizontalHeader()->setStretchLastSection(false);
        tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        tableView->horizontalHeader()->setMinimumSectionSize(96);
        tableView->horizontalHeader()->setSectionResizeMode(kSourceColumn, QHeaderView::Stretch);
        tableView->horizontalHeader()->setSectionResizeMode(kTargetColumn, QHeaderView::Stretch);
        tableView->horizontalHeader()->setSectionResizeMode(kStatusColumn, QHeaderView::Interactive);
        tableView->horizontalHeader()->setSectionResizeMode(kActionColumn, QHeaderView::Fixed);
        tableView->setColumnWidth(kSourceColumn, kPathColumnMinimumWidth);
        tableView->setColumnWidth(kTargetColumn, kPathColumnMinimumWidth);
        tableView->setItemDelegateForColumn(kStatusColumn, new PairStatusDelegate(tableView));
        tableView->setColumnWidth(kStatusColumn, kStatusColumnWidth);
        tableView->setColumnWidth(kActionColumn, kActionColumnWidth);
        tableView->verticalHeader()->setVisible(false);
        tableView->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::SingleSelection);
        tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tableView->setAlternatingRowColors(false);
    };
    initPairTableView(_ui->localSyncTab->pairTableView(), _localPairTableModel);
    initPairTableView(_ui->httpClientTab->pairTableView(), _httpClientPairTableModel);

    _ui->httpServerTab->sharedFolderTableWidget()->horizontalHeader()->setStretchLastSection(false);
    _ui->httpServerTab->sharedFolderTableWidget()->horizontalHeader()->setSectionResizeMode(
        kSharedFolderNameColumn, QHeaderView::ResizeToContents);
    _ui->httpServerTab->sharedFolderTableWidget()->horizontalHeader()->setSectionResizeMode(
        kSharedFolderPathColumn, QHeaderView::Stretch);
    _ui->httpServerTab->sharedFolderTableWidget()->verticalHeader()->setVisible(false);
    _ui->httpServerTab->sharedFolderTableWidget()->setAlternatingRowColors(true);

    _ui->httpClientTab->availableSourcesTableWidget()->horizontalHeader()->setStretchLastSection(false);
    _ui->httpClientTab->availableSourcesTableWidget()->horizontalHeader()->setSectionResizeMode(
        kAvailableSourceCheckColumn, QHeaderView::ResizeToContents);
    _ui->httpClientTab->availableSourcesTableWidget()->horizontalHeader()->setSectionResizeMode(
        kAvailableSourceNameColumn, QHeaderView::ResizeToContents);
    _ui->httpClientTab->availableSourcesTableWidget()->horizontalHeader()->setSectionResizeMode(
        kAvailableSourceIdColumn, QHeaderView::ResizeToContents);
    _ui->httpClientTab->availableSourcesTableWidget()->horizontalHeader()->setSectionResizeMode(
        kAvailableSourceRootNameColumn, QHeaderView::Stretch);
    _ui->httpClientTab->availableSourcesTableWidget()->verticalHeader()->setVisible(false);
    _ui->httpClientTab->availableSourcesTableWidget()->setAlternatingRowColors(true);
    _ui->httpClientTab->availableSourcesTableWidget()->setColumnWidth(kAvailableSourceCheckColumn, 56);

    applyButtonStyles();

    connect(_ui->localSyncTab->addPairButton(), &QPushButton::clicked, this, &MainWindow::slotAddPair);
    connect(_ui->localSyncTab->removePairButton(), &QPushButton::clicked, this, &MainWindow::slotRemoveSelectedPair);
    connect(_ui->httpClientTab->removePairButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotRemoveSelectedHttpClientPair);
    connect(_ui->httpClientTab->syncAllPairsButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotSyncAllHttpClientPairs);
    connect(_ui->httpClientTab->fetchSourcesButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotFetchHttpClientSources);
    connect(_ui->httpClientTab->browseTargetRootButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotBrowseHttpClientTargetRoot);
    connect(_ui->httpClientTab->addSelectedSourcesButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotAddSelectedHttpClientSources);
    connect(_ui->httpServerTab->startHttpServerButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotStartHttpServer);
    connect(_ui->httpServerTab->stopHttpServerButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotStopHttpServer);
    connect(_ui->httpServerTab->addSharedFolderButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotAddHttpSharedFolder);
    connect(_ui->httpServerTab->editSharedFolderButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotEditHttpSharedFolder);
    connect(_ui->httpServerTab->removeSharedFolderButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotRemoveSelectedHttpSharedFolder);
    connect(_ui->httpServerTab->sharedFolderTableWidget(),
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() { refreshHttpServerUi(); });
    connect(_ui->localSyncTab->pairTableView()->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this]() { slotPairSelectionChanged(); });
    connect(_ui->httpClientTab->pairTableView()->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this]() { slotHttpClientPairSelectionChanged(); });
    connect(_ui->localSyncTab->startMonitorButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotStartMonitoring);
    connect(_ui->localSyncTab->stopMonitorButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotStopMonitoring);
    connect(_ui->localSyncTab->syncAllPairsButton(),
            &QPushButton::clicked,
            this,
            &MainWindow::slotSyncAllPairs);
    connect(_ui->localSyncTab->maxParallelSyncComboBox(),
            &QComboBox::currentIndexChanged,
            this,
            [this](int index) {
                const int selectedCount = _ui->localSyncTab->maxParallelSyncComboBox()->itemData(index).toInt();
                _maxParallelSyncCount = selectedCount == kAutoMaxParallelSyncCount ? kAutoMaxParallelSyncCount
                                                                                  : qBound(1, selectedCount, 4);
                saveSettings();
                appendLog(_maxParallelSyncCount == kAutoMaxParallelSyncCount
                              ? tr("已设置最大并发同步数：自动。")
                              : tr("已设置最大并发同步数：%1 组。").arg(_maxParallelSyncCount));
                startQueuedSyncs();
                refreshActionWidgets();
                updateControlState();
            });
    connect(_ui->localSyncTab->compareModeComboBox(), &QComboBox::currentIndexChanged, this, [this](int index) {
        _compareMode = _ui->localSyncTab->compareModeComboBox()->itemData(index).toInt();
        saveSettings();
        appendLog(tr("已切换比对模式：%1。").arg(_ui->localSyncTab->compareModeComboBox()->itemText(index)));
    });
}

void MainWindow::applyButtonStyles()
{
    if (_ui == nullptr || _ui->localSyncTab == nullptr) {
        return;
    }

    const QPalette palette = this->palette();
    _ui->localSyncTab->addPairButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->localSyncTab->removePairButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Danger, palette));
    _ui->localSyncTab->startMonitorButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->localSyncTab->stopMonitorButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Warning, palette));
    _ui->localSyncTab->syncAllPairsButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Success, palette));
    _ui->httpServerTab->startHttpServerButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->httpServerTab->stopHttpServerButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Danger, palette));
    _ui->httpServerTab->addSharedFolderButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->httpServerTab->editSharedFolderButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Neutral, palette));
    _ui->httpServerTab->removeSharedFolderButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Danger, palette));
    _ui->httpClientTab->fetchSourcesButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Primary, palette));
    _ui->httpClientTab->browseTargetRootButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Neutral, palette));
    _ui->httpClientTab->addSelectedSourcesButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Success, palette));
    _ui->httpClientTab->removePairButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Danger, palette));
    _ui->httpClientTab->syncAllPairsButton()->setStyleSheet(buildActionButtonStyle(ButtonStyleKind::E_Success, palette));

    _ui->localSyncTab->addPairButton()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->removePairButton()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->startMonitorButton()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->stopMonitorButton()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->syncAllPairsButton()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->maxParallelSyncComboBox()->setCursor(Qt::PointingHandCursor);
    _ui->localSyncTab->compareModeComboBox()->setCursor(Qt::PointingHandCursor);
    _ui->httpServerTab->startHttpServerButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpServerTab->stopHttpServerButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpServerTab->addSharedFolderButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpServerTab->editSharedFolderButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpServerTab->removeSharedFolderButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpClientTab->fetchSourcesButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpClientTab->browseTargetRootButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpClientTab->addSelectedSourcesButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpClientTab->removePairButton()->setCursor(Qt::PointingHandCursor);
    _ui->httpClientTab->syncAllPairsButton()->setCursor(Qt::PointingHandCursor);
}

void MainWindow::loadSettings()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const bool isLegacyFastCompareEnabled =
        settings.value(QStringLiteral("syncOptions/fastCompareEnabled"), true).toBool();
    _compareMode =
        settings.value(QStringLiteral("syncOptions/compareMode"),
                       isLegacyFastCompareEnabled ? FolderSyncWorker::E_FastCompare
                                                  : FolderSyncWorker::E_StrictCompare)
            .toInt();
    _compareMode = qBound(static_cast<int>(FolderSyncWorker::E_StrictCompare),
                          _compareMode,
                          static_cast<int>(FolderSyncWorker::E_TurboCompare));
    _maxParallelSyncCount =
        settings.value(QStringLiteral("syncOptions/maxParallelSyncCount"), kDefaultMaxParallelSyncCount).toInt();
    if (_maxParallelSyncCount != kAutoMaxParallelSyncCount) {
        _maxParallelSyncCount = qBound(1, _maxParallelSyncCount, 4);
    }
    {
        const QSignalBlocker blocker(_ui->localSyncTab->maxParallelSyncComboBox());
        const int maxParallelIndex = _ui->localSyncTab->maxParallelSyncComboBox()->findData(_maxParallelSyncCount);
        _ui->localSyncTab->maxParallelSyncComboBox()->setCurrentIndex(maxParallelIndex >= 0 ? maxParallelIndex : 0);
    }
    {
        const QSignalBlocker blocker(_ui->localSyncTab->compareModeComboBox());
        const int compareModeIndex = _ui->localSyncTab->compareModeComboBox()->findData(_compareMode);
        _ui->localSyncTab->compareModeComboBox()->setCurrentIndex(compareModeIndex >= 0 ? compareModeIndex : 1);
    }
    _httpServerAccessToken = settings.value(QStringLiteral("httpServer/accessToken")).toString().trimmed();
    _httpServerPort = static_cast<quint16>(settings.value(QStringLiteral("httpServer/port"), 8086).toUInt());
    if (_httpServerPort == 0) {
        _httpServerPort = 8086;
    }
    _ui->httpServerTab->httpServerTokenEdit()->setText(_httpServerAccessToken);
    _ui->httpServerTab->httpServerPortSpinBox()->setValue(_httpServerPort);

    const int sharedFolderCount = settings.beginReadArray(QStringLiteral("httpServer/sharedFolders"));
    for (int index = 0; index < sharedFolderCount; ++index) {
        settings.setArrayIndex(index);
        HttpSharedFolderConfig sharedFolderConfig;
        sharedFolderConfig.id = settings.value(QStringLiteral("id")).toString().trimmed();
        sharedFolderConfig.name = settings.value(QStringLiteral("name")).toString().trimmed();
        sharedFolderConfig.rootPath = normalizeLocalPath(settings.value(QStringLiteral("rootPath")).toString());
        if (sharedFolderConfig.id.isEmpty()) {
            sharedFolderConfig.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        QString errorMessage;
        if (!validateHttpSharedFolderConfig(sharedFolderConfig, -1, &errorMessage)) {
            appendLog(tr("已忽略无效 HTTP 共享目录，第 %1 项，error=%2").arg(index + 1).arg(errorMessage));
            continue;
        }
        _httpSharedFolderConfigs.append(sharedFolderConfig);
    }
    settings.endArray();
    if (_httpSharedFolderConfigs.isEmpty()) {
        const QString legacyHttpServerRootPath =
            normalizeLocalPath(settings.value(QStringLiteral("httpServer/rootPath")).toString());
        if (!legacyHttpServerRootPath.isEmpty()) {
            HttpSharedFolderConfig sharedFolderConfig;
            sharedFolderConfig.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            sharedFolderConfig.name = QFileInfo(legacyHttpServerRootPath).fileName();
            if (sharedFolderConfig.name.isEmpty()) {
                sharedFolderConfig.name = tr("默认共享目录");
            }
            sharedFolderConfig.rootPath = legacyHttpServerRootPath;
            _httpSharedFolderConfigs.append(sharedFolderConfig);
        }
    }

    _httpClientServerUrl =
        normalizeHttpSourceUrl(settings.value(QStringLiteral("httpClient/serverUrl")).toString());
    _httpClientAccessToken =
        settings.value(QStringLiteral("httpClient/accessToken")).toString().trimmed();
    _httpClientTargetRootPath =
        normalizeLocalPath(settings.value(QStringLiteral("httpClient/targetRootPath")).toString());
    _ui->httpClientTab->serverUrlEdit()->setText(_httpClientServerUrl);
    _ui->httpClientTab->accessTokenEdit()->setText(_httpClientAccessToken);
    _ui->httpClientTab->targetRootEdit()->setText(QDir::toNativeSeparators(_httpClientTargetRootPath));

    const int pairCount = settings.beginReadArray(QStringLiteral("folderPairs"));
    for (int index = 0; index < pairCount; ++index) {
        settings.setArrayIndex(index);

        FolderPairConfig pairConfig;
        pairConfig.sourceType =
            settings.value(QStringLiteral("sourceType"), E_LocalDirectorySource).toInt();
        pairConfig.sourcePath = pairConfig.sourceType == E_HttpDirectorySource
            ? normalizeHttpSourceUrl(settings.value(QStringLiteral("sourcePath")).toString())
            : normalizeLocalPath(settings.value(QStringLiteral("sourcePath")).toString());
        pairConfig.sourceAccessToken =
            settings.value(QStringLiteral("sourceAccessToken")).toString().trimmed();
        pairConfig.remoteSourceId =
            settings.value(QStringLiteral("remoteSourceId")).toString().trimmed();
        pairConfig.remoteSourceName =
            settings.value(QStringLiteral("remoteSourceName")).toString().trimmed();
        pairConfig.targetPath = normalizeLocalPath(settings.value(QStringLiteral("targetPath")).toString());
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
    settings.setValue(QStringLiteral("syncOptions/compareMode"), _compareMode);
    settings.setValue(QStringLiteral("syncOptions/fastCompareEnabled"),
                      _compareMode != FolderSyncWorker::E_StrictCompare);
    settings.setValue(QStringLiteral("syncOptions/maxParallelSyncCount"), _maxParallelSyncCount);
    const QString httpServerAccessToken = _ui != nullptr && _ui->httpServerTab != nullptr
        ? _ui->httpServerTab->httpServerTokenEdit()->text().trimmed()
        : _httpServerAccessToken;
    const quint16 httpServerPort = _ui != nullptr && _ui->httpServerTab != nullptr
        ? static_cast<quint16>(_ui->httpServerTab->httpServerPortSpinBox()->value())
        : _httpServerPort;
    settings.setValue(QStringLiteral("httpServer/accessToken"), httpServerAccessToken);
    settings.setValue(QStringLiteral("httpServer/port"), httpServerPort);
    settings.remove(QStringLiteral("httpServer/sharedFolders"));
    settings.beginWriteArray(QStringLiteral("httpServer/sharedFolders"));
    for (int index = 0; index < _httpSharedFolderConfigs.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("id"), _httpSharedFolderConfigs.at(index).id);
        settings.setValue(QStringLiteral("name"), _httpSharedFolderConfigs.at(index).name);
        settings.setValue(QStringLiteral("rootPath"), _httpSharedFolderConfigs.at(index).rootPath);
    }
    settings.endArray();
    settings.setValue(QStringLiteral("httpClient/serverUrl"),
                      _ui != nullptr && _ui->httpClientTab != nullptr
                          ? normalizeHttpSourceUrl(_ui->httpClientTab->serverUrlEdit()->text())
                          : _httpClientServerUrl);
    settings.setValue(QStringLiteral("httpClient/accessToken"),
                      _ui != nullptr && _ui->httpClientTab != nullptr
                          ? _ui->httpClientTab->accessTokenEdit()->text().trimmed()
                          : _httpClientAccessToken);
    settings.setValue(QStringLiteral("httpClient/targetRootPath"),
                      _ui != nullptr && _ui->httpClientTab != nullptr
                          ? normalizeLocalPath(_ui->httpClientTab->targetRootEdit()->text())
                          : _httpClientTargetRootPath);
    settings.remove(QStringLiteral("folderPairs"));
    settings.beginWriteArray(QStringLiteral("folderPairs"));
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("sourceType"), _folderPairConfigs.at(index).sourceType);
        settings.setValue(QStringLiteral("sourcePath"), _folderPairConfigs.at(index).sourcePath);
        settings.setValue(QStringLiteral("sourceAccessToken"), _folderPairConfigs.at(index).sourceAccessToken);
        settings.setValue(QStringLiteral("remoteSourceId"), _folderPairConfigs.at(index).remoteSourceId);
        settings.setValue(QStringLiteral("remoteSourceName"), _folderPairConfigs.at(index).remoteSourceName);
        settings.setValue(QStringLiteral("targetPath"), _folderPairConfigs.at(index).targetPath);
        settings.setValue(QStringLiteral("isSyncEnabled"), _folderPairConfigs.at(index).isSyncEnabled);
    }
    settings.endArray();
}

void MainWindow::refreshHttpServerUi()
{
    const bool isRunning = _httpSyncServer != nullptr && _httpSyncServer->isRunning();
    if (isRunning) {
        QStringList endpointLines;
        endpointLines.reserve(_httpSyncServer->endpointUrls().size() + 2);
        endpointLines.append(tr("当前 HTTP URL（客户端可直接填写）："));
        for (const QString &endpointUrl : _httpSyncServer->endpointUrls()) {
            endpointLines.append(QStringLiteral("%1/api/v1").arg(endpointUrl));
        }
        _ui->httpServerTab->httpServerEndpointEdit()->setPlainText(endpointLines.join(QLatin1Char('\n')));
    } else {
        _ui->httpServerTab->httpServerEndpointEdit()->setPlainText(
            tr("未启动。建议其他电脑填写这里显示的 http://IP:端口/api/v1 地址。"));
    }

    const bool hasSharedFolders = !_httpSharedFolderConfigs.isEmpty();
    const bool hasSelection = _ui->httpServerTab->sharedFolderTableWidget()->currentRow() >= 0;
    _ui->httpServerTab->startHttpServerButton()->setEnabled(!isRunning && hasSharedFolders);
    _ui->httpServerTab->stopHttpServerButton()->setEnabled(isRunning);
    _ui->httpServerTab->httpServerPortSpinBox()->setEnabled(!isRunning);
    _ui->httpServerTab->httpServerTokenEdit()->setEnabled(!isRunning);
    _ui->httpServerTab->addSharedFolderButton()->setEnabled(!isRunning);
    _ui->httpServerTab->editSharedFolderButton()->setEnabled(!isRunning && hasSelection);
    _ui->httpServerTab->removeSharedFolderButton()->setEnabled(!isRunning && hasSelection);
}

void MainWindow::refreshHttpSharedFolderTable()
{
    QTableWidget *tableWidget = _ui->httpServerTab->sharedFolderTableWidget();
    const int currentRow = tableWidget->currentRow();
    tableWidget->setRowCount(_httpSharedFolderConfigs.size());
    for (int row = 0; row < _httpSharedFolderConfigs.size(); ++row) {
        const HttpSharedFolderConfig &sharedFolderConfig = _httpSharedFolderConfigs.at(row);
        auto *nameItem = new QTableWidgetItem(sharedFolderConfig.name);
        auto *pathItem = new QTableWidgetItem(QDir::toNativeSeparators(sharedFolderConfig.rootPath));
        nameItem->setToolTip(sharedFolderConfig.id);
        pathItem->setToolTip(sharedFolderConfig.rootPath);
        tableWidget->setItem(row, kSharedFolderNameColumn, nameItem);
        tableWidget->setItem(row, kSharedFolderPathColumn, pathItem);
    }
    if (_httpSharedFolderConfigs.isEmpty()) {
        tableWidget->clearSelection();
    } else {
        tableWidget->setCurrentCell(qBound(0, currentRow, _httpSharedFolderConfigs.size() - 1), 0);
    }
    refreshHttpServerUi();
}

void MainWindow::refreshHttpClientSourceTable()
{
    QTableWidget *tableWidget = _ui->httpClientTab->availableSourcesTableWidget();
    tableWidget->setRowCount(_httpRemoteSourceConfigs.size());
    for (int row = 0; row < _httpRemoteSourceConfigs.size(); ++row) {
        const HttpRemoteSourceConfig &remoteSourceConfig = _httpRemoteSourceConfigs.at(row);
        auto *checkedItem = new QTableWidgetItem;
        checkedItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        checkedItem->setCheckState(Qt::Unchecked);
        auto *nameItem = new QTableWidgetItem(remoteSourceConfig.name);
        auto *idItem = new QTableWidgetItem(remoteSourceConfig.id);
        auto *rootNameItem = new QTableWidgetItem(remoteSourceConfig.rootName);
        tableWidget->setItem(row, kAvailableSourceCheckColumn, checkedItem);
        tableWidget->setItem(row, kAvailableSourceNameColumn, nameItem);
        tableWidget->setItem(row, kAvailableSourceIdColumn, idItem);
        tableWidget->setItem(row, kAvailableSourceRootNameColumn, rootNameItem);
    }
}

void MainWindow::refreshPairTable()
{
    const int currentLocalPairIndex = currentSelectedPairIndex();
    const int currentHttpPairIndex = currentSelectedHttpClientPairIndex();
    _localPairIndexes = buildPairIndexesBySourceType(E_LocalDirectorySource);
    _httpClientPairIndexes = buildPairIndexesBySourceType(E_HttpDirectorySource);

    auto rebuildRows = [this](QStandardItemModel *tableModel, const QVector<int> &pairIndexes) {
        tableModel->setRowCount(0);
        for (int row = 0; row < pairIndexes.size(); ++row) {
            const int pairIndex = pairIndexes.at(row);
            if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
                continue;
            }

            const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
            const QString sourcePathText = buildSourceDisplayText(pairConfig);
            const QString targetPathText = buildTargetDisplayText(pairConfig);

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

            tableModel->setItem(row, kSourceColumn, sourceItem);
            tableModel->setItem(row, kTargetColumn, targetItem);
            tableModel->setItem(row, kStatusColumn, statusItem);
            tableModel->setItem(row, kActionColumn, actionItem);
            refreshStatusCell(pairIndex);
        }
    };

    rebuildRows(_localPairTableModel, _localPairIndexes);
    rebuildRows(_httpClientPairTableModel, _httpClientPairIndexes);
    selectPairRow(_ui->localSyncTab->pairTableView(),
                  _localPairTableModel,
                  _localPairIndexes,
                  currentLocalPairIndex);
    selectPairRow(_ui->httpClientTab->pairTableView(),
                  _httpClientPairTableModel,
                  _httpClientPairIndexes,
                  currentHttpPairIndex);

    refreshActionWidgets();
    updateControlState();
}

void MainWindow::refreshActionWidgets()
{
    auto refreshActionWidgetsForTable = [this](QTableView *tableView,
                                               QStandardItemModel *tableModel,
                                               const QVector<int> &pairIndexes) {
        for (int row = 0; row < tableModel->rowCount() && row < pairIndexes.size(); ++row) {
            const QModelIndex actionIndex = tableModel->index(row, kActionColumn);
            if (!actionIndex.isValid()) {
                continue;
            }

            tableView->setIndexWidget(actionIndex, createActionWidget(tableView, pairIndexes.at(row)));
        }

        tableView->setColumnWidth(kSourceColumn, kPathColumnMinimumWidth);
        tableView->setColumnWidth(kTargetColumn, kPathColumnMinimumWidth);
        tableView->setColumnWidth(kStatusColumn, kStatusColumnWidth);
        tableView->setColumnWidth(kActionColumn, kActionColumnWidth);
    };

    refreshActionWidgetsForTable(_ui->localSyncTab->pairTableView(), _localPairTableModel, _localPairIndexes);
    refreshActionWidgetsForTable(_ui->httpClientTab->pairTableView(),
                                 _httpClientPairTableModel,
                                 _httpClientPairIndexes);
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

    const QVector<int> enabledLocalPairIndexes = buildEnabledPairIndexes(E_LocalDirectorySource);
    if (enabledLocalPairIndexes.isEmpty()) {
        _folderWatcher->clear();
        return;
    }

    QStringList watchedFolders;
    watchedFolders.reserve(enabledLocalPairIndexes.size());
    for (int pairIndex : enabledLocalPairIndexes) {
        const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
        // 只监听 A 主目录，避免程序同步 B 时反复触发自身监控。
        watchedFolders.append(pairConfig.sourcePath);
    }

    _folderWatcher->setWatchedFolders(watchedFolders);
}

void MainWindow::updateControlState()
{
    const bool hasLocalPairs = !_localPairIndexes.isEmpty();
    const bool hasHttpClientPairs = !_httpClientPairIndexes.isEmpty();
    const bool hasLocalSelection = currentSelectedPairIndex() >= 0;
    const bool hasHttpClientSelection = currentSelectedHttpClientPairIndex() >= 0;
    const bool hasEnabledLocalPairs = !buildEnabledPairIndexes(E_LocalDirectorySource).isEmpty();
    const bool hasEnabledHttpClientPairs = !buildEnabledPairIndexes(E_HttpDirectorySource).isEmpty();
    const bool hasRunningSync = runningSyncCount() > 0;

    _ui->localSyncTab->pairTableView()->setEnabled(hasLocalPairs);
    _ui->localSyncTab->addPairButton()->setEnabled(true);
    _ui->localSyncTab->removePairButton()->setEnabled(!hasRunningSync && hasLocalSelection);
    _ui->localSyncTab->startMonitorButton()->setEnabled(!_isMonitoring && hasEnabledLocalPairs);
    _ui->localSyncTab->stopMonitorButton()->setEnabled(_isMonitoring);
    _ui->localSyncTab->syncAllPairsButton()->setEnabled(hasEnabledLocalPairs);
    _ui->httpClientTab->pairTableView()->setEnabled(hasHttpClientPairs);
    _ui->httpClientTab->removePairButton()->setEnabled(!hasRunningSync && hasHttpClientSelection);
    _ui->httpClientTab->syncAllPairsButton()->setEnabled(hasEnabledHttpClientPairs);
    _ui->httpClientTab->addSelectedSourcesButton()->setEnabled(!_httpRemoteSourceConfigs.isEmpty());
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
    QVector<int> startedPairIndexes;
    startedPairIndexes.reserve(availableSyncCount);
    for (int pairIndex : pendingPairIndexes) {
        if (startedPairCount >= availableSyncCount) {
            break;
        }

        if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size() || isPairSyncRunning(pairIndex)) {
            continue;
        }

        const QString pendingReason = _pendingSyncReasons.take(pairIndex);
        if (startPairSync(pairIndex, pendingReason, false)) {
            startedPairIndexes.append(pairIndex);
            ++startedPairCount;
        }
    }

    for (int pairIndex : startedPairIndexes) {
        kickoffPairSync(pairIndex);
    }
}

void MainWindow::refreshStatusCell(int pairIndex)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
    const qint64 progressMaximum = pairConfig.progressMaximum <= 0 ? 0 : pairConfig.progressMaximum;
    const qint64 progressValue = progressMaximum <= 0 ? 0 : qBound<qint64>(0, pairConfig.progressValue, progressMaximum);
    const QString stateText = buildPairStateText(pairConfig);
    const QString tooltipText = progressMaximum > 0
        ? tr("%1\n进度：%2/%3").arg(stateText).arg(progressValue).arg(progressMaximum)
        : tr("%1\n进度：处理中").arg(stateText);

    auto updateStatusItem = [&](QStandardItemModel *tableModel, const QVector<int> &pairIndexes) {
        const int row = pairIndexes.indexOf(pairIndex);
        if (row < 0) {
            return;
        }

        QStandardItem *statusItem = tableModel->item(row, kStatusColumn);
        if (statusItem == nullptr) {
            return;
        }

        statusItem->setData(stateText, Qt::DisplayRole);
        statusItem->setData(progressValue, E_ProgressValueRole);
        statusItem->setData(progressMaximum, E_ProgressMaximumRole);
        statusItem->setData(pairConfig.isSyncEnabled, E_IsSyncEnabledRole);
        statusItem->setToolTip(tooltipText);
    };
    updateStatusItem(_localPairTableModel, _localPairIndexes);
    updateStatusItem(_httpClientPairTableModel, _httpClientPairIndexes);
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
    requestSyncByIndexes(buildEnabledPairIndexes(E_LocalDirectorySource), reason);
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

    QVector<int> startedPairIndexes;
    startedPairIndexes.reserve(readyPairIndexes.size());
    for (int pairIndex : readyPairIndexes) {
        if (startPairSync(pairIndex, reason, false)) {
            startedPairIndexes.append(pairIndex);
        }
    }
    for (int pairIndex : startedPairIndexes) {
        kickoffPairSync(pairIndex);
    }

    const int startedPairCount = startedPairIndexes.size();
    const int pendingPairCount = pendingPairIndexes.size();

    if (startedPairCount > 0 || pendingPairCount > 0) {
        appendLog(tr("已发起并行同步请求：立即启动 %1 组，待当前任务完成后补同步 %2 组，reason=%3")
                      .arg(startedPairCount)
                      .arg(pendingPairCount)
                      .arg(reason));
        if (!startedPairIndexes.isEmpty()) {
            appendLog(tr("已立即启动：%1").arg(buildPairListSummaryText(startedPairIndexes)));
        }
    }
    if (pendingPairCount > 0) {
        appendLog(tr("已加入待同步队列：%1").arg(buildPairListSummaryText(pendingPairIndexes)));
    }

    startQueuedSyncs();
    refreshActionWidgets();
    updateControlState();
}

bool MainWindow::startPairSync(int pairIndex, const QString &reason, bool shouldKickoffImmediately)
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size() || isPairSyncRunning(pairIndex)) {
        return false;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
    auto *thread = new QThread(this);
    QObject *worker = nullptr;
    int workerKind = E_LocalFolderSyncWorker;
    if (isHttpSourcePair(pairConfig)) {
        worker = new HttpFolderSyncWorker();
        workerKind = E_HttpFolderSyncWorker;
    } else {
        worker = new FolderSyncWorker();
        workerKind = E_LocalFolderSyncWorker;
    }
    RunningSyncContext syncContext;
    syncContext.thread = thread;
    syncContext.worker = worker;
    syncContext.workerKind = workerKind;
    syncContext.currentStep = 0;
    syncContext.totalSteps = 0;
    syncContext.uiProgressTimer.start();
    syncContext.reason = reason;
    _runningSyncContexts.insert(pairIndex, syncContext);

    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connectWorkerSignals(worker, workerKind, pairIndex, thread);

    updatePairProgress(pairIndex, 0, 0);
    updatePairStatus(pairIndex, tr("准备扫描"));
    refreshActionWidgets();
    updateControlState();

    thread->start();
    if (shouldKickoffImmediately) {
        kickoffPairSync(pairIndex);
    }
    return true;
}

void MainWindow::kickoffPairSync(int pairIndex)
{
    if (!_runningSyncContexts.contains(pairIndex)
        || pairIndex < 0
        || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    RunningSyncContext &syncContext = _runningSyncContexts[pairIndex];
    if (syncContext.worker == nullptr) {
        return;
    }

    const FolderPairConfig &pairConfig = _folderPairConfigs.at(pairIndex);
    if (syncContext.workerKind == E_HttpFolderSyncWorker) {
        QMetaObject::invokeMethod(syncContext.worker,
                                  "slotStartSync",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, pairConfig.sourcePath),
                                  Q_ARG(QString, pairConfig.remoteSourceId),
                                  Q_ARG(QString, pairConfig.targetPath),
                                  Q_ARG(QString, pairConfig.sourceAccessToken),
                                  Q_ARG(QString, syncContext.reason),
                                  Q_ARG(int, _compareMode));
        return;
    }

    QMetaObject::invokeMethod(syncContext.worker,
                              "slotStartSync",
                              Qt::QueuedConnection,
                              Q_ARG(QString, pairConfig.sourcePath),
                              Q_ARG(QString, pairConfig.targetPath),
                              Q_ARG(QString, syncContext.reason),
                              Q_ARG(FolderSyncWorker::CompareMode,
                                    static_cast<FolderSyncWorker::CompareMode>(_compareMode)));
}

void MainWindow::connectWorkerSignals(QObject *worker, int workerKind, int pairIndex, QThread *thread)
{
    if (worker == nullptr || thread == nullptr) {
        return;
    }

    auto onStarted = [this, pairIndex](qint64 totalSteps,
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
    };
    auto onProgress = [this, pairIndex](qint64 currentStep, qint64 totalSteps, const QString &currentItem) {
        handlePairSyncProgress(pairIndex, currentStep, totalSteps, currentItem);
    };
    auto onFinished = [this, pairIndex, thread](bool success, const QString &summary) {
        handlePairSyncFinished(pairIndex, success, summary);
        if (thread->isRunning()) {
            thread->quit();
        }
    };
    auto onLog = [this, pairIndex](const QString &message) {
        appendLog(tr("第 %1 组：%2").arg(pairIndex + 1).arg(message));
    };

    if (workerKind == E_HttpFolderSyncWorker) {
        auto *httpWorker = qobject_cast<HttpFolderSyncWorker *>(worker);
        connect(httpWorker, &HttpFolderSyncWorker::sigSyncStarted, this, onStarted, Qt::QueuedConnection);
        connect(httpWorker, &HttpFolderSyncWorker::sigSyncProgress, this, onProgress, Qt::QueuedConnection);
        connect(httpWorker, &HttpFolderSyncWorker::sigSyncFinished, this, onFinished, Qt::QueuedConnection);
        connect(httpWorker, &HttpFolderSyncWorker::sigLogMessage, this, onLog, Qt::QueuedConnection);
        return;
    }

    auto *folderWorker = qobject_cast<FolderSyncWorker *>(worker);
    connect(folderWorker, &FolderSyncWorker::sigSyncStarted, this, onStarted, Qt::QueuedConnection);
    connect(folderWorker, &FolderSyncWorker::sigSyncProgress, this, onProgress, Qt::QueuedConnection);
    connect(folderWorker, &FolderSyncWorker::sigSyncFinished, this, onFinished, Qt::QueuedConnection);
    connect(folderWorker, &FolderSyncWorker::sigLogMessage, this, onLog, Qt::QueuedConnection);
}

bool MainWindow::invokeWorkerCancel(QObject *worker, int workerKind)
{
    if (worker == nullptr) {
        return false;
    }

    if (workerKind == E_HttpFolderSyncWorker) {
        return QMetaObject::invokeMethod(worker, "slotCancelSync", Qt::QueuedConnection);
    }

    return QMetaObject::invokeMethod(worker, "slotCancelSync", Qt::QueuedConnection);
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
    const bool isPhaseChanged = stripProgressSuffix(syncContext.lastUiStatusText)
        != stripProgressSuffix(currentItem);
    const bool isFinished = totalSteps > 0 && currentStep >= totalSteps;
    const bool shouldUpdateUi = isPhaseChanged
        || isFinished
        || syncContext.lastUiProgressValue < 0
        || syncContext.lastUiProgressMaximum != syncContext.totalSteps
        || !syncContext.uiProgressTimer.isValid()
        || syncContext.uiProgressTimer.elapsed() >= kProgressUiUpdateIntervalMs;
    if (!shouldUpdateUi) {
        return;
    }

    updatePairProgress(pairIndex, currentStep, totalSteps);
    updatePairStatus(pairIndex, currentItem);
    syncContext.lastUiProgressValue = currentStep;
    syncContext.lastUiProgressMaximum = syncContext.totalSteps;
    syncContext.lastUiStatusText = currentItem;
    syncContext.uiProgressTimer.restart();
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
    if (_maxParallelSyncCount == kAutoMaxParallelSyncCount) {
        return qMax(1, buildEnabledPairIndexes().size());
    }

    return qBound(1, _maxParallelSyncCount, 4);
}

QVector<int> MainWindow::buildEnabledPairIndexes() const
{
    return buildEnabledPairIndexes(-1);
}

QVector<int> MainWindow::buildEnabledPairIndexes(int sourceType) const
{
    QVector<int> pairIndexes;
    pairIndexes.reserve(_folderPairConfigs.size());
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        const FolderPairConfig &pairConfig = _folderPairConfigs.at(index);
        if (!pairConfig.isSyncEnabled) {
            continue;
        }

        if (sourceType >= 0 && pairConfig.sourceType != sourceType) {
            continue;
        }

        pairIndexes.append(index);
    }
    return pairIndexes;
}

QVector<int> MainWindow::buildPairIndexesBySourceType(int sourceType) const
{
    QVector<int> pairIndexes;
    pairIndexes.reserve(_folderPairConfigs.size());
    for (int index = 0; index < _folderPairConfigs.size(); ++index) {
        if (_folderPairConfigs.at(index).sourceType == sourceType) {
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
    return currentSelectedPairIndex(_ui->localSyncTab->pairTableView(), _localPairIndexes);
}

int MainWindow::currentSelectedHttpClientPairIndex() const
{
    return currentSelectedPairIndex(_ui->httpClientTab->pairTableView(), _httpClientPairIndexes);
}

int MainWindow::currentSelectedPairIndex(const QTableView *tableView, const QVector<int> &pairIndexes) const
{
    if (tableView == nullptr || tableView->selectionModel() == nullptr) {
        return -1;
    }

    const QModelIndexList selectedRows = tableView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        return -1;
    }

    const int row = selectedRows.first().row();
    return row >= 0 && row < pairIndexes.size() ? pairIndexes.at(row) : -1;
}

void MainWindow::selectPairRow(int pairIndex) const
{
    if (pairIndex < 0 || pairIndex >= _folderPairConfigs.size()) {
        return;
    }

    if (isHttpSourcePair(_folderPairConfigs.at(pairIndex))) {
        selectPairRow(_ui->httpClientTab->pairTableView(),
                      _httpClientPairTableModel,
                      _httpClientPairIndexes,
                      pairIndex);
        _ui->mainTabWidget->setCurrentWidget(_ui->httpClientTab);
        return;
    }

    selectPairRow(_ui->localSyncTab->pairTableView(), _localPairTableModel, _localPairIndexes, pairIndex);
    _ui->mainTabWidget->setCurrentWidget(_ui->localSyncTab);
}

void MainWindow::selectPairRow(QTableView *tableView,
                               QStandardItemModel *tableModel,
                               const QVector<int> &pairIndexes,
                               int pairIndex) const
{
    if (tableView == nullptr || tableModel == nullptr) {
        return;
    }

    const int row = pairIndexes.indexOf(pairIndex);
    if (row < 0 || row >= tableModel->rowCount()) {
        return;
    }

    const QModelIndex currentModelIndex = tableModel->index(row, kSourceColumn);
    if (!currentModelIndex.isValid()) {
        return;
    }

    tableView->setCurrentIndex(currentModelIndex);
    tableView->selectRow(row);
    tableView->scrollTo(currentModelIndex);
}

void MainWindow::addPair()
{
    FolderPairConfig pairConfig;
    const int newPairIndex = static_cast<int>(_folderPairConfigs.size());
    pairConfig.sourceType = E_LocalDirectorySource;
    pairConfig.remoteSourceId.clear();
    pairConfig.remoteSourceName.clear();
    pairConfig.statusText = tr("待同步");
    pairConfig.isSyncEnabled = true;
    if (!editPairConfig(&pairConfig, -1, tr("新增本地同步对"))) {
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
        invokeWorkerCancel(syncContext.worker, syncContext.workerKind);
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

QWidget *MainWindow::createActionWidget(QTableView *ownerTableView, int pairIndex)
{
    auto *container = new QWidget(ownerTableView);
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
    editButton->setToolTip(tr("编辑这一组同步配置"));
    syncButton->setToolTip(tr("立即同步这一组配置"));
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

bool MainWindow::isHttpSourcePair(const FolderPairConfig &pairConfig) const
{
    return pairConfig.sourceType == E_HttpDirectorySource;
}

QString MainWindow::buildSourceDisplayText(const FolderPairConfig &pairConfig) const
{
    if (isHttpSourcePair(pairConfig)) {
        const QString sourceName = pairConfig.remoteSourceName.isEmpty()
            ? pairConfig.remoteSourceId
            : pairConfig.remoteSourceName;
        return sourceName.isEmpty() ? tr("[HTTP] %1").arg(pairConfig.sourcePath)
                                    : tr("[HTTP] %1 @ %2").arg(sourceName, pairConfig.sourcePath);
    }

    return QDir::toNativeSeparators(pairConfig.sourcePath);
}

QString MainWindow::buildTargetDisplayText(const FolderPairConfig &pairConfig) const
{
    return QDir::toNativeSeparators(pairConfig.targetPath);
}

QString MainWindow::buildPairDisplayText(const FolderPairConfig &pairConfig, int pairIndex) const
{
    return tr("第 %1 组 [%2 -> %3]")
        .arg(pairIndex + 1)
        .arg(buildSourceDisplayText(pairConfig))
        .arg(buildTargetDisplayText(pairConfig));
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

QString MainWindow::normalizeLocalPath(const QString &path) const
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

QString MainWindow::normalizeHttpSourceUrl(const QString &sourceUrl) const
{
    return normalizeHttpSourceUrlInternal(sourceUrl);
}

QString MainWindow::buildHttpClientSourceListUrl(const QString &sourceUrl) const
{
    QUrl sourcesUrl(normalizeHttpSourceUrl(sourceUrl));
    QString path = sourcesUrl.path();
    if (!path.endsWith(QStringLiteral("/sources"))) {
        path += QStringLiteral("/sources");
    }
    sourcesUrl.setPath(path);
    sourcesUrl.setQuery(QString());
    sourcesUrl.setFragment(QString());
    return sourcesUrl.toString(QUrl::RemoveFragment);
}

QString MainWindow::buildHttpClientImportPath(const HttpRemoteSourceConfig &remoteSourceConfig) const
{
    QString folderName = remoteSourceConfig.name.trimmed();
    if (folderName.isEmpty()) {
        folderName = remoteSourceConfig.rootName.trimmed();
    }
    if (folderName.isEmpty()) {
        folderName = remoteSourceConfig.id.trimmed();
    }

    folderName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    return QDir(_httpClientTargetRootPath).absoluteFilePath(folderName);
}

bool MainWindow::validateHttpSharedFolderConfig(const HttpSharedFolderConfig &sharedFolderConfig,
                                                int ignoredIndex,
                                                QString *errorMessage) const
{
    if (sharedFolderConfig.name.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("共享目录名称不能为空。");
        }
        return false;
    }

    if (sharedFolderConfig.rootPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("共享目录路径不能为空。");
        }
        return false;
    }

    const QFileInfo rootInfo(sharedFolderConfig.rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("共享目录不存在或不是目录，path=%1").arg(sharedFolderConfig.rootPath);
        }
        return false;
    }

    for (int index = 0; index < _httpSharedFolderConfigs.size(); ++index) {
        if (index == ignoredIndex) {
            continue;
        }

        const HttpSharedFolderConfig &otherConfig = _httpSharedFolderConfigs.at(index);
        if (otherConfig.name.compare(sharedFolderConfig.name, Qt::CaseInsensitive) == 0) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("共享目录名称不能重复，name=%1").arg(sharedFolderConfig.name);
            }
            return false;
        }

        if (isSameOrNestedPath(sharedFolderConfig.rootPath, otherConfig.rootPath)) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("共享目录之间不能相同或互相嵌套，path1=%1，path2=%2")
                                    .arg(sharedFolderConfig.rootPath, otherConfig.rootPath);
            }
            return false;
        }
    }

    return true;
}

bool MainWindow::editPairConfig(FolderPairConfig *pairConfig, int ignoredIndex, const QString &windowTitle) const
{
    if (pairConfig == nullptr) {
        return false;
    }

    PairEditDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(windowTitle);
    dialog.setSourceType(static_cast<PairSourceType>(pairConfig->sourceType));
    dialog.setSourceLocation(pairConfig->sourcePath);
    dialog.setSourceAccessToken(pairConfig->sourceAccessToken);
    dialog.setTargetPath(pairConfig->targetPath);

    while (dialog.exec() == QDialog::Accepted) {
        FolderPairConfig updatedConfig = *pairConfig;
        updatedConfig.sourceType = dialog.sourceType();
        const QString sourceLocation = dialog.sourceLocation();
        if (updatedConfig.sourceType == E_HttpDirectorySource) {
            const QString normalizedSourceUrl = normalizeHttpSourceUrl(sourceLocation);
            updatedConfig.sourcePath = normalizedSourceUrl.isEmpty() ? sourceLocation.trimmed() : normalizedSourceUrl;
            updatedConfig.sourceAccessToken = dialog.sourceAccessToken();
        } else {
            updatedConfig.sourcePath = normalizeLocalPath(sourceLocation);
            updatedConfig.sourceAccessToken.clear();
            updatedConfig.remoteSourceId.clear();
            updatedConfig.remoteSourceName.clear();
        }
        updatedConfig.targetPath = normalizeLocalPath(dialog.targetPath());
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
            *errorMessage = pairConfig.sourceType == E_HttpDirectorySource ? tr("请先填写 HTTP 源地址。")
                                                                           : tr("请先填写 A 主目录。");
        }
        return false;
    }

    if (pairConfig.targetPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("请先填写 B 备份目录。");
        }
        return false;
    }

    if (!isHttpSourcePair(pairConfig)) {
        const QFileInfo sourceInfo(pairConfig.sourcePath);
        if (sourceInfo.exists() && !sourceInfo.isDir()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("A 主路径已存在但不是目录，path=%1").arg(pairConfig.sourcePath);
            }
            return false;
        }
    }

    const QFileInfo targetInfo(pairConfig.targetPath);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("B 备份路径已存在但不是目录，path=%1").arg(pairConfig.targetPath);
        }
        return false;
    }

    if (!isHttpSourcePair(pairConfig) && isSameOrNestedPath(pairConfig.sourcePath, pairConfig.targetPath)) {
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
        bool targetConflict = isSameOrNestedPath(pairConfig.targetPath, existingPair.targetPath);
        if (!isHttpSourcePair(existingPair)) {
            targetConflict = targetConflict || isSameOrNestedPath(pairConfig.targetPath, existingPair.sourcePath);
        }
        if (!isHttpSourcePair(pairConfig)) {
            targetConflict = targetConflict || isSameOrNestedPath(pairConfig.sourcePath, existingPair.targetPath);
        }
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

    if (isHttpSourcePair(pairConfig)) {
        const QString normalizedSourceUrl = normalizeHttpSourceUrl(pairConfig.sourcePath);
        const QUrl sourceUrl(normalizedSourceUrl);
        if (normalizedSourceUrl.isEmpty()
            || !sourceUrl.isValid()
            || sourceUrl.scheme().isEmpty()
            || sourceUrl.host().isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 源地址无效，url=%1").arg(pairConfig.sourcePath);
            }
            return false;
        }
    }

    return true;
}
