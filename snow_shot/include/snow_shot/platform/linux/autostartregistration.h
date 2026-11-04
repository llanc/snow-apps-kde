#ifndef SNOW_SHOT_PLATFORM_LINUX_AUTOSTARTREGISTRATION_H
#define SNOW_SHOT_PLATFORM_LINUX_AUTOSTARTREGISTRATION_H

#include <QString>

namespace snow_shot::platform::linux_desktop {

struct AutostartResult {
    bool success = false;
    QString error;
};

class AutostartRegistration final {
  public:
    [[nodiscard]] static QString configHome();
    [[nodiscard]] static QString entryPath(const QString& configHome);
    [[nodiscard]] static bool isEnabled(const QString& configHome);
    [[nodiscard]] static AutostartResult setEnabled(bool enabled, const QString& executable,
                                                    const QString& configHome);
    [[nodiscard]] static AutostartResult reconcile(bool enabled, const QString& executable,
                                                   const QString& configHome);
};

} // namespace snow_shot::platform::linux_desktop

#endif // SNOW_SHOT_PLATFORM_LINUX_AUTOSTARTREGISTRATION_H
