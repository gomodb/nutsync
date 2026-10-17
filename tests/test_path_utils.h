#pragma once

#include <QObject>

namespace NutSync::Test {
class TestPathUtils : public QObject
{
    Q_OBJECT

private slots:
    void normalizesRelativePaths();
    void computesPathRelations();
    void matchesExcludePatterns();
    void buildsRemoteUrls();
    void buildsConflictNames();
    void parsesHttpDates();
    void formatsSizes();
};
} // namespace NutSync::Test
