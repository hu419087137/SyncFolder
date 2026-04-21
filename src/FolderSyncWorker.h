#pragma once

#include <QAtomicInteger>
#include <QObject>

/**
 * @brief FolderSyncWorker 在后台线程中执行 A 到 B 的单向同步。
 *
 * 把同步逻辑放到独立线程中，可以避免扫描、复制和删除大量文件时阻塞 UI，
 * 这样进度条和日志仍然能实时刷新。
 */
class FolderSyncWorker : public QObject
{
    Q_OBJECT

public:
    enum CompareMode
    {
        E_StrictCompare = 0,
        E_FastCompare = 1,
        E_TurboCompare = 2
    };
    Q_ENUM(CompareMode)

    /**
     * @brief 构造同步工作对象。
     * @param parent QObject 父对象。
     */
    explicit FolderSyncWorker(QObject *parent = nullptr);

public slots:
    /**
     * @brief 执行一次以 A 为主、B 为备份的同步任务。
     * @param sourcePath A 主目录绝对路径。
     * @param targetPath B 备份目录绝对路径。
     * @param reason 触发本次同步的原因，便于日志定位。
     * @param compareMode 文件差异判断模式。
     */
    void slotStartSync(const QString &sourcePath,
                       const QString &targetPath,
                       const QString &reason,
                       CompareMode compareMode);

    /**
     * @brief 请求取消当前同步任务。
     *
     * 取消请求会在扫描、比对、复制或步骤切换时被后台线程检查，
     * 已经提交完成的文件操作不会回滚。
     */
    void slotCancelSync();

signals:
    /**
     * @brief 同步计划已生成，准备开始执行。
     * @param totalProgressUnits 本次同步总进度单位数。
     *
     * 删除/创建目录通常只占 1 个进度单位，大文件复制会被拆成多个进度单位，
     * 这样界面可以更及时地反馈长时间复制任务的执行进度。
     * @param removeFileCount 备份前需要删除的文件数量。
     * @param addFileCount 备份前需要新增的文件数量。
     * @param updateFileCount 备份前需要同步覆盖的文件数量。
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
     * @param totalProgressUnits 本次同步总进度单位数；当值小于等于 0 时，表示当前处于扫描或比对中的忙碌态。
     * @param currentItem 当前正在处理的对象说明。
     */
    void sigSyncProgress(qint64 currentProgressUnits, qint64 totalProgressUnits, const QString &currentItem);

    /**
     * @brief 一次同步流程已经结束。
     * @param success `true` 表示同步成功，`false` 表示同步失败。
     * @param summary 本次同步的结果摘要。
     */
    void sigSyncFinished(bool success, const QString &summary);

    /**
     * @brief 输出可检索的日志信息。
     * @param message 日志内容。
     */
    void sigLogMessage(const QString &message);

private:
    QAtomicInteger<int> _isCancelRequested = 0;
};
