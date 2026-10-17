#pragma once

#include <QObject>

namespace NutSync::Test {
class TestLocalChangeMonitor : public QObject
{
    Q_OBJECT

private slots:
    void detectsFileChanges();
    void ignoresExcludedFilesAndDirectories();
    void suppressesDuringSyncAndResumes();
    void watchesNewAndRenamedDirectories();
};
} // namespace NutSync::Test
