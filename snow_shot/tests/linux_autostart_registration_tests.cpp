#include "snow_shot/platform/linux/autostartregistration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>

using snow_shot::platform::linux_desktop::AutostartRegistration;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "test file could not be opened");
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "test file could not be written");
    require(file.write(content) == content.size(), "test file write was incomplete");
}

void registrationRespectsUserEntriesAndLaunches() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary autostart directory is unavailable");
    const QString root = directory.path();
    const QString config = root + QStringLiteral("/config");
    const QString programDir = root + QStringLiteral("/with % \" $ ` \\ spaces");
    require(QDir().mkpath(programDir), "test executable directory could not be created");
    const QString executable = programDir + QStringLiteral("/snow-shot");
    writeFile(executable, "#!/bin/sh\nprintf '%s' \"$1\" > \"$SNOW_AUTOSTART_TEST_MARKER\"\n");
    require(
        QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
        "test executable could not be made executable");
    const QString entry = AutostartRegistration::entryPath(config);
    require(!AutostartRegistration::isEnabled(config) &&
                AutostartRegistration::reconcile(false, executable, config).success &&
                !QFile::exists(entry),
            "disabled startup must not create an entry");
    require(AutostartRegistration::reconcile(true, executable, config).success &&
                AutostartRegistration::isEnabled(config),
            "an enabled preference must create a usable startup entry");
    const QByteArray original = readFile(entry);
    require(original.contains("X-SnowShotKDE-Managed=true") && original.contains(" --autostart\n"),
            "the startup entry must be identifiable and launch without opening a window");
    require(AutostartRegistration::reconcile(true, executable, config).success &&
                readFile(entry) == original,
            "startup reconciliation must preserve an existing managed entry");

    if (!QStandardPaths::findExecutable(QStringLiteral("gio")).isEmpty()) {
        const QString marker = root + QStringLiteral("/launched");
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("SNOW_AUTOSTART_TEST_MARKER"), marker);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), config);
        environment.insert(QStringLiteral("XDG_DATA_HOME"), root + QStringLiteral("/data"));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral("gio"), {QStringLiteral("launch"), entry});
        const bool finished = process.waitForFinished(15000);
        if (!finished || process.exitCode() != 0)
            std::cerr << process.readAllStandardError().constData();
        require(finished && process.exitCode() == 0, "gio must accept the startup entry");
        for (int attempt = 0; attempt < 100 && !QFile::exists(marker); ++attempt)
            QThread::msleep(50);
        require(QFile::exists(marker) && readFile(marker) == "--autostart",
                "the desktop entry must execute the exact path and startup argument");
    }

    const QByteArray hidden = original + "Hidden=true\n";
    writeFile(entry, hidden);
    require(!AutostartRegistration::isEnabled(config) &&
                AutostartRegistration::reconcile(true, executable, config).success &&
                readFile(entry) == hidden,
            "KDE's externally disabled startup entry must be preserved");
    require(AutostartRegistration::setEnabled(true, executable, config).success &&
                AutostartRegistration::isEnabled(config),
            "explicitly enabling startup must restore the managed entry");
    require(AutostartRegistration::setEnabled(false, executable, config).success &&
                !QFile::exists(entry),
            "explicitly disabling startup must remove the managed entry");
    writeFile(entry, "[Desktop Entry]\nName=Foreign\n");
    const QByteArray foreign = readFile(entry);
    require(!AutostartRegistration::setEnabled(true, executable, config).success &&
                !AutostartRegistration::setEnabled(false, executable, config).success &&
                readFile(entry) == foreign,
            "a user-owned startup entry must never be replaced or removed");
    require(QFile::remove(entry) && QFile::link(root + QStringLiteral("/missing"), entry) &&
                !AutostartRegistration::setEnabled(true, executable, config).success &&
                QFileInfo(entry).isSymLink(),
            "a startup symlink must never be followed or replaced");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    registrationRespectsUserEntriesAndLaunches();
    return 0;
}
