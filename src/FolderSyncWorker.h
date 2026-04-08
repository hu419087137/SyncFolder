#pragma once

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
     */
    void slotStartSync(const QString &sourcePath, const QString &targetPath, const QString &reason);

signals:
    /**
     * @brief 同步计划已生成，准备开始执行。
     * @param totalSteps 本次需要执行的总步骤数。
     * @param reason 触发同步的原因。
     */
    void sigSyncStarted(int totalSteps, const QString &reason);

    /**
     * @brief 当前步骤进度发生变化。
     * @param currentStep 当前步骤序号，从 1 开始。
     * @param totalSteps 本次同步总步骤数。
     * @param currentItem 当前正在处理的对象说明。
     */
    void sigSyncProgress(int currentStep, int totalSteps, const QString &currentItem);

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
};
