#pragma once

#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

namespace NutSync {

/**
 * QNetworkAccessManager that never forwards HTTP Authorization headers to a
 * different origin during a redirect. Credentials are scoped to the sync
 * pair's base URL (scheme + host + port).
 */
class SecuredNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT

public:
    explicit SecuredNetworkAccessManager(const QUrl &baseUrl, QObject *parent = nullptr);

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override;

private:
    static QString originOf(const QUrl &url);

    QString m_origin;
};

} // namespace NutSync
