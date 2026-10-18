#include "core/network_utils.h"

#include <QHostAddress>
#include <QUrl>

namespace NutSync::NetworkUtils {

bool isLoopbackHost(const QString &host)
{
    const QString normalized = host.trimmed();
    if (normalized.isEmpty())
        return false;
    if (normalized.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;

    QHostAddress address;
    return address.setAddress(normalized) && address.isLoopback();
}

bool allowsCleartextCredentials(const QUrl &url)
{
    return url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
        || isLoopbackHost(url.host());
}

} // namespace NutSync::NetworkUtils
