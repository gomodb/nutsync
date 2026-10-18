#pragma once

#include "core/sync_types.h"

namespace NutSync::NetworkSettings {

bool apply(const AppSettings &settings, const QString &proxyPassword, QString *error = nullptr);

} // namespace NutSync::NetworkSettings
