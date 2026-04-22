#pragma once

#include <QAtomicInteger>
#include <QObject>
#include <QPointer>

class QNetworkReply;

/**
 * @brief HttpFolderSyncWorker 在后台线程中执行 “HTTP 源目录 -> 本地目录” 的单向同步。
 *
 * 该工作对象会先拉取远端清单，再在本地构建删除、建目录和下载文件计划，
 * 以尽量复用现有界面的进度展示方式，并保持 UI 线程无阻塞。
 */
class HttpFolderSyncWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造 HTTP 同步工作对象。
     * @param parent QObject 父对象。
     */
    explicit HttpFolderSyncWorker(QObject *parent = nullptr);

public slots:
    /**
     * @brief 执行一次 HTTP 源目录拉取同步。
     * @param sourceUrl HTTP 服务基地址。
     * @param targetPath 本地同步目标目录。
     * @param accessToken 访问令牌，可为空。
     * @param reason 触发本次同步的原因。
     * @param compareMode 文件差异判断模式编号。
     */
    void slotStartSync(const QString &sourceUrl,
                       const QString &targetPath,
                       const QString &accessToken,
                       const QString &reason,
                       int compareMode);

    /**
     * @brief 请求取消当前 HTTP 同步任务。
     */
    void slotCancelSync();

signals:
    /**
     * @brief 同步计划已生成，准备开始执行。
     * @param totalProgressUnits 本次同步总进度单位数。
     * @param removeFileCount 本地需要删除的文件数量。
     * @param addFileCount 本地需要新增的文件数量。
     * @param updateFileCount 本地需要覆盖更新的文件数量。
     * @param reason 触发同步的原因。
     */
    void sigSyncStarted(qint64 totalProgressUnits,
                        int removeFileCount,
                        int addFileCount,
                        int updateFileCount,
                        const QString &reason);

    /**
     * @brief 当前步骤进度发生变化。
     * @param currentProgressUnits 当前已完成的进度单位数。
     * @param totalProgressUnits 本次同步总进度单位数；当值小于等于 0 时表示忙碌态。
     * @param currentItem 当前正在处理的对象说明。
     */
    void sigSyncProgress(qint64 currentProgressUnits, qint64 totalProgressUnits, const QString &currentItem);

    /**
     * @brief 一次 HTTP 同步流程已经结束。
     * @param success `true` 表示同步成功。
     * @param summary 本次同步摘要。
     */
    void sigSyncFinished(bool success, const QString &summary);

    /**
     * @brief 输出可检索的日志信息。
     * @param message 日志内容。
     */
    void sigLogMessage(const QString &message);

public:
    /**
     * @brief 记录当前正在执行的网络请求对象，便于取消时立即中断。
     * @param reply 当前活动的网络回复对象。
     */
    bool isCancelRequested() const;

    /**
     * @brief 记录当前正在执行的网络请求对象，便于取消时立即中断。
     * @param reply 当前活动的网络回复对象。
     */
    void setCurrentReply(QNetworkReply *reply);

    /**
     * @brief 清除当前网络请求对象记录。
     * @param reply 即将结束的网络回复对象。
     */
    void clearCurrentReply(QNetworkReply *reply);

private:
    QAtomicInteger<int> _isCancelRequested = 0;
    QPointer<QNetworkReply> _currentReply;
};
