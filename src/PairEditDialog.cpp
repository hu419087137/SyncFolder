#include "PairEditDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

#include "ui_PairEditDialog.h"

namespace
{
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
} // namespace

PairEditDialog::PairEditDialog(QWidget *parent)
    : QDialog(parent),
      _ui(new Ui::PairEditDialog)
{
    _ui->setupUi(this);

    _ui->sourceTypeComboBox->addItem(tr("本地目录"), E_LocalDirectorySource);
    _ui->sourceTypeComboBox->addItem(tr("HTTP 源目录"), E_HttpDirectorySource);

    connect(_ui->sourceTypeComboBox, &QComboBox::currentIndexChanged, this, &PairEditDialog::slotSourceTypeChanged);
    connect(_ui->browseSourceButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseSourceFolder);
    connect(_ui->browseTargetButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseTargetFolder);

    setSourceType(E_LocalDirectorySource);
}

PairEditDialog::~PairEditDialog()
{
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
        ? normalizeHttpSourceUrl(_ui->sourcePathEdit->text())
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
    _ui->modeHintLabel->setText(
        isHttpSource
            ? tr("HTTP 模式会从远端 A 设备拉取清单和文件，再同步到当前电脑的 B 目录。")
            : tr("本地模式会把当前电脑上的 A 主目录直接同步到 B 备份目录。"));
}
