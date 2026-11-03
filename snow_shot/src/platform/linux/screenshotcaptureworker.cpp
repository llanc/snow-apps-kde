#include "../../presentation/capture/screenshotcaptureworker.h"
#include "screenshotportal.h"
#include "snow_shot/presentation/screenshotcapturecoordinator.h"

#include <QDBusConnection>
#include <QDebug>
#include <QEventLoop>
#include <QImageReader>
#include <QMetaObject>
#include <QTimer>

#include <utility>

ScreenshotCaptureWorker::~ScreenshotCaptureWorker() = default;

void ScreenshotCaptureWorker::prepare(quint64 requestId,
                                      const QPointer<ScreenshotCaptureCoordinator>& coordinator) {
    postPrepared(requestId, coordinator, QDBusConnection::sessionBus().isConnected());
}

void ScreenshotCaptureWorker::refreshLayout(
    quint64 requestId, const QPointer<ScreenshotCaptureCoordinator>& coordinator) {
    if (!coordinator)
        return;
    const bool ok = QDBusConnection::sessionBus().isConnected();
    QMetaObject::invokeMethod(
        coordinator,
        [coordinator, requestId, ok] {
            if (coordinator)
                emit coordinator->layoutRefreshed(requestId, ok);
        },
        Qt::QueuedConnection);
}

void ScreenshotCaptureWorker::capture(const ScreenshotCaptureRequest& request,
                                      const QPointer<ScreenshotCaptureCoordinator>& coordinator,
                                      SnowCaptureCancellationToken*,
                                      const ScreenshotCaptureScreen* screen,
                                      const std::atomic_bool* cancelled) {
    ScreenshotCaptureResult result;
    result.requestId = request.requestId;
    result.purpose = request.purpose;
    if (screen == nullptr || !screen->physicalRect.isValid() ||
        request.purpose != ScreenshotCapturePurpose::Initial ||
        (cancelled != nullptr && cancelled->load(std::memory_order_relaxed))) {
        qWarning("Linux portal capture requires one screen and an initial capture request");
        postCaptureResult(coordinator, std::move(result));
        return;
    }

    snow_shot::platform::ScreenshotPortal portal;
    using PortalResult = snow_shot::platform::ScreenshotPortal::Result;
    PortalResult portalResult = PortalResult::Failed;
    QUrl imageUri;
    bool done = false;
    QEventLoop loop;
    connect(&portal, &snow_shot::platform::ScreenshotPortal::finished, &loop,
            [&](PortalResult value, const QUrl& uri) {
                portalResult = value;
                imageUri = uri;
                done = true;
                loop.quit();
            });
    QTimer cancellationTimer;
    cancellationTimer.setInterval(50);
    connect(&cancellationTimer, &QTimer::timeout, &portal, [&] {
        if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed))
            portal.cancel();
    });
    if (!portal.start()) {
        qWarning("Could not start the desktop screenshot portal request");
        postCaptureResult(coordinator, std::move(result));
        return;
    }
    cancellationTimer.start();
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed))
        portal.cancel();
    if (!done)
        loop.exec();
    cancellationTimer.stop();
    if (portalResult != PortalResult::Success ||
        (cancelled != nullptr && cancelled->load(std::memory_order_relaxed))) {
        postCaptureResult(coordinator, std::move(result));
        return;
    }

    QImageReader reader(imageUri.toLocalFile());
    if (reader.size() != screen->physicalRect.size()) {
        qWarning("Portal screenshot dimensions do not match the selected display");
        postCaptureResult(coordinator, std::move(result));
        return;
    }
    QImage image = reader.read();
    if (image.isNull() || image.size() != screen->physicalRect.size()) {
        qWarning("Could not read the desktop portal screenshot");
        postCaptureResult(coordinator, std::move(result));
        return;
    }

    CapturedDisplayModel display;
    display.name = screen->name;
    display.stableId = QStringLiteral("linux:") + screen->name;
    display.physicalRect = screen->physicalRect;
    display.canvasRect = display.physicalRect;
    display.primary = true;
    display.active = true;

    ScreenshotCaptureLayout layout;
    layout.requestId = request.requestId;
    layout.generation = request.requestId;
    layout.displays.push_back(display);
    if (coordinator) {
        QMetaObject::invokeMethod(
            coordinator,
            [coordinator, layout = std::move(layout)] {
                if (coordinator)
                    emit coordinator->layoutReady(layout);
            },
            Qt::QueuedConnection);
    }
    display.image = std::move(image);
    result.displays.push_back(std::move(display));
    result.succeeded = true;
    postCaptureResult(coordinator, std::move(result));
}

void ScreenshotCaptureWorker::postPrepared(
    quint64 requestId, const QPointer<ScreenshotCaptureCoordinator>& coordinator, bool ok) {
    if (!coordinator)
        return;
    QMetaObject::invokeMethod(
        coordinator,
        [coordinator, requestId, ok] {
            if (coordinator)
                emit coordinator->prepared(requestId, ok);
        },
        Qt::QueuedConnection);
}

void ScreenshotCaptureWorker::postCaptureResult(
    const QPointer<ScreenshotCaptureCoordinator>& coordinator, ScreenshotCaptureResult result) {
    if (!coordinator)
        return;
    QMetaObject::invokeMethod(
        coordinator,
        [coordinator, result = std::move(result)]() mutable {
            if (coordinator)
                emit coordinator->captureFinished(std::move(result));
        },
        Qt::QueuedConnection);
}
