#include "editing/worker_memory_limit.h"

#include <algorithm>
#include <limits>

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace snow::image_viewer {
bool applyWorkerMemoryLimit(qulonglong bytes, bool initialize) {
    if (bytes == 0)
        return false;
#if defined(Q_OS_WIN)
    Q_UNUSED(initialize);
    static HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit =
        static_cast<SIZE_T>(std::min<qulonglong>(bytes, std::numeric_limits<SIZE_T>::max()));
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return false;
    static bool assigned = false;
    if (!assigned) {
        if (!AssignProcessToJobObject(job, GetCurrentProcess()))
            return false;
        assigned = true;
    }
#else
#if defined(Q_OS_LINUX)
    // File-backed raster mappings are separately budgeted. RLIMIT_AS also counts
    // those mappings and the loaded libraries, so it cannot represent this limit.
    constexpr int resource = RLIMIT_DATA;
#else
    constexpr int resource = RLIMIT_AS;
#endif
    rlimit limit{};
    if (getrlimit(resource, &limit) != 0)
        return false;
    const rlim_t requested =
        static_cast<rlim_t>(std::min<qulonglong>(bytes, std::numeric_limits<rlim_t>::max()));
    if (initialize)
        limit.rlim_max = std::min(requested, limit.rlim_max);
    limit.rlim_cur = std::min(requested, limit.rlim_max);
    if (setrlimit(resource, &limit) != 0)
        return false;
#endif
    return true;
}
} // namespace snow::image_viewer
