#include "HttpSyncServer.h"

#include <QtHttpServer/qhttpserver.h>
#include <QtHttpServer/qhttpserverrequest.h>
#include <QtHttpServer/qhttpserverresponse.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>

namespace
{
struct ManifestEntry
{
    QString relativePath;
    bool isDirectory = false;
    qint64 fileSize = 0;
    qint64 modifiedTimeMs = 0;
};

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

bool isSameOrChildPath(const QString &rootPath, const QString &candidatePath)
{
    const QString normalizedRootPath = QDir::cleanPath(rootPath);
    const QString normalizedCandidatePath = QDir::cleanPath(candidatePath);
    const Qt::CaseSensitivity caseSensitivity = pathCaseSensitivity();

    if (normalizedRootPath.compare(normalizedCandidatePath, caseSensitivity) == 0) {
        return true;
    }

    const QString rootPrefix = normalizedRootPath + QDir::separator();
    return normalizedCandidatePath.startsWith(rootPrefix, caseSensitivity);
}

QString normalizeRelativePath(const QDir &rootDir, const QString &absolutePath)
{
    return QDir::fromNativeSeparators(rootDir.relativeFilePath(absolutePath));
}

bool tryParseRelativeFilePath(const QString &rawPath, QString *relativePath)
{
    QString normalizedPath = QDir::fromNativeSeparators(rawPath.trimmed());
    while (normalizedPath.startsWith(QLatin1Char('/'))) {
        normalizedPath.remove(0, 1);
    }

    normalizedPath = QDir::cleanPath(normalizedPath);
    if (normalizedPath.isEmpty()
        || normalizedPath == QStringLiteral(".")
        || normalizedPath == QStringLiteral("..")
        || normalizedPath.startsWith(QStringLiteral("../"))) {
        return false;
    }

    if (relativePath != nullptr) {
        *relativePath = normalizedPath;
    }
    return true;
}

QStringList buildEndpointUrls(quint16 listenPort)
{
    if (listenPort == 0) {
        return {};
    }

    QSet<QString> urls;
    urls.insert(QStringLiteral("http://127.0.0.1:%1").arg(listenPort));
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) {
            continue;
        }

        urls.insert(QStringLiteral("http://%1:%2").arg(address.toString()).arg(listenPort));
    }

    QStringList endpointUrls = urls.values();
    std::sort(endpointUrls.begin(), endpointUrls.end());
    return endpointUrls;
}

QHttpServerResponse::StatusCode toStatusCode(int statusCode)
{
    return static_cast<QHttpServerResponse::StatusCode>(statusCode);
}

QByteArray buildCompactErrorJson(const QString &errorMessage)
{
    const QJsonObject errorObject{{QStringLiteral("error"), errorMessage}};
    return QJsonDocument(errorObject).toJson(QJsonDocument::Compact);
}
} // namespace

HttpSyncServer::HttpSyncServer(QObject *parent)
    : QObject(parent),
      _httpServer(nullptr),
      _listenPort(0)
{
}

HttpSyncServer::~HttpSyncServer()
{
    stopServer();
}

bool HttpSyncServer::startServer(const QString &rootPath,
                                 quint16 listenPort,
                                 const QString &accessToken,
                                 QString *errorMessage)
{
    const QString normalizedRootPath = normalizeLocalPath(rootPath);
    if (normalizedRootPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务根目录不能为空。");
        }
        return false;
    }

    const QFileInfo rootInfo(normalizedRootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务根目录不存在或不是目录，path=%1").arg(normalizedRootPath);
        }
        return false;
    }

    stopServer();

    _rootPath = normalizedRootPath;
    _accessToken = accessToken.trimmed();
    _listenPort = listenPort;
    _httpServer = new QHttpServer(this);

    const bool manifestRouteOk = _httpServer->route(
        QStringLiteral("/api/v1/manifest"),
        [this](const QHttpServerRequest &request) { return handleManifestRequest(request); });
    const bool fileRouteOk = _httpServer->route(
        QStringLiteral("/api/v1/file"),
        [this](const QHttpServerRequest &request) { return handleFileRequest(request); });
    const bool pingRouteOk = _httpServer->route(QStringLiteral("/api/v1/ping"), [this]() {
        QJsonObject infoObject{
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("listenPort"), static_cast<int>(_listenPort)},
            {QStringLiteral("hasToken"), !_accessToken.isEmpty()}};
        QHttpServerResponse response(infoObject);
        response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
        return response;
    });
    if (!manifestRouteOk || !fileRouteOk || !pingRouteOk) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("注册 HTTP 接口失败，请检查 QtHttpServer 路由配置。");
        }
        delete _httpServer;
        _httpServer = nullptr;
        _rootPath.clear();
        _accessToken.clear();
        _listenPort = 0;
        return false;
    }

    const quint16 actualListenPort = _httpServer->listen(QHostAddress::Any, listenPort);
    if (actualListenPort == 0) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("启动 HTTP 服务失败，端口可能已被占用，port=%1").arg(listenPort);
        }
        delete _httpServer;
        _httpServer = nullptr;
        _rootPath.clear();
        _accessToken.clear();
        _listenPort = 0;
        return false;
    }

    _listenPort = actualListenPort;
    emit sigLogMessage(tr("HTTP 同步服务已启动，root=%1，port=%2，鉴权=%3，urls=%4")
                           .arg(QDir::toNativeSeparators(_rootPath))
                           .arg(_listenPort)
                           .arg(_accessToken.isEmpty() ? tr("关闭") : tr("开启"))
                           .arg(endpointUrls().join(tr("； "))));
    emit sigServerStateChanged(true, _listenPort);
    return true;
}

void HttpSyncServer::stopServer()
{
    if (_httpServer == nullptr) {
        return;
    }

    delete _httpServer;
    _httpServer = nullptr;

    const quint16 previousListenPort = _listenPort;
    _listenPort = 0;
    emit sigLogMessage(tr("HTTP 同步服务已停止，port=%1").arg(previousListenPort));
    emit sigServerStateChanged(false, 0);
}

bool HttpSyncServer::isRunning() const
{
    return _httpServer != nullptr && _listenPort > 0;
}

quint16 HttpSyncServer::listenPort() const
{
    return _listenPort;
}

QStringList HttpSyncServer::endpointUrls() const
{
    return buildEndpointUrls(_listenPort);
}

QHttpServerResponse HttpSyncServer::handleManifestRequest(const QHttpServerRequest &request)
{
    if (!isAuthorized(request)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::Unauthorized),
                                  tr("访问令牌无效。"));
    }

    QByteArray manifestJson;
    QString errorMessage;
    if (!buildManifestJson(&manifestJson, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::InternalServerError),
                                  errorMessage);
    }

    QHttpServerResponse response(QByteArrayLiteral("application/json; charset=utf-8"), manifestJson);
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    return response;
}

QHttpServerResponse HttpSyncServer::handleFileRequest(const QHttpServerRequest &request)
{
    if (!isAuthorized(request)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::Unauthorized),
                                  tr("访问令牌无效。"));
    }

    QString relativePath;
    if (!tryParseRelativeFilePath(request.query().queryItemValue(QStringLiteral("path")), &relativePath)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::BadRequest),
                                  tr("文件路径参数无效。"));
    }

    const QString absolutePath = QDir(_rootPath).absoluteFilePath(relativePath);
    if (!isSameOrChildPath(_rootPath, absolutePath)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::Forbidden),
                                  tr("禁止访问目录外的文件。"));
    }

    const QFileInfo fileInfo(absolutePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::NotFound),
                                  tr("请求的文件不存在，path=%1").arg(relativePath));
    }

    if (fileInfo.isSymLink()) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::InternalServerError),
                                  tr("检测到不支持的符号链接，path=%1").arg(relativePath));
    }

    QHttpServerResponse response = QHttpServerResponse::fromFile(absolutePath);
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    response.setHeader(QByteArrayLiteral("X-Sync-Relative-Path"), relativePath.toUtf8());
    response.setHeader(QByteArrayLiteral("X-Sync-MTime-Ms"),
                       QByteArray::number(fileInfo.lastModified().toMSecsSinceEpoch()));
    return response;
}

QHttpServerResponse HttpSyncServer::buildErrorResponse(int statusCode, const QString &errorMessage) const
{
    QHttpServerResponse response(QByteArrayLiteral("application/json; charset=utf-8"),
                                 buildCompactErrorJson(errorMessage),
                                 toStatusCode(statusCode));
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    return response;
}

bool HttpSyncServer::isAuthorized(const QHttpServerRequest &request) const
{
    if (_accessToken.isEmpty()) {
        return true;
    }

    const QString authHeader = QString::fromUtf8(request.value(QByteArrayLiteral("Authorization"))).trimmed();
    if (authHeader.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)
        && authHeader.mid(7).trimmed() == _accessToken) {
        return true;
    }

    return request.query().queryItemValue(QStringLiteral("token")).trimmed() == _accessToken;
}

bool HttpSyncServer::buildManifestJson(QByteArray *manifestJson, QString *errorMessage) const
{
    if (manifestJson == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 清单输出参数不能为空。");
        }
        return false;
    }

    QDir rootDir(_rootPath);
    QVector<ManifestEntry> manifestEntries;
    QDirIterator iterator(_rootPath,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        if (fileInfo.isSymLink()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 服务根目录内存在不支持的符号链接，path=%1")
                                    .arg(fileInfo.absoluteFilePath());
            }
            return false;
        }

        const QString relativePath = normalizeRelativePath(rootDir, fileInfo.absoluteFilePath());
        if (fileInfo.isDir()) {
            manifestEntries.append({relativePath, true, 0, 0});
            continue;
        }

        if (fileInfo.isFile()) {
            manifestEntries.append(
                {relativePath, false, fileInfo.size(), fileInfo.lastModified().toMSecsSinceEpoch()});
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务根目录内存在不支持的文件类型，path=%1")
                                .arg(fileInfo.absoluteFilePath());
        }
        return false;
    }

    std::sort(manifestEntries.begin(), manifestEntries.end(), [](const ManifestEntry &left, const ManifestEntry &right) {
        return left.relativePath < right.relativePath;
    });

    QJsonArray entriesJson;
    for (const ManifestEntry &entry : manifestEntries) {
        QJsonObject entryObject{{QStringLiteral("path"), entry.relativePath},
                                {QStringLiteral("type"),
                                 entry.isDirectory ? QStringLiteral("dir") : QStringLiteral("file")}};
        if (!entry.isDirectory) {
            entryObject.insert(QStringLiteral("size"), static_cast<double>(entry.fileSize));
            entryObject.insert(QStringLiteral("mtimeMs"), static_cast<double>(entry.modifiedTimeMs));
        }
        entriesJson.append(entryObject);
    }

    const QJsonObject manifestObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("generatedAtMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("rootName"), QFileInfo(_rootPath).fileName()},
        {QStringLiteral("entryCount"), static_cast<int>(manifestEntries.size())},
        {QStringLiteral("entries"), entriesJson}};
    *manifestJson = QJsonDocument(manifestObject).toJson(QJsonDocument::Compact);
    return true;
}
