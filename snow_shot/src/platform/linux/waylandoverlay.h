#ifndef SNOW_SHOT_PLATFORM_LINUX_WAYLANDOVERLAY_H
#define SNOW_SHOT_PLATFORM_LINUX_WAYLANDOVERLAY_H

#include <QString>

class QWidget;

namespace snow_shot::platform {
void configureWaylandScreenshotOverlay(QWidget& window, const QString& platformName);
} // namespace snow_shot::platform

#endif // SNOW_SHOT_PLATFORM_LINUX_WAYLANDOVERLAY_H
