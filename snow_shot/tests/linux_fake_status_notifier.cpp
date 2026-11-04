#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QStringList>

#include <cstdio>

class FakeStatusNotifierWatcher final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered CONSTANT)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion CONSTANT)
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)

  public:
    bool hostRegistered() const {
        return true;
    }
    int protocolVersion() const {
        return 0;
    }
    QStringList registeredItems() const {
        return m_items;
    }

  public slots:
    void RegisterStatusNotifierItem(const QString& item) {
        const QString service = item.startsWith(QLatin1Char('/')) ? message().service() : item;
        const QString path =
            item.startsWith(QLatin1Char('/')) ? item : QStringLiteral("/StatusNotifierItem");
        const QString entry = service + path;
        if (!m_items.contains(entry))
            m_items.append(entry);
        std::printf("registered %s %s\n", qPrintable(service), qPrintable(path));
        std::fflush(stdout);
        emit StatusNotifierItemRegistered(entry);
    }
    void RegisterStatusNotifierHost(const QString&) {}

  signals:
    void StatusNotifierItemRegistered(const QString& service);
    void StatusNotifierItemUnregistered(const QString& service);
    void StatusNotifierHostRegistered();

  private:
    QStringList m_items;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    FakeStatusNotifierWatcher watcher;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.kde.StatusNotifierWatcher")) ||
        !bus.registerObject(QStringLiteral("/StatusNotifierWatcher"), &watcher,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals |
                                QDBusConnection::ExportAllProperties))
        return 1;
    std::puts("ready");
    std::fflush(stdout);
    return app.exec();
}

#include "linux_fake_status_notifier.moc"
