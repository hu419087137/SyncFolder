#include "HttpFolderSyncWorker.h"

#include "FolderSyncWorker.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <functional>

namespace
{
constexpr qint64 kDownloadProgressChunkSize = 4 * 1024 * 1024;
constexpr int kNetworkTransferTimeoutMs = 60000;

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
    E_DownloadFile
};

struct ManifestEntry
{
    QString relativePath;
    SyncEntryType type = SyncEntryType::E_Unknown;
    qint64 fileSize = 0;
    qint64 modifiedTimeMs = 0;
};

struct SyncOperation
{
    SyncOperationType type = SyncOperationType::E_DownloadFile;
    QString targetPath;
    QString relativePath;
    QUrl sourceUrl;
    qint64 progressUnits = 1;
    qint64 modifiedTimeMs = 0;
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

struct ExecutionStats
{
    int removeFileCount = 0;
    int removeDirectoryCount = 0;
    int createDirectoryCount = 0;
    int downloadFileCount = 0;
    qint64 completedProgressUnits = 0;
};

using CancelCallback = std::function<bool()>;

QString normalizeLocalPath(const QString &path)
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

QString normalizeRelativePath(const QDir &rootDir, const QString &absolutePath)
{
    return QDir::fromNativeSeparators(rootDir.relativeFilePath(absolutePath));
}

QString displayRelativePath(const QString &relativePath)
{
    return relativePath.isEmpty() ? QObject::tr(".") : relativePath;
}

qint64 calculateDownloadProgressUnits(qint64 fileSize)
{
    if (fileSize <= 0) {
        return 1;
    }

    return (fileSize + kDownloadProgressChunkSize - 1) / kDownloadProgressChunkSize;
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

QString buildManifestProgressText(qint64 receivedBytes, qint64 totalBytes)
{
    if (receivedBytes <= 0 || totalBytes <= 0) {
        return QObject::tr("正在获取远端清单");
    }

    const int percent = qBound<int>(
        0,
        qRound(static_cast<double>(receivedBytes) * 100.0 / static_cast<double>(totalBytes)),
        100);
    return QObject::tr("正在获取远端清单（%1%）").arg(percent);
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
    case SyncOperationType::E_DownloadFile:
        return QObject::tr("下载文件：%1").arg(displayRelativePath(operation.relativePath));
    }

    return QObject::tr("未知步骤：%1").arg(displayRelativePath(operation.relativePath));
}

QString normalizeHttpSourceUrl(const QString &sourceUrl, QString *errorMessage)
{
    const QString trimmedUrl = sourceUrl.trimmed();
    if (trimmedUrl.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 服务地址不能为空。");
        }
        return QString();
    }

    QUrl url = QUrl::fromUserInput(trimmedUrl);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 服务地址无效，url=%1").arg(trimmedUrl);
        }
        return QString();
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 服务地址仅支持 http 或 https，url=%1").arg(trimmedUrl);
        }
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

QUrl buildFileUrl(const QString &normalizedSourceUrl, const QString &relativePath)
{
    QUrl fileUrl(normalizedSourceUrl);
    QString path = fileUrl.path();
    if (!path.endsWith(QStringLiteral("/file"))) {
        path += QStringLiteral("/file");
    }
    fileUrl.setPath(path);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("path"), relativePath);
    fileUrl.setQuery(query);
    fileUrl.setFragment(QString());
    return fileUrl;
}

QString summarizeCompareMode(int compareMode)
{
    switch (static_cast<FolderSyncWorker::CompareMode>(compareMode)) {
    case FolderSyncWorker::E_StrictCompare:
        return QObject::tr("HTTP 源暂不支持全文比对，已回退为大小和修改时间比对。");
    case FolderSyncWorker::E_FastCompare:
    case FolderSyncWorker::E_TurboCompare:
        return QObject::tr("HTTP 源使用大小和修改时间比对。");
    }

    return QObject::tr("HTTP 源使用大小和修改时间比对。");
}

bool isSameByManifestMetadata(const ManifestEntry &manifestEntry, const QFileInfo &targetInfo)
{
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        return false;
    }

    if (manifestEntry.fileSize != targetInfo.size()) {
        return false;
    }

    constexpr qint64 kTimestampToleranceMs = 2000;
    return qAbs(targetInfo.lastModified().toMSecsSinceEpoch() - manifestEntry.modifiedTimeMs)
        <= kTimestampToleranceMs;
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

    if (file.remove()) {
        return true;
    }

    if (!QFile::setPermissions(filePath, file.permissions() | QFile::WriteUser)) {
        qWarning() << "Failed to add write permission before removing file. path=" << filePath;
    }

    QFileInfo fileInfo(filePath);
    if (fileInfo.dir().remove(fileInfo.fileName())) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("删除文件失败，path=%1，error=%2").arg(filePath, file.errorString());
    }
    return false;
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
                *errorMessage = QObject::tr("写入本地文件失败，target=%1，error=%2")
                                    .arg(targetPath, targetFile->errorString());
            }
            return false;
        }
        totalWritten += writtenBytes;
    }

    return true;
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

bool parseManifest(const QByteArray &manifestJson,
                   QVector<ManifestEntry> *manifestEntries,
                   QString *errorMessage)
{
    if (manifestEntries == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 清单输出参数不能为空。");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(manifestJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !jsonDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("解析 HTTP 清单失败，error=%1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonArray entriesJson = jsonDocument.object().value(QStringLiteral("entries")).toArray();
    QSet<QString> uniquePaths;
    manifestEntries->clear();
    manifestEntries->reserve(entriesJson.size());
    for (const QJsonValue &entryValue : entriesJson) {
        if (!entryValue.isObject()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("HTTP 清单中存在无效条目。");
            }
            return false;
        }

        const QJsonObject entryObject = entryValue.toObject();
        const QString relativePath = QDir::cleanPath(
            QDir::fromNativeSeparators(entryObject.value(QStringLiteral("path")).toString().trimmed()));
        const QString entryType = entryObject.value(QStringLiteral("type")).toString();
        if (relativePath.isEmpty()
            || relativePath == QStringLiteral(".")
            || relativePath == QStringLiteral("..")
            || relativePath.startsWith(QStringLiteral("../"))) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("HTTP 清单中存在越界路径，path=%1").arg(relativePath);
            }
            return false;
        }

        if (uniquePaths.contains(relativePath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("HTTP 清单中存在重复路径，path=%1").arg(relativePath);
            }
            return false;
        }
        uniquePaths.insert(relativePath);

        if (entryType == QStringLiteral("dir")) {
            manifestEntries->append({relativePath, SyncEntryType::E_Directory, 0, 0});
            continue;
        }

        if (entryType == QStringLiteral("file")) {
            manifestEntries->append({relativePath,
                                     SyncEntryType::E_File,
                                     static_cast<qint64>(
                                         entryObject.value(QStringLiteral("size")).toDouble(-1)),
                                     static_cast<qint64>(
                                         entryObject.value(QStringLiteral("mtimeMs")).toDouble(0))});
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 清单中存在未知条目类型，path=%1，type=%2")
                                .arg(relativePath, entryType);
        }
        return false;
    }

    std::sort(manifestEntries->begin(), manifestEntries->end(), [](const ManifestEntry &left, const ManifestEntry &right) {
        return left.relativePath < right.relativePath;
    });
    return true;
}

bool buildSyncPlan(const QVector<ManifestEntry> &manifestEntries,
                   const QString &normalizedTargetPath,
                   const QString &normalizedSourceUrl,
                   QVector<SyncOperation> *operations,
                   SyncPlanStats *planStats,
                   const std::function<void(const PlanStageProgress &)> &stageCallback,
                   const CancelCallback &cancelCallback,
                   QString *errorMessage)
{
    if (operations == nullptr || planStats == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("同步计划输出参数不能为空。");
        }
        return false;
    }

    operations->clear();
    *planStats = SyncPlanStats();

    QHash<QString, ManifestEntry> manifestEntryMap;
    manifestEntryMap.reserve(manifestEntries.size());
    for (const ManifestEntry &manifestEntry : manifestEntries) {
        manifestEntryMap.insert(manifestEntry.relativePath, manifestEntry);
    }

    QVector<SyncOperation> removeFileOperations;
    QVector<SyncOperation> removeDirectoryOperations;
    QVector<SyncOperation> createDirectoryOperations;
    QVector<SyncOperation> downloadFileOperations;
    QSet<QString> operationKeys;
    QStringList removedDirectoryRoots;

    QDir targetRoot(normalizedTargetPath);
    QDirIterator targetIterator(normalizedTargetPath,
                                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                QDirIterator::Subdirectories);
    qint64 checkedTargetEntryCount = 0;
    while (targetIterator.hasNext()) {
        if (cancelCallback != nullptr && cancelCallback()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("用户取消同步。");
            }
            return false;
        }

        targetIterator.next();
        const QFileInfo targetInfo = targetIterator.fileInfo();
        if (targetInfo.isSymLink()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("本地目录中存在不支持的符号链接，path=%1")
                                    .arg(targetInfo.absoluteFilePath());
            }
            return false;
        }

        const QString relativePath = normalizeRelativePath(targetRoot, targetInfo.absoluteFilePath());
        if (isUnderRemovedDirectoryRoot(relativePath, removedDirectoryRoots)) {
            continue;
        }

        ++checkedTargetEntryCount;
        const auto manifestEntryIt = manifestEntryMap.constFind(relativePath);
        if (manifestEntryIt == manifestEntryMap.constEnd()) {
            if (targetInfo.isDir()) {
                appendUniqueOperation({SyncOperationType::E_RemoveDirectory,
                                       targetInfo.absoluteFilePath(),
                                       relativePath},
                                      &operationKeys,
                                      &removeDirectoryOperations);
                removedDirectoryRoots.append(relativePath);
                ++planStats->removeDirectoryCount;
            } else if (targetInfo.isFile()) {
                appendUniqueOperation({SyncOperationType::E_RemoveFile,
                                       targetInfo.absoluteFilePath(),
                                       relativePath},
                                      &operationKeys,
                                      &removeFileOperations);
                ++planStats->removeFileCount;
            } else if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("本地目录中存在不支持的文件类型，path=%1")
                                    .arg(targetInfo.absoluteFilePath());
                return false;
            }

            if (stageCallback != nullptr && shouldReportCountingProgress(checkedTargetEntryCount)) {
                stageCallback({checkedTargetEntryCount,
                               0,
                               buildProcessedProgressText(QObject::tr("正在检查本地目录"),
                                                          checkedTargetEntryCount)});
            }
            continue;
        }

        const ManifestEntry &manifestEntry = manifestEntryIt.value();
        const bool typeMatches = (manifestEntry.type == SyncEntryType::E_Directory && targetInfo.isDir())
            || (manifestEntry.type == SyncEntryType::E_File && targetInfo.isFile());
        if (!typeMatches) {
            if (targetInfo.isDir()) {
                appendUniqueOperation({SyncOperationType::E_RemoveDirectory,
                                       targetInfo.absoluteFilePath(),
                                       relativePath},
                                      &operationKeys,
                                      &removeDirectoryOperations);
                removedDirectoryRoots.append(relativePath);
                ++planStats->removeDirectoryCount;
            } else {
                appendUniqueOperation({SyncOperationType::E_RemoveFile,
                                       targetInfo.absoluteFilePath(),
                                       relativePath},
                                      &operationKeys,
                                      &removeFileOperations);
                ++planStats->removeFileCount;
            }
        }

        if (stageCallback != nullptr && shouldReportCountingProgress(checkedTargetEntryCount)) {
            stageCallback({checkedTargetEntryCount,
                           0,
                           buildProcessedProgressText(QObject::tr("正在检查本地目录"),
                                                      checkedTargetEntryCount)});
        }
    }

    if (stageCallback != nullptr && checkedTargetEntryCount > 0
        && !shouldReportCountingProgress(checkedTargetEntryCount)) {
        stageCallback({checkedTargetEntryCount,
                       0,
                       buildProcessedProgressText(QObject::tr("正在检查本地目录"),
                                                  checkedTargetEntryCount)});
    }

    if (stageCallback != nullptr) {
        stageCallback({0,
                       manifestEntries.size(),
                       buildPlanningProgressText(QObject::tr("正在比对文件差异"), 0, manifestEntries.size())});
    }

    qint64 comparedEntryCount = 0;
    for (const ManifestEntry &manifestEntry : manifestEntries) {
        if (cancelCallback != nullptr && cancelCallback()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("用户取消同步。");
            }
            return false;
        }

        ++comparedEntryCount;
        const QString targetAbsolutePath = targetRoot.absoluteFilePath(manifestEntry.relativePath);
        const QFileInfo targetInfo(targetAbsolutePath);
        if (manifestEntry.type == SyncEntryType::E_Directory) {
            if (!targetInfo.exists() || !targetInfo.isDir()) {
                appendUniqueOperation({SyncOperationType::E_CreateDirectory,
                                       targetAbsolutePath,
                                       manifestEntry.relativePath},
                                      &operationKeys,
                                      &createDirectoryOperations);
                ++planStats->createDirectoryCount;
            }
        } else if (manifestEntry.type == SyncEntryType::E_File) {
            if (!isSameByManifestMetadata(manifestEntry, targetInfo)) {
                appendUniqueOperation({SyncOperationType::E_DownloadFile,
                                       targetAbsolutePath,
                                       manifestEntry.relativePath,
                                       buildFileUrl(normalizedSourceUrl, manifestEntry.relativePath),
                                       calculateDownloadProgressUnits(manifestEntry.fileSize),
                                       manifestEntry.modifiedTimeMs},
                                      &operationKeys,
                                      &downloadFileOperations);
                if (targetInfo.exists() && targetInfo.isFile()) {
                    ++planStats->updateFileCount;
                } else {
                    ++planStats->addFileCount;
                }
            }
        } else if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("HTTP 清单中存在不支持的条目类型，path=%1")
                                .arg(manifestEntry.relativePath);
            return false;
        }

        if (stageCallback != nullptr && shouldReportPlanningProgress(comparedEntryCount, manifestEntries.size())) {
            stageCallback({comparedEntryCount,
                           manifestEntries.size(),
                           buildPlanningProgressText(QObject::tr("正在比对文件差异"),
                                                    comparedEntryCount,
                                                    manifestEntries.size())});
        }
    }

    operations->reserve(removeFileOperations.size() + removeDirectoryOperations.size()
                        + createDirectoryOperations.size() + downloadFileOperations.size());
    operations->append(removeFileOperations);
    operations->append(removeDirectoryOperations);
    operations->append(createDirectoryOperations);
    operations->append(downloadFileOperations);
    return true;
}

bool executeLocalOperation(const SyncOperation &operation, QString *errorMessage)
{
    switch (operation.type) {
    case SyncOperationType::E_RemoveFile:
        return removeFileAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_RemoveDirectory:
        return removeDirectoryAtPath(operation.targetPath, errorMessage);
    case SyncOperationType::E_CreateDirectory:
        return ensureDirectoryExists(operation.targetPath, errorMessage);
    case SyncOperationType::E_DownloadFile:
        break;
    }

    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("未识别的本地同步操作，path=%1").arg(operation.targetPath);
    }
    return false;
}

void recordExecutedOperation(const SyncOperation &operation, ExecutionStats *executionStats)
{
    if (executionStats == nullptr) {
        return;
    }

    switch (operation.type) {
    case SyncOperationType::E_RemoveFile:
        ++executionStats->removeFileCount;
        break;
    case SyncOperationType::E_RemoveDirectory:
        ++executionStats->removeDirectoryCount;
        break;
    case SyncOperationType::E_CreateDirectory:
        ++executionStats->createDirectoryCount;
        break;
    case SyncOperationType::E_DownloadFile:
        ++executionStats->downloadFileCount;
        break;
    }

    executionStats->completedProgressUnits += qMax<qint64>(1, operation.progressUnits);
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
    int downloadFileCount = 0;
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
        case SyncOperationType::E_DownloadFile:
            ++downloadFileCount;
            break;
        }
    }

    return QObject::tr("同步完成，共处理 %1 个步骤：创建目录 %2，下载文件 %3，删除文件 %4，删除目录 %5。")
        .arg(operations.size())
        .arg(createDirectoryCount)
        .arg(downloadFileCount)
        .arg(removeFileCount)
        .arg(removeDirectoryCount);
}

QString formatElapsedTime(qint64 elapsedMs)
{
    if (elapsedMs < 1000) {
        return QObject::tr("%1 ms").arg(elapsedMs);
    }

    const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
    if (elapsedSeconds < 60.0) {
        return QObject::tr("%1 秒").arg(elapsedSeconds, 0, 'f', 1);
    }

    const qint64 elapsedMinutes = elapsedMs / 60000;
    const qint64 remainingSeconds = (elapsedMs % 60000) / 1000;
    return QObject::tr("%1 分 %2 秒").arg(elapsedMinutes).arg(remainingSeconds);
}

QString buildExecutionSummary(const ExecutionStats &executionStats,
                              int plannedOperationCount,
                              qint64 totalProgressUnits,
                              qint64 elapsedMs,
                              const QString &reason)
{
    return QObject::tr("HTTP 同步结果摘要：处理步骤 %1/%2，创建目录 %3，下载文件 %4，删除文件 %5，删除目录 %6，进度单位 %7/%8，耗时 %9，reason=%10。")
        .arg(executionStats.createDirectoryCount + executionStats.downloadFileCount
             + executionStats.removeFileCount + executionStats.removeDirectoryCount)
        .arg(plannedOperationCount)
        .arg(executionStats.createDirectoryCount)
        .arg(executionStats.downloadFileCount)
        .arg(executionStats.removeFileCount)
        .arg(executionStats.removeDirectoryCount)
        .arg(executionStats.completedProgressUnits)
        .arg(totalProgressUnits)
        .arg(formatElapsedTime(elapsedMs))
        .arg(reason);
}

QString buildPlanPreviewSummary(const SyncPlanStats &planStats)
{
    return QObject::tr("备份前检查：待删除文件 %1，待新增文件 %2，待同步文件 %3。")
        .arg(planStats.removeFileCount)
        .arg(planStats.addFileCount)
        .arg(planStats.updateFileCount);
}

class HttpRequestExecutor
{
public:
    HttpRequestExecutor(HttpFolderSyncWorker *worker,
                        QNetworkAccessManager *networkAccessManager,
                        const CancelCallback &cancelCallback)
        : _worker(worker),
          _networkAccessManager(networkAccessManager),
          _cancelCallback(cancelCallback)
    {
    }

    bool get(const QUrl &url,
             const QString &accessToken,
             const std::function<void(qint64, qint64)> &progressCallback,
             QByteArray *responseBody,
             QString *errorMessage)
    {
        if (_networkAccessManager == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("网络访问管理器未初始化。");
            }
            return false;
        }

        QNetworkReply *reply = createReply(url, accessToken);
        if (reply == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("创建网络请求失败，url=%1").arg(url.toString());
            }
            return false;
        }

        _worker->setCurrentReply(reply);
        QEventLoop eventLoop;
        if (progressCallback != nullptr) {
            QObject::connect(reply,
                             &QNetworkReply::downloadProgress,
                             &eventLoop,
                             [&progressCallback](qint64 receivedBytes, qint64 totalBytes) {
                                 progressCallback(receivedBytes, totalBytes);
                             });
        }
        QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();

        const QByteArray replyBody = reply->readAll();
        const bool isSuccessful = validateReply(reply, url, replyBody, errorMessage);
        _worker->clearCurrentReply(reply);
        reply->deleteLater();

        if (!isSuccessful) {
            return false;
        }

        if (responseBody != nullptr) {
            *responseBody = replyBody;
        }
        return true;
    }

    bool downloadToFile(const SyncOperation &operation,
                        const QString &accessToken,
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

        QSaveFile targetFile(operation.targetPath);
        if (!targetFile.open(QIODevice::WriteOnly)) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("打开本地文件失败，target=%1，error=%2")
                                    .arg(operation.targetPath, targetFile.errorString());
            }
            return false;
        }

        QNetworkReply *reply = createReply(operation.sourceUrl, accessToken);
        if (reply == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("创建下载请求失败，url=%1").arg(operation.sourceUrl.toString());
            }
            return false;
        }

        _worker->setCurrentReply(reply);

        QEventLoop eventLoop;
        QByteArray errorBody;
        QString writeErrorMessage;
        qint64 downloadedBytes = 0;
        qint64 lastReportedUnits = 0;

        QObject::connect(reply, &QIODevice::readyRead, &eventLoop, [&]() {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (statusCode >= 400) {
                errorBody.append(reply->readAll());
                return;
            }

            const QByteArray buffer = reply->readAll();
            if (buffer.isEmpty()) {
                return;
            }

            if (!writeAll(&targetFile, buffer, operation.targetPath, &writeErrorMessage)) {
                reply->abort();
                return;
            }

            downloadedBytes += buffer.size();
            const qint64 currentUnits =
                qMin(operation.progressUnits, calculateDownloadProgressUnits(downloadedBytes));
            if (progressCallback != nullptr && currentUnits > lastReportedUnits) {
                progressCallback(currentUnits, describeOperation(operation));
                lastReportedUnits = currentUnits;
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();

        const QByteArray tailBuffer = reply->readAll();
        if (!tailBuffer.isEmpty()) {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (statusCode >= 400) {
                errorBody.append(tailBuffer);
            } else if (writeErrorMessage.isEmpty()) {
                if (!writeAll(&targetFile, tailBuffer, operation.targetPath, &writeErrorMessage)) {
                    reply->abort();
                } else {
                    downloadedBytes += tailBuffer.size();
                }
            }
        }

        const bool isSuccessful = writeErrorMessage.isEmpty()
            && validateReply(reply, operation.sourceUrl, errorBody, errorMessage);
        _worker->clearCurrentReply(reply);
        reply->deleteLater();

        if (!writeErrorMessage.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = writeErrorMessage;
            }
            targetFile.cancelWriting();
            return false;
        }

        if (!isSuccessful) {
            targetFile.cancelWriting();
            return false;
        }

        if (!targetFile.commit()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("提交下载文件失败，target=%1，error=%2")
                                    .arg(operation.targetPath, targetFile.errorString());
            }
            return false;
        }

        QFile targetMetadataFile(operation.targetPath);
        if (targetMetadataFile.open(QIODevice::ReadWrite)) {
            if (!targetMetadataFile.setFileTime(QDateTime::fromMSecsSinceEpoch(operation.modifiedTimeMs),
                                                QFileDevice::FileModificationTime)) {
                qWarning() << "Failed to update downloaded file timestamp. target=" << operation.targetPath
                           << "mtimeMs=" << operation.modifiedTimeMs
                           << "error=" << targetMetadataFile.errorString();
            }
        } else {
            qWarning() << "Failed to open downloaded file for timestamp update. target="
                       << operation.targetPath
                       << "error=" << targetMetadataFile.errorString();
        }

        if (progressCallback != nullptr && operation.progressUnits > lastReportedUnits) {
            progressCallback(operation.progressUnits, describeOperation(operation));
        }
        return true;
    }

private:
    QNetworkReply *createReply(const QUrl &url, const QString &accessToken) const
    {
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(kNetworkTransferTimeoutMs);
        request.setRawHeader(QByteArrayLiteral("Accept"),
                             QByteArrayLiteral("application/json, application/octet-stream, */*"));
        if (!accessToken.trimmed().isEmpty()) {
            request.setRawHeader(QByteArrayLiteral("Authorization"),
                                 QStringLiteral("Bearer %1").arg(accessToken.trimmed()).toUtf8());
        }
        return _networkAccessManager->get(request);
    }

    bool validateReply(QNetworkReply *reply,
                       const QUrl &url,
                       const QByteArray &errorBody,
                       QString *errorMessage) const
    {
        if (_cancelCallback != nullptr && _cancelCallback()) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("用户取消同步。");
            }
            return false;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (errorMessage != nullptr) {
                QString detail = QString::fromUtf8(errorBody).trimmed();
                if (detail.size() > 160) {
                    detail = detail.left(160) + QStringLiteral("...");
                }
                *errorMessage = detail.isEmpty()
                    ? QObject::tr("访问远端失败，url=%1，httpStatus=%2，error=%3")
                          .arg(url.toString())
                          .arg(statusCode)
                          .arg(reply->errorString())
                    : QObject::tr("访问远端失败，url=%1，httpStatus=%2，error=%3，body=%4")
                          .arg(url.toString())
                          .arg(statusCode)
                          .arg(reply->errorString(), detail);
            }
            return false;
        }

        if (statusCode < 200 || statusCode >= 300) {
            if (errorMessage != nullptr) {
                QString detail = QString::fromUtf8(errorBody).trimmed();
                if (detail.size() > 160) {
                    detail = detail.left(160) + QStringLiteral("...");
                }
                *errorMessage = detail.isEmpty()
                    ? QObject::tr("远端返回异常状态，url=%1，httpStatus=%2")
                          .arg(url.toString())
                          .arg(statusCode)
                    : QObject::tr("远端返回异常状态，url=%1，httpStatus=%2，body=%3")
                          .arg(url.toString())
                          .arg(statusCode)
                          .arg(detail);
            }
            return false;
        }

        return true;
    }

    HttpFolderSyncWorker *_worker;
    QNetworkAccessManager *_networkAccessManager;
    CancelCallback _cancelCallback;
};
} // namespace

HttpFolderSyncWorker::HttpFolderSyncWorker(QObject *parent)
    : QObject(parent)
{
}

void HttpFolderSyncWorker::slotCancelSync()
{
    _isCancelRequested.storeRelease(1);
    if (_currentReply != nullptr) {
        _currentReply->abort();
    }
}

void HttpFolderSyncWorker::slotStartSync(const QString &sourceUrl,
                                         const QString &targetPath,
                                         const QString &accessToken,
                                         const QString &reason,
                                         int compareMode)
{
    QElapsedTimer syncTimer;
    syncTimer.start();
    _isCancelRequested.storeRelease(0);
    _currentReply = nullptr;

    auto cancelCallback = [this]() { return isCancelRequested(); };

    QString errorMessage;
    const QString normalizedSourceUrl = normalizeHttpSourceUrl(sourceUrl, &errorMessage);
    const QString normalizedTargetPath = normalizeLocalPath(targetPath);
    emit sigLogMessage(QObject::tr("收到 HTTP 同步请求，reason=%1，source=%2，target=%3")
                           .arg(reason, normalizedSourceUrl, normalizedTargetPath));
    emit sigLogMessage(summarizeCompareMode(compareMode));

    if (!errorMessage.isEmpty()) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    if (normalizedTargetPath.isEmpty()) {
        errorMessage = QObject::tr("本地同步目录不能为空，target=%1").arg(targetPath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    const QFileInfo targetInfo(normalizedTargetPath);
    if (targetInfo.exists() && !targetInfo.isDir()) {
        errorMessage = QObject::tr("本地同步路径已存在且不是目录，target=%1").arg(normalizedTargetPath);
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    if (!ensureDirectoryExists(normalizedTargetPath, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    QNetworkAccessManager networkAccessManager;
    networkAccessManager.setTransferTimeout(kNetworkTransferTimeoutMs);
    networkAccessManager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    HttpRequestExecutor requestExecutor(this, &networkAccessManager, cancelCallback);

    emit sigSyncProgress(0, 0, QObject::tr("正在获取远端清单"));
    QByteArray manifestJson;
    if (!requestExecutor.get(buildManifestUrl(normalizedSourceUrl),
                             accessToken,
                             [this](qint64 receivedBytes, qint64 totalBytes) {
                                 emit sigSyncProgress(0,
                                                      0,
                                                      buildManifestProgressText(receivedBytes, totalBytes));
                             },
                             &manifestJson,
                             &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }

    QVector<ManifestEntry> manifestEntries;
    if (!parseManifest(manifestJson, &manifestEntries, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        emit sigSyncFinished(false, errorMessage);
        return;
    }
    emit sigLogMessage(QObject::tr("远端清单获取完成，entryCount=%1").arg(manifestEntries.size()));

    QVector<SyncOperation> operations;
    SyncPlanStats planStats;
    if (!buildSyncPlan(manifestEntries,
                       normalizedTargetPath,
                       normalizedSourceUrl,
                       &operations,
                       &planStats,
                       [this](const PlanStageProgress &progress) {
                           emit sigSyncProgress(progress.current, progress.total, progress.text);
                       },
                       cancelCallback,
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
        const QString summary = QObject::tr("HTTP 源与本地目录已一致，无需同步。");
        emit sigLogMessage(buildExecutionSummary(ExecutionStats(),
                                                 operations.size(),
                                                 totalProgressUnits,
                                                 syncTimer.elapsed(),
                                                 reason));
        emit sigLogMessage(summary);
        emit sigSyncFinished(true, summary);
        return;
    }

    emit sigLogMessage(QObject::tr("HTTP 同步计划生成完成，totalSteps=%1，reason=%2")
                           .arg(totalProgressUnits)
                           .arg(reason));

    qint64 completedProgressUnits = 0;
    ExecutionStats executionStats;
    for (const SyncOperation &operation : operations) {
        if (cancelCallback()) {
            const QString summary = QObject::tr("同步已取消，reason=%1").arg(reason);
            emit sigLogMessage(buildExecutionSummary(executionStats,
                                                     operations.size(),
                                                     totalProgressUnits,
                                                     syncTimer.elapsed(),
                                                     reason));
            emit sigLogMessage(summary);
            emit sigSyncFinished(false, summary);
            return;
        }

        const QString currentItem = describeOperation(operation);
        emit sigSyncProgress(completedProgressUnits, totalProgressUnits, currentItem);

        QString executeErrorMessage;
        bool executeSuccess = false;
        if (operation.type == SyncOperationType::E_DownloadFile) {
            executeSuccess = requestExecutor.downloadToFile(
                operation,
                accessToken,
                [this, completedProgressUnits, totalProgressUnits](qint64 operationProgressUnits,
                                                                   const QString &progressItem) {
                    emit sigSyncProgress(completedProgressUnits + operationProgressUnits,
                                         totalProgressUnits,
                                         progressItem);
                },
                &executeErrorMessage);
        } else {
            executeSuccess = executeLocalOperation(operation, &executeErrorMessage);
        }

        if (!executeSuccess) {
            const QString summary =
                QObject::tr("同步失败，reason=%1，step=%2，error=%3")
                    .arg(reason, currentItem, executeErrorMessage);
            emit sigLogMessage(buildExecutionSummary(executionStats,
                                                     operations.size(),
                                                     totalProgressUnits,
                                                     syncTimer.elapsed(),
                                                     reason));
            emit sigLogMessage(summary);
            emit sigSyncFinished(false, summary);
            return;
        }

        completedProgressUnits += qMax<qint64>(1, operation.progressUnits);
        recordExecutedOperation(operation, &executionStats);
        emit sigSyncProgress(completedProgressUnits, totalProgressUnits, currentItem);
    }

    const QString summary = buildSummary(operations);
    emit sigLogMessage(buildExecutionSummary(executionStats,
                                             operations.size(),
                                             totalProgressUnits,
                                             syncTimer.elapsed(),
                                             reason));
    emit sigLogMessage(summary);
    emit sigSyncFinished(true, summary);
}

bool HttpFolderSyncWorker::isCancelRequested() const
{
    return _isCancelRequested.loadAcquire() != 0 || QThread::currentThread()->isInterruptionRequested();
}

void HttpFolderSyncWorker::setCurrentReply(QNetworkReply *reply)
{
    _currentReply = reply;
}

void HttpFolderSyncWorker::clearCurrentReply(QNetworkReply *reply)
{
    if (_currentReply == reply) {
        _currentReply.clear();
    }
}
