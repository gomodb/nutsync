#pragma once

#include <QString>

namespace NutSync::AppPaths {

QString configDir();
QString configFile();
QString dataDir();
QString stateDatabase();
QString logFile();
QString cacheDir();
QString trashDir();

void ensureDirectories();

} // namespace NutSync::AppPaths
