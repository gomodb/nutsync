#pragma once

#include "core/sync_types.h"

#include <QString>

namespace NutSync {

class ConfigStore
{
public:
    explicit ConfigStore(QString filePath = {});

    bool load(AppConfig *config, QString *error = nullptr) const;
    bool save(const AppConfig &config, QString *error = nullptr) const;

    QString filePath() const;

private:
    QString m_filePath;
};

} // namespace NutSync
