#pragma once

#include <QDialog>
#include <QPointer>

#include "SyncTypes.h"

class QNetworkAccessManager;
class QNetworkReply;

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
     * @brief 设置同步源类型。
     * @param sourceType 同步源类型。
     */
    void setSourceType(PairSourceType sourceType);

    /**
     * @brief 设置同步源位置。
     * @param sourceLocation 本地模式下为目录路径，HTTP 模式下为服务地址。
     */
    void setSourceLocation(const QString &sourceLocation);

    /**
     * @brief 设置 B 备份目录路径。
     * @param targetPath B 备份目录绝对路径。
     */
    void setTargetPath(const QString &targetPath);

    /**
     * @brief 设置 HTTP 源访问令牌。
     * @param sourceAccessToken 访问令牌；为空时表示不启用鉴权。
     */
    void setSourceAccessToken(const QString &sourceAccessToken);

    /**
     * @brief 获取同步源类型。
     * @return 当前选择的同步源类型。
     */
    PairSourceType sourceType() const;

    /**
     * @brief 获取同步源位置。
     * @return 本地模式下为规范化目录路径，HTTP 模式下为用户输入的服务地址。
     */
    QString sourceLocation() const;

    /**
     * @brief 获取 B 备份目录路径。
     * @return B 备份目录绝对路径。
     */
    QString targetPath() const;

    /**
     * @brief 获取 HTTP 源访问令牌。
     * @return 访问令牌。
     */
    QString sourceAccessToken() const;

private slots:
    void slotSourceTypeChanged(int index);
    void slotBrowseSourceFolder();
    void slotBrowseTargetFolder();
    void slotTestSourceConnection();
    void slotPreviewSourceManifest();
    void slotProbeReplyFinished();
    void slotProbeInputChanged();

private:
    enum ProbeActionKind
    {
        E_NoProbeAction = 0,
        E_TestConnectionAction = 1,
        E_PreviewManifestAction = 2
    };

    void applySourceTypeUi(PairSourceType sourceType);
    void updateProbeUiState(bool isBusy);
    void clearProbeResult(bool preserveStatusText = false);
    bool startProbeRequest(ProbeActionKind probeActionKind);
    bool buildManifestPreviewText(const QByteArray &manifestJson,
                                  QString *summaryText,
                                  QString *previewText,
                                  QString *errorMessage) const;

    Ui::PairEditDialog *_ui;
    QNetworkAccessManager *_networkAccessManager;
    QPointer<QNetworkReply> _activeProbeReply;
    int _activeProbeActionKind;
};
