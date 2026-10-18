#include "core/webdav_backend.h"

#include "core/path_utils.h"
#include "core/network_utils.h"
#include "core/sync_types.h"

#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>

namespace NutSync {

namespace {

constexpr auto kUserAgent = "NutSync/" NUTSYNC_VERSION;

QByteArray basicAuthValue(const QString &username, const QString &password)
{
    const QByteArray credentials = (username + QLatin1Char(':') + password).toUtf8();
    return QByteArrayLiteral("Basic ") + credentials.toBase64();
}

int statusCode(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

bool isSuccessStatus(int status)
{
    return status >= 200 && status < 300;
}

QString statusText(int status)
{
    if (status == 0)
        return QStringLiteral("无 HTTP 响应");
    return QStringLiteral("HTTP %1").arg(status);
}

} // namespace

WebDavBackend::WebDavBackend(WebDavOptions options)
    : m_options(std::move(options))
    , m_network(m_options.baseUrl)
{
    m_baseUrl = m_options.baseUrl;
    if (m_baseUrl.isValid()) {
        if (m_options.username.isEmpty() && !m_baseUrl.userName().isEmpty()) {
            m_options.username = m_baseUrl.userName();
            m_options.password = m_baseUrl.password();
        }
        QString path = m_baseUrl.path();
        if (!path.endsWith(QLatin1Char('/')))
            path.append(QLatin1Char('/'));
        m_baseUrl.setPath(path);
        m_baseUrl.setFragment({});
        m_baseUrl.setUserInfo({});
        if (!m_options.username.isEmpty()
            && !NetworkUtils::allowsCleartextCredentials(m_baseUrl)) {
            qWarning() << "NutSync: refusing to send WebDAV credentials over cleartext HTTP";
        }
    }
}

QString WebDavBackend::typeName() const
{
    return QStringLiteral("WebDAV");
}

QString WebDavBackend::endpointDescription() const
{
    return m_baseUrl.toDisplayString(QUrl::RemovePassword);
}

QNetworkRequest WebDavBackend::requestFor(const QString &path) const
{
    QNetworkRequest request(PathUtils::remoteUrl(m_baseUrl, path));
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(m_options.timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    if (!m_options.username.isEmpty()
        && NetworkUtils::allowsCleartextCredentials(m_baseUrl)) {
        request.setRawHeader("Authorization",
                             basicAuthValue(m_options.username, m_options.password));
    }
    return request;
}

QByteArray WebDavBackend::propfindBody() const
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<d:propfind xmlns:d=\"DAV:\">\n"
        "  <d:prop>\n"
        "    <d:resourcetype/>\n"
        "    <d:getcontentlength/>\n"
        "    <d:getlastmodified/>\n"
        "    <d:getetag/>\n"
        "  </d:prop>\n"
        "</d:propfind>\n");
}

bool WebDavBackend::propfind(const QString &path, int depth, QByteArray *body, int *status,
                             QString *error)
{
    QNetworkRequest request = requestFor(path);
    request.setRawHeader("Depth", QByteArray::number(depth));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/xml; charset=utf-8"));

    QNetworkReply *reply = m_network.sendCustomRequest(request, "PROPFIND", propfindBody());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    *status = statusCode(reply);
    const QByteArray responseBody = reply->readAll();
    if (body)
        *body = responseBody;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorString = reply->errorString();
    reply->deleteLater();

    if (*status == 0) {
        if (error)
            *error = QObject::tr("连接 %1 失败：%2")
                         .arg(m_baseUrl.toDisplayString(QUrl::RemovePassword),
                              networkErrorString);
        return false;
    }
    if (!isSuccessStatus(*status) && *status != 207) {
        if (error)
            *error = httpError(*status, responseBody, statusText(*status));
        return false;
    }
    if (networkError != QNetworkReply::NoError && *status == 0) {
        if (error)
            *error = networkErrorString;
        return false;
    }
    return true;
}

QString WebDavBackend::httpError(int status, const QByteArray &body,
                                 const QString &fallback) const
{
    QString reason = fallback;
    if (status == 401) {
        reason = QObject::tr("认证失败（HTTP 401），请检查用户名和密码");
    } else if (status == 403) {
        reason = QObject::tr("没有访问权限（HTTP 403）");
    } else if (status == 404) {
        reason = QObject::tr("资源不存在（HTTP 404）");
    } else if (status == 409) {
        reason = QObject::tr("远程父目录不存在（HTTP 409）");
    } else if (status == 412) {
        reason = QObject::tr("远程内容已被其他客户端修改（HTTP 412），本次操作已取消");
    } else if (status == 423) {
        reason = QObject::tr("远程资源被锁定（HTTP 423）");
    } else if (status == 507) {
        reason = QObject::tr("远程存储空间不足（HTTP 507）");
    } else if (status >= 300 && status < 400) {
        reason = QObject::tr("服务器要求重定向到其他地址，请直接填写最终 URL");
    } else if (status == 400) {
        reason = QObject::tr("请求被远程服务器拒绝（HTTP 400）");
    } else if (status == 405) {
        reason = QObject::tr("远程服务器不支持此操作（HTTP 405）");
    } else if (status >= 500) {
        reason = QObject::tr("远程服务器错误（%1）").arg(fallback);
    }

    QString snippet = QString::fromUtf8(body.left(300)).simplified();
    if (!snippet.isEmpty() && !snippet.startsWith(QLatin1String("<")))
        reason += QStringLiteral("：") + snippet;
    return reason;
}

bool WebDavBackend::testConnection(QString *error)
{
    QByteArray body;
    int status = 0;
    QString localError;
    if (!propfind(QString(), 0, &body, &status, &localError)) {
        if (error)
            *error = localError;
        return false;
    }
    if (status != 207) {
        if (error)
            *error = QObject::tr("服务器未返回 WebDAV 响应（%1）").arg(statusText(status));
        return false;
    }
    QString parseError;
    parseMultiStatus(body, PathUtils::remoteUrl(m_baseUrl, {}), &parseError);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return false;
    }
    return true;
}

QString WebDavBackend::relativePathFromHref(const QString &href, const QUrl &requestUrl,
                                            bool *ok)
{
    if (ok)
        *ok = false;
    if (href.isEmpty())
        return {};

    QUrl hrefUrl(href);
    if (hrefUrl.isRelative()) {
        // A PROPFIND href may be relative to the collection URL. Ensure the
        // base has a trailing slash so it is treated as a directory.
        QUrl base = requestUrl;
        QString basePath = base.path();
        if (!basePath.endsWith(QLatin1Char('/'))) {
            basePath.append(QLatin1Char('/'));
            base.setPath(basePath);
        }
        hrefUrl = base.resolved(hrefUrl);
    }

    const QString hrefPath =
        QUrl::fromPercentEncoding(hrefUrl.path(QUrl::FullyEncoded).toUtf8());
    QString basePath =
        QUrl::fromPercentEncoding(requestUrl.path(QUrl::FullyEncoded).toUtf8());

    if (!basePath.endsWith(QLatin1Char('/')))
        basePath.append(QLatin1Char('/'));

    // PROPFIND with Depth 0 returns the requested resource itself. Depending
    // on the server the href may or may not carry a trailing slash.
    if (hrefPath == basePath || hrefPath == basePath.chopped(1)) {
        if (ok)
            *ok = true;
        return {};
    }
    if (!hrefPath.startsWith(basePath))
        return {};

    QString relative = hrefPath.mid(basePath.size());
    while (relative.endsWith(QLatin1Char('/')))
        relative.chop(1);
    relative = PathUtils::normalizeRelativePath(relative, ok);
    if (relative.isEmpty())
        return {};
    if (ok)
        *ok = true;
    return relative;
}

QList<RemoteEntry> WebDavBackend::parseMultiStatus(const QByteArray &xml,
                                                   const QUrl &requestUrl,
                                                   QString *error,
                                                   bool includeSelf,
                                                   const QString &selfPath,
                                                   const QString &pathPrefix)
{
    QList<RemoteEntry> entries;
    QXmlStreamReader reader(xml);

    QString currentHref;
    EntryType currentType = EntryType::File;
    qint64 currentSize = 0;
    qint64 currentMtime = 0;
    QString currentEtag;
    bool inResponse = false;
    bool inPropstat = false;
    int propstatStatus = 0;
    bool sawResponse = false;

    auto finishResponse = [&]() {
        if (currentHref.isEmpty())
            return;
        bool ok = false;
        const QString relative = relativePathFromHref(currentHref, requestUrl, &ok);
        if (ok && (!relative.isEmpty() || includeSelf)) {
            RemoteEntry entry;
            if (relative.isEmpty()) {
                entry.path = selfPath;
            } else if (pathPrefix.isEmpty()) {
                entry.path = relative;
            } else {
                entry.path = pathPrefix + QLatin1Char('/') + relative;
            }
            entry.type = currentType;
            entry.size = currentType == EntryType::File ? currentSize : 0;
            entry.mtimeMs = currentMtime;
            entry.etag = PathUtils::normalizeEtag(currentEtag);
            entries.append(entry);
        }
    };

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("response")) {
                inResponse = true;
                sawResponse = true;
                currentHref.clear();
                currentType = EntryType::File;
                currentSize = 0;
                currentMtime = 0;
                currentEtag.clear();
                propstatStatus = 0;
            } else if (!inResponse) {
                continue;
            } else if (name == QLatin1String("href")) {
                currentHref = reader.readElementText().trimmed();
            } else if (name == QLatin1String("propstat")) {
                inPropstat = true;
                propstatStatus = 0;
            } else if (name == QLatin1String("collection")) {
                currentType = EntryType::Directory;
            } else if (name == QLatin1String("getcontentlength")) {
                bool ok = false;
                const qint64 value = reader.readElementText().trimmed().toLongLong(&ok);
                if (ok)
                    currentSize = value;
            } else if (name == QLatin1String("getlastmodified")) {
                currentMtime = PathUtils::parseHttpDate(reader.readElementText())
                                   .toMSecsSinceEpoch();
            } else if (name == QLatin1String("getetag")) {
                currentEtag = reader.readElementText().trimmed();
            } else if (name == QLatin1String("status") && inPropstat) {
                const QString statusText = reader.readElementText().trimmed();
                const QStringList parts = statusText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (parts.size() >= 2)
                    propstatStatus = parts.at(1).toInt();
            }
        } else if (reader.isEndElement()) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("propstat")) {
                inPropstat = false;
                if (propstatStatus != 0 && propstatStatus != 200) {
                    currentSize = 0;
                    currentMtime = 0;
                    currentEtag.clear();
                }
            } else if (name == QLatin1String("response")) {
                finishResponse();
                inResponse = false;
            }
        }
    }

    if (reader.hasError()) {
        if (error)
            *error = QObject::tr("解析 WebDAV 响应失败：%1").arg(reader.errorString());
        return {};
    }
    if (!sawResponse) {
        if (error)
            *error = QObject::tr("WebDAV 服务器返回了空的 Multi-Status 响应");
    }
    return entries;
}

bool WebDavBackend::listDirectory(const QString &path, QList<RemoteEntry> *entries,
                                  QString *error)
{
    int status = 0;
    QByteArray body;
    QString localError;
    if (!propfind(path, 1, &body, &status, &localError)) {
        if (error)
            *error = localError;
        return false;
    }
    if (status != 207) {
        if (error)
            *error = QObject::tr("列目录失败：%1").arg(statusText(status));
        return false;
    }

    QString parseError;
    const QList<RemoteEntry> parsed = parseMultiStatus(
        body, PathUtils::remoteUrl(m_baseUrl, path), &parseError, false, {}, path);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return false;
    }
    if (entries)
        *entries = parsed;
    return true;
}

bool WebDavBackend::scan(QList<RemoteEntry> *entries, QString *error,
                         const CancelledCallback &cancelled)
{
    if (!entries) {
        if (error)
            *error = QObject::tr("内部错误：远程条目输出为空");
        return false;
    }
    entries->clear();
    if (!m_baseUrl.isValid()) {
        if (error)
            *error = QObject::tr("WebDAV 地址无效");
        return false;
    }
    QStringList directories {QString()};
    QSet<QString> seen;
    for (int index = 0; index < directories.size(); ++index) {
        if (cancelled && cancelled()) {
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        const QString directory = directories.at(index);
        QList<RemoteEntry> children;
        QString listError;
        if (!listDirectory(directory, &children, &listError)) {
            if (error) {
                *error = directory.isEmpty()
                    ? listError
                    : QObject::tr("列远程目录“%1”失败：%2").arg(directory, listError);
            }
            return false;
        }
        for (const RemoteEntry &entry : children) {
            if (entry.path.isEmpty() || seen.contains(entry.path))
                continue;
            seen.insert(entry.path);
            entries->append(entry);
            if (entry.type == EntryType::Directory)
                directories.append(entry.path);
        }
    }
    return true;
}

bool WebDavBackend::stat(const QString &path, RemoteEntry *entry, bool *exists,
                         QString *error)
{
    if (exists)
        *exists = false;
    int status = 0;
    QByteArray body;
    QString localError;
    if (!propfind(path, 0, &body, &status, &localError)) {
        if (status == 404)
            return true;
        if (error)
            *error = localError;
        return false;
    }
    if (status == 404)
        return true;
    if (status != 207) {
        if (error)
            *error = QObject::tr("读取远程元数据失败：%1").arg(statusText(status));
        return false;
    }
    QString parseError;
    const QList<RemoteEntry> parsed = parseMultiStatus(
        body, PathUtils::remoteUrl(m_baseUrl, path), &parseError, true, path, {});
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return false;
    }
    if (parsed.isEmpty())
        return true;
    if (entry)
        *entry = parsed.constFirst();
    if (exists)
        *exists = true;
    return true;
}

bool WebDavBackend::download(const QString &path, const QString &localFile,
                             DownloadResult *result, QString *error,
                             const CancelledCallback &cancelled)
{
    QNetworkRequest request = requestFor(path);
    QNetworkReply *reply = m_network.get(request);

    QSaveFile file(localFile);
    if (!file.open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QObject::tr("无法写入本地文件“%1”：%2")
                         .arg(localFile, file.errorString());
        return false;
    }

    qint64 written = 0;
    bool writeError = false;
    QString writeErrorString;
    QEventLoop loop;
    QTimer cancelTimer;
    cancelTimer.setInterval(200);

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        if (cancelled && cancelled()) {
            reply->abort();
            return;
        }
        const QByteArray data = reply->readAll();
        if (!data.isEmpty() && file.write(data) != data.size()) {
            writeError = true;
            writeErrorString = file.errorString();
            reply->abort();
            return;
        }
        written += data.size();
    });
    QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
        if (cancelled && cancelled())
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    cancelTimer.start();
    loop.exec();
    cancelTimer.stop();

    const QByteArray remaining = reply->readAll();
    if (!remaining.isEmpty()) {
        const qint64 remainingWritten = file.write(remaining);
        if (remainingWritten != remaining.size()) {
            writeError = true;
            writeErrorString = file.errorString();
        }
        written += remainingWritten;
    }

    const int status = statusCode(reply);
    const auto networkError = reply->error();
    const QString networkErrorString = reply->errorString();
    const QString etag = PathUtils::normalizeEtag(
        QString::fromLatin1(reply->rawHeader("ETag")));
    const qint64 contentLength =
        reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const QDateTime lastModified = PathUtils::parseHttpDate(
        QString::fromLatin1(reply->rawHeader("Last-Modified")));
    reply->deleteLater();

    if (writeError) {
        file.cancelWriting();
        if (error)
            *error = QObject::tr("写入本地文件失败：%1").arg(writeErrorString);
        return false;
    }
    if (status != 200) {
        file.cancelWriting();
        if (error) {
            *error = status == 0
                ? QObject::tr("下载失败：%1").arg(networkErrorString)
                : QObject::tr("下载失败：%1").arg(statusText(status));
        }
        return false;
    }
    if (networkError != QNetworkReply::NoError && networkError != QNetworkReply::OperationCanceledError) {
        file.cancelWriting();
        if (error)
            *error = QObject::tr("下载失败：%1").arg(networkErrorString);
        return false;
    }
    if (cancelled && cancelled()) {
        file.cancelWriting();
        if (error)
            *error = QObject::tr("同步已取消");
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("保存下载文件失败：%1").arg(file.errorString());
        return false;
    }

    if (lastModified.isValid()) {
        QFile local(localFile);
        if (local.open(QIODevice::ReadWrite))
            local.setFileTime(lastModified, QFileDevice::FileModificationTime);
    }

    if (result) {
        result->size = contentLength >= 0 ? contentLength : written;
        result->mtimeMs = lastModified.isValid() ? lastModified.toMSecsSinceEpoch() : 0;
        result->etag = etag;
    }
    return true;
}

bool WebDavBackend::upload(const QString &localFile, const QString &path,
                           const QString &expectedEtag, QString *error,
                           const CancelledCallback &cancelled)
{
    if (cancelled && cancelled()) {
        if (error)
            *error = QObject::tr("同步已取消");
        return false;
    }
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取本地文件“%1”：%2")
                         .arg(localFile, file.errorString());
        return false;
    }

    QNetworkRequest request = requestFor(path);
    request.setHeader(QNetworkRequest::ContentLengthHeader, file.size());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/octet-stream"));
    if (!expectedEtag.isEmpty()) {
        if (expectedEtag == QLatin1String("*"))
            request.setRawHeader("If-None-Match", "*");
        else
            request.setRawHeader("If-Match", expectedEtag.toUtf8());
    }

    QNetworkReply *reply = m_network.put(request, &file);
    QEventLoop loop;
    QTimer cancelTimer;
    cancelTimer.setInterval(200);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
        if (cancelled && cancelled())
            reply->abort();
    });
    cancelTimer.start();
    loop.exec();
    cancelTimer.stop();

    const int status = statusCode(reply);
    const QByteArray body = reply->readAll();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (status >= 200 && status < 300)
        return true;
    if (error) {
        *error = status == 0
            ? QObject::tr("上传失败：%1").arg(networkError)
            : QObject::tr("上传失败：%1").arg(httpError(status, body, statusText(status)));
    }
    return false;
}

bool WebDavBackend::makeDirectory(const QString &path, QString *error)
{
    QNetworkRequest request = requestFor(path);
    QNetworkReply *reply = m_network.sendCustomRequest(request, "MKCOL");
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int status = statusCode(reply);
    const QByteArray body = reply->readAll();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (isSuccessStatus(status) || status == 405)
        return true;
    if (status == 0) {
        if (error)
            *error = QObject::tr("创建远程目录失败：%1").arg(networkError);
        return false;
    }
    if (error) {
        *error = QObject::tr("创建远程目录“%1”失败：%2")
                     .arg(path.isEmpty() ? QStringLiteral("/") : path,
                          httpError(status, body, statusText(status)));
    }
    return false;
}

bool WebDavBackend::remove(const QString &path, bool recursive, const QString &expectedEtag,
                           QString *error, const CancelledCallback &cancelled)
{
    Q_UNUSED(recursive)
    if (cancelled && cancelled()) {
        if (error)
            *error = QObject::tr("同步已取消");
        return false;
    }
    QNetworkRequest request = requestFor(path);
    if (!expectedEtag.isEmpty() && expectedEtag != QLatin1String("*"))
        request.setRawHeader("If-Match", expectedEtag.toUtf8());

    QNetworkReply *reply = m_network.deleteResource(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int status = statusCode(reply);
    const QByteArray body = reply->readAll();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (status == 404 || isSuccessStatus(status))
        return true;
    if (status == 0) {
        if (error)
            *error = QObject::tr("删除远程路径失败：%1").arg(networkError);
        return false;
    }
    if (error) {
        *error = QObject::tr("删除远程路径“%1”失败：%2")
                     .arg(path.isEmpty() ? QStringLiteral("/") : path,
                          httpError(status, body, statusText(status)));
    }
    return false;
}

} // namespace NutSync
