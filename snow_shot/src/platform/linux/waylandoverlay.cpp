#include "waylandoverlay.h"

#include <QWidget>
#include <QWindow>

namespace snow_shot::platform {
void configureWaylandScreenshotOverlay(QWidget& window, const QString& platformName) {
    if (!platformName.startsWith(QLatin1String("wayland")))
        return;
    // Wayland does not let clients position normal top-level windows. Let the
    // compositor place the screenshot surface on its selected screen instead.
    window.setWindowState(window.windowState() | Qt::WindowFullScreen);
    // A pooled widget can retain its state after its native window is recreated.
    if (QWindow* surface = window.windowHandle()) {
        surface->setWindowState(Qt::WindowFullScreen);
    }
    window.setAttribute(Qt::WA_ShowWithoutActivating, false);
}
} // namespace snow_shot::platform
