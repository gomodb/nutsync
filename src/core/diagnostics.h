#pragma once

#include "core/sync_types.h"

#include <QString>

namespace NutSync::Diagnostics {

QString buildReport(const AppConfig &config);
QString readLogTail(const QString &logPath, int maxLines = 200);

} // namespace NutSync::Diagnostics
