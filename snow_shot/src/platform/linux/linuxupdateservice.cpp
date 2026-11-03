#include "snow_shot/update/updateservice.h"

#include <QEvent>

namespace snow_shot::update {

struct UpdateService::Impl {
    UpdateStatus status;
};

UpdateService::UpdateService(Options, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
    setObjectName(QStringLiteral("snowShotUpdateService"));
}
UpdateService::~UpdateService() = default;
const UpdateStatus& UpdateService::status() const {
    return m_impl->status;
}
bool UpdateService::managedByStore() const {
    return false;
}
bool UpdateService::busy() const {
    return false;
}
bool UpdateService::handoffPending() const {
    return false;
}
void UpdateService::start() {}
void UpdateService::setMode(const QString&) {}
void UpdateService::setSystemProxy(bool) {}
void UpdateService::setProgressAppearance(const QJsonObject&) {}
void UpdateService::check(bool) {
    emit operationFinished(QStringLiteral("check"), QStringLiteral("unavailable"));
}
void UpdateService::download() {}
void UpdateService::cancel() {}
void UpdateService::requestRestart() {}
void UpdateService::beginApply() {}
void UpdateService::applyAtStartup() {
    emit operationFinished(QStringLiteral("apply"), QStringLiteral("success"));
}
void UpdateService::setRelaunchArguments(const QStringList&) {}
void UpdateService::clearRelaunchArguments() {}
void UpdateService::reportBlocked(const QString&) {}
bool UpdateService::event(QEvent* event) {
    return QObject::event(event);
}

} // namespace snow_shot::update
