#include "core/webdav_backend.h"

#include "core/path_utils.h"
#include "core/retry_policy.h"
#include "core/app_paths.h"
#include "core/network_utils.h"
#include "core/sync_types.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileInfo>
#include <QNetworkReply>
#include <QThreadPool>
#include <QtConcurrent>
#include <QAuthenticator>
#include <QBuffer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSaveFile>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>

namespace NutSync {

namespace {

constexpr auto kUserAgent = "NutSync/" NUTSYNC_VERSION;

class ByteThrottle
{
public:
    explicit ByteThrottle(qint64 bytesPerSecond)
        : m_limit(bytesPerSecond)
    {
        if (m_limit > 0)
            m_timer.start();
    }

    void add(qint64 bytes)
    {
        if (m_limit <= 0 || bytes <= 0)
            return;
        m_bytes += bytes;
        const qint64 targetMs = m_bytes * 1000 / m_limit;
        const qint64 elapsedMs = m_timer.elapsed();
        if (targetMs > elapsedMs)
            QThread::msleep(static_cast<unsigned long>(targetMs - elapsedMs));
    }

private:
    qint64 m_limit = 0;
    qint64 m_bytes = 0;
    QElapsedTimer m_timer;
};

class RateLimitedFile final : public QIODevice
{
public:
    RateLimitedFile(QString path, qint64 bytesPerSecond)
        : m_path(std::move(path))
        , m_limit(bytesPerSecond)
    {
    }

    bool open(OpenMode mode) override
    {
        if (!m_file.open(mode))
            return false;
        m_read = 0;
        if (m_limit > 0)
            m_timer.start();
        return QIODevice::open(mode);
    }

    void close() override
    {
        m_file.close();
        QIODevice::close();
    }

    bool isSequential() const override { return false; }
    qint64 size() const override { return m_file.size(); }
    qint64 pos() const override { return m_file.pos(); }
    bool seek(qint64 position) override
    {
        m_read = 0;
        if (m_limit > 0)
            m_timer.restart();
        return m_file.seek(position);
    }
    QString errorString() const { return m_file.errorString(); }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 chunk = m_limit > 0 ? qMax<qint64>(4096, m_limit / 10) : maxSize;
        const qint64 read = m_file.read(data, qMin(maxSize, chunk));
        if (read > 0 && m_limit > 0) {
            m_read += read;
            const qint64 targetMs = m_read * 1000 / m_limit;
            const qint64 elapsedMs = m_timer.elapsed();
            if (targetMs > elapsedMs)
                QThread::msleep(static_cast<unsigned long>(targetMs - elapsedMs));
        }
        return read;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QString m_path;
    QFile m_file{m_path};
    qint64 m_limit = 0;
    qint64 m_read = 0;
    QElapsedTimer m_timer;
};

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

RemoteErrorClass errorClassForStatus(int status)
{
    if (status == 0)
        return RemoteErrorClass::Network;
    if (status == 401)
        return RemoteErrorClass::Authentication;
    if (status == 403)
        return RemoteErrorClass::Permission;
    if (status == 404)
        return RemoteErrorClass::NotFound;
    if (status == 408)
        return RemoteErrorClass::Timeout;
    if (status == 409 || status == 412 || status == 423)
        return RemoteErrorClass::Conflict;
    if (status == 429)
        return RemoteErrorClass::RateLimited;
    if (status == 507)
        return RemoteErrorClass::Quota;
    if (status >= 300 && status < 400)
        return RemoteErrorClass::Redirect;
    if (status == 400 || status == 405)
        return RemoteErrorClass::Protocol;
    if (status >= 500 && status < 600)
        return RemoteErrorClass::Server;
    return RemoteErrorClass::Other;
}

RemoteErrorClass classifyReply(int status, QNetworkReply::NetworkError networkError)
{
    if (status == 0)
        return RemoteErrorClass::Network;
    if (status >= 200 && status < 300 && networkError != QNetworkReply::NoError)
        return RemoteErrorClass::Network;
    return errorClassForStatus(status);
}

QString downloadPartPath(const QString &localFile)
{
    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(localFile).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256);
    QDir().mkpath(AppPaths::cacheDir());
    return AppPaths::cacheDir() + QStringLiteral("/download-") + QString::fromLatin1(hash.toHex())
        + QStringLiteral(".part");
}

QString downloadMetaPath(const QString &partPath)
{
    return partPath + QStringLiteral(".json");
}

struct DownloadResumeInfo
{
    QString etag;
    QDateTime lastModified;

    bool hasRemoteVersion() const { return !etag.isEmpty() || lastModified.isValid(); }
};

DownloadResumeInfo readDownloadMeta(const QString &partPath)
{
    DownloadResumeInfo info;
    QFile meta(downloadMetaPath(partPath));
    if (!meta.open(QIODevice::ReadOnly))
        return info;
    const QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
    info.etag = object.value(QStringLiteral("etag")).toString();
    info.lastModified =
        QDateTime::fromString(object.value(QStringLiteral("lastModified")).toString(), Qt::ISODate);
    return info;
}

void writeDownloadMeta(const QString &partPath, const QString &etag, const QDateTime &lastModified)
{
    if (etag.isEmpty() && !lastModified.isValid())
        return;
    QJsonObject object;
    object.insert(QStringLiteral("etag"), etag);
    object.insert(QStringLiteral("lastModified"),
                  lastModified.isValid() ? lastModified.toString(Qt::ISODate) : QString());
    QSaveFile meta(downloadMetaPath(partPath));
    if (!meta.open(QIODevice::WriteOnly))
        return;
    meta.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    meta.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    meta.commit();
}

void removeDownloadState(const QString &partPath)
{
    QFile::remove(downloadMetaPath(partPath));
    QFile::remove(partPath);
}

bool retryableFailure(int status, QNetworkReply::NetworkError networkError)
{
    RemoteErrorClass errorClass = errorClassForStatus(status);
    if (networkError != QNetworkReply::NoError
        && (status == 0 || (status >= 200 && status < 300))) {
        errorClass = RemoteErrorClass::Network;
    }
    return RetryPolicy::isRetryable(errorClass);
}

int retryAfterDelayMs(QNetworkReply *reply)
{
    const QByteArray value = reply->rawHeader("Retry-After").trimmed();
    if (value.isEmpty())
        return -1;

    bool ok = false;
    const int seconds = value.toInt(&ok);
    if (ok)
        return qBound(0, seconds, 300) * 1000;

    const QDateTime date = QDateTime::fromString(QString::fromLatin1(value), Qt::RFC2822Date);
    if (date.isValid()) {
        const qint64 delay = QDateTime::currentDateTimeUtc().msecsTo(date.toUTC());
        return static_cast<int>(qBound<qint64>(0LL, delay, 300000LL));
    }
    return -1;
}

bool waitBeforeRetry(int attempt, int retryAfterMs, int baseDelayMs, int maxDelayMs,
                     const CancelledCallback &cancelled)
{
    if (cancelled && cancelled())
        return false;

    int delay = retryAfterMs;
    if (delay < 0)
        delay = qMin(maxDelayMs, baseDelayMs * (1 << qMin(attempt, 10)));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < delay) {
        if (cancelled && cancelled())
            return false;
        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(100, delay - timer.elapsed())));
    }
    return !(cancelled && cancelled());
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
        if (!m_options.username.isEmpty() && !NetworkUtils::allowsCleartextCredentials(m_baseUrl)) {
            qWarning() << "NutSync: refusing to send WebDAV credentials over cleartext HTTP";
        }
    }

    if (!m_options.clientCertificatePath.isEmpty()) {
        QList<QSslCertificate> certificates =
            QSslCertificate::fromPath(m_options.clientCertificatePath, QSsl::Pem);
        if (certificates.isEmpty()) {
            certificates = QSslCertificate::fromPath(m_options.clientCertificatePath, QSsl::Der);
        }
        QFile keyFile(m_options.clientKeyPath);
        QSslKey key;
        if (keyFile.open(QIODevice::ReadOnly)) {
            key = QSslKey(&keyFile, QSsl::Rsa);
            if (key.isNull()) {
                keyFile.seek(0);
                key = QSslKey(&keyFile, QSsl::Ec);
            }
        }
        if (certificates.isEmpty() || key.isNull()) {
            qWarning() << "NutSync: unable to load client certificate or private key";
        } else {
            m_sslConfiguration = QSslConfiguration::defaultConfiguration();
            m_sslConfiguration.setLocalCertificateChain(certificates);
            m_sslConfiguration.setPrivateKey(key);
            m_hasClientCertificate = true;
        }
    }

    if (m_options.authMode == QLatin1String("digest")) {
        QObject::connect(
            &m_network, &QNetworkAccessManager::authenticationRequired,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QUrl target = reply ? reply->url() : QUrl();
                const bool sameOrigin = target.scheme() == m_baseUrl.scheme()
                    && target.host() == m_baseUrl.host()
                    && target.port(target.scheme() == QLatin1String("https") ? 443 : 80)
                        == m_baseUrl.port(m_baseUrl.scheme() == QLatin1String("https") ? 443 : 80);
                if (!sameOrigin)
                    return;
                authenticator->setUser(m_options.username);
                authenticator->setPassword(m_options.password);
            });
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

RemoteErrorClass WebDavBackend::lastErrorClass() const
{
    return m_lastErrorClass;
}

QNetworkRequest WebDavBackend::requestFor(const QString &path) const
{
    return requestForAbsolute(PathUtils::remoteUrl(m_baseUrl, path));
}

QNetworkRequest WebDavBackend::requestForAbsolute(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(m_options.timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    if (m_hasClientCertificate)
        request.setSslConfiguration(m_sslConfiguration);
    if (NetworkUtils::allowsCleartextCredentials(m_baseUrl)) {
        if (m_options.authMode == QLatin1String("bearer")) {
            if (!m_options.password.isEmpty()) {
                request.setRawHeader("Authorization",
                                     QByteArrayLiteral("Bearer ") + m_options.password.toUtf8());
            }
        } else if (m_options.authMode == QLatin1String("basic") && !m_options.username.isEmpty()) {
            request.setRawHeader("Authorization",
                                 basicAuthValue(m_options.username, m_options.password));
        }
    }
    return request;
}

QByteArray WebDavBackend::propfindBody() const
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
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
                             QString *error, const CancelledCallback &cancelled)
{
    m_lastErrorClass = RemoteErrorClass::None;
    for (int attempt = 0;; ++attempt) {
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
        const int retryAfter = retryAfterDelayMs(reply);
        const QNetworkReply::NetworkError networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        reply->deleteLater();

        if (*status == 207 || (*status != 0 && isSuccessStatus(*status))) {
            if (body)
                *body = responseBody;
            return true;
        }

        const bool retry =
            retryableFailure(*status, networkError) && attempt < m_options.retryCount;
        if (retry
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, cancelled)) {
            continue;
        }

        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        m_lastErrorClass = classifyReply(*status, networkError);
        if (body)
            *body = responseBody;
        if (error) {
            if (*status == 0) {
                *error =
                    QObject::tr("连接 %1 失败：%2")
                        .arg(m_baseUrl.toDisplayString(QUrl::RemovePassword), networkErrorString);
            } else {
                *error = httpError(*status, responseBody, statusText(*status));
            }
        }
        Q_UNUSED(networkError)
        return false;
    }
}

QString WebDavBackend::httpError(int status, const QByteArray &body, const QString &fallback) const
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

    const QString snippet = QString::fromUtf8(body.left(500)).simplified();
    if (!snippet.isEmpty())
        qDebug().noquote() << "[webdav]" << statusText(status) << snippet;
    return reason;
}

bool WebDavBackend::testConnection(QString *error)
{
    m_lastErrorClass = RemoteErrorClass::None;
    QByteArray body;
    int status = 0;
    QString localError;
    if (!propfind(QString(), 0, &body, &status, &localError)) {
        if (error)
            *error = localError;
        return false;
    }
    if (status != 207) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = QObject::tr("服务器未返回 WebDAV 响应（%1）").arg(statusText(status));
        return false;
    }
    QString parseError;
    parseMultiStatus(body, PathUtils::remoteUrl(m_baseUrl, {}), &parseError);
    if (!parseError.isEmpty()) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = parseError;
        return false;
    }
    return true;
}

QString WebDavBackend::relativePathFromHref(const QString &href, const QUrl &requestUrl, bool *ok)
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

    const QString hrefPath = QUrl::fromPercentEncoding(hrefUrl.path(QUrl::FullyEncoded).toUtf8());
    QString basePath = QUrl::fromPercentEncoding(requestUrl.path(QUrl::FullyEncoded).toUtf8());

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

QList<RemoteEntry> WebDavBackend::parseMultiStatus(const QByteArray &xml, const QUrl &requestUrl,
                                                   QString *error, bool includeSelf,
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
                currentMtime =
                    PathUtils::parseHttpDate(reader.readElementText()).toMSecsSinceEpoch();
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

bool WebDavBackend::listDirectory(const QString &path, QList<RemoteEntry> *entries, QString *error,
                                  const CancelledCallback &cancelled)
{
    int status = 0;
    QByteArray body;
    QString localError;
    if (!propfind(path, 1, &body, &status, &localError, cancelled)) {
        if (error)
            *error = localError;
        return false;
    }
    if (status != 207) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = QObject::tr("列目录失败：%1").arg(statusText(status));
        return false;
    }

    QString parseError;
    const QList<RemoteEntry> parsed =
        parseMultiStatus(body, PathUtils::remoteUrl(m_baseUrl, path), &parseError, false, {}, path);
    if (!parseError.isEmpty()) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
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
    m_lastErrorClass = RemoteErrorClass::None;
    if (!entries) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = QObject::tr("内部错误：远程条目输出为空");
        return false;
    }
    entries->clear();
    if (!m_baseUrl.isValid()) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = QObject::tr("WebDAV 地址无效");
        return false;
    }

    const auto inScope = [this](const QString &path) {
        if (m_options.includePaths.isEmpty())
            return true;
        for (const QString &include : m_options.includePaths) {
            if (PathUtils::isSameOrChild(path, include)
                || PathUtils::isSameOrChild(include, path)) {
                return true;
            }
        }
        return false;
    };

    struct DirectoryScanResult
    {
        QString path;
        QList<RemoteEntry> entries;
        bool success = false;
        QString error;
        RemoteErrorClass errorClass = RemoteErrorClass::None;
    };

    QStringList level{QString()};
    QSet<QString> seen;
    QThreadPool pool;
    pool.setMaxThreadCount(qBound(1, m_options.scanConcurrency, 8));
    while (!level.isEmpty()) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        level.sort();
        QFuture<DirectoryScanResult> future =
            QtConcurrent::mapped(&pool, level, [this, cancelled](const QString &directory) {
                DirectoryScanResult result;
                result.path = directory;
                WebDavBackend worker(m_options);
                result.success =
                    worker.listDirectory(directory, &result.entries, &result.error, cancelled);
                result.errorClass = worker.lastErrorClass();
                return result;
            });
        future.waitForFinished();
        const QList<DirectoryScanResult> results = future.results();
        QStringList nextLevel;
        for (const DirectoryScanResult &result : results) {
            if (!result.success) {
                m_lastErrorClass = result.errorClass;
                if (error) {
                    *error = result.path.isEmpty()
                        ? result.error
                        : QObject::tr("列远程目录“%1”失败：%2").arg(result.path, result.error);
                }
                return false;
            }
            for (const RemoteEntry &entry : result.entries) {
                if (entry.path.isEmpty() || seen.contains(entry.path) || !inScope(entry.path))
                    continue;
                seen.insert(entry.path);
                entries->append(entry);
                if (entry.type == EntryType::Directory)
                    nextLevel.append(entry.path);
            }
        }
        nextLevel.removeDuplicates();
        level = nextLevel;
    }
    return true;
}

bool WebDavBackend::stat(const QString &path, RemoteEntry *entry, bool *exists, QString *error)
{
    m_lastErrorClass = RemoteErrorClass::None;
    if (exists)
        *exists = false;
    int status = 0;
    QByteArray body;
    QString localError;
    if (!propfind(path, 0, &body, &status, &localError)) {
        if (status == 404) {
            m_lastErrorClass = RemoteErrorClass::None;
            return true;
        }
        if (error)
            *error = localError;
        return false;
    }
    if (status == 404) {
        m_lastErrorClass = RemoteErrorClass::None;
        return true;
    }
    if (status != 207) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
        if (error)
            *error = QObject::tr("读取远程元数据失败：%1").arg(statusText(status));
        return false;
    }
    QString parseError;
    const QList<RemoteEntry> parsed =
        parseMultiStatus(body, PathUtils::remoteUrl(m_baseUrl, path), &parseError, true, path, {});
    if (!parseError.isEmpty()) {
        m_lastErrorClass = RemoteErrorClass::Protocol;
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

bool WebDavBackend::download(const QString &path, const QString &localFile, DownloadResult *result,
                             QString *error, const CancelledCallback &cancelled)
{
    m_lastErrorClass = RemoteErrorClass::None;
    const QString partPath = downloadPartPath(localFile);
    QFile part(partPath);
    if (!part.open(QIODevice::ReadWrite)) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("无法创建下载临时文件：%1").arg(part.errorString());
        return false;
    }
    part.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    DownloadResumeInfo resume = readDownloadMeta(partPath);
    if (part.size() > 0 && !resume.hasRemoteVersion()) {
        if (!part.resize(0)) {
            m_lastErrorClass = RemoteErrorClass::LocalIo;
            if (error)
                *error = QObject::tr("无法重置下载临时文件：%1").arg(part.errorString());
            return false;
        }
    }

    QString etag = resume.etag;
    QDateTime lastModified = resume.lastModified;
    for (int attempt = 0;; ++attempt) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            part.flush();
            writeDownloadMeta(partPath, etag, lastModified);
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        part.flush();
        qint64 offset = part.size();
        if (!part.seek(offset)) {
            m_lastErrorClass = RemoteErrorClass::LocalIo;
            if (error)
                *error = QObject::tr("无法写入下载临时文件：%1").arg(part.errorString());
            return false;
        }

        QNetworkRequest request = requestFor(path);
        if (offset > 0) {
            request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
            const QString ifRange = !etag.isEmpty()
                ? etag
                : (lastModified.isValid() ? lastModified.toUTC().toString(Qt::RFC2822Date)
                                          : QString());
            if (!ifRange.isEmpty())
                request.setRawHeader("If-Range", ifRange.toUtf8());
        }
        QNetworkReply *reply = m_network.get(request);
        bool writeError = false;
        QString writeErrorString;

        auto consume = [&](const QByteArray &data) {
            if (data.isEmpty() || writeError)
                return;
            if (statusCode(reply) == 200 && offset > 0) {
                if (!part.resize(0) || !part.seek(0)) {
                    writeError = true;
                    writeErrorString = part.errorString();
                    reply->abort();
                    return;
                }
                offset = 0;
            }
            if (part.write(data) != data.size()) {
                writeError = true;
                writeErrorString = part.errorString();
                reply->abort();
            }
        };

        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [this, offset](qint64 received, qint64 total) {
                             reportTransferProgress(offset + received, total);
                         });
        QEventLoop loop;
        QTimer cancelTimer;
        cancelTimer.setInterval(200);
        ByteThrottle downloadThrottle(m_options.transferLimitBytesPerSecond);
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
            if (cancelled && cancelled()) {
                reply->abort();
                return;
            }
            const QByteArray data = reply->readAll();
            downloadThrottle.add(data.size());
            consume(data);
        });
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
            if (cancelled && cancelled())
                reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        cancelTimer.start();
        loop.exec();
        cancelTimer.stop();
        consume(reply->readAll());

        const int status = statusCode(reply);
        const auto networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        const int retryAfter = retryAfterDelayMs(reply);
        const QString responseEtag =
            PathUtils::normalizeEtag(QString::fromLatin1(reply->rawHeader("ETag")));
        const QDateTime responseLastModified =
            PathUtils::parseHttpDate(QString::fromLatin1(reply->rawHeader("Last-Modified")));
        reply->deleteLater();
        if (!responseEtag.isEmpty())
            etag = responseEtag;
        if (responseLastModified.isValid())
            lastModified = responseLastModified;

        if (writeError) {
            m_lastErrorClass = RemoteErrorClass::LocalIo;
            part.flush();
            writeDownloadMeta(partPath, etag, lastModified);
            if (error)
                *error = QObject::tr("写入本地文件失败：%1").arg(writeErrorString);
            return false;
        }
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            part.flush();
            writeDownloadMeta(partPath, etag, lastModified);
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        if ((status == 200 || status == 206) && networkError == QNetworkReply::NoError) {
            part.flush();
            writeDownloadMeta(partPath, etag, lastModified);
            break;
        }
        if (retryableFailure(status, networkError) && attempt < m_options.retryCount
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, cancelled)) {
            continue;
        }
        m_lastErrorClass = classifyReply(status, networkError);
        part.flush();
        writeDownloadMeta(partPath, etag, lastModified);
        if (error) {
            *error = status == 0
                ? QObject::tr("下载失败：%1").arg(networkErrorString)
                : QObject::tr("下载失败：%1").arg(httpError(status, {}, statusText(status)));
        }
        return false;
    }

    part.flush();
    if (!part.seek(0)) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("无法读取下载临时文件：%1").arg(part.errorString());
        return false;
    }
    QSaveFile output(localFile);
    if (!output.open(QIODevice::WriteOnly)) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("无法写入本地文件“%1”：%2").arg(localFile, output.errorString());
        return false;
    }
    while (!part.atEnd()) {
        const QByteArray chunk = part.read(1024 * 1024);
        if (chunk.isEmpty())
            break;
        if (output.write(chunk) != chunk.size()) {
            m_lastErrorClass = RemoteErrorClass::LocalIo;
            if (error)
                *error = QObject::tr("写入本地文件失败：%1").arg(output.errorString());
            return false;
        }
    }
    if (!output.commit()) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("保存下载文件失败：%1").arg(output.errorString());
        return false;
    }

    const qint64 resultSize = part.size();
    if (lastModified.isValid()) {
        QFile local(localFile);
        if (local.open(QIODevice::ReadWrite))
            local.setFileTime(lastModified, QFileDevice::FileModificationTime);
    }
    removeDownloadState(partPath);
    if (result) {
        result->size = resultSize;
        result->mtimeMs = lastModified.isValid() ? lastModified.toMSecsSinceEpoch() : 0;
        result->etag = etag;
    }
    return true;
}

bool WebDavBackend::uploadChunked(const QString &localFile, const QString &path, QString *error,
                                  const CancelledCallback &cancelled)
{
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("无法读取本地文件“%1”：%2").arg(localFile, file.errorString());
        return false;
    }
    if (file.size() == 0)
        return false;

    const QString basePath = m_baseUrl.path();
    const int filesPosition = basePath.indexOf(QLatin1String("/files/"));
    const QString user = basePath.mid(filesPosition + 7).section(QLatin1Char('/'), 0, 0);
    if (filesPosition < 0 || user.isEmpty()) {
        if (error)
            *error = QObject::tr("无法从 WebDAV 地址推导 Nextcloud 上传目录");
        return false;
    }
    const QString uploadId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString uploadsPath = basePath.left(filesPosition) + QStringLiteral("/uploads/") + user
        + QLatin1Char('/') + uploadId + QLatin1Char('/');
    QUrl uploadsUrl = m_baseUrl;
    uploadsUrl.setPath(uploadsPath);

    auto failure = [&](const QString &message) {
        if (error)
            *error = message;
        QNetworkRequest cleanup = requestForAbsolute(uploadsUrl);
        QNetworkReply *reply = m_network.deleteResource(cleanup);
        QEventLoop cleanupLoop;
        QObject::connect(reply, &QNetworkReply::finished, &cleanupLoop, &QEventLoop::quit);
        cleanupLoop.exec();
        reply->deleteLater();
        return false;
    };

    QNetworkRequest collectionRequest = requestForAbsolute(uploadsUrl);
    collectionRequest.setRawHeader("OC-Total-Length", QByteArray::number(file.size()));
    QNetworkReply *collectionReply = m_network.sendCustomRequest(collectionRequest, "MKCOL");
    QEventLoop collectionLoop;
    QObject::connect(collectionReply, &QNetworkReply::finished, &collectionLoop, &QEventLoop::quit);
    collectionLoop.exec();
    const int collectionStatus = statusCode(collectionReply);
    const QString collectionError = collectionReply->errorString();
    collectionReply->deleteLater();
    if (!isSuccessStatus(collectionStatus) && collectionStatus != 405) {
        m_lastErrorClass = classifyReply(collectionStatus, QNetworkReply::NoError);
        if (error)
            *error = QObject::tr("创建 Nextcloud 上传目录失败：%1").arg(collectionError);
        return false;
    }

    file.seek(0);
    const qint64 chunkSize = qBound(1, m_options.chunkSizeMb, 100) * 1024LL * 1024LL;
    int chunkIndex = 0;
    qint64 sent = 0;
    while (!file.atEnd()) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        const QByteArray data = file.read(chunkSize);
        if (data.isEmpty())
            break;
        QUrl chunkUrl = uploadsUrl;
        chunkUrl.setPath(uploadsPath
                         + QStringLiteral("%1").arg(chunkIndex, 8, 10, QLatin1Char('0')));
        bool uploaded = false;
        QString chunkError;
        for (int attempt = 0;; ++attempt) {
            QNetworkRequest request = requestForAbsolute(chunkUrl);
            request.setHeader(QNetworkRequest::ContentLengthHeader, data.size());
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/octet-stream"));
            QBuffer buffer;
            buffer.setData(data);
            buffer.open(QIODevice::ReadOnly);
            QNetworkReply *reply = m_network.put(request, &buffer);
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
            const auto networkError = reply->error();
            const int retryAfter = retryAfterDelayMs(reply);
            chunkError = reply->errorString();
            reply->deleteLater();
            if (isSuccessStatus(status) && networkError == QNetworkReply::NoError) {
                uploaded = true;
                break;
            }
            if (retryableFailure(status, networkError) && attempt < m_options.retryCount
                && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                                   m_options.retryMaxDelayMs, cancelled)) {
                continue;
            }
            m_lastErrorClass = classifyReply(status, networkError);
            chunkError = status == 0 ? chunkError : httpError(status, body, statusText(status));
            break;
        }
        if (!uploaded)
            return failure(QObject::tr("上传分块失败：%1").arg(chunkError));
        sent += data.size();
        reportTransferProgress(sent, file.size());
        ++chunkIndex;
    }

    QUrl moveSource = uploadsUrl;
    moveSource.setPath(uploadsPath + QStringLiteral(".file"));
    QNetworkRequest moveRequest = requestForAbsolute(moveSource);
    moveRequest.setRawHeader("Destination", PathUtils::remoteUrl(m_baseUrl, path).toEncoded());
    moveRequest.setRawHeader("Overwrite", "T");
    moveRequest.setRawHeader("OC-Total-Length", QByteArray::number(file.size()));
    QNetworkReply *moveReply = m_network.sendCustomRequest(moveRequest, "MOVE");
    QEventLoop moveLoop;
    QObject::connect(moveReply, &QNetworkReply::finished, &moveLoop, &QEventLoop::quit);
    moveLoop.exec();
    const int moveStatus = statusCode(moveReply);
    const QByteArray moveBody = moveReply->readAll();
    const QString moveError = moveReply->errorString();
    moveReply->deleteLater();
    if (!isSuccessStatus(moveStatus)) {
        m_lastErrorClass = classifyReply(moveStatus, QNetworkReply::NoError);
        return failure(QObject::tr("完成分块上传失败：%1")
                           .arg(moveStatus == 0
                                    ? moveError
                                    : httpError(moveStatus, moveBody, statusText(moveStatus))));
    }
    reportTransferProgress(file.size(), file.size());
    return true;
}

bool WebDavBackend::upload(const QString &localFile, const QString &path,
                           const QString &expectedEtag, QString *error,
                           const CancelledCallback &cancelled)
{
    m_lastErrorClass = RemoteErrorClass::None;
    if (cancelled && cancelled()) {
        m_lastErrorClass = RemoteErrorClass::Cancelled;
        if (error)
            *error = QObject::tr("同步已取消");
        return false;
    }
    RateLimitedFile file(localFile, m_options.transferLimitBytesPerSecond);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastErrorClass = RemoteErrorClass::LocalIo;
        if (error)
            *error = QObject::tr("无法读取本地文件“%1”：%2").arg(localFile, file.errorString());
        return false;
    }

    for (int attempt = 0;; ++attempt) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        if (!file.seek(0)) {
            m_lastErrorClass = RemoteErrorClass::LocalIo;
            if (error)
                *error = QObject::tr("无法读取本地文件“%1”：%2").arg(localFile, file.errorString());
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
        QObject::connect(reply, &QNetworkReply::uploadProgress, reply,
                         [this](qint64 sent, qint64 total) {
                             reportTransferProgress(sent, total);
                         });
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
        const int retryAfter = retryAfterDelayMs(reply);
        const auto networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        reply->deleteLater();

        if (status >= 200 && status < 300 && networkError == QNetworkReply::NoError)
            return true;
        if (retryableFailure(status, networkError) && attempt < m_options.retryCount
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, cancelled)) {
            continue;
        }
        m_lastErrorClass = classifyReply(status, networkError);
        if (error) {
            *error = status == 0
                ? QObject::tr("上传失败：%1").arg(networkErrorString)
                : QObject::tr("上传失败：%1").arg(httpError(status, body, statusText(status)));
        }
        return false;
    }
}

bool WebDavBackend::move(const QString &fromPath, const QString &toPath, bool overwrite,
                         QString *error, const CancelledCallback &cancelled)
{
    m_lastErrorClass = RemoteErrorClass::None;
    for (int attempt = 0;; ++attempt) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
            if (error)
                *error = QObject::tr("同步已取消");
            return false;
        }
        QNetworkRequest request = requestFor(fromPath);
        request.setRawHeader("Destination", PathUtils::remoteUrl(m_baseUrl, toPath).toEncoded());
        request.setRawHeader("Overwrite", overwrite ? "T" : "F");
        QNetworkReply *reply = m_network.sendCustomRequest(request, "MOVE");
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
        const int retryAfter = retryAfterDelayMs(reply);
        const auto networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        reply->deleteLater();

        if (status == 201 || status == 204 || isSuccessStatus(status))
            return true;
        if (retryableFailure(status, networkError) && attempt < m_options.retryCount
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, cancelled)) {
            continue;
        }
        m_lastErrorClass = classifyReply(status, networkError);
        if (error) {
            *error = status == 0 ? QObject::tr("移动远程文件失败：%1").arg(networkErrorString)
                                 : QObject::tr("移动远程文件失败：%1")
                                       .arg(httpError(status, body, statusText(status)));
        }
        return false;
    }
}

bool WebDavBackend::makeDirectory(const QString &path, QString *error)
{
    m_lastErrorClass = RemoteErrorClass::None;
    for (int attempt = 0;; ++attempt) {
        QNetworkRequest request = requestFor(path);
        QNetworkReply *reply = m_network.sendCustomRequest(request, "MKCOL");
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        const int status = statusCode(reply);
        const QByteArray body = reply->readAll();
        const int retryAfter = retryAfterDelayMs(reply);
        const auto networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        reply->deleteLater();

        if (isSuccessStatus(status) || status == 405)
            return true;
        if (retryableFailure(status, networkError) && attempt < m_options.retryCount
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, {})) {
            continue;
        }
        m_lastErrorClass = classifyReply(status, networkError);
        if (status == 0) {
            if (error)
                *error = QObject::tr("创建远程目录失败：%1").arg(networkErrorString);
            return false;
        }
        if (error) {
            *error = QObject::tr("创建远程目录“%1”失败：%2")
                         .arg(path.isEmpty() ? QStringLiteral("/") : path,
                              httpError(status, body, statusText(status)));
        }
        return false;
    }
}

bool WebDavBackend::remove(const QString &path, bool recursive, const QString &expectedEtag,
                           QString *error, const CancelledCallback &cancelled)
{
    m_lastErrorClass = RemoteErrorClass::None;
    Q_UNUSED(recursive)
    if (cancelled && cancelled()) {
        m_lastErrorClass = RemoteErrorClass::Cancelled;
        if (error)
            *error = QObject::tr("同步已取消");
        return false;
    }
    for (int attempt = 0;; ++attempt) {
        if (cancelled && cancelled()) {
            m_lastErrorClass = RemoteErrorClass::Cancelled;
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
        const int retryAfter = retryAfterDelayMs(reply);
        const auto networkError = reply->error();
        const QString networkErrorString = reply->errorString();
        reply->deleteLater();

        if (status == 404 || isSuccessStatus(status))
            return true;
        if (retryableFailure(status, networkError) && attempt < m_options.retryCount
            && waitBeforeRetry(attempt, retryAfter, m_options.retryBaseDelayMs,
                               m_options.retryMaxDelayMs, cancelled)) {
            continue;
        }
        m_lastErrorClass = classifyReply(status, networkError);
        if (status == 0) {
            if (error)
                *error = QObject::tr("删除远程路径失败：%1").arg(networkErrorString);
            return false;
        }
        if (error) {
            *error = QObject::tr("删除远程路径“%1”失败：%2")
                         .arg(path.isEmpty() ? QStringLiteral("/") : path,
                              httpError(status, body, statusText(status)));
        }
        return false;
    }
}

} // namespace NutSync
