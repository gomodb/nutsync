#include "mock_remote_backend.h"

#include "core/path_utils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <utility>

namespace NutSync::Test {

namespace {

QString fileEtag(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (file.open(QIODevice::ReadOnly)) {
        hash.addData(&file);
    }
    hash.addData(QByteArray::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()));
    return QString::fromLatin1(hash.result().toHex());
}

QString directoryEtag(const QString &path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QFileInfo(path).lastModified().toUTC().toString(Qt::ISODateWithMs).toUtf8(),
            QCryptographicHash::Md5)
            .toHex());
}

} // namespace

MockRemoteBackend::MockRemoteBackend(QString root)
    : m_root(QDir::cleanPath(std::move(root)))
{
}

QString MockRemoteBackend::typeName() const
{
    return QStringLiteral("Mock");
}

QString MockRemoteBackend::endpointDescription() const
{
    return m_root;
}

QString MockRemoteBackend::root() const
{
    return m_root;
}

QString MockRemoteBackend::absolutePath(const QString &relativePath) const
{
    return relativePath.isEmpty() ? m_root : QDir(m_root).filePath(relativePath);
}

bool MockRemoteBackend::testConnection(QString *error)
{
    if (!QFileInfo(m_root).isDir()) {
        if (error)
            *error = QStringLiteral("remote root is missing");
        return false;
    }
    return true;
}

QString MockRemoteBackend::etagFor(const QString &absolutePath) const
{
    QFileInfo info(absolutePath);
    return info.isDir() ? directoryEtag(absolutePath) : fileEtag(absolutePath);
}

bool MockRemoteBackend::entryFor(const QString &relativePath, RemoteEntry *entry, bool *exists,
                                 QString *error) const
{
    if (exists)
        *exists = false;
    bool ok = false;
    const QString normalized = PathUtils::normalizeRelativePath(relativePath, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("invalid path");
        return false;
    }
    const QString absolute = absolutePath(normalized);
    const QFileInfo info(absolute);
    if (!info.exists() || info.isSymLink())
        return true;
    if (!info.isFile() && !info.isDir()) {
        if (error)
            *error = QStringLiteral("unsupported remote entry type");
        return false;
    }
    if (exists)
        *exists = true;
    if (entry) {
        entry->path = normalized;
        entry->type = info.isDir() ? EntryType::Directory : EntryType::File;
        entry->size = info.isFile() ? info.size() : 0;
        entry->mtimeMs = info.lastModified().toMSecsSinceEpoch();
        entry->etag = etagFor(absolute);
    }
    return true;
}

bool MockRemoteBackend::scan(QList<RemoteEntry> *entries, QString *error,
                             const CancelledCallback &cancelled)
{
    if (!entries)
        return false;
    entries->clear();
    if (cancelled && cancelled())
        return false;
    if (!QFileInfo(m_root).isDir()) {
        if (error)
            *error = QStringLiteral("remote root is missing");
        return false;
    }

    QStringList paths;
    QDirIterator iterator(m_root,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        if (iterator.fileInfo().isSymLink())
            continue;
        if (!iterator.fileInfo().isFile() && !iterator.fileInfo().isDir())
            continue;
        paths.append(absolute);
    }
    paths.sort();

    for (const QString &absolute : std::as_const(paths)) {
        if (cancelled && cancelled())
            return false;
        const QString relative = QDir(m_root).relativeFilePath(absolute);
        RemoteEntry entry;
        bool exists = false;
        if (!entryFor(relative, &entry, &exists, error))
            return false;
        if (exists)
            entries->append(entry);
    }
    return true;
}

bool MockRemoteBackend::stat(const QString &path, RemoteEntry *entry, bool *exists, QString *error)
{
    return entryFor(path, entry, exists, error);
}

bool MockRemoteBackend::download(const QString &path, const QString &localFile,
                                 DownloadResult *result, QString *error,
                                 const CancelledCallback &cancelled)
{
    if (cancelled && cancelled())
        return false;
    RemoteEntry entry;
    bool exists = false;
    if (!entryFor(path, &entry, &exists, error))
        return false;
    if (!exists) {
        if (error)
            *error = QStringLiteral("remote path does not exist");
        return false;
    }
    if (entry.type != EntryType::File) {
        if (error)
            *error = QStringLiteral("remote path is not a file");
        return false;
    }

    QFile source(absolutePath(path));
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = source.errorString();
        return false;
    }
    QSaveFile target(localFile);
    if (!target.open(QIODevice::WriteOnly)) {
        if (error)
            *error = target.errorString();
        return false;
    }
    const QByteArray data = source.readAll();
    target.write(data);
    if (!target.commit()) {
        if (error)
            *error = target.errorString();
        return false;
    }
    QFile local(localFile);
    if (local.open(QIODevice::ReadWrite)) {
        local.setFileTime(QFileInfo(absolutePath(path)).lastModified(),
                          QFileDevice::FileModificationTime);
    }
    if (result) {
        result->size = data.size();
        result->mtimeMs = entry.mtimeMs;
        result->etag = entry.etag;
    }
    return true;
}

bool MockRemoteBackend::upload(const QString &localFile, const QString &path,
                               const QString &expectedEtag, QString *error,
                               const CancelledCallback &cancelled)
{
    if (cancelled && cancelled())
        return false;
    RemoteEntry current;
    bool exists = false;
    if (!entryFor(path, &current, &exists, error))
        return false;
    if (expectedEtag == QLatin1String("*") && exists) {
        if (error)
            *error = QStringLiteral("precondition failed: remote exists");
        return false;
    }
    if (!expectedEtag.isEmpty() && expectedEtag != QLatin1String("*")) {
        if (!exists || PathUtils::normalizeEtag(current.etag) != expectedEtag) {
            if (error)
                *error = QStringLiteral("precondition failed: etag mismatch");
            return false;
        }
    }

    const QString target = absolutePath(path);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create parent directory");
        return false;
    }
    QFile source(localFile);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = source.errorString();
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(source.readAll());
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QFile uploaded(target);
    if (uploaded.open(QIODevice::ReadWrite))
        uploaded.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return true;
}

bool MockRemoteBackend::move(const QString &fromPath, const QString &toPath, bool overwrite,
                             QString *error, const CancelledCallback &cancelled)
{
    if (cancelled && cancelled())
        return false;
    RemoteEntry source;
    bool exists = false;
    if (!entryFor(fromPath, &source, &exists, error))
        return false;
    if (!exists)
        return true;
    RemoteEntry destination;
    bool destinationExists = false;
    if (!entryFor(toPath, &destination, &destinationExists, error))
        return false;
    if (destinationExists && !overwrite) {
        if (error)
            *error = QStringLiteral("destination exists");
        return false;
    }
    if (destinationExists) {
        const QString target = absolutePath(toPath);
        if (!QFileInfo::exists(target)
            || (source.type == EntryType::Directory ? !QDir(target).removeRecursively()
                                                    : !QFile::remove(target))) {
            if (error)
                *error = QStringLiteral("cannot remove destination");
            return false;
        }
    }
    const QString sourcePath = absolutePath(fromPath);
    const QString targetPath = absolutePath(toPath);
    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create destination parent");
        return false;
    }
    const bool moved = source.type == EntryType::Directory ? QDir().rename(sourcePath, targetPath)
                                                           : QFile::rename(sourcePath, targetPath);
    if (!moved) {
        if (error)
            *error = QStringLiteral("cannot move path");
        return false;
    }
    return true;
}
bool MockRemoteBackend::makeDirectory(const QString &path, QString *error)
{
    const QString target = absolutePath(path);
    if (QFileInfo(target).isDir())
        return true;
    if (!QDir().mkpath(target)) {
        if (error)
            *error = QStringLiteral("cannot create remote directory");
        return false;
    }
    return true;
}

bool MockRemoteBackend::remove(const QString &path, bool recursive, const QString &expectedEtag,
                               QString *error, const CancelledCallback &cancelled)
{
    if (cancelled && cancelled())
        return false;
    RemoteEntry current;
    bool exists = false;
    if (!entryFor(path, &current, &exists, error))
        return false;
    if (!exists)
        return true;
    if (!expectedEtag.isEmpty() && PathUtils::normalizeEtag(current.etag) != expectedEtag) {
        if (error)
            *error = QStringLiteral("delete precondition failed");
        return false;
    }
    const QString target = absolutePath(path);
    const bool removed = current.type == EntryType::Directory ? QDir(target).removeRecursively()
                                                              : QFile::remove(target);
    if (!removed && QFileInfo::exists(target)) {
        if (error)
            *error = QStringLiteral("cannot remove remote path");
        return false;
    }
    Q_UNUSED(recursive)
    return true;
}

bool MockRemoteBackend::writeFile(const QString &relativePath, const QByteArray &content,
                                  QString *error)
{
    const QString target = absolutePath(relativePath);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create parent directory");
        return false;
    }
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(content) != content.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    if (file.open(QIODevice::ReadWrite))
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return true;
}

QByteArray MockRemoteBackend::readFile(const QString &relativePath, bool *ok) const
{
    QFile file(absolutePath(relativePath));
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return file.readAll();
}

QStringList MockRemoteBackend::listAll() const
{
    QStringList result;
    QDirIterator iterator(m_root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        result.append(QDir(m_root).relativeFilePath(path));
    }
    result.sort();
    return result;
}

} // namespace NutSync::Test
