#pragma once

#include <QString>

namespace NutSync::AppPaths {

QString configDir();
QString configFile();
QString dataDir();
QString stateDatabase();
QString logFile();
QString cacheDir();

void ensureDirectories();

} // namespace NutSync::AppPaths
