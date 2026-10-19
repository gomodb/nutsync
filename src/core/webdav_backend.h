#pragma once

#include "core/remote_backend.h"
#include "core/secured_network_access_manager.h"

#include <QNetworkAccessManager>
#include <QSslConfiguration>
#include <QUrl>

namespace NutSync {

struct WebDavOptions
{
    QUrl baseUrl;
    QString username;
    QString password;
    QString authMode = QStringLiteral("basic");
    QString clientCertificatePath;
    QString clientKeyPath;
    bool chunkedUpload = false;
    int chunkSizeMb = 10;
    int timeoutMs = 120000;
    int retryCount = 3;
    int retryBaseDelayMs = 500;
    int retryMaxDelayMs = 30000;
    int transferLimitBytesPerSecond = 0;
};

class WebDavBackend final : public RemoteBackend
{
public:
    explicit WebDavBackend(WebDavOptions options);

    QString typeName() const override;
    QString endpointDescription() const override;
    RemoteErrorClass lastErrorClass() const override;

    bool testConnection(QString *error) override;
    bool scan(QList<RemoteEntry> *entries, QString *error,
              const CancelledCallback &cancelled) override;
    bool stat(const QString &path, RemoteEntry *entry, bool *exists, QString *error) override;
    bool download(const QString &path, const QString &localFile, DownloadResult *result,
                  QString *error, const CancelledCallback &cancelled) override;
    bool upload(const QString &localFile, const QString &path, const QString &expectedEtag,
                QString *error, const CancelledCallback &cancelled) override;
    bool makeDirectory(const QString &path, QString *error) override;
    bool move(const QString &fromPath, const QString &toPath, bool overwrite, QString *error,
              const CancelledCallback &cancelled) override;
    bool remove(const QString &path, bool recursive, const QString &expectedEtag, QString *error,
                const CancelledCallback &cancelled) override;

private:
    QNetworkRequest requestFor(const QString &path) const;
    QNetworkRequest requestForAbsolute(const QUrl &url) const;
    bool uploadChunked(const QString &localFile, const QString &path, QString *error,
                       const CancelledCallback &cancelled);
    QByteArray propfindBody() const;
    bool listDirectory(const QString &path, QList<RemoteEntry> *entries, QString *error,
                       const CancelledCallback &cancelled = {});
    bool propfind(const QString &path, int depth, QByteArray *body, int *status, QString *error,
                  const CancelledCallback &cancelled = {});
    QString httpError(int status, const QByteArray &body, const QString &fallback) const;
    static QList<RemoteEntry> parseMultiStatus(const QByteArray &xml, const QUrl &requestUrl,
                                               QString *error, bool includeSelf = false,
                                               const QString &selfPath = {},
                                               const QString &pathPrefix = {});
    static QString relativePathFromHref(const QString &href, const QUrl &requestUrl, bool *ok);

    WebDavOptions m_options;
    SecuredNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QSslConfiguration m_sslConfiguration;
    bool m_hasClientCertificate = false;
    RemoteErrorClass m_lastErrorClass = RemoteErrorClass::None;
};

} // namespace NutSync
