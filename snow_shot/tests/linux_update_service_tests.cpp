#include "snow_shot/update/updateservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryDir>

#include <cstdlib>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory creation failed");
    snow_shot::update::UpdateService::Options options;
    options.root = directory.path();
    options.applicationDirectory = directory.path();
    snow_shot::update::UpdateService service(options);
    QString lastOperation;
    QString lastOutcome;
    QObject::connect(&service, &snow_shot::update::UpdateService::operationFinished, &service,
                     [&](const QString& operation, const QString& outcome) {
                         lastOperation = operation;
                         lastOutcome = outcome;
                     });

    service.start();
    service.check();
    require(lastOperation == QStringLiteral("check") &&
                lastOutcome == QStringLiteral("unavailable"),
            "Linux update checks must complete as unavailable");
    service.download();
    service.beginApply();
    service.requestRestart();
    require(service.status().state == snow_shot::update::UpdateState::Unavailable &&
                !service.busy() && !service.handoffPending(),
            "Linux must not start the Windows updater or a handoff");
    service.applyAtStartup();
    require(lastOperation == QStringLiteral("apply") && lastOutcome == QStringLiteral("success"),
            "disabled startup updates must allow application startup");
    return 0;
}
