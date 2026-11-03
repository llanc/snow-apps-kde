#include "snow_shot/platform/windowcaptureexclusion.h"

namespace snow_shot::platform {

bool setWindowExcludedFromCapture(QWidget*, bool) {
    return false;
}

std::optional<std::uint32_t> captureWindowId(QWidget*) {
    return std::nullopt;
}

} // namespace snow_shot::platform
