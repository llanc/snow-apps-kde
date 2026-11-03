#include "globalshortcutbackend_p.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdlib>

using namespace snow_shot::presentation;
using snow_shot::shortcuts::ShortcutBinding;

class FakeGlobalAccel final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KGlobalAccel")

  public:
    bool occupied = false;
    bool refuseAssignment = false;
    uint lastFlags = 0;
    QStringList lastAction;
    QList<int> lastKeys;
    QString componentPath = QStringLiteral("/component/snow_shot");

  public slots:
    bool isGlobalShortcutAvailable(int, const QString&) const {
        return !occupied;
    }
    void doRegister(const QStringList& action) {
        lastAction = action;
    }
    QList<int> setShortcut(const QStringList& action, const QList<int>& keys, uint flags) {
        lastAction = action;
        lastKeys = keys;
        lastFlags = flags;
        return occupied || refuseAssignment ? QList<int>{} : keys;
    }
    QDBusObjectPath getComponent(const QString&) const {
        return QDBusObjectPath(componentPath);
    }
    bool unregister(const QString&, const QString&) {
        return true;
    }
};

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

template <typename Predicate> bool waitFor(Predicate predicate) {
    QEventLoop loop;
    QTimer timeout;
    QTimer poll;
    timeout.setSingleShot(true);
    poll.setInterval(1);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (predicate())
            loop.quit();
    });
    timeout.start(3000);
    poll.start();
    if (!predicate())
        loop.exec();
    return predicate();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    QThread serverThread;
    FakeGlobalAccel fake;
    fake.moveToThread(&serverThread);
    serverThread.start();
    QDBusConnection server = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                           QStringLiteral("fake-kglobalaccel"));
    require(server.isConnected(), "private D-Bus connection failed");
    require(server.registerObject(QStringLiteral("/kglobalaccel"), &fake,
                                  QDBusConnection::ExportAllSlots),
            "fake global shortcut object registration failed");

    {
        auto coldBackend = createKdeGlobalShortcutBackend();
        bool availabilityNotified = false;
        coldBackend->setAvailabilityChangedHandler(
            [&](const QList<int>&) { availabilityNotified = true; });
        require(server.registerService(QStringLiteral("org.kde.kglobalaccel")),
                "fake global shortcut service registration failed");
        require(waitFor([&] { return availabilityNotified; }),
                "a late shortcut service must request a registration retry");
        require(coldBackend->registerShortcut(1, ShortcutBinding{QStringLiteral("Ctrl+Shift+F9")})
                    .registered,
                "registration must recover if the shortcut service starts late");
        coldBackend->unregisterShortcut(1);
    }
    auto backend = createKdeGlobalShortcutBackend();
    require(!backend->validateShortcut(ShortcutBinding{QStringLiteral("Ctrl")}).supported,
            "modifier-only shortcut must be rejected");
    const ShortcutBinding binding{QStringLiteral("Ctrl+Shift+F9")};
    require(backend->validateShortcut(binding).supported, "valid shortcut was rejected");
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");
    require(backend->registerShortcut(1, binding).failureReason ==
                GlobalShortcutFailureReason::UnsupportedPlatform,
            "KDE shortcut backend must not claim support on other desktops");
    qputenv("XDG_CURRENT_DESKTOP", "KDE");
    QMetaObject::invokeMethod(&fake, [&] { fake.occupied = true; }, Qt::BlockingQueuedConnection);
    require(backend->registerShortcut(1, binding).failureReason ==
                GlobalShortcutFailureReason::AlreadyInUse,
            "occupied shortcut must not be taken");
    QMetaObject::invokeMethod(&fake, [&] { fake.occupied = false; }, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(
        &fake, [&] { fake.refuseAssignment = true; }, Qt::BlockingQueuedConnection);
    require(backend->registerShortcut(1, binding).failureReason ==
                GlobalShortcutFailureReason::AlreadyInUse,
            "a shortcut rejected after the availability check must not be reported as registered");
    QMetaObject::invokeMethod(
        &fake, [&] { fake.refuseAssignment = false; }, Qt::BlockingQueuedConnection);
    require(backend->registerShortcut(1, binding).registered, "shortcut registration failed");
    require(backend->registerShortcut(2, binding).failureReason ==
                GlobalShortcutFailureReason::AlreadyInUse,
            "duplicate shortcut must be rejected");
    QMetaObject::invokeMethod(
        &fake,
        [&] {
            require(fake.lastFlags == 6U && fake.lastAction.size() == 4 &&
                        fake.lastKeys.size() == 1,
                    "incorrect global shortcut registration request");
        },
        Qt::BlockingQueuedConnection);

    int activated = 0;
    backend->setActivationHandler([&](int id) { activated = id; });
    auto activation = QDBusMessage::createSignal(QStringLiteral("/component/snow_shot"),
                                                 QStringLiteral("org.kde.kglobalaccel.Component"),
                                                 QStringLiteral("globalShortcutPressed"));
    activation << QStringLiteral("snow-shot") << QStringLiteral("_k_session:snow-shot-1")
               << qlonglong(0);
    require(server.send(activation), "could not emit shortcut activation");
    require(waitFor([&] { return activated == 1; }), "shortcut activation was not delivered");

    QList<int> invalidated;
    backend->setAvailabilityChangedHandler([&](const QList<int>& ids) { invalidated = ids; });
    const QStringList action{QStringLiteral("snow-shot"), QStringLiteral("_k_session:snow-shot-1"),
                             QStringLiteral("Snow Shot"), binding.portableText};
    auto changed = QDBusMessage::createSignal(QStringLiteral("/kglobalaccel"),
                                              QStringLiteral("org.kde.KGlobalAccel"),
                                              QStringLiteral("yourShortcutGotChanged"));
    changed << action << QVariant::fromValue(QList<int>{});
    require(server.send(changed), "could not emit shortcut change");
    require(waitFor([&] { return invalidated.contains(1); }),
            "external shortcut changes must invalidate the registration");

    invalidated.clear();
    bool availabilityNotified = false;
    backend->setAvailabilityChangedHandler([&](const QList<int>& ids) {
        invalidated = ids;
        availabilityNotified = true;
    });
    require(server.unregisterService(QStringLiteral("org.kde.kglobalaccel")),
            "could not stop the fake shortcut service");
    require(waitFor([&] { return availabilityNotified; }) && invalidated.contains(1),
            "service loss must invalidate active registrations");
    backend->unregisterShortcut(1);
    availabilityNotified = false;
    QMetaObject::invokeMethod(
        &fake, [&] { fake.componentPath = QStringLiteral("/component/snow_shot_new"); },
        Qt::BlockingQueuedConnection);
    require(server.registerService(QStringLiteral("org.kde.kglobalaccel")),
            "could not restart the fake shortcut service");
    require(waitFor([&] { return availabilityNotified; }),
            "service restart must request a registration retry");
    require(backend->registerShortcut(1, binding).registered,
            "shortcut registration must recover after a service restart");
    activated = 0;
    auto recoveredActivation = QDBusMessage::createSignal(
        QStringLiteral("/component/snow_shot_new"),
        QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("globalShortcutPressed"));
    recoveredActivation << QStringLiteral("snow-shot") << QStringLiteral("_k_session:snow-shot-1")
                        << qlonglong(0);
    require(server.send(recoveredActivation), "could not emit recovered shortcut activation");
    require(waitFor([&] { return activated == 1; }),
            "shortcut activation must recover if the service changes the component path");
    backend->unregisterShortcut(1);
    activated = 0;
    require(server.send(activation), "could not emit shortcut activation");
    require(!waitFor([&] { return activated != 0; }), "unregistered shortcuts must not trigger");

    backend.reset();
    server.unregisterService(QStringLiteral("org.kde.kglobalaccel"));
    QMetaObject::invokeMethod(
        &fake, [&] { fake.moveToThread(app.thread()); }, Qt::BlockingQueuedConnection);
    serverThread.quit();
    serverThread.wait();
    return 0;
}

#include "linux_global_shortcut_tests.moc"
