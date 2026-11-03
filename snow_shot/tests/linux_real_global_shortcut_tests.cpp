#include "globalshortcutbackend_p.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (qgetenv("SNOW_LINUX_PRIVATE_TEST_BUS") != "1" || qgetenv("XDG_CURRENT_DESKTOP") != "KDE") {
        std::fputs("This test must run in the isolated virtual KDE session\n", stderr);
        return 2;
    }
    using namespace snow_shot::presentation;
    QElapsedTimer timer;
    timer.start();
    auto* bus = QDBusConnection::sessionBus().interface();
    while (bus && !bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel")) &&
           timer.elapsed() < 10000) {
        QThread::msleep(50);
    }
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel"))) {
        std::fputs("The virtual compositor did not provide the KDE shortcut service\n", stderr);
        return 1;
    }
    const snow_shot::shortcuts::ShortcutBinding binding{QStringLiteral("Ctrl+Alt+Shift+F12")};
    auto backend = createKdeGlobalShortcutBackend();
    const auto result = backend->registerShortcut(101, binding);
    if (!result.registered) {
        std::fprintf(stderr, "KDE rejected the test shortcut, reason: %d\n",
                     int(result.failureReason));
        return 1;
    }
    backend->unregisterShortcut(101);
    const auto again = backend->registerShortcut(102, binding);
    if (!again.registered) {
        std::fprintf(stderr, "KDE did not release the test shortcut, reason: %d\n",
                     int(again.failureReason));
        return 1;
    }
    backend->unregisterShortcut(102);
    return 0;
}
