#pragma once

#include <QObject>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;

/**
 * @brief HttpSyncServer 把本地目录以只读 HTTP 接口形式暴露给其他设备拉取。
 *
 * 当前服务端只提供单向拉取所需的最小接口：目录清单和单文件下载，
 * 这样可以先支撑 “A 提供目录、B/C 拉取同步” 的跨电脑场景。
 */
class HttpSyncServer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造 HTTP 同步服务对象。
     * @param parent QObject 父对象。
     */
    explicit HttpSyncServer(QObject *parent = nullptr);

    /**
     * @brief 析构服务对象，并确保监听端口被释放。
     */
    ~HttpSyncServer() override;

    /**
     * @brief 启动 HTTP 同步服务。
     * @param rootPath 对外提供同步内容的本地根目录。
     * @param listenPort 监听端口。
     * @param accessToken 可选访问令牌；为空时表示不启用鉴权。
     * @param errorMessage 启动失败时返回详细错误。
     * @return `true` 表示启动成功，`false` 表示启动失败。
     */
    bool startServer(const QString &rootPath,
                     quint16 listenPort,
                     const QString &accessToken,
                     QString *errorMessage);

    /**
     * @brief 停止 HTTP 同步服务。
     */
    void stopServer();

    /**
     * @brief 查询服务当前是否处于运行状态。
     * @return `true` 表示服务已启动。
     */
    bool isRunning() const;

    /**
     * @brief 获取当前监听端口。
     * @return 监听端口；未启动时返回 0。
     */
    quint16 listenPort() const;

    /**
     * @brief 获取可供其他设备填写的基地址提示。
     * @return 可访问的 HTTP 基地址列表。
     */
    QStringList endpointUrls() const;

signals:
    /**
     * @brief 输出服务端相关日志。
     * @param message 日志内容。
     */
    void sigLogMessage(const QString &message);

    /**
     * @brief 服务状态发生变化时通知界面刷新。
     * @param isRunning 当前是否正在监听。
     * @param listenPort 当前监听端口。
     */
    void sigServerStateChanged(bool isRunning, quint16 listenPort);

private:
    QHttpServerResponse handleManifestRequest(const QHttpServerRequest &request);
    QHttpServerResponse handleFileRequest(const QHttpServerRequest &request);
    QHttpServerResponse buildErrorResponse(int statusCode, const QString &errorMessage) const;
    bool isAuthorized(const QHttpServerRequest &request) const;
    bool buildManifestJson(QByteArray *manifestJson, QString *errorMessage) const;

    QHttpServer *_httpServer;
    QString _rootPath;
    QString _accessToken;
    quint16 _listenPort;
};
