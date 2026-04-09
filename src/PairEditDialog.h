#pragma once

#include <QDialog>

namespace Ui
{
class PairEditDialog;
}

/**
 * @brief PairEditDialog 用于弹窗编辑一组 A/B 同步路径。
 *
 * 把新增和编辑入口统一成弹窗，可以减少主窗口上的固定输入区，
 * 也更适合后续扩展更多同步对属性。
 */
class PairEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造路径编辑弹窗。
     * @param parent QWidget 父对象。
     */
    explicit PairEditDialog(QWidget *parent = nullptr);

    /**
     * @brief 析构弹窗。
     */
    ~PairEditDialog() override;

    /**
     * @brief 设置 A 主目录路径。
     * @param sourcePath A 主目录绝对路径。
     */
    void setSourcePath(const QString &sourcePath);

    /**
     * @brief 设置 B 备份目录路径。
     * @param targetPath B 备份目录绝对路径。
     */
    void setTargetPath(const QString &targetPath);

    /**
     * @brief 获取 A 主目录路径。
     * @return A 主目录绝对路径。
     */
    QString sourcePath() const;

    /**
     * @brief 获取 B 备份目录路径。
     * @return B 备份目录绝对路径。
     */
    QString targetPath() const;

private slots:
    void slotBrowseSourceFolder();
    void slotBrowseTargetFolder();

private:
    Ui::PairEditDialog *_ui;
};
