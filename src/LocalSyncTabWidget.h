#pragma once

#include <QWidget>

class QComboBox;
class QPushButton;
class QTableView;

namespace Ui
{
class LocalSyncTabWidget;
}

/**
 * @brief LocalSyncTabWidget 承载本地文件夹同步页的界面元素。
 *
 * 主窗口负责具体业务逻辑，本控件只负责提供由 `.ui` 维护的本地同步页布局，
 * 便于后续继续扩展而不把 MainWindow.ui 堆得过大。
 */
class LocalSyncTabWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造本地同步页控件。
     * @param parent QWidget 父对象。
     */
    explicit LocalSyncTabWidget(QWidget *parent = nullptr);

    /**
     * @brief 析构本地同步页控件。
     */
    ~LocalSyncTabWidget() override;

    /**
     * @brief 获取“新增同步对”按钮。
     * @return 新增按钮对象。
     */
    QPushButton *addPairButton() const;

    /**
     * @brief 获取“删除选中项”按钮。
     * @return 删除按钮对象。
     */
    QPushButton *removePairButton() const;

    /**
     * @brief 获取本地同步表格视图。
     * @return 表格视图对象。
     */
    QTableView *pairTableView() const;

    /**
     * @brief 获取“启动全部监控”按钮。
     * @return 启动监控按钮对象。
     */
    QPushButton *startMonitorButton() const;

    /**
     * @brief 获取“停止监控”按钮。
     * @return 停止监控按钮对象。
     */
    QPushButton *stopMonitorButton() const;

    /**
     * @brief 获取“同步全部”按钮。
     * @return 同步全部按钮对象。
     */
    QPushButton *syncAllPairsButton() const;

    /**
     * @brief 获取最大并发选择框。
     * @return 并发数选择框对象。
     */
    QComboBox *maxParallelSyncComboBox() const;

    /**
     * @brief 获取比对模式选择框。
     * @return 比对模式选择框对象。
     */
    QComboBox *compareModeComboBox() const;

private:
    Ui::LocalSyncTabWidget *_ui;
};
