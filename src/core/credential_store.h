#pragma once

#include <QString>

namespace NutSync {

class CredentialStore
{
public:
    CredentialStore();

    bool isAvailable() const;
    QString readPassword(const QString &key, QString *error = nullptr) const;
    bool writePassword(const QString &key, const QString &password, QString *error = nullptr) const;
    bool deletePassword(const QString &key, QString *error = nullptr) const;

    static QString serviceName();

private:
    QString m_service;
};

} // namespace NutSync
