add_executable(nutsync_tests
    test_main.cpp
    test_autostart.cpp
    test_autostart.h
    test_config_store.cpp
    test_config_store.h
    test_nextcloud.cpp
    test_nextcloud.h
    test_diagnostics.cpp
    test_diagnostics.h
    test_conflict_resolver.cpp
    test_conflict_resolver.h
    test_credential_store.cpp
    test_credential_store.h
    test_language.cpp
    test_language.h
    test_network_security.cpp
    test_network_security.h
    test_local_change_monitor.cpp
    test_local_change_monitor.h
    test_rate_limiter.cpp
    test_rate_limiter.h
    test_retry_policy.cpp
    test_retry_policy.h
    mock_remote_backend.cpp
    mock_remote_backend.h
    test_path_utils.cpp
    test_path_utils.h
    test_sync_planner.cpp
    test_sync_planner.h
    test_state_store.cpp
    test_state_store.h
    test_sync_engine.cpp
    test_sync_engine.h
    test_trash_manager.cpp
    test_trash_manager.h
    test_webdav_backend.cpp
    test_webdav_backend.h
    test_sync_controller.cpp
    test_sync_controller.h
)

target_link_libraries(nutsync_tests PRIVATE
    nutsync_core
    Qt6::Test
    Qt6::Concurrent
)

target_compile_definitions(nutsync_tests PRIVATE
    NUTSYNC_TEST_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}/data"
    NUTSYNC_WEBDAV_SERVER="${CMAKE_CURRENT_SOURCE_DIR}/mock_webdav_server.py"
)

add_test(NAME nutsync_tests COMMAND nutsync_tests)
set_tests_properties(nutsync_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-cache;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-data"
    TIMEOUT 180
)
