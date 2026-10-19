#pragma once

#include "core/remote_backend.h"

namespace NutSync::Test {

class MockRemoteBackend final : public RemoteBackend
{
public:
    explicit MockRemoteBackend(QString root);

    QString typeName() const override;
    QString endpointDescription() const override;

    bool testConnection(QString *error) override;
    bool scan(QList<RemoteEntry> *entries, QString *error,
              const CancelledCallback &cancelled) override;
    bool stat(const QString &path, RemoteEntry *entry, bool *exists, QString *error) override;
    bool download(const QString &path, const QString &localFile, DownloadResult *result,
                  QString *error, const CancelledCallback &cancelled) override;
    bool upload(const QString &localFile, const QString &path, const QString &expectedEtag,
                QString *error, const CancelledCallback &cancelled) override;
    bool makeDirectory(const QString &path, QString *error) override;
    bool move(const QString &fromPath, const QString &toPath, bool overwrite, QString *error,
              const CancelledCallback &cancelled) override;
    bool remove(const QString &path, bool recursive, const QString &expectedEtag, QString *error,
                const CancelledCallback &cancelled) override;

    QString root() const;
    QString absolutePath(const QString &relativePath) const;
    bool writeFile(const QString &relativePath, const QByteArray &content,
                   QString *error = nullptr);
    QByteArray readFile(const QString &relativePath, bool *ok = nullptr) const;
    QStringList listAll() const;

private:
    bool entryFor(const QString &relativePath, RemoteEntry *entry, bool *exists,
                  QString *error) const;
    QString etagFor(const QString &absolutePath) const;
    QString m_root;
};

} // namespace NutSync::Test
