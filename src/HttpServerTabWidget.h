#pragma once

#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace Ui
{
class HttpServerTabWidget;
}

/**
 * @brief HttpServerTabWidget 承载 HTTP 服务端页的界面元素。
 *
 * 该页主要用于维护当前电脑对外发布的共享目录列表，以及启动和停止 HTTP 服务。
 */
class HttpServerTabWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造 HTTP 服务端页控件。
     * @param parent QWidget 父对象。
     */
    explicit HttpServerTabWidget(QWidget *parent = nullptr);

    /**
     * @brief 析构 HTTP 服务端页控件。
     */
    ~HttpServerTabWidget() override;

    /**
     * @brief 获取共享目录表格。
     * @return 共享目录表格对象。
     */
    QTableWidget *sharedFolderTableWidget() const;

    /**
     * @brief 获取“新增共享目录”按钮。
     * @return 按钮对象。
     */
    QPushButton *addSharedFolderButton() const;

    /**
     * @brief 获取“编辑共享目录”按钮。
     * @return 按钮对象。
     */
    QPushButton *editSharedFolderButton() const;

    /**
     * @brief 获取“删除共享目录”按钮。
     * @return 按钮对象。
     */
    QPushButton *removeSharedFolderButton() const;

    /**
     * @brief 获取 HTTP 端口输入框。
     * @return 端口输入框对象。
     */
    QSpinBox *httpServerPortSpinBox() const;

    /**
     * @brief 获取访问令牌输入框。
     * @return 访问令牌输入框对象。
     */
    QLineEdit *httpServerTokenEdit() const;

    /**
     * @brief 获取启动服务按钮。
     * @return 按钮对象。
     */
    QPushButton *startHttpServerButton() const;

    /**
     * @brief 获取停止服务按钮。
     * @return 按钮对象。
     */
    QPushButton *stopHttpServerButton() const;

    /**
     * @brief 获取当前服务 URL 显示框。
     * @return 只读文本框对象。
     */
    QPlainTextEdit *httpServerEndpointEdit() const;

private:
    Ui::HttpServerTabWidget *_ui;
};
