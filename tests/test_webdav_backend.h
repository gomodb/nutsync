#pragma once

#include <QObject>

namespace NutSync::Test {
class TestWebDavBackend : public QObject
{
    Q_OBJECT

private slots:
    void backendOperations();
    void engineEndToEnd();
};
} // namespace NutSync::Test
