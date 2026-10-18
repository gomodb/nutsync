#include "core/secured_network_access_manager.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace NutSync {

namespace {
QString normalizedOrigin(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    const int defaultPort = scheme == QLatin1String("https") ? 443 : 80;
    return scheme + QStringLiteral("://") + url.host().toLower() + QLatin1Char(':')
        + QString::number(url.port(defaultPort));
}
} // namespace

SecuredNetworkAccessManager::SecuredNetworkAccessManager(const QUrl &baseUrl, QObject *parent)
    : QNetworkAccessManager(parent)
    , m_origin(normalizedOrigin(baseUrl))
{
}

QString SecuredNetworkAccessManager::originOf(const QUrl &url)
{
    return normalizedOrigin(url);
}

QNetworkReply *SecuredNetworkAccessManager::createRequest(Operation operation,
                                                          const QNetworkRequest &request,
                                                          QIODevice *outgoingData)
{
    QNetworkRequest secured(request);
    if (originOf(secured.url()) != m_origin)
        secured.setRawHeader("Authorization", QByteArray());
    return QNetworkAccessManager::createRequest(operation, secured, outgoingData);
}

} // namespace NutSync
