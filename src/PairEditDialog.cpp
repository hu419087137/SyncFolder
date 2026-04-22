#include "PairEditDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include "ui_PairEditDialog.h"

namespace
{
constexpr int kProbeTimeoutMs = 15000;
constexpr int kPreviewEntryLimit = 80;

QString normalizeLocalPath(const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

QString normalizeHttpSourceUrl(const QString &sourceUrl)
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

QUrl buildManifestUrl(const QString &normalizedSourceUrl)
{
    QUrl manifestUrl(normalizedSourceUrl);
    QString path = manifestUrl.path();
    if (!path.endsWith(QStringLiteral("/manifest"))) {
        path += QStringLiteral("/manifest");
    }
    manifestUrl.setPath(path);
    manifestUrl.setQuery(QString());
    manifestUrl.setFragment(QString());
    return manifestUrl;
}

QNetworkRequest buildProbeRequest(const QUrl &requestUrl, const QString &accessToken)
{
    QNetworkRequest request(requestUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kProbeTimeoutMs);
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    if (!accessToken.trimmed().isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             QStringLiteral("Bearer %1").arg(accessToken.trimmed()).toUtf8());
    }
    return request;
}

QString buildFileSizeText(qint64 fileSize)
{
    if (fileSize < 1024) {
        return QObject::tr("%1 B").arg(fileSize);
    }

    const double fileSizeKb = static_cast<double>(fileSize) / 1024.0;
    if (fileSizeKb < 1024.0) {
        return QObject::tr("%1 KB").arg(fileSizeKb, 0, 'f', 1);
    }

    const double fileSizeMb = fileSizeKb / 1024.0;
    if (fileSizeMb < 1024.0) {
        return QObject::tr("%1 MB").arg(fileSizeMb, 0, 'f', 1);
    }

    const double fileSizeGb = fileSizeMb / 1024.0;
    return QObject::tr("%1 GB").arg(fileSizeGb, 0, 'f', 2);
}
} // namespace

PairEditDialog::PairEditDialog(QWidget *parent)
    : QDialog(parent),
      _ui(new Ui::PairEditDialog),
      _networkAccessManager(new QNetworkAccessManager(this)),
      _activeProbeActionKind(E_NoProbeAction)
{
    _ui->setupUi(this);

    _ui->sourceTypeComboBox->addItem(tr("本地目录"), E_LocalDirectorySource);
    _ui->sourceTypeComboBox->addItem(tr("HTTP 源目录"), E_HttpDirectorySource);
    _ui->manifestPreviewEdit->setReadOnly(true);
    _ui->manifestPreviewEdit->setPlaceholderText(tr("点击“测试连接”或“预览清单”后，这里会显示远端目录摘要。"));
    _ui->probeStatusLabel->setText(tr("HTTP 模式下可先测试连接，再预览远端清单。"));

    connect(_ui->sourceTypeComboBox, &QComboBox::currentIndexChanged, this, &PairEditDialog::slotSourceTypeChanged);
    connect(_ui->browseSourceButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseSourceFolder);
    connect(_ui->browseTargetButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseTargetFolder);
    connect(_ui->testConnectionButton, &QPushButton::clicked, this, &PairEditDialog::slotTestSourceConnection);
    connect(_ui->previewManifestButton, &QPushButton::clicked, this, &PairEditDialog::slotPreviewSourceManifest);
    connect(_ui->sourcePathEdit, &QLineEdit::textChanged, this, &PairEditDialog::slotProbeInputChanged);
    connect(_ui->sourceAccessTokenEdit, &QLineEdit::textChanged, this, &PairEditDialog::slotProbeInputChanged);

    setSourceType(E_LocalDirectorySource);
}

PairEditDialog::~PairEditDialog()
{
    if (_activeProbeReply != nullptr) {
        _activeProbeReply->abort();
    }
    delete _ui;
}

void PairEditDialog::setSourceType(PairSourceType sourceType)
{
    const int sourceTypeIndex = _ui->sourceTypeComboBox->findData(sourceType);
    _ui->sourceTypeComboBox->setCurrentIndex(sourceTypeIndex >= 0 ? sourceTypeIndex : 0);
    applySourceTypeUi(sourceType);
}

void PairEditDialog::setSourceLocation(const QString &sourceLocation)
{
    const PairSourceType currentSourceType = sourceType();
    _ui->sourcePathEdit->setText(currentSourceType == E_HttpDirectorySource
                                     ? sourceLocation.trimmed()
                                     : QDir::toNativeSeparators(sourceLocation));
}

void PairEditDialog::setTargetPath(const QString &targetPath)
{
    _ui->targetPathEdit->setText(QDir::toNativeSeparators(targetPath));
}

void PairEditDialog::setSourceAccessToken(const QString &sourceAccessToken)
{
    _ui->sourceAccessTokenEdit->setText(sourceAccessToken);
}

PairSourceType PairEditDialog::sourceType() const
{
    return static_cast<PairSourceType>(_ui->sourceTypeComboBox->currentData().toInt());
}

QString PairEditDialog::sourceLocation() const
{
    return sourceType() == E_HttpDirectorySource
        ? _ui->sourcePathEdit->text().trimmed()
        : normalizeLocalPath(_ui->sourcePathEdit->text());
}

QString PairEditDialog::targetPath() const
{
    return normalizeLocalPath(_ui->targetPathEdit->text());
}

QString PairEditDialog::sourceAccessToken() const
{
    return _ui->sourceAccessTokenEdit->text().trimmed();
}

void PairEditDialog::slotSourceTypeChanged(int index)
{
    applySourceTypeUi(static_cast<PairSourceType>(_ui->sourceTypeComboBox->itemData(index).toInt()));
    slotProbeInputChanged();
}

void PairEditDialog::slotBrowseSourceFolder()
{
    if (sourceType() != E_LocalDirectorySource) {
        return;
    }

    const QString currentPath = sourceLocation();
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 A 主目录"),
        currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!selectedPath.isEmpty()) {
        setSourceLocation(selectedPath);
    }
}

void PairEditDialog::slotBrowseTargetFolder()
{
    const QString currentPath = targetPath();
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 B 备份目录"),
        currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!selectedPath.isEmpty()) {
        setTargetPath(selectedPath);
    }
}

void PairEditDialog::slotTestSourceConnection()
{
    startProbeRequest(E_TestConnectionAction);
}

void PairEditDialog::slotPreviewSourceManifest()
{
    startProbeRequest(E_PreviewManifestAction);
}

void PairEditDialog::slotProbeReplyFinished()
{
    if (_activeProbeReply == nullptr) {
        updateProbeUiState(false);
        _activeProbeActionKind = E_NoProbeAction;
        return;
    }

    QNetworkReply *reply = _activeProbeReply;
    _activeProbeReply.clear();
    updateProbeUiState(false);

    const QByteArray replyBody = reply->readAll();
    const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString errorMessage;
    if (reply->error() != QNetworkReply::NoError) {
        errorMessage = tr("连接失败，httpStatus=%1，error=%2")
                           .arg(httpStatusCode)
                           .arg(reply->errorString());
    } else if (httpStatusCode < 200 || httpStatusCode >= 300) {
        const QString bodyText = QString::fromUtf8(replyBody).trimmed();
        errorMessage = bodyText.isEmpty() ? tr("连接失败，httpStatus=%1").arg(httpStatusCode)
                                          : tr("连接失败，httpStatus=%1，body=%2")
                                                .arg(httpStatusCode)
                                                .arg(bodyText);
    }

    if (!errorMessage.isEmpty()) {
        _ui->probeStatusLabel->setText(errorMessage);
        if (_activeProbeActionKind == E_PreviewManifestAction) {
            _ui->manifestPreviewEdit->setPlainText(QString());
        }
        reply->deleteLater();
        _activeProbeActionKind = E_NoProbeAction;
        return;
    }

    QString summaryText;
    QString previewText;
    if (!buildManifestPreviewText(replyBody, &summaryText, &previewText, &errorMessage)) {
        _ui->probeStatusLabel->setText(errorMessage);
        if (_activeProbeActionKind == E_PreviewManifestAction) {
            _ui->manifestPreviewEdit->setPlainText(QString());
        }
        reply->deleteLater();
        _activeProbeActionKind = E_NoProbeAction;
        return;
    }

    _ui->probeStatusLabel->setText(summaryText);
    if (_activeProbeActionKind == E_PreviewManifestAction) {
        _ui->manifestPreviewEdit->setPlainText(previewText);
    }

    reply->deleteLater();
    _activeProbeActionKind = E_NoProbeAction;
}

void PairEditDialog::slotProbeInputChanged()
{
    if (_activeProbeReply != nullptr) {
        return;
    }

    clearProbeResult();
}

void PairEditDialog::applySourceTypeUi(PairSourceType sourceType)
{
    const bool isHttpSource = sourceType == E_HttpDirectorySource;
    _ui->sourceLabel->setText(isHttpSource ? tr("HTTP 源地址") : tr("A 主目录"));
    _ui->sourcePathEdit->setPlaceholderText(isHttpSource
                                                ? tr("例如 http://192.168.1.10:8086/api/v1")
                                                : tr("请输入或选择 A 主目录"));
    _ui->sourceAccessTokenEdit->setEnabled(isHttpSource);
    _ui->browseSourceButton->setEnabled(!isHttpSource);
    _ui->browseSourceButton->setToolTip(isHttpSource ? tr("HTTP 模式下请直接填写服务地址") : tr("浏览本地目录"));
    _ui->sourceAccessTokenEdit->setPlaceholderText(
        isHttpSource ? tr("可选：填写 A 端 HTTP 服务访问令牌") : tr("本地目录模式下无需填写访问令牌"));
    _ui->testConnectionButton->setVisible(isHttpSource);
    _ui->previewManifestButton->setVisible(isHttpSource);
    _ui->probeStatusLabel->setVisible(isHttpSource);
    _ui->manifestPreviewEdit->setVisible(isHttpSource);
    _ui->modeHintLabel->setText(
        isHttpSource
            ? tr("HTTP 模式会从远端 A 设备拉取清单和文件，再同步到当前电脑的 B 目录。")
            : tr("本地模式会把当前电脑上的 A 主目录直接同步到 B 备份目录。"));
    clearProbeResult();
}

void PairEditDialog::updateProbeUiState(bool isBusy)
{
    _ui->testConnectionButton->setEnabled(!isBusy);
    _ui->previewManifestButton->setEnabled(!isBusy);
    if (isBusy) {
        _ui->probeStatusLabel->setText(_activeProbeActionKind == E_TestConnectionAction
                                           ? tr("正在测试远端连接...")
                                           : tr("正在拉取远端清单预览..."));
    }
}

void PairEditDialog::clearProbeResult(bool preserveStatusText)
{
    if (sourceType() != E_HttpDirectorySource) {
        _ui->probeStatusLabel->setText(QString());
        _ui->manifestPreviewEdit->setPlainText(QString());
        return;
    }

    if (!preserveStatusText) {
        _ui->probeStatusLabel->setText(tr("HTTP 模式下可先测试连接，再预览远端清单。"));
    }
    _ui->manifestPreviewEdit->setPlainText(QString());
}

bool PairEditDialog::startProbeRequest(ProbeActionKind probeActionKind)
{
    if (sourceType() != E_HttpDirectorySource) {
        return false;
    }

    if (_activeProbeReply != nullptr) {
        _activeProbeReply->abort();
        _activeProbeReply.clear();
    }

    const QString normalizedSourceUrl = normalizeHttpSourceUrl(_ui->sourcePathEdit->text());
    if (normalizedSourceUrl.isEmpty()) {
        _ui->probeStatusLabel->setText(tr("请先填写有效的 HTTP 源地址，例如 http://192.168.1.10:8086/api/v1"));
        _ui->manifestPreviewEdit->setPlainText(QString());
        return false;
    }

    _activeProbeActionKind = probeActionKind;
    QNetworkRequest request = buildProbeRequest(buildManifestUrl(normalizedSourceUrl), sourceAccessToken());
    _activeProbeReply = _networkAccessManager->get(request);
    connect(_activeProbeReply, &QNetworkReply::finished, this, &PairEditDialog::slotProbeReplyFinished);
    updateProbeUiState(true);
    if (probeActionKind == E_TestConnectionAction) {
        _ui->manifestPreviewEdit->setPlainText(QString());
    }
    return true;
}

bool PairEditDialog::buildManifestPreviewText(const QByteArray &manifestJson,
                                              QString *summaryText,
                                              QString *previewText,
                                              QString *errorMessage) const
{
    if (summaryText == nullptr || previewText == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("清单预览输出参数不能为空。");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("解析远端清单失败，error=%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject manifestObject = manifestDocument.object();
    const QJsonArray entryArray = manifestObject.value(QStringLiteral("entries")).toArray();
    int fileCount = 0;
    int directoryCount = 0;
    QStringList previewLines;
    previewLines.reserve(qMin(entryArray.size(), kPreviewEntryLimit) + 6);
    previewLines.append(tr("根目录：%1").arg(manifestObject.value(QStringLiteral("rootName")).toString()));
    const qint64 generatedAtMs = static_cast<qint64>(
        manifestObject.value(QStringLiteral("generatedAtMs")).toDouble(0));
    if (generatedAtMs > 0) {
        previewLines.append(tr("生成时间：%1").arg(
            QDateTime::fromMSecsSinceEpoch(generatedAtMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }

    for (int index = 0; index < entryArray.size(); ++index) {
        if (!entryArray.at(index).isObject()) {
            continue;
        }

        const QJsonObject entryObject = entryArray.at(index).toObject();
        const QString entryPath = entryObject.value(QStringLiteral("path")).toString();
        const QString entryType = entryObject.value(QStringLiteral("type")).toString();
        if (entryType == QStringLiteral("dir")) {
            ++directoryCount;
            if (previewLines.size() < kPreviewEntryLimit + 6) {
                previewLines.append(tr("[DIR ] %1").arg(entryPath));
            }
            continue;
        }

        if (entryType == QStringLiteral("file")) {
            ++fileCount;
            if (previewLines.size() < kPreviewEntryLimit + 6) {
                previewLines.append(
                    tr("[FILE] %1  (%2)")
                        .arg(entryPath)
                        .arg(buildFileSizeText(static_cast<qint64>(
                            entryObject.value(QStringLiteral("size")).toDouble(0)))));
            }
        }
    }

    *summaryText = tr("连接成功：目录 %1 个，文件 %2 个，总条目 %3 个。")
                       .arg(directoryCount)
                       .arg(fileCount)
                       .arg(entryArray.size());
    if (entryArray.size() > kPreviewEntryLimit) {
        previewLines.append(tr("... 其余 %1 个条目未展开显示").arg(entryArray.size() - kPreviewEntryLimit));
    }
    *previewText = previewLines.join(QLatin1Char('\n'));
    return true;
}
