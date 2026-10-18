#pragma once

#include <QString>

namespace NutSync {

class Logger
{
public:
    static void install();
    static void setLevel(const QString &level);
    static void setRetention(int maxSizeMb, int files);
    static QString level();
    static void log(const QString &category, const QString &message);
    static QString logFilePath();
};

} // namespace NutSync
