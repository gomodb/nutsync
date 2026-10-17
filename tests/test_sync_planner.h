#pragma once

#include <QObject>

namespace NutSync::Test {
class TestSyncPlanner : public QObject
{
    Q_OBJECT

private slots:
    void newFiles();
    void newDirectories();
    void bothNewDirectoriesRecordBaseline();
    void changedFiles();
    void deletionPropagation();
    void deletionConflicts();
    void stateOnlyEntriesAreRemoved();
    void directoryDeletionPropagation();
    void modifiedChildBlocksDirectoryDeletion();
    void typeMismatchIsConflict();
    void nestedDirectoriesAreOrdered();
    void directoryStateResetWhenBothSidesBecameDirectories();
    void localDeletionsAreRestoredWhenDisabled();
    void remoteDeletionsStillPropagateWhenLocalDeletionSyncIsDisabled();
};
} // namespace NutSync::Test
