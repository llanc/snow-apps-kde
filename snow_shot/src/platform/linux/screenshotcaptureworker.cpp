#include "../../presentation/capture/screenshotcaptureworker.h"
#include "screenshotportal.h"
#include "snow_shot/presentation/screenshotcapturecoordinator.h"

#include <QDBusConnection>
#include <QDebug>
#include <QEventLoop>
#include <QImageReader>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
bool isFullDisplayImage(const QSize& image, const QSize& logical) {
    if (!image.isValid() || !logical.isValid() || image.isEmpty() || logical.isEmpty())
        return false;
    const double scaleX = double(image.width()) / double(logical.width());
    const double scaleY = double(image.height()) / double(logical.height());
    // Qt may round the Wayland screen DPR up to an integer while the portal
    // captures the compositor's fractional output scale. Allow uniform scaling,
    // but reject images that do not have the full display's aspect ratio.
    return std::abs(scaleX - scaleY) <= std::max(scaleX, scaleY) * 0.005;
}
} // namespace

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
    if (screen == nullptr || !screen->physicalRect.isValid() || !screen->logicalRect.isValid() ||
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
    if (!isFullDisplayImage(reader.size(), screen->logicalRect.size())) {
        qWarning("Portal screenshot dimensions do not match the selected display");
        postCaptureResult(coordinator, std::move(result));
        return;
    }
    QImage image = reader.read();
    if (image.isNull() || !isFullDisplayImage(image.size(), screen->logicalRect.size())) {
        qWarning("Could not read the desktop portal screenshot");
        postCaptureResult(coordinator, std::move(result));
        return;
    }

    CapturedDisplayModel display;
    display.name = screen->name;
    display.stableId = QStringLiteral("linux:") + screen->name;
    display.physicalRect = QRect(screen->physicalRect.topLeft(), image.size());
    display.canvasRect = display.physicalRect;
    display.capturedLogicalRect = screen->logicalRect;
    display.backend = ScreenshotCaptureBackend::DesktopPortal;
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
