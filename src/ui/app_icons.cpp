#include "ui/app_icons.h"

#include <QApplication>
#include <QColor>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace NutSync::AppIcons {

namespace {
QString monochromePath(bool dark)
{
    return dark ? QStringLiteral(":/resources/icons/nutsync-symbolic-dark.svg")
                : QStringLiteral(":/resources/icons/nutsync-symbolic-light.svg");
}

QString syncingPath(bool dark)
{
    return dark ? QStringLiteral(":/resources/icons/nutsync-syncing-dark.svg")
                : QStringLiteral(":/resources/icons/nutsync-syncing-light.svg");
}

QColor kdeWindowColor()
{
    const QString configPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    if (!QFileInfo::exists(configPath))
        return {};

    const QSettings settings(configPath, QSettings::IniFormat);
    const QStringList parts =
        settings.value(QStringLiteral("Colors:Window/BackgroundNormal")).toString().split(
            QLatin1Char(','));
    if (parts.size() < 3)
        return {};

    bool redOk = false;
    bool greenOk = false;
    bool blueOk = false;
    const int red = parts.at(0).trimmed().toInt(&redOk);
    const int green = parts.at(1).trimmed().toInt(&greenOk);
    const int blue = parts.at(2).trimmed().toInt(&blueOk);
    if (!redOk || !greenOk || !blueOk)
        return {};
    return QColor(red, green, blue);
}
} // namespace

bool isDarkPalette(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

bool isDarkTheme()
{
    // Read kdeglobals directly: some Qt/KDE combinations do not deliver a
    // palette-change event when the Plasma color scheme is changed. The panel
    // already uses the new scheme, so the tray icon must follow it too.
    const QColor color = kdeWindowColor();
    if (color.isValid())
        return color.lightness() < 128;
    return isDarkPalette(QApplication::palette());
}

QIcon colored()
{
    return QIcon(QStringLiteral(":/resources/icons/nutsync.svg"));
}

QIcon monochrome(bool dark)
{
    return QIcon(monochromePath(dark));
}

QIcon syncing(bool dark)
{
    return QIcon(syncingPath(dark));
}

} // namespace NutSync::AppIcons
