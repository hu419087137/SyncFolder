#include "FolderSyncWorker.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtGlobal>
#include <QVector>

#include <filesystem>
namespace
{
constexpr qint64 kTimestampToleranceMs = 2000;

enum class SyncEntryType
{
    E_Unknown,
    E_Directory,
    E_File
};

enum class SyncOperationType
{
    E_RemoveFile,
    E_RemoveDirectory,
    E_CreateDirectory,
    E_CopyFile
};

struct SyncOperation
{
    SyncOperationType type = SyncOperationType::E_CopyFile;
    QString sourcePath;
    QString targetPath;
    QString relativePath;
};

QString normalizePath(const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(QFileInfo(trimmedPath).absoluteFilePath());
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool isSameOrNestedPath(const QString &sourcePath, const QString &targetPath)
{
    const QString normalizedSourcePath = QDir::cleanPath(sourcePath);
    const QString normalizedTargetPath = QDir::cleanPath(targetPath);
    const Qt::CaseSensitivity caseSensitivity = pathCaseSensitivity();

    if (normalizedSourcePath.compare(normalizedTargetPath, caseSensitivity) == 0) {
        return true;
    }

    const QString sourcePrefix = normalizedSourcePath + QDir::separator();
    const QString targetPrefix = normalizedTargetPath + QDir::separator();
    return normalizedSourcePath.startsWith(targetPrefix, caseSensitivity)
        || normalizedTargetPath.startsWith(sourcePrefix, caseSensitivity);
}

QString normalizeRelativePath(const QDir &rootDir, const QString &absolutePath)
{
    return QDir::fromNativeSeparators(rootDir.relativeFilePath(absolutePath));
}

QString displayRelativePath(const QString &relativePath)
{
    return relativePath.isEmpty() ? QObject::tr(".") : relativePath;
}

QString describeOperation(const SyncOperation &operation)
{
    switch (operation.type) {
    case SyncOperationType::E_RemoveFile:
        return QObject::tr("删除文件：%1").arg(displayRelativePath(operation.relativePath));
    case SyncOperationType::E_RemoveDirectory:
        return QObject::tr("删除目录：%1").arg(displayRelativePath(operation.relativePath));
    case SyncOperationType::E_CreateDirectory:
        return QObject::tr("创建目录：%1").arg(displayRelativePath(operation.relativePath));
    case SyncOperationType::E_CopyFile:
        return QObject::tr("复制文件：%1").arg(displayRelativePath(operation.relativePath));
    }

    return QObject::tr("未知步骤：%1").arg(displayRelativePath(operation.relativePath));
}

bool isUnderRemovedDirectoryRoot(const QString &relativePath, const QStringList &removedDirectoryRoots)
{
    for (const QString &removedRoot : removedDirectoryRoots) {
        if (relativePath == removedRoot || relativePath.startsWith(removedRoot + QLatin1Char('/'))) {
            return true;
        }
    }

    return false;
}

void appendUniqueOperation(const SyncOperation &operation,
                           QSet<QString> *operationKeys,
                           QVector<SyncOperation> *operations)
{
    const QString key = QStringLiteral("%1|%2")
                            .arg(static_cast<int>(operation.type))
                            .arg(QDir::cleanPath(operation.targetPath));
    if (operationKeys->contains(key)) {
        return;
    }

    operationKeys->insert(key);
    operations->append(operation);
}

bool isFileDifferent(const QFileInfo &sourceInfo, const QFileInfo &targetInfo)
{
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        return true;
    }

    if (sourceInfo.size() != targetInfo.size()) {
        return true;
    }

    const qint64 timestampDeltaMs =
        qAbs(sourceInfo.lastModified().toMSecsSinceEpoch() - targetInfo.lastModified().toMSecsSinceEpoch());
    return timestampDeltaMs > kTimestampToleranceMs;
}

bool ensureDirectoryExists(const QString &directoryPath, QString *errorMessage)
{
    QDir directory(directoryPath);
    if (directory.exists()) {
        return true;
    }

    if (QDir().mkpath(directoryPath)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("创建目录失败，path=%1").arg(directoryPath);
    }

    return false;
}

bool removeFileAtPath(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.exists()) {
        return true;
    }

    file.setPermissions(file.permissions() | QFileDevice::WriteOwner);
    if (file.remove()) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("删除文件失败，path=%1，error=%2").arg(filePath, file.errorString());
    }

    return false;
        // try {
        //     std::filesystem::path fsPath(filePath.toStdString());
        //     return std::filesystem::remove_all(fsPath);
        // } catch (const std::exception &e) {
        //     qWarning() << "Error removing path:" << e.what();
        //     *errorMessage = QObject::tr("删除文件失败，path=%1，error=%2").arg(filePath, e.what());
        //     return false;
        // }
}

bool removeDirectoryAtPath(const QString &directoryPath, QString *errorMessage)
{
    QDir directory(directoryPath);
    if (!directory.exists()) {
        return true;
    }

    if (directory.removeRecursively()) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("删除目录失败，path=%1").arg(directoryPath);
    }

    return false;
}

bool copyFileWithMetadata(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    const QFileInfo targetInfo(targetPath);
    if (!ensureDirectoryExists(targetInfo.dir().absolutePath(), errorMessage)) {
        return false;
    }

    if (QFileInfo::exists(targetPath) && !removeFileAtPath(targetPath, errorMessage)) {
        return false;
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("复制文件失败，source=%1，target=%2").arg(sourcePath, targetPath);
        }
        return false;
    }

    const QFileInfo sourceInfo(sourcePath);
    QFile targetFile(targetPath);
    if (!targetFile.setPermissions(sourceInfo.permissions())) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("复制成功但设置权限失败，target=%1").arg(targetPath);
        }
        return false;
    }

    if (!targetFile.open(QIODevice::ReadWrite)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("复制成功但无法打开目标文件设置时间，target=%1，error=%2")
                                .arg(targetPath, targetFile.errorString());
        }
        return false;
    }

    const bool fileTimeUpdated =
        targetFile.setFileTime(sourceInfo.lastModified(), QFileDevice::FileModificationTime);
    targetFile.close();
    if (!fileTimeUpdated) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("复制成功但设置修改时间失败，target=%1").arg(targetPath);
        }
        return false;
    }

    return true;
}

bool collectSourceEntries(const QString &sourcePath,
                          QHash<QString, SyncEntryType> *sourceEntries,
                          QString *errorMessage)
{
    QDir sourceRoot(sourcePath);
    QDirIterator iterator(sourcePath,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();

        const QFileInfo fileInfo = iterator.fileInfo();
        if (fileInfo.isSymLink()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("检测到不支持的符号链接，path=%1").arg(fileInfo.absoluteFilePath());
            }
            return false;
        }

        const QString relativePath = normalizeRelativePath(sourceRoot, fileInfo.absoluteFilePath());
        if (fileInfo.isDir()) {
            sourceEntries->insert(relativePath, SyncEntryType::E_Directory);
            continue;
        }

        if (fileInfo.isFile()) {
            sourceEntries->insert(relativePath, SyncEntryType::E_File);
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("检测到不支持的文件类型，path=%1").arg(fileInfo.absoluteFilePath());
        }
        return false;
    }

    return true;
}

bool buildSyncPlan(const QString &sourcePath,
                   const QString &targetPath,
                   QVector<SyncOperation> *operations,
                   QString *errorMessage)
{
    QHash<QString, SyncEntryType> sourceEntries;
    if (!collectSourceEntries(sourcePath, &sourceEntries, errorMessage)) {
        return false;
    }

    const QDir targetRoot(targetPath);
    QVector<SyncOperation> removeFileOperations;
    QVector<SyncOperation> removeDirectoryOperations;
    QVector<SyncOperation> createDirectoryOperations;
    QVector<SyncOperation> copyFileOperations;
    QSet<QString> operationKeys;
    QStringList removedDirectoryRoots;

    QDirIterator targetIterator(targetPath,
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
    while (targetIterator.hasNext()) {
        targetIterator.next();

        const QFileInfo targetInfo = targetIterator.fileInfo();
        if (targetInfo.isSymLink()) {
            if (errorMessage != nullptr) {
                *errorMessage =
                    QObject::tr("备份目录中存在不支持的符号链接，path=%1").arg(targetInfo.absoluteFilePath());
            }
            return false;
        }

        const QString relativePath = normalizeRelativePath(targetRoot, targetInfo.absoluteFilePath());
        if (isUnderRemovedDirectoryRoot(relativePath, removedDirectoryRoots)) {
            continue;
        }

        const SyncEntryType sourceEntryType = sourceEntries.value(relativePath, SyncEntryType::E_Unknown);
        if (targetInfo.isDir()) {
            if (sourceEntryType == SyncEntryType::E_File || sourceEntryType == SyncEntryType::E_Unknown) {
                appendUniqueOperation({SyncOperationType::E_RemoveDirectory, QString(), targetInfo.absoluteFilePath(),
                                       relativePath},
                                      &operationKeys,
                                      &removeDirectoryOperations);
                removedDirectoryRoots.append(relativePath);
            }
            continue;
        }

        if (targetInfo.isFile()) {
            if (sourceEntryType == SyncEntryType::E_Directory || sourceEntryType == SyncEntryType::E_Unknown) {
                appendUniqueOperation(
                    {SyncOperationType::E_RemoveFile, QString(), targetInfo.absoluteFilePath(), relativePath},
                    &operationKeys,
                    &removeFileOperations);
            }
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("备份目录中存在不支持的文件类型，path=%1")
                                .arg(targetInfo.absoluteFilePath());
        }
        return false;
    }

    const QDir sourceRoot(sourcePath);
    QDirIterator sourceIterator(sourcePath,
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
    while (sourceIterator.hasNext()) {
        sourceIterator.next();

        const QFileInfo sourceInfo = sourceIterator.fileInfo();
        const QString relativePath = normalizeRelativePath(sourceRoot, sourceInfo.absoluteFilePath());
        const QString targetAbsolutePath = targetRoot.absoluteFilePath(relativePath);
        const QFileInfo targetInfo(targetAbsolutePath);

        if (sourceInfo.isDir()) {
            if (!targetInfo.exists() || !targetInfo.isDir()) {
                appendUniqueOperation(
                    {SyncOperationType::E_CreateDirectory, QString(), targetAbsolutePath, relativePath},
                    &operationKeys,
                    &createDirectoryOperations);
            }
            continue;
        }

        if (sourceInfo.isFile()) {
            if (!targetInfo.exists() || !targetInfo.isFile() || isFileDifferent(sourceInfo, targetInfo)) {
                appendUniqueOperation(
                    {SyncOperationType::E_CopyFile, sourceInfo.absoluteFilePath(), targetAbsolutePath, relativePath},
                    &operationKeys,
                    &copyFileOperations);
            }
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("主目录中存在不支持的文件类型，path=%1").arg(sourceInfo.absoluteFilePath());
        }
        return false;
    }

    operations->reserve(removeFileOperations.size() + removeDirectoryOperations.size()
                        + createDirectoryOperations.size() + copyFileOperations.size());
    operations->append(removeFileOperations);
    operations->append(removeDirectoryOperations);
    operations->append(createDirectoryOperations);
    operations->append(copyFileOperations);
    return true;
}

bool executeOperation(const SyncOperation &operation, QString *errorMessage)
{
    switch (operation.type) {
    case SyncOperationType::E_RemoveFile:
        return removeFileAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_RemoveDirectory:
        return removeDirectoryAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_CreateDirectory:
        return ensureDirectoryExists(operation.targetPath, errorMessage);
    case SyncOperationType::E_CopyFile:
        return copyFileWithMetadata(operation.sourcePath, operation.targetPath, errorMessage);
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("未识别的同步操作，path=%1").arg(operation.targetPath);
    }
    return false;
}

QString buildSummary(const QVector<SyncOperation> &operations)
{
    int removeFileCount = 0;
    int removeDirectoryCount = 0;
    int createDirectoryCount = 0;
    int copyFileCount = 0;

    for (const SyncOperation &operation : operations) {
        switch (operation.type) {
        case SyncOperationType::E_RemoveFile:
            ++removeFileCount;
            break;
        case SyncOperationType::E_RemoveDirectory:
            ++removeDirectoryCount;
            break;
        case SyncOperationType::E_CreateDirectory:
            ++createDirectoryCount;
            break;
        case SyncOperationType::E_CopyFile:
            ++copyFileCount;
            break;
        }
    }

    return QObject::tr("同步完成，共处理 %1 个步骤：创建目录 %2，复制文件 %3，删除文件 %4，删除目录 %5。")
        .arg(operations.size())
        .arg(createDirectoryCount)
        .arg(copyFileCount)
        .arg(removeFileCount)
        .arg(removeDirectoryCount);
}
} // namespace

FolderSyncWorker::FolderSyncWorker(QObject *parent)
    : QObject(parent)
{
}

void FolderSyncWorker::slotStartSync(const QString &sourcePath, const QString &targetPath, const QString &reason)
{
    const QString normalizedSourcePath = normalizePath(sourcePath);
    const QString normalizedTargetPath = normalizePath(targetPath);

    emit sigLogMessage(QObject::tr("收到同步请求，reason=%1，source=%2，target=%3")
                           .arg(reason, normalizedSourcePath, normalizedTargetPath));

    QString errorMessage;
    if (normalizedSourcePath.isEmpty() || normalizedTargetPath.isEmpty()) {
        errorMessage = QObject::tr("同步路径不能为空，source=%1，target=%2")
                           .arg(normalizedSourcePath, normalizedTargetPath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    const QFileInfo sourceInfo(normalizedSourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        errorMessage = QObject::tr("主目录不存在或不是目录，source=%1").arg(normalizedSourcePath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    const QFileInfo targetInfo(normalizedTargetPath);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        errorMessage = QObject::tr("备份路径已存在且不是目录，target=%1").arg(normalizedTargetPath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    if (isSameOrNestedPath(normalizedSourcePath, normalizedTargetPath)) {
        errorMessage = QObject::tr("A 和 B 不能相同，也不能互相嵌套，source=%1，target=%2")
                           .arg(normalizedSourcePath, normalizedTargetPath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    if (!ensureDirectoryExists(normalizedTargetPath, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    QVector<SyncOperation> operations;
    if (!buildSyncPlan(normalizedSourcePath, normalizedTargetPath, &operations, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    emit sigSyncStarted(operations.size(), reason);
    if (operations.isEmpty()) {
        const QString summary = QObject::tr("A 与 B 已一致，无需同步。");
        emit sigLogMessage(summary);
        emit sigSyncFinished(true, summary);
        return;
    }

    emit sigLogMessage(QObject::tr("同步计划生成完成，totalSteps=%1，reason=%2")
                           .arg(operations.size())
                           .arg(reason));

    for (int index = 0; index < operations.size(); ++index) {
        const SyncOperation &operation = operations.at(index);
        const QString currentItem = describeOperation(operation);
        emit sigSyncProgress(index + 1, operations.size(), currentItem);

        QString executeErrorMessage;
        if (!executeOperation(operation, &executeErrorMessage)) {
            const QString summary =
                QObject::tr("同步失败，reason=%1，step=%2，error=%3")
                    .arg(reason, currentItem, executeErrorMessage);
            emit sigLogMessage(summary);
            emit sigSyncFinished(false, summary);
            return;
        }
    }

    const QString summary = buildSummary(operations);
    emit sigLogMessage(summary);
    emit sigSyncFinished(true, summary);
}
