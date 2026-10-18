cmake_minimum_required(VERSION 3.21)

project(NutSync
    VERSION 0.2.1
    DESCRIPTION "Two-way WebDAV synchronization for the KDE desktop"
    HOMEPAGE_URL "https://github.com/gomodb/nutsync"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)
include(CTest)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    Concurrent
)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

qt_standard_project_setup()

find_package(Qt6Keychain QUIET)
find_package(KF6StatusNotifierItem QUIET)
find_package(KF6Notifications QUIET)

add_library(nutsync_core STATIC
    src/core/app_paths.cpp
    src/core/app_paths.h
    src/core/autostart_manager.cpp
    src/core/autostart_manager.h
    src/core/config_store.cpp
    src/core/config_store.h
    src/core/credential_store.cpp
    src/core/credential_store.h
    src/core/local_change_monitor.cpp
    src/core/local_change_monitor.h
    src/core/logger.cpp
    src/core/logger.h
    src/core/path_utils.cpp
    src/core/path_utils.h
    src/core/remote_backend.h
    src/core/sync_engine.cpp
    src/core/sync_engine.h
    src/core/sync_planner.cpp
    src/core/sync_planner.h
    src/core/sync_rate_limiter.cpp
    src/core/sync_rate_limiter.h
    src/core/sync_state_store.cpp
    src/core/sync_state_store.h
    src/core/sync_types.cpp
    src/core/sync_types.h
    src/core/webdav_backend.cpp
    src/core/webdav_backend.h
    src/ui/sync_controller.cpp
    src/ui/sync_controller.h
)

target_include_directories(nutsync_core PUBLIC src)
target_link_libraries(nutsync_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
)

target_compile_definitions(nutsync_core PUBLIC
    NUTSYNC_VERSION="${PROJECT_VERSION}"
)

if(Qt6Keychain_FOUND)
    target_link_libraries(nutsync_core PRIVATE Qt6Keychain::Qt6Keychain)
    target_compile_definitions(nutsync_core PRIVATE NUTSYNC_HAVE_KEYCHAIN=1)
    message(STATUS "NutSync: using QtKeychain for credential storage")
else()
    message(WARNING "NutSync: QtKeychain not found; passwords cannot be stored securely")
endif()

qt_add_executable(nutsync
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/app_icons.cpp
    src/ui/app_icons.h
    src/ui/pair_dialog.cpp
    src/ui/pair_dialog.h
    src/ui/tray_controller.cpp
    src/ui/tray_controller.h
)

target_link_libraries(nutsync PRIVATE
    nutsync_core
    Qt6::Gui
    Qt6::Widgets
)

if(KF6StatusNotifierItem_FOUND)
    target_link_libraries(nutsync PRIVATE KF6::StatusNotifierItem)
    target_compile_definitions(nutsync PRIVATE NUTSYNC_HAVE_KSTATUSNOTIFIER=1)
    message(STATUS "NutSync: using KStatusNotifierItem")
endif()

if(KF6Notifications_FOUND)
    target_link_libraries(nutsync PRIVATE KF6::Notifications)
    target_compile_definitions(nutsync PRIVATE NUTSYNC_HAVE_KNOTIFICATIONS=1)
    message(STATUS "NutSync: using KNotification")
endif()

qt_add_resources(nutsync "nutsync_resources"
    PREFIX "/"
    FILES
        resources/icons/nutsync.svg
        resources/icons/nutsync-symbolic-light.svg
        resources/icons/nutsync-symbolic-dark.svg
        resources/icons/nutsync-syncing-light.svg
        resources/icons/nutsync-syncing-dark.svg
)

install(TARGETS nutsync RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.nutsync.NutSync.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.nutsync.NutSync.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/nutsync.notifyrc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6)
install(FILES resources/icons/nutsync.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/nutsync)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
