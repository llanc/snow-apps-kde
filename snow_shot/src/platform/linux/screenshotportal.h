#ifndef SNOW_SHOT_PLATFORM_LINUX_SCREENSHOTPORTAL_H
#define SNOW_SHOT_PLATFORM_LINUX_SCREENSHOTPORTAL_H

#include <QDBusConnection>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace snow_shot::platform {

// The portal decides whether and how to ask for permission. Subscribe to the
// request's response before calling Screenshot, since a response can precede
// the method reply on D-Bus.
class ScreenshotPortal final : public QObject {
    Q_OBJECT

  public:
    enum class Result { Success, Cancelled, Failed, TimedOut };
    Q_ENUM(Result)

    explicit ScreenshotPortal(QDBusConnection connection = QDBusConnection::sessionBus(),
                              QObject* parent = nullptr);
    ~ScreenshotPortal() override;

    [[nodiscard]] bool start(int timeoutMs = 120000);
    void cancel();

  signals:
    void finished(snow_shot::platform::ScreenshotPortal::Result result, const QUrl& imageUri);

  private slots:
    void handleResponse(uint response, const QVariantMap& results);

  private:
    void finish(Result result, const QUrl& imageUri = {}, bool closeRequest = false);
    void closeRequest(const QString& requestPath);
    void disconnectResponse();

    QDBusConnection m_connection;
    QTimer m_timeout;
    QString m_requestPath;
    quint64 m_generation = 0;
    bool m_pending = false;
};

} // namespace snow_shot::platform

Q_DECLARE_METATYPE(snow_shot::platform::ScreenshotPortal::Result)

#endif // SNOW_SHOT_PLATFORM_LINUX_SCREENSHOTPORTAL_H
