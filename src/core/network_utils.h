#pragma once

#include <QString>

class QUrl;

namespace NutSync::NetworkUtils {

bool isLoopbackHost(const QString &host);
bool allowsCleartextCredentials(const QUrl &url);

} // namespace NutSync::NetworkUtils
