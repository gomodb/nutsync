#pragma once

#include <QString>

namespace NutSync {

/**
 * Manages the per-user XDG autostart entry for NutSync.
 *
 * The setting is represented by the presence of
 * $XDG_CONFIG_HOME/autostart/io.github.gomodb.NutSync.desktop, so the
 * preferences checkbox always reflects the actual state on disk.
 */
class AutostartManager
{
public:
    explicit AutostartManager(QString configDir = {});

    QString desktopFilePath() const;
    bool isEnabled() const;
    bool setEnabled(bool enabled, QString *error = nullptr) const;

    static QString defaultConfigDir();
    static QString desktopFileName();
    static void migrateLegacy();

private:
    void migrateLegacyEntry() const;

    QString m_configDir;
};

} // namespace NutSync
