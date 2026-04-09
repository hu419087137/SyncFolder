#include "PairEditDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include "ui_PairEditDialog.h"

namespace
{
QString normalizePath(const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}
} // namespace

PairEditDialog::PairEditDialog(QWidget *parent)
    : QDialog(parent),
      _ui(new Ui::PairEditDialog)
{
    _ui->setupUi(this);

    connect(_ui->browseSourceButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseSourceFolder);
    connect(_ui->browseTargetButton, &QPushButton::clicked, this, &PairEditDialog::slotBrowseTargetFolder);
}

PairEditDialog::~PairEditDialog()
{
    delete _ui;
}

void PairEditDialog::setSourcePath(const QString &sourcePath)
{
    _ui->sourcePathEdit->setText(QDir::toNativeSeparators(sourcePath));
}

void PairEditDialog::setTargetPath(const QString &targetPath)
{
    _ui->targetPathEdit->setText(QDir::toNativeSeparators(targetPath));
}

QString PairEditDialog::sourcePath() const
{
    return normalizePath(_ui->sourcePathEdit->text());
}

QString PairEditDialog::targetPath() const
{
    return normalizePath(_ui->targetPathEdit->text());
}

void PairEditDialog::slotBrowseSourceFolder()
{
    const QString currentPath = sourcePath();
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        tr("选择 A 主目录"),
        currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!selectedPath.isEmpty()) {
        setSourcePath(selectedPath);
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
