#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QCoreApplication>
#include <QImage>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <utility>

class FakeScreenshotPortal final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")

  public:
    explicit FakeScreenshotPortal(QString imagePath) : m_imagePath(std::move(imagePath)) {}

  public slots:
    QDBusObjectPath Screenshot(const QString&, const QVariantMap& options) {
        QString sender = message().service();
        sender.remove(0, 1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender +
                             QLatin1Char('/') +
                             options.value(QStringLiteral("handle_token")).toString();
        const QDBusConnection bus = connection();
        const QString imagePath = m_imagePath;
        QTimer::singleShot(0, this, [bus, imagePath, path] {
            QVariantMap results;
            results.insert(QStringLiteral("uri"), QUrl::fromLocalFile(imagePath).toString());
            QDBusMessage response = QDBusMessage::createSignal(
                path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
            response << 0U << results;
            bus.send(response);
        });
        return QDBusObjectPath(path);
    }

  private:
    QString m_imagePath;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 4)
        return 2;
    bool widthOk = false;
    bool heightOk = false;
    const QSize size(QString::fromLocal8Bit(argv[2]).toInt(&widthOk),
                     QString::fromLocal8Bit(argv[3]).toInt(&heightOk));
    if (!widthOk || !heightOk || !size.isValid())
        return 2;
    QImage image(size, QImage::Format_ARGB32);
    image.fill(QColor(42, 118, 194));
    const QString imagePath = QString::fromLocal8Bit(argv[1]);
    if (!image.save(imagePath))
        return 3;
    FakeScreenshotPortal portal(imagePath);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.freedesktop.portal.Desktop")) ||
        !bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &portal,
                            QDBusConnection::ExportAllSlots))
        return 4;
    std::puts("ready");
    std::fflush(stdout);
    return app.exec();
}

#include "linux_fake_screenshot_portal.moc"
