#include "editing/worker_memory_limit.h"

#include <QTemporaryDir>

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr rlim_t kMiB = 1024U * 1024U;

int exerciseMemoryLimit(const char* path) {
    rlimit inherited{};
    if (getrlimit(RLIMIT_DATA, &inherited) != 0 || inherited.rlim_max < 96U * kMiB)
        return 1;
    const rlimit restricted{96U * kMiB, 96U * kMiB};
    if (setrlimit(RLIMIT_DATA, &restricted) != 0 ||
        !snow::image_viewer::applyWorkerMemoryLimit(128U * kMiB, true))
        return 2;
    rlimit actual{};
    if (getrlimit(RLIMIT_DATA, &actual) != 0 || actual.rlim_max != restricted.rlim_max ||
        actual.rlim_cur != restricted.rlim_cur ||
        !snow::image_viewer::applyWorkerMemoryLimit(64U * kMiB))
        return 3;

    const int file = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (file < 0 || ftruncate(file, 256U * kMiB) != 0)
        return 4;
    void* shared = mmap(nullptr, 256U * kMiB, PROT_READ, MAP_SHARED, file, 0);
    close(file);
    if (shared == MAP_FAILED)
        return 5;
    void* privateMemory =
        mmap(nullptr, 128U * kMiB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool rejected = privateMemory == MAP_FAILED && errno == ENOMEM;
    if (privateMemory != MAP_FAILED)
        munmap(privateMemory, 128U * kMiB);
    munmap(shared, 256U * kMiB);
    if (!rejected || !snow::image_viewer::applyWorkerMemoryLimit(96U * kMiB))
        return 6;
    if (getrlimit(RLIMIT_DATA, &actual) != 0 || actual.rlim_cur != 96U * kMiB)
        return 7;
    return 0;
}
} // namespace

int main() {
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    const QByteArray path = directory.filePath(QStringLiteral("raster.bin")).toLocal8Bit();
    const pid_t child = fork();
    if (child == 0)
        _exit(exerciseMemoryLimit(path.constData()));
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        std::fprintf(stderr, "Linux worker memory-limit test failed (status %d)\n", status);
        return 1;
    }
    return 0;
}
