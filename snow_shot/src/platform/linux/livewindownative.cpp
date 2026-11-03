#include "../../presentation/pinned/livewindownative_p.h"

namespace snow_shot::presentation {
std::optional<LiveWindowTarget> liveWindowTargetAtPoint(const QPoint&, const QVector<quintptr>&) {
    return std::nullopt;
}

namespace live_window_detail {
std::unique_ptr<NativeWindowCapture> createNativeWindowCapture(const LiveWindowTarget&) {
    return nullptr;
}

std::unique_ptr<NativeWindowMonitor> createNativeWindowMonitor(const LiveWindowTarget&) {
    return nullptr;
}

std::unique_ptr<NativeWindowPresentation> createNativeWindowPresentation(const LiveWindowTarget&) {
    return nullptr;
}
} // namespace live_window_detail
} // namespace snow_shot::presentation
