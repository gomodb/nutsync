#pragma once

#include "core/remote_backend.h"

#include <QNetworkAccessManager>
#include <QUrl>

namespace NutSync {

struct WebDavOptions
{
    QUrl baseUrl;
    QString username;
    QString password;
    int timeoutMs = 120000;
};

class WebDavBackend final : public RemoteBackend
{
public:
    explicit WebDavBackend(WebDavOptions options);

    QString typeName() const override;
    QString endpointDescription() const override;

    bool testConnection(QString *error) override;
    bool scan(QList<RemoteEntry> *entries, QString *error,
              const CancelledCallback &cancelled) override;
    bool stat(const QString &path, RemoteEntry *entry, bool *exists,
              QString *error) override;
    bool download(const QString &path, const QString &localFile,
                  DownloadResult *result, QString *error,
                  const CancelledCallback &cancelled) override;
    bool upload(const QString &localFile, const QString &path,
                const QString &expectedEtag, QString *error,
                const CancelledCallback &cancelled) override;
    bool makeDirectory(const QString &path, QString *error) override;
    bool remove(const QString &path, bool recursive, const QString &expectedEtag,
                QString *error, const CancelledCallback &cancelled) override;

private:
    QNetworkRequest requestFor(const QString &path) const;
    QByteArray propfindBody() const;
    bool listDirectory(const QString &path, QList<RemoteEntry> *entries,
                       QString *error);
    bool propfind(const QString &path, int depth, QByteArray *body, int *status,
                  QString *error);
    QString httpError(int status, const QByteArray &body, const QString &fallback) const;
    static QList<RemoteEntry> parseMultiStatus(const QByteArray &xml,
                                               const QUrl &requestUrl,
                                               QString *error,
                                               bool includeSelf = false,
                                               const QString &selfPath = {},
                                               const QString &pathPrefix = {});
    static QString relativePathFromHref(const QString &href, const QUrl &requestUrl,
                                        bool *ok);

    WebDavOptions m_options;
    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
};

} // namespace NutSync
