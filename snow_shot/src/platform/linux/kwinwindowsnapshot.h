#ifndef SNOW_SHOT_PLATFORM_LINUX_KWINWINDOWSNAPSHOT_H
#define SNOW_SHOT_PLATFORM_LINUX_KWINWINDOWSNAPSHOT_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace snow_shot::platform {
struct KWinWindowSnapshot {
    // Output-relative fractions, independent of the Qt and portal image scales.
    QPointF cursor;
    QVector<QRectF> windows; // Front to back.
};

std::optional<KWinWindowSnapshot> snapshotKWinWindows(const QString& outputName);
} // namespace snow_shot::platform

#endif // SNOW_SHOT_PLATFORM_LINUX_KWINWINDOWSNAPSHOT_H
