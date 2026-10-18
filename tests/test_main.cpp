#include "test_autostart.h"
#include "test_config_store.h"
#include "test_credential_store.h"
#include "test_language.h"
#include "test_network_security.h"
#include "test_local_change_monitor.h"
#include "test_path_utils.h"
#include "test_rate_limiter.h"
#include "test_retry_policy.h"
#include "test_state_store.h"
#include "test_sync_controller.h"
#include "test_trash_manager.h"
#include "test_sync_engine.h"
#include "test_sync_planner.h"
#include "test_webdav_backend.h"

#include <QCoreApplication>
#include <QTest>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("nutsync-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("NutSync"));

    int status = 0;
    {
        NutSync::Test::TestAutostart test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestConfigStore test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestCredentialStore test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestRetryPolicy test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestLanguage test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestNetworkSecurity test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestLocalChangeMonitor test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestPathUtils test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestSyncPlanner test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestRateLimiter test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestSyncEngine test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestWebDavBackend test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestSyncController test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestStateStore test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        NutSync::Test::TestTrashManager test;
        status |= QTest::qExec(&test, argc, argv);
    }
    return status;
}
