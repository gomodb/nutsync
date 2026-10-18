#pragma once

#include <QObject>

namespace NutSync::Test {
class TestTrashManager : public QObject
{
    Q_OBJECT

private slots:
    void copiesFilesAndDirectories();
    void listsAndRestoresEntries();
};
} // namespace NutSync::Test
