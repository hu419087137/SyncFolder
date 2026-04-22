#include "HttpSyncServer.h"

#include <QtHttpServer/qhttpserver.h>
#include <QtHttpServer/qhttpserverrequest.h>
#include <QtHttpServer/qhttpserverresponder.h>
#include <QtHttpServer/qhttpserverresponse.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSet>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace
{
struct ManifestEntry
{
    QString relativePath;
    bool isDirectory = false;
    qint64 fileSize = 0;
    qint64 modifiedTimeMs = 0;
};

struct ByteRange
{
    bool hasRange = false;
    qint64 start = 0;
    qint64 end = 0;
};

class FileRangeDevice : public QIODevice
{
public:
    FileRangeDevice(const QString &filePath, qint64 offset, qint64 length, QObject *parent = nullptr)
        : QIODevice(parent),
          _file(filePath),
          _offset(offset),
          _length(length),
          _position(0)
    {
    }

    bool open(OpenMode mode) override
    {
        if ((mode & QIODevice::ReadOnly) == 0 || (mode & QIODevice::WriteOnly) != 0) {
            setErrorString(QObject::tr("文件分段设备只支持只读模式。"));
            return false;
        }

        if (!_file.open(QIODevice::ReadOnly)) {
            setErrorString(_file.errorString());
            return false;
        }

        if (!_file.seek(_offset)) {
            setErrorString(_file.errorString());
            _file.close();
            return false;
        }

        _position = 0;
        return QIODevice::open(mode);
    }

    void close() override
    {
        _file.close();
        QIODevice::close();
    }

    qint64 size() const override
    {
        return _length;
    }

    qint64 pos() const override
    {
        return _position;
    }

    bool seek(qint64 pos) override
    {
        if (pos < 0 || pos > _length) {
            return false;
        }

        if (!_file.seek(_offset + pos)) {
            setErrorString(_file.errorString());
            return false;
        }

        _position = pos;
        return true;
    }

    bool atEnd() const override
    {
        return _position >= _length;
    }

    bool isSequential() const override
    {
        return false;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (maxSize <= 0) {
            return 0;
        }

        const qint64 remainingBytes = _length - _position;
        if (remainingBytes <= 0) {
            return 0;
        }

        const qint64 bytesToRead = qMin(maxSize, remainingBytes);
        const qint64 readBytes = _file.read(data, bytesToRead);
        if (readBytes < 0) {
            setErrorString(_file.errorString());
            return -1;
        }

        _position += readBytes;
        return readBytes;
    }

    qint64 writeData(const char *data, qint64 maxSize) override
    {
        Q_UNUSED(data)
        Q_UNUSED(maxSize)
        setErrorString(QObject::tr("文件分段设备不支持写入。"));
        return -1;
    }

private:
    QFile _file;
    qint64 _offset = 0;
    qint64 _length = 0;
    qint64 _position = 0;
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
    const QString normalizedRootPath = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    const QString normalizedCandidatePath =
        QDir::fromNativeSeparators(QDir::cleanPath(candidatePath));
    const Qt::CaseSensitivity caseSensitivity = pathCaseSensitivity();

    if (normalizedRootPath.compare(normalizedCandidatePath, caseSensitivity) == 0) {
        return true;
    }

    const QString rootPrefix = normalizedRootPath + QLatin1Char('/');
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

QHttpServerResponder::StatusCode toResponderStatusCode(int statusCode)
{
    return static_cast<QHttpServerResponder::StatusCode>(statusCode);
}

void writeErrorResponse(QHttpServerResponder &&responder, int statusCode, const QString &errorMessage)
{
    responder.write(buildCompactErrorJson(errorMessage),
                    {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")},
                     {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")}},
                    toResponderStatusCode(statusCode));
}

bool parseByteRange(const QByteArray &rangeHeader, qint64 fileSize, ByteRange *byteRange)
{
    if (byteRange == nullptr) {
        return false;
    }

    *byteRange = ByteRange();
    const QByteArray trimmedHeader = rangeHeader.trimmed();
    if (trimmedHeader.isEmpty()) {
        return true;
    }

    if (!trimmedHeader.startsWith(QByteArrayLiteral("bytes="))) {
        return false;
    }

    const QByteArray rangeValue = trimmedHeader.mid(QByteArrayLiteral("bytes=").size()).trimmed();
    if (rangeValue.contains(',')) {
        return false;
    }

    const int dashIndex = rangeValue.indexOf('-');
    if (dashIndex < 0) {
        return false;
    }

    const QByteArray startText = rangeValue.left(dashIndex).trimmed();
    const QByteArray endText = rangeValue.mid(dashIndex + 1).trimmed();
    if (startText.isEmpty() && endText.isEmpty()) {
        return false;
    }

    bool ok = false;
    qint64 start = 0;
    qint64 end = fileSize - 1;
    if (startText.isEmpty()) {
        const qint64 suffixLength = endText.toLongLong(&ok);
        if (!ok || suffixLength <= 0) {
            return false;
        }

        start = qMax<qint64>(0, fileSize - suffixLength);
    } else {
        start = startText.toLongLong(&ok);
        if (!ok || start < 0) {
            return false;
        }

        if (!endText.isEmpty()) {
            end = endText.toLongLong(&ok);
            if (!ok || end < 0) {
                return false;
            }
        }
    }

    if (fileSize <= 0 || start >= fileSize || end < start) {
        return false;
    }

    byteRange->hasRange = true;
    byteRange->start = start;
    byteRange->end = qMin(end, fileSize - 1);
    return true;
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

bool HttpSyncServer::startServer(const QVector<SharedFolderConfig> &sharedFolderConfigs,
                                 quint16 listenPort,
                                 const QString &accessToken,
                                 QString *errorMessage)
{
    QVector<SharedFolderConfig> normalizedSharedFolderConfigs;
    normalizedSharedFolderConfigs.reserve(sharedFolderConfigs.size());
    QSet<QString> usedSourceIds;
    for (const SharedFolderConfig &sharedFolderConfig : sharedFolderConfigs) {
        SharedFolderConfig normalizedConfig;
        normalizedConfig.id = sharedFolderConfig.id.trimmed();
        normalizedConfig.name = sharedFolderConfig.name.trimmed();
        normalizedConfig.rootPath = normalizeLocalPath(sharedFolderConfig.rootPath);
        if (normalizedConfig.id.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 共享目录 ID 不能为空，name=%1").arg(normalizedConfig.name);
            }
            return false;
        }
        if (usedSourceIds.contains(normalizedConfig.id)) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 共享目录 ID 不能重复，sourceId=%1").arg(normalizedConfig.id);
            }
            return false;
        }
        usedSourceIds.insert(normalizedConfig.id);

        if (normalizedConfig.name.isEmpty()) {
            normalizedConfig.name = QFileInfo(normalizedConfig.rootPath).fileName();
        }
        if (normalizedConfig.name.isEmpty()) {
            normalizedConfig.name = normalizedConfig.id;
        }

        const QFileInfo rootInfo(normalizedConfig.rootPath);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 共享目录不存在或不是目录，source=%1，path=%2")
                                    .arg(normalizedConfig.name, normalizedConfig.rootPath);
            }
            return false;
        }

        normalizedSharedFolderConfigs.append(normalizedConfig);
    }

    if (normalizedSharedFolderConfigs.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务至少需要配置一个共享目录。");
        }
        return false;
    }

    stopServer();

    _sharedFolderConfigs = normalizedSharedFolderConfigs;
    _accessToken = accessToken.trimmed();
    _listenPort = listenPort;
    _httpServer = new QHttpServer(this);

    const bool sourcesRouteOk = _httpServer->route(
        QStringLiteral("/api/v1/sources"),
        [this](const QHttpServerRequest &request) { return handleSourcesRequest(request); });
    const bool manifestRouteOk = _httpServer->route(
        QStringLiteral("/api/v1/manifest"),
        [this](const QHttpServerRequest &request) { return handleManifestRequest(request); });
    const bool fileRouteOk = _httpServer->route(
        QStringLiteral("/api/v1/file"),
        [this](const QHttpServerRequest &request, QHttpServerResponder &&responder) {
            handleFileRequest(request, std::move(responder));
        });
    const bool pingRouteOk = _httpServer->route(QStringLiteral("/api/v1/ping"), [this]() {
        QJsonObject infoObject{
            {QStringLiteral("status"), QStringLiteral("ok")},
            {QStringLiteral("listenPort"), static_cast<int>(_listenPort)},
            {QStringLiteral("sourceCount"), static_cast<int>(_sharedFolderConfigs.size())},
            {QStringLiteral("hasToken"), !_accessToken.isEmpty()}};
        QHttpServerResponse response(infoObject);
        response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
        return response;
    });
    if (!sourcesRouteOk || !manifestRouteOk || !fileRouteOk || !pingRouteOk) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("注册 HTTP 接口失败，请检查 QtHttpServer 路由配置。");
        }
        delete _httpServer;
        _httpServer = nullptr;
        _sharedFolderConfigs.clear();
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
        _sharedFolderConfigs.clear();
        _accessToken.clear();
        _listenPort = 0;
        return false;
    }

    _listenPort = actualListenPort;
    QStringList sourceTexts;
    sourceTexts.reserve(_sharedFolderConfigs.size());
    for (const SharedFolderConfig &sharedFolderConfig : _sharedFolderConfigs) {
        sourceTexts.append(tr("%1(%2)")
                               .arg(sharedFolderConfig.name,
                                    QDir::toNativeSeparators(sharedFolderConfig.rootPath)));
    }
    emit sigLogMessage(tr("HTTP 同步服务已启动，sources=%1，port=%2，鉴权=%3，urls=%4")
                           .arg(sourceTexts.join(tr("； ")))
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
    _sharedFolderConfigs.clear();
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

QHttpServerResponse HttpSyncServer::handleSourcesRequest(const QHttpServerRequest &request)
{
    if (!isAuthorized(request)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::Unauthorized),
                                  tr("访问令牌无效。"));
    }

    QByteArray sourcesJson;
    QString errorMessage;
    if (!buildSourcesJson(&sourcesJson, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::InternalServerError),
                                  errorMessage);
    }

    QHttpServerResponse response(QByteArrayLiteral("application/json; charset=utf-8"), sourcesJson);
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    return response;
}

QHttpServerResponse HttpSyncServer::handleManifestRequest(const QHttpServerRequest &request)
{
    if (!isAuthorized(request)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::Unauthorized),
                                  tr("访问令牌无效。"));
    }

    const SharedFolderConfig *sharedFolderConfig = nullptr;
    QString errorMessage;
    if (!resolveSharedFolderFromRequest(request, &sharedFolderConfig, &errorMessage)) {
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::BadRequest),
                                  errorMessage);
    }

    QByteArray manifestJson;
    if (!buildManifestJson(*sharedFolderConfig, &manifestJson, &errorMessage)) {
        emit sigLogMessage(errorMessage);
        return buildErrorResponse(static_cast<int>(QHttpServerResponse::StatusCode::InternalServerError),
                                  errorMessage);
    }

    QHttpServerResponse response(QByteArrayLiteral("application/json; charset=utf-8"), manifestJson);
    response.setHeader(QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store"));
    return response;
}

void HttpSyncServer::handleFileRequest(const QHttpServerRequest &request, QHttpServerResponder &&responder)
{
    if (!isAuthorized(request)) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::Unauthorized),
                           tr("访问令牌无效。"));
        return;
    }

    const SharedFolderConfig *sharedFolderConfig = nullptr;
    QString sourceErrorMessage;
    if (!resolveSharedFolderFromRequest(request, &sharedFolderConfig, &sourceErrorMessage)) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::BadRequest),
                           sourceErrorMessage);
        return;
    }

    QString relativePath;
    if (!tryParseRelativeFilePath(request.query().queryItemValue(QStringLiteral("path")), &relativePath)) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::BadRequest),
                           tr("文件路径参数无效。"));
        return;
    }

    const QString absolutePath = QDir(sharedFolderConfig->rootPath).absoluteFilePath(relativePath);
    if (!isSameOrChildPath(sharedFolderConfig->rootPath, absolutePath)) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::Forbidden),
                           tr("禁止访问目录外的文件。"));
        return;
    }

    const QFileInfo fileInfo(absolutePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::NotFound),
                           tr("请求的文件不存在，path=%1").arg(relativePath));
        return;
    }

    if (fileInfo.isSymLink()) {
        writeErrorResponse(std::move(responder),
                           static_cast<int>(QHttpServerResponse::StatusCode::InternalServerError),
                           tr("检测到不支持的符号链接，path=%1").arg(relativePath));
        return;
    }

    const qint64 fileSize = fileInfo.size();
    const qint64 modifiedTimeMs = fileInfo.lastModified().toMSecsSinceEpoch();
    ByteRange byteRange;
    if (!parseByteRange(request.value(QByteArrayLiteral("Range")), fileSize, &byteRange)) {
        responder.write(buildCompactErrorJson(tr("请求的 Range 头无效或超出文件范围。")),
                        {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")},
                         {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")},
                         {QByteArrayLiteral("Accept-Ranges"), QByteArrayLiteral("bytes")},
                         {QByteArrayLiteral("Content-Range"),
                          QByteArrayLiteral("bytes */") + QByteArray::number(fileSize)}},
                        QHttpServerResponder::StatusCode::RequestRangeNotSatisfiable);
        return;
    }

    if (byteRange.hasRange) {
        FileRangeDevice *rangeDevice = new FileRangeDevice(absolutePath,
                                                           byteRange.start,
                                                           byteRange.end - byteRange.start + 1);
        const QByteArray contentRange = QByteArrayLiteral("bytes ")
            + QByteArray::number(byteRange.start)
            + QByteArrayLiteral("-")
            + QByteArray::number(byteRange.end)
            + QByteArrayLiteral("/")
            + QByteArray::number(fileSize);
        responder.write(rangeDevice,
                        {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/octet-stream")},
                         {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")},
                         {QByteArrayLiteral("Accept-Ranges"), QByteArrayLiteral("bytes")},
                         {QByteArrayLiteral("Content-Range"), contentRange},
                         {QByteArrayLiteral("X-Sync-Source-Id"), sharedFolderConfig->id.toUtf8()},
                         {QByteArrayLiteral("X-Sync-Relative-Path"), relativePath.toUtf8()},
                         {QByteArrayLiteral("X-Sync-MTime-Ms"), QByteArray::number(modifiedTimeMs)}},
                        QHttpServerResponder::StatusCode::PartialContent);
        return;
    }

    FileRangeDevice *fileDevice = new FileRangeDevice(absolutePath, 0, fileSize);
    responder.write(fileDevice,
                    {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/octet-stream")},
                     {QByteArrayLiteral("Cache-Control"), QByteArrayLiteral("no-store")},
                     {QByteArrayLiteral("Accept-Ranges"), QByteArrayLiteral("bytes")},
                     {QByteArrayLiteral("X-Sync-Source-Id"), sharedFolderConfig->id.toUtf8()},
                     {QByteArrayLiteral("X-Sync-Relative-Path"), relativePath.toUtf8()},
                     {QByteArrayLiteral("X-Sync-MTime-Ms"), QByteArray::number(modifiedTimeMs)}},
                    QHttpServerResponder::StatusCode::Ok);
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

bool HttpSyncServer::resolveSharedFolderFromRequest(const QHttpServerRequest &request,
                                                    const SharedFolderConfig **sharedFolderConfig,
                                                    QString *errorMessage) const
{
    if (sharedFolderConfig == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 共享目录输出参数不能为空。");
        }
        return false;
    }

    *sharedFolderConfig = nullptr;
    if (_sharedFolderConfigs.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务当前没有可同步目录。");
        }
        return false;
    }

    const QString sourceId = request.query().queryItemValue(QStringLiteral("sourceId")).trimmed();
    if (sourceId.isEmpty()) {
        if (_sharedFolderConfigs.size() == 1) {
            *sharedFolderConfig = &_sharedFolderConfigs.first();
            return true;
        }

        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 服务端配置了多个共享目录，请先指定 sourceId。");
        }
        return false;
    }

    for (const SharedFolderConfig &candidateConfig : _sharedFolderConfigs) {
        if (candidateConfig.id == sourceId) {
            *sharedFolderConfig = &candidateConfig;
            return true;
        }
    }

    if (errorMessage != nullptr) {
        *errorMessage = tr("HTTP 共享目录不存在，sourceId=%1").arg(sourceId);
    }
    return false;
}

bool HttpSyncServer::buildSourcesJson(QByteArray *sourcesJson, QString *errorMessage) const
{
    if (sourcesJson == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 目录列表输出参数不能为空。");
        }
        return false;
    }

    QJsonArray sourcesArray;
    for (const SharedFolderConfig &sharedFolderConfig : _sharedFolderConfigs) {
        sourcesArray.append(QJsonObject{{QStringLiteral("id"), sharedFolderConfig.id},
                                        {QStringLiteral("name"), sharedFolderConfig.name},
                                        {QStringLiteral("rootName"),
                                         QFileInfo(sharedFolderConfig.rootPath).fileName()}});
    }

    const QJsonObject sourcesObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("generatedAtMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("sourceCount"), static_cast<int>(sourcesArray.size())},
        {QStringLiteral("sources"), sourcesArray}};
    *sourcesJson = QJsonDocument(sourcesObject).toJson(QJsonDocument::Compact);
    return true;
}

bool HttpSyncServer::buildManifestJson(const SharedFolderConfig &sharedFolderConfig,
                                       QByteArray *manifestJson,
                                       QString *errorMessage) const
{
    if (manifestJson == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = tr("HTTP 清单输出参数不能为空。");
        }
        return false;
    }

    QDir rootDir(sharedFolderConfig.rootPath);
    QVector<ManifestEntry> manifestEntries;
    QDirIterator iterator(sharedFolderConfig.rootPath,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo fileInfo = iterator.fileInfo();
        if (fileInfo.isSymLink()) {
            if (errorMessage != nullptr) {
                *errorMessage = tr("HTTP 共享目录内存在不支持的符号链接，source=%1，path=%2")
                                    .arg(sharedFolderConfig.name, fileInfo.absoluteFilePath());
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
            *errorMessage = tr("HTTP 共享目录内存在不支持的文件类型，source=%1，path=%2")
                                .arg(sharedFolderConfig.name, fileInfo.absoluteFilePath());
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
        {QStringLiteral("sourceId"), sharedFolderConfig.id},
        {QStringLiteral("sourceName"), sharedFolderConfig.name},
        {QStringLiteral("rootName"), QFileInfo(sharedFolderConfig.rootPath).fileName()},
        {QStringLiteral("entryCount"), static_cast<int>(manifestEntries.size())},
        {QStringLiteral("entries"), entriesJson}};
    *manifestJson = QJsonDocument(manifestObject).toJson(QJsonDocument::Compact);
    return true;
}
