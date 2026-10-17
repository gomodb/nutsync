#include "core/app_paths.h"
#include "core/logger.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("nutsync"));
    QApplication::setApplicationDisplayName(QStringLiteral("NutSync"));
    QApplication::setOrganizationName(QStringLiteral("NutSync"));
    QApplication::setOrganizationDomain(QStringLiteral("gomodb.github.io"));
    QApplication::setApplicationVersion(QStringLiteral(NUTSYNC_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("io.github.nutsync.NutSync"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icons/nutsync.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("适用于 KDE 的 WebDAV 双向同步客户端"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption hiddenOption(
        QStringLiteral("hidden"),
        QObject::tr("启动后隐藏主窗口（仅在有系统托盘时生效）"));
    parser.addOption(hiddenOption);
    parser.process(application);

    NutSync::Logger::install();
    NutSync::AppPaths::ensureDirectories();

    QLockFile lockFile(NutSync::AppPaths::configDir() + QStringLiteral("/nutsync.lock"));
    lockFile.setStaleLockTime(30000);
    if (!lockFile.tryLock(100)) {
        QMessageBox::information(
            nullptr, QObject::tr("NutSync 已在运行"),
            QObject::tr("另一个 NutSync 实例已经启动。\n"
                        "请使用系统托盘中的图标打开主窗口。"));
        return 1;
    }

    NutSync::MainWindow window;
    if (!parser.isSet(hiddenOption) && !window.shouldStartHidden())
        window.show();
    else if (!window.hasTray())
        window.show();

    return application.exec();
}
