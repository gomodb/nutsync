#include "core/network_settings.h"

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QString>
#include <QObject>

namespace NutSync::NetworkSettings {

bool apply(const AppSettings &settings, const QString &proxyPassword, QString *error)
{
    if (settings.proxyMode == QLatin1String("system")) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    } else if (settings.proxyMode == QLatin1String("manual")) {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        if (settings.proxyHost.isEmpty()) {
            if (error)
                *error = QObject::tr("手动代理模式需要填写代理主机");
            QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
            return false;
        }
        QNetworkProxy proxy(QNetworkProxy::HttpProxy, settings.proxyHost, settings.proxyPort,
                            settings.proxyUser, proxyPassword);
        QNetworkProxy::setApplicationProxy(proxy);
    } else {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }

    if (settings.caBundlePath.isEmpty())
        return true;

    const QList<QSslCertificate> certificates =
        QSslCertificate::fromPath(settings.caBundlePath, QSsl::Pem);
    if (certificates.isEmpty()) {
        if (error) {
            *error = QObject::tr("无法读取 CA 证书文件“%1”")
                         .arg(settings.caBundlePath);
        }
        return false;
    }

    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    QList<QSslCertificate> caCertificates = configuration.caCertificates();
    caCertificates.append(certificates);
    configuration.setCaCertificates(caCertificates);
    QSslConfiguration::setDefaultConfiguration(configuration);
    return true;
}

} // namespace NutSync::NetworkSettings
