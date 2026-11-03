#include "waylandoverlay.h"

#include <QWidget>

namespace snow_shot::platform {
void configureWaylandScreenshotOverlay(QWidget& window, const QString& platformName) {
    if (!platformName.startsWith(QLatin1String("wayland")))
        return;
    // Wayland does not let clients position normal top-level windows. Let the
    // compositor place the screenshot surface on its selected screen instead.
    window.setWindowState(window.windowState() | Qt::WindowFullScreen);
    window.setAttribute(Qt::WA_ShowWithoutActivating, false);
}
} // namespace snow_shot::platform
