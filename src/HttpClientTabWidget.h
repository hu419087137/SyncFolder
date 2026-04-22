#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTableView;
class QTableWidget;

namespace Ui
{
class HttpClientTabWidget;
}

/**
 * @brief HttpClientTabWidget 承载 HTTP 客户端页的界面元素。
 *
 * 该页负责连接远端 HTTP 服务、拉取可同步目录列表，并把选中的目录批量加入本地备份任务。
 */
class HttpClientTabWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造 HTTP 客户端页控件。
     * @param parent QWidget 父对象。
     */
    explicit HttpClientTabWidget(QWidget *parent = nullptr);

    /**
     * @brief 析构 HTTP 客户端页控件。
     */
    ~HttpClientTabWidget() override;

    /**
     * @brief 获取远端 HTTP 地址输入框。
     * @return 输入框对象。
     */
    QLineEdit *serverUrlEdit() const;

    /**
     * @brief 获取远端访问令牌输入框。
     * @return 输入框对象。
     */
    QLineEdit *accessTokenEdit() const;

    /**
     * @brief 获取“获取目录列表”按钮。
     * @return 按钮对象。
     */
    QPushButton *fetchSourcesButton() const;

    /**
     * @brief 获取远端目录列表表格。
     * @return 表格对象。
     */
    QTableWidget *availableSourcesTableWidget() const;

    /**
     * @brief 获取本地导入根目录输入框。
     * @return 输入框对象。
     */
    QLineEdit *targetRootEdit() const;

    /**
     * @brief 获取本地导入根目录浏览按钮。
     * @return 按钮对象。
     */
    QPushButton *browseTargetRootButton() const;

    /**
     * @brief 获取“添加选中目录到同步列表”按钮。
     * @return 按钮对象。
     */
    QPushButton *addSelectedSourcesButton() const;

    /**
     * @brief 获取 HTTP 客户端同步任务表格。
     * @return 表格视图对象。
     */
    QTableView *pairTableView() const;

    /**
     * @brief 获取“删除选中客户端任务”按钮。
     * @return 按钮对象。
     */
    QPushButton *removePairButton() const;

    /**
     * @brief 获取“同步全部 HTTP 客户端任务”按钮。
     * @return 按钮对象。
     */
    QPushButton *syncAllPairsButton() const;

private:
    Ui::HttpClientTabWidget *_ui;
};
