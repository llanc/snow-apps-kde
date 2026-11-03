#include "screenshotportal.h"
#include "screenshotcaptureworker.h"
#include "snow_shot/platform/windowcaptureexclusion.h"
#include "snow_shot/presentation/screenshotcapturecoordinator.h"
#include "presentation/pinned/livewindownative_p.h"
#include "waylandoverlay.h"

#include <QApplication>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <utility>

using snow_shot::platform::ScreenshotPortal;

// The isolated fixture only needs the real coordinator's signals, not its Rust worker.
ScreenshotCaptureCoordinator::ScreenshotCaptureCoordinator(QObject* parent) : QObject(parent) {}
ScreenshotCaptureCoordinator::~ScreenshotCaptureCoordinator() = default;

class FakeScreenshotPortal final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")

  public:
    uint response = 0;
    QString uri = QStringLiteral("file:///tmp/portal%20image.png");
    bool replyImmediately = true;
    bool returnDifferentHandle = false;

  public slots:
    QDBusObjectPath Screenshot(const QString&, const QVariantMap& options) {
        QString sender = message().service();
        sender.remove(0, 1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender +
                             QLatin1Char('/') +
                             options.value(QStringLiteral("handle_token")).toString();
        if (replyImmediately) {
            QVariantMap results;
            results.insert(QStringLiteral("uri"), uri);
            QDBusMessage signal = QDBusMessage::createSignal(
                path, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"));
            signal << response << results;
            // The signal goes out before the method reply, exercising the race.
            connection().send(signal);
        }
        return QDBusObjectPath(returnDifferentHandle ? path + QStringLiteral("_different") : path);
    }
};

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

struct Completion {
    explicit Completion(ScreenshotPortal& portal) {
        QObject::connect(&portal, &ScreenshotPortal::finished, &portal,
                         [this](ScreenshotPortal::Result value, const QUrl& imageUri) {
                             ++count;
                             result = value;
                             uri = imageUri;
                         });
    }

    bool wait(ScreenshotPortal& portal, int expectedCount) const {
        if (count >= expectedCount)
            return true;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&portal, &ScreenshotPortal::finished, &loop, &QEventLoop::quit);
        timer.start(3000);
        loop.exec();
        return count >= expectedCount;
    }

    int count = 0;
    ScreenshotPortal::Result result = ScreenshotPortal::Result::Failed;
    QUrl uri;
};
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QDBusConnection server =
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-portal"));
    FakeScreenshotPortal fake;
    require(server.isConnected(), "private D-Bus connection failed");
    require(server.registerService(QStringLiteral("org.freedesktop.portal.Desktop")),
            "fake portal service registration failed");
    require(server.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), &fake,
                                  QDBusConnection::ExportAllSlots),
            "fake portal object registration failed");

    {
        QWidget window;
        window.setAttribute(Qt::WA_ShowWithoutActivating, true);
        snow_shot::platform::configureWaylandScreenshotOverlay(window, QStringLiteral("offscreen"));
        require(!window.windowState().testFlag(Qt::WindowFullScreen) &&
                    window.testAttribute(Qt::WA_ShowWithoutActivating),
                "non-Wayland overlays must keep their original window state");
        snow_shot::platform::configureWaylandScreenshotOverlay(window, QStringLiteral("wayland"));
        require(window.windowState().testFlag(Qt::WindowFullScreen) &&
                    !window.testAttribute(Qt::WA_ShowWithoutActivating),
                "Wayland overlays must be full screen and accept focus");
        require(!snow_shot::platform::setWindowExcludedFromCapture(&window, true) &&
                    !snow_shot::platform::captureWindowId(&window).has_value(),
                "unsupported window exclusion must not claim to be active");
    }

    {
        using namespace snow_shot::presentation;
        const LiveWindowTarget target;
        require(!liveWindowTargetAtPoint(QPoint{}, {}).has_value() &&
                    !live_window_detail::createNativeWindowCapture(target) &&
                    !live_window_detail::createNativeWindowMonitor(target) &&
                    !live_window_detail::createNativeWindowPresentation(target),
                "unsupported live window capture must not report a native target");
    }

    {
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(2000), "portal request did not start");
        require(completion.wait(portal, 1), "early response was lost");
        require(completion.result == ScreenshotPortal::Result::Success, "capture should succeed");
        require(completion.uri.toLocalFile() == QStringLiteral("/tmp/portal image.png"),
                "portal URI was not decoded correctly");
    }
    for (const auto& [response, expected] : {std::pair{1U, ScreenshotPortal::Result::Cancelled},
                                             std::pair{2U, ScreenshotPortal::Result::Failed}}) {
        fake.response = response;
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(2000), "portal request did not start");
        require(completion.wait(portal, 1), "portal response timed out");
        require(completion.result == expected, "wrong portal failure result");
    }
    {
        fake.replyImmediately = false;
        fake.returnDifferentHandle = true;
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(2000), "portal request did not start");
        require(completion.wait(portal, 1), "unexpected portal handle did not finish");
        require(completion.result == ScreenshotPortal::Result::Failed,
                "a portal request with an unexpected handle must fail");
        fake.returnDifferentHandle = false;
        fake.replyImmediately = true;
    }
    {
        fake.response = 0;
        fake.uri = QStringLiteral("https://example.invalid/capture.png");
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(2000), "portal request did not start");
        require(completion.wait(portal, 1), "portal response timed out");
        require(completion.result == ScreenshotPortal::Result::Failed,
                "non-local screenshot URI must be rejected");
    }
    {
        fake.uri = QStringLiteral("file:///tmp/portal%20image.png");
        fake.replyImmediately = false;
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(50), "portal request did not start");
        require(!portal.start(50), "overlapping portal request must be rejected");
        require(completion.wait(portal, 1), "portal timeout did not fire");
        require(completion.result == ScreenshotPortal::Result::TimedOut, "wrong timeout result");
        fake.replyImmediately = true;
        require(portal.start(2000), "portal request did not restart");
        require(completion.wait(portal, 2), "portal retry timed out");
        require(completion.result == ScreenshotPortal::Result::Success, "portal retry failed");
    }
    {
        fake.replyImmediately = false;
        ScreenshotPortal portal;
        Completion completion(portal);
        require(portal.start(2000), "portal request did not start");
        portal.cancel();
        require(completion.count == 1 && completion.result == ScreenshotPortal::Result::Cancelled,
                "portal cancellation did not complete");
        fake.replyImmediately = true;
        require(portal.start(2000), "cancelled portal request did not restart");
        require(completion.wait(portal, 2), "portal retry after cancellation timed out");
        require(completion.result == ScreenshotPortal::Result::Success, "portal retry failed");
    }
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory creation failed");
        const QString path = dir.filePath(QStringLiteral("capture.png"));
        QImage image(40, 30, QImage::Format_ARGB32);
        image.fill(Qt::red);
        require(image.save(path), "fixture screenshot could not be saved");
        fake.uri = QUrl::fromLocalFile(path).toString();
        fake.replyImmediately = true;

        ScreenshotCaptureCoordinator coordinator;
        ScreenshotCaptureWorker worker;
        QVector<ScreenshotCaptureLayout> layouts;
        QVector<ScreenshotCaptureResult> results;
        QObject::connect(
            &coordinator, &ScreenshotCaptureCoordinator::layoutReady, &coordinator,
            [&](ScreenshotCaptureLayout layout) { layouts.push_back(std::move(layout)); });
        QObject::connect(
            &coordinator, &ScreenshotCaptureCoordinator::captureFinished, &coordinator,
            [&](ScreenshotCaptureResult result) { results.push_back(std::move(result)); });
        ScreenshotCaptureRequest request;
        request.requestId = 42;
        const ScreenshotCaptureScreen screen{QStringLiteral("test-screen"), QRect(0, 0, 40, 30)};
        std::atomic_bool cancelled = false;
        worker.capture(request, &coordinator, nullptr, &screen, &cancelled);
        QCoreApplication::processEvents();
        require(layouts.size() == 1 && results.size() == 1 && results.last().succeeded,
                "portal capture should publish a layout and an image");
        require(results.last().requestId == 42 && results.last().displays.size() == 1 &&
                    results.last().displays.first().image.pixelColor(0, 0) == QColor(Qt::red),
                "portal screenshot pixels or request identity were lost");
        require(layouts.first().displays.first().stableId ==
                        results.first().displays.first().stableId &&
                    layouts.first().displays.first().physicalRect == screen.physicalRect,
                "portal layout must match its image");

        const ScreenshotCaptureScreen wrongSize{QStringLiteral("test-screen"), QRect(0, 0, 41, 30)};
        worker.capture(request, &coordinator, nullptr, &wrongSize, &cancelled);
        QCoreApplication::processEvents();
        require(results.size() == 2 && !results.last().succeeded && layouts.size() == 1,
                "mismatched screenshot dimensions must be rejected");

        worker.capture(request, &coordinator, nullptr, nullptr, &cancelled);
        QCoreApplication::processEvents();
        require(results.size() == 3 && !results.last().succeeded,
                "missing or multiple screens must be rejected");

        fake.replyImmediately = false;
        QTimer::singleShot(10, [&] { cancelled.store(true, std::memory_order_relaxed); });
        worker.capture(request, &coordinator, nullptr, &screen, &cancelled);
        QCoreApplication::processEvents();
        require(results.size() == 4 && !results.last().succeeded && layouts.size() == 1,
                "cancelling a pending capture must not publish an image");
    }
    return 0;
}

#include "linux_screenshot_portal_tests.moc"
