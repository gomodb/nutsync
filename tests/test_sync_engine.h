#pragma once

#include <QObject>

namespace NutSync::Test {
class TestSyncEngine : public QObject
{
    Q_OBJECT

private slots:
    void basicBidirectionalTransfer();
    void deletedFilesAreBackedUp();
    void selectiveSync();
    void detectsSubsequentChanges();
    void sameContentMtimeChangeDoesNotUpload();
    void propagatesRemoteDeletion();
    void propagatesLocalDeletion();
    void handlesEmptyDirectories();
    void propagatesDirectoryDeletion();
    void keepsBothConflictCopies();
    void preferLocalConflict();
    void preferRemoteConflict();
    void identicalInitialFilesDoNotConflict();
    void excludedLocalEntriesAreNotUploaded();
    void excludedRemoteContentBlocksDirectoryDeletion();
    void typeConflictReportsError();
    void missingRemoteRootWithStateDoesNotDeleteLocalFiles();
    void createsRemoteRootForFirstSync();
    void cancelledBeforeStartReportsCancellation();
    void localSymlinkDoesNotDeleteRemoteFile();
    void localDeletionRestoresWhenSyncDeletionsDisabled();
    void localDirectoryDeletionRestoresWhenSyncDeletionsDisabled();
    void deletionConflictDoesNotDeleteRemoteWhenSyncDeletionsDisabled();
    void stateStoreSupportsPrefixRemoval();
};
} // namespace NutSync::Test
