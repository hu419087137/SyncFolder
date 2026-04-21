#include "FolderSyncWorker.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QtGlobal>
#include <QVector>

#include <functional>
namespace
{
constexpr qint64 kFileCompareChunkSize = 1024 * 1024;
constexpr qint64 kCopyProgressChunkSize = 4 * 1024 * 1024;

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

enum class FileCompareResult
{
    E_Same,
    E_Different,
    E_Error
};

struct SyncOperation
{
    SyncOperationType type = SyncOperationType::E_CopyFile;
    QString sourcePath;
    QString targetPath;
    QString relativePath;
    qint64 progressUnits = 1;
};

struct SourceEntry
{
    QString absolutePath;
    QString relativePath;
    SyncEntryType type = SyncEntryType::E_Unknown;
    qint64 fileSize = 0;
};

struct SyncPlanStats
{
    int removeFileCount = 0;
    int addFileCount = 0;
    int updateFileCount = 0;
    int removeDirectoryCount = 0;
    int createDirectoryCount = 0;
};

struct PlanStageProgress
{
    qint64 current = 0;
    qint64 total = 0;
    QString text;
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

qint64 calculateCopyProgressUnits(qint64 fileSize)
{
    if (fileSize <= 0) {
        return 1;
    }

    return (fileSize + kCopyProgressChunkSize - 1) / kCopyProgressChunkSize;
}

QString buildPlanningProgressText(const QString &stageText, qint64 current, qint64 total)
{
    if (total <= 0) {
        return stageText;
    }

    return QObject::tr("%1（%2/%3）").arg(stageText).arg(current).arg(total);
}

QString buildProcessedProgressText(const QString &stageText, qint64 currentCount)
{
    if (currentCount <= 0) {
        return stageText;
    }

    return QObject::tr("%1（已处理 %2）").arg(stageText).arg(currentCount);
}

QString buildFileCompareProgressText(qint64 currentEntry, qint64 totalEntries, int filePercent)
{
    return QObject::tr("正在比对文件差异（%1/%2，当前文件 %3%）")
        .arg(currentEntry)
        .arg(totalEntries)
        .arg(filePercent);
}

bool shouldReportPlanningProgress(qint64 current, qint64 total)
{
    if (total <= 0) {
        return false;
    }

    if (current <= 0 || current >= total) {
        return true;
    }

    const qint64 reportInterval = qMax<qint64>(1, total / 200);
    return current % reportInterval == 0;
}

bool shouldReportCountingProgress(qint64 currentCount)
{
    if (currentCount <= 0) {
        return false;
    }

    return currentCount == 1 || currentCount % 200 == 0;
}

bool shouldReportByteProgress(qint64 currentBytes, qint64 totalBytes)
{
    if (currentBytes <= 0 || totalBytes <= 0) {
        return false;
    }

    if (currentBytes >= totalBytes) {
        return true;
    }

    const qint64 reportInterval = qMax<qint64>(kFileCompareChunkSize, totalBytes / 100);
    return currentBytes % reportInterval < kFileCompareChunkSize;
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

FileCompareResult compareFileContent(const QFileInfo &sourceInfo,
                                     const QFileInfo &targetInfo,
                                     const std::function<void(qint64, qint64)> &progressCallback,
                                     QString *errorMessage)
{
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        return FileCompareResult::E_Different;
    }

    if (sourceInfo.size() != targetInfo.size()) {
        return FileCompareResult::E_Different;
    }

    QFile sourceFile(sourceInfo.absoluteFilePath());
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("读取主目录文件失败，path=%1，error=%2")
                                .arg(sourceInfo.absoluteFilePath(), sourceFile.errorString());
        }
        return FileCompareResult::E_Error;
    }

    QFile targetFile(targetInfo.absoluteFilePath());
    if (!targetFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("读取备份目录文件失败，path=%1，error=%2")
                                .arg(targetInfo.absoluteFilePath(), targetFile.errorString());
        }
        return FileCompareResult::E_Error;
    }

    while (true) {
        const qint64 comparedBytes = sourceFile.pos();
        if (progressCallback != nullptr && shouldReportByteProgress(comparedBytes, sourceInfo.size())) {
            progressCallback(comparedBytes, sourceInfo.size());
        }

        const QByteArray sourceChunk = sourceFile.read(kFileCompareChunkSize);
        if (sourceChunk.isEmpty() && sourceFile.error() != QFileDevice::NoError) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("读取主目录文件内容失败，path=%1，error=%2")
                                    .arg(sourceInfo.absoluteFilePath(), sourceFile.errorString());
            }
            return FileCompareResult::E_Error;
        }

        const QByteArray targetChunk = targetFile.read(kFileCompareChunkSize);
        if (targetChunk.isEmpty() && targetFile.error() != QFileDevice::NoError) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("读取备份目录文件内容失败，path=%1，error=%2")
                                    .arg(targetInfo.absoluteFilePath(), targetFile.errorString());
            }
            return FileCompareResult::E_Error;
        }

        if (sourceChunk != targetChunk) {
            return FileCompareResult::E_Different;
        }

        if (sourceChunk.isEmpty()) {
            if (progressCallback != nullptr) {
                progressCallback(sourceInfo.size(), sourceInfo.size());
            }
            return FileCompareResult::E_Same;
        }
    }
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

    // file.setPermissions(file.permissions() | QFileDevice::WriteOwner);
    if (file.remove()) {
        return true;
    }
    if (!QFile::setPermissions(filePath, file.permissions() | QFile::WriteUser)) {
        qWarning() << "Failed to set write permission. Try running the program as administrator/root.";
        // 或者尝试提权（Windows 上可以请求重新以管理员身份启动）
    }

    QFileInfo fileInfo(filePath);
    // auto oldCurrentDirPath = QDir::currentPath();
    if (fileInfo.dir().remove(fileInfo.fileName())){
        return true;
    }
    if (errorMessage != nullptr) {
        qWarning()<<fileInfo.permissions();
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

bool writeAll(QFileDevice *targetFile, const QByteArray &buffer, const QString &targetPath, QString *errorMessage)
{
    qint64 totalWritten = 0;
    while (totalWritten < buffer.size()) {
        const qint64 writtenBytes =
            targetFile->write(buffer.constData() + totalWritten, buffer.size() - totalWritten);
        if (writtenBytes <= 0) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("写入备份目录文件失败，target=%1，error=%2")
                                    .arg(targetPath, targetFile->errorString());
            }
            return false;
        }
        totalWritten += writtenBytes;
    }

    return true;
}

bool copyFileWithMetadata(const SyncOperation &operation,
                          const std::function<void(qint64, const QString &)> &progressCallback,
                          QString *errorMessage)
{
    const QFileInfo targetInfo(operation.targetPath);
    if (!ensureDirectoryExists(targetInfo.dir().absolutePath(), errorMessage)) {
        return false;
    }

    if (QFileInfo::exists(operation.targetPath)
        && !removeFileAtPath(operation.targetPath, errorMessage)) {
        return false;
    }

    QFile sourceFile(operation.sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("读取主目录文件失败，source=%1，error=%2")
                                .arg(operation.sourcePath, sourceFile.errorString());
        }
        return false;
    }

    QSaveFile targetFile(operation.targetPath);
    if (!targetFile.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("打开备份目录文件失败，target=%1，error=%2")
                                .arg(operation.targetPath, targetFile.errorString());
        }
        return false;
    }

    qint64 copiedBytes = 0;
    qint64 lastReportedUnits = 0;
    while (true) {
        const QByteArray sourceChunk = sourceFile.read(kCopyProgressChunkSize);
        if (sourceChunk.isEmpty()) {
            if (sourceFile.error() != QFileDevice::NoError) {
                if (errorMessage != nullptr) {
                    *errorMessage = QObject::tr("读取主目录文件内容失败，source=%1，error=%2")
                                        .arg(operation.sourcePath, sourceFile.errorString());
                }
                return false;
            }
            break;
        }

        if (!writeAll(&targetFile, sourceChunk, operation.targetPath, errorMessage)) {
            return false;
        }

        copiedBytes += sourceChunk.size();
        const qint64 currentUnits =
            qMin(operation.progressUnits, calculateCopyProgressUnits(copiedBytes));
        if (progressCallback != nullptr && currentUnits > lastReportedUnits) {
            progressCallback(currentUnits, describeOperation(operation));
            lastReportedUnits = currentUnits;
        }
    }

    if (!targetFile.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("提交复制文件失败，target=%1，error=%2")
                                .arg(operation.targetPath, targetFile.errorString());
        }
        return false;
    }

    return true;
}

bool collectSourceEntries(const QString &sourcePath,
                          QVector<SourceEntry> *sourceEntries,
                          QHash<QString, SyncEntryType> *sourceEntryTypes,
                          const std::function<void(const PlanStageProgress &)> &stageCallback,
                          QString *errorMessage)
{
    QDir sourceRoot(sourcePath);
    QDirIterator iterator(sourcePath,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    qint64 collectedEntryCount = 0;
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
            ++collectedEntryCount;
            sourceEntries->append({fileInfo.absoluteFilePath(), relativePath, SyncEntryType::E_Directory, 0});
            sourceEntryTypes->insert(relativePath, SyncEntryType::E_Directory);
            if (stageCallback != nullptr && shouldReportCountingProgress(collectedEntryCount)) {
                stageCallback({collectedEntryCount,
                               0,
                               buildProcessedProgressText(QObject::tr("正在扫描主目录"),
                                                          collectedEntryCount)});
            }
            continue;
        }

        if (fileInfo.isFile()) {
            ++collectedEntryCount;
            sourceEntries->append(
                {fileInfo.absoluteFilePath(), relativePath, SyncEntryType::E_File, fileInfo.size()});
            sourceEntryTypes->insert(relativePath, SyncEntryType::E_File);
            if (stageCallback != nullptr && shouldReportCountingProgress(collectedEntryCount)) {
                stageCallback({collectedEntryCount,
                               0,
                               buildProcessedProgressText(QObject::tr("正在扫描主目录"),
                                                          collectedEntryCount)});
            }
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("检测到不支持的文件类型，path=%1").arg(fileInfo.absoluteFilePath());
        }
        return false;
    }

    if (stageCallback != nullptr && collectedEntryCount > 0
        && !shouldReportCountingProgress(collectedEntryCount)) {
        stageCallback({collectedEntryCount,
                       0,
                       buildProcessedProgressText(QObject::tr("正在扫描主目录"), collectedEntryCount)});
    }

    return true;
}

bool buildSyncPlan(const QString &sourcePath,
                   const QString &targetPath,
                   QVector<SyncOperation> *operations,
                   SyncPlanStats *planStats,
                   const std::function<void(const PlanStageProgress &)> &stageCallback,
                   QString *errorMessage)
{
    QVector<SourceEntry> sourceEntries;
    QHash<QString, SyncEntryType> sourceEntryTypes;
    if (stageCallback != nullptr) {
        stageCallback({0, 0, QObject::tr("正在扫描主目录")});
    }
    if (!collectSourceEntries(sourcePath,
                              &sourceEntries,
                              &sourceEntryTypes,
                              stageCallback,
                              errorMessage)) {
        return false;
    }

    const QDir targetRoot(targetPath);
    QVector<SyncOperation> removeFileOperations;
    QVector<SyncOperation> removeDirectoryOperations;
    QVector<SyncOperation> createDirectoryOperations;
    QVector<SyncOperation> copyFileOperations;
    QSet<QString> operationKeys;
    QStringList removedDirectoryRoots;

    if (stageCallback != nullptr) {
        stageCallback({0, 0, QObject::tr("正在检查备份目录")});
    }
    QDirIterator targetIterator(targetPath,
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
    qint64 checkedTargetEntryCount = 0;
    while (targetIterator.hasNext()) {
        targetIterator.next();
        ++checkedTargetEntryCount;

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
            if (targetInfo.isFile()) {
                ++planStats->removeFileCount;
            }
            continue;
        }

        const SyncEntryType sourceEntryType = sourceEntryTypes.value(relativePath, SyncEntryType::E_Unknown);
        if (targetInfo.isDir()) {
            if (sourceEntryType == SyncEntryType::E_File || sourceEntryType == SyncEntryType::E_Unknown) {
                appendUniqueOperation({SyncOperationType::E_RemoveDirectory, QString(), targetInfo.absoluteFilePath(),
                                       relativePath},
                                     &operationKeys,
                                      &removeDirectoryOperations);
                removedDirectoryRoots.append(relativePath);
                ++planStats->removeDirectoryCount;
            }
            if (stageCallback != nullptr && shouldReportCountingProgress(checkedTargetEntryCount)) {
                stageCallback({checkedTargetEntryCount,
                               0,
                               buildProcessedProgressText(QObject::tr("正在检查备份目录"),
                                                          checkedTargetEntryCount)});
            }
            continue;
        }

        if (targetInfo.isFile()) {
            if (sourceEntryType == SyncEntryType::E_Directory || sourceEntryType == SyncEntryType::E_Unknown) {
                appendUniqueOperation(
                    {SyncOperationType::E_RemoveFile, QString(), targetInfo.absoluteFilePath(), relativePath},
                    &operationKeys,
                    &removeFileOperations);
                ++planStats->removeFileCount;
            }
            if (stageCallback != nullptr && shouldReportCountingProgress(checkedTargetEntryCount)) {
                stageCallback({checkedTargetEntryCount,
                               0,
                               buildProcessedProgressText(QObject::tr("正在检查备份目录"),
                                                          checkedTargetEntryCount)});
            }
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("备份目录中存在不支持的文件类型，path=%1")
                                .arg(targetInfo.absoluteFilePath());
        }
        return false;
    }

    if (stageCallback != nullptr && checkedTargetEntryCount > 0
        && !shouldReportCountingProgress(checkedTargetEntryCount)) {
        stageCallback({checkedTargetEntryCount,
                       0,
                       buildProcessedProgressText(QObject::tr("正在检查备份目录"), checkedTargetEntryCount)});
    }

    const qint64 sourceEntryCount = sourceEntries.size();
    if (stageCallback != nullptr) {
        stageCallback({0, sourceEntryCount, buildPlanningProgressText(QObject::tr("正在比对文件差异"), 0, sourceEntryCount)});
    }
    qint64 comparedSourceEntryCount = 0;
    for (const SourceEntry &sourceEntry : sourceEntries) {
        ++comparedSourceEntryCount;

        const QString targetAbsolutePath = targetRoot.absoluteFilePath(sourceEntry.relativePath);
        const QFileInfo targetInfo(targetAbsolutePath);

        if (sourceEntry.type == SyncEntryType::E_Directory) {
            if (!targetInfo.exists() || !targetInfo.isDir()) {
                appendUniqueOperation(
                    {SyncOperationType::E_CreateDirectory, QString(), targetAbsolutePath, sourceEntry.relativePath},
                    &operationKeys,
                    &createDirectoryOperations);
                ++planStats->createDirectoryCount;
            }
            if (stageCallback != nullptr
                && shouldReportPlanningProgress(comparedSourceEntryCount, sourceEntryCount)) {
                stageCallback(
                    {comparedSourceEntryCount,
                     sourceEntryCount,
                     buildPlanningProgressText(QObject::tr("正在比对文件差异"),
                                              comparedSourceEntryCount,
                                              sourceEntryCount)});
            }
            continue;
        }

        if (sourceEntry.type == SyncEntryType::E_File) {
            QString compareErrorMessage;
            const QFileInfo sourceInfo(sourceEntry.absolutePath);
            const FileCompareResult compareResult =
                compareFileContent(sourceInfo,
                                   targetInfo,
                                   [stageCallback, comparedSourceEntryCount, sourceEntryCount](
                                       qint64 comparedBytes,
                                       qint64 totalBytes) {
                                       if (stageCallback == nullptr || totalBytes <= 0) {
                                           return;
                                       }

                                       stageCallback(
                                           {comparedSourceEntryCount,
                                            sourceEntryCount,
                                            buildFileCompareProgressText(
                                                comparedSourceEntryCount,
                                                sourceEntryCount,
                                                qBound<int>(
                                                    0,
                                                    qRound(static_cast<double>(comparedBytes) * 100.0
                                                           / static_cast<double>(totalBytes)),
                                                    100))});
                                   },
                                   &compareErrorMessage);
            if (compareResult == FileCompareResult::E_Error) {
                if (errorMessage != nullptr) {
                    *errorMessage = compareErrorMessage;
                }
                return false;
            }

            if (compareResult == FileCompareResult::E_Different) {
                appendUniqueOperation(
                    {SyncOperationType::E_CopyFile,
                     sourceEntry.absolutePath,
                     targetAbsolutePath,
                     sourceEntry.relativePath,
                     calculateCopyProgressUnits(sourceEntry.fileSize)},
                    &operationKeys,
                    &copyFileOperations);

                if (targetInfo.exists() && targetInfo.isFile()) {
                    ++planStats->updateFileCount;
                } else {
                    ++planStats->addFileCount;
                }
            }
            if (stageCallback != nullptr
                && shouldReportPlanningProgress(comparedSourceEntryCount, sourceEntryCount)) {
                stageCallback(
                    {comparedSourceEntryCount,
                     sourceEntryCount,
                     buildPlanningProgressText(QObject::tr("正在比对文件差异"),
                                              comparedSourceEntryCount,
                                              sourceEntryCount)});
            }
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage =
                QObject::tr("主目录中存在不支持的文件类型，path=%1").arg(sourceEntry.absolutePath);
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

bool executeOperation(const SyncOperation &operation,
                      const std::function<void(qint64, const QString &)> &progressCallback,
                      QString *errorMessage)
{
    switch (operation.type) {
    case SyncOperationType::E_RemoveFile:
        return removeFileAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_RemoveDirectory:
        return removeDirectoryAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_CreateDirectory:
        return ensureDirectoryExists(operation.targetPath, errorMessage);
    case SyncOperationType::E_CopyFile:
        return copyFileWithMetadata(operation, progressCallback, errorMessage);
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("未识别的同步操作，path=%1").arg(operation.targetPath);
    }
    return false;
}

qint64 calculateTotalProgressUnits(const QVector<SyncOperation> &operations)
{
    qint64 totalProgressUnits = 0;
    for (const SyncOperation &operation : operations) {
        totalProgressUnits += qMax<qint64>(1, operation.progressUnits);
    }

    return totalProgressUnits;
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

QString buildPlanPreviewSummary(const SyncPlanStats &planStats)
{
    return QObject::tr("备份前检查：待删除文件 %1，待新增文件 %2，待同步文件 %3。")
        .arg(planStats.removeFileCount)
        .arg(planStats.addFileCount)
        .arg(planStats.updateFileCount);
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
    SyncPlanStats planStats;
    emit sigSyncProgress(0, 0, QObject::tr("准备扫描同步差异"));
    if (!buildSyncPlan(normalizedSourcePath,
                       normalizedTargetPath,
                       &operations,
                       &planStats,
                       [this](const PlanStageProgress &progress) {
                           emit sigSyncProgress(progress.current, progress.total, progress.text);
                       },
                       &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    const QString planPreviewSummary = buildPlanPreviewSummary(planStats);
    emit sigLogMessage(planPreviewSummary);
    const qint64 totalProgressUnits = calculateTotalProgressUnits(operations);
    emit sigSyncStarted(totalProgressUnits,
                        planStats.removeFileCount,
                        planStats.addFileCount,
                        planStats.updateFileCount,
                        reason);
    if (operations.isEmpty()) {
        const QString summary = QObject::tr("A 与 B 已一致，无需同步。");
        emit sigLogMessage(summary);
        emit sigSyncFinished(true, summary);
        return;
    }

    emit sigLogMessage(QObject::tr("同步计划生成完成，totalSteps=%1，reason=%2")
                           .arg(totalProgressUnits)
                           .arg(reason));

    qint64 completedProgressUnits = 0;
    for (int index = 0; index < operations.size(); ++index) {
        const SyncOperation &operation = operations.at(index);
        const QString currentItem = describeOperation(operation);
        emit sigSyncProgress(completedProgressUnits, totalProgressUnits, currentItem);

        QString executeErrorMessage;
        if (!executeOperation(operation,
                              [this, completedProgressUnits, totalProgressUnits](qint64 operationProgressUnits,
                                                                                const QString &progressItem) {
                                  emit sigSyncProgress(completedProgressUnits + operationProgressUnits,
                                                       totalProgressUnits,
                                                       progressItem);
                              },
                              &executeErrorMessage)) {
            const QString summary =
                QObject::tr("同步失败，reason=%1，step=%2，error=%3")
                    .arg(reason, currentItem, executeErrorMessage);
            emit sigLogMessage(summary);
            emit sigSyncFinished(false, summary);
            return;
        }

        completedProgressUnits += qMax<qint64>(1, operation.progressUnits);
        emit sigSyncProgress(completedProgressUnits, totalProgressUnits, currentItem);
    }

    const QString summary = buildSummary(operations);
    emit sigLogMessage(summary);
    emit sigSyncFinished(true, summary);
}
