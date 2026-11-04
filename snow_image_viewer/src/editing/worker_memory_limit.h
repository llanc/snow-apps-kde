#pragma once

#include <QtGlobal>

namespace snow::image_viewer {
bool applyWorkerMemoryLimit(qulonglong bytes, bool initialize = false);
}
