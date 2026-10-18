#pragma once

#include <QObject>

namespace NutSync::Test {
class TestNetworkSecurity : public QObject
{
    Q_OBJECT

private slots:
    void classifiesLoopbackHosts();
    void doesNotFollowCrossOriginRedirects();
};
} // namespace NutSync::Test
