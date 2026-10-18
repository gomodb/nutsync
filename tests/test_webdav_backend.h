#pragma once

#include <QObject>

namespace NutSync::Test {
class TestWebDavBackend : public QObject
{
    Q_OBJECT

private slots:
    void backendOperations();
    void retriesTransientRequests();
    void resumesTruncatedDownload();
    void engineEndToEnd();
};
} // namespace NutSync::Test
