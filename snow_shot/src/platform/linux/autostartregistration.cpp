#include "snow_shot/platform/linux/autostartregistration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace snow_shot::platform::linux_desktop {
namespace {
constexpr auto kEntryName = "io.github.llanc.SnowShotKDE.desktop";
constexpr auto kMarker = "X-SnowShotKDE-Managed=true";

AutostartResult failed() {
    return {false, QCoreApplication::translate("AutoStartRegistration",
                                               "Could not update the automatic startup entry")};
}

AutostartResult foreignEntry() {
    return {false, QCoreApplication::translate(
                       "AutoStartRegistration",
                       "The existing automatic startup entry is not managed by Snow Shot")};
}

QString desktopArgument(QString value) {
    value.replace(QLatin1Char('%'), QStringLiteral("%%"));
    for (const QChar character :
         {QLatin1Char('\\'), QLatin1Char('"'), QLatin1Char('$'), QLatin1Char('`')}) {
        value.replace(character, QString(QLatin1Char('\\')) + character);
    }
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool managedContent(const QByteArray& content) {
    for (const QByteArray& line : content.split('\n')) {
        if (line.trimmed() == kMarker)
            return true;
    }
    return false;
}

bool readExisting(const QString& path, QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return false;
    content = file.readAll();
    return file.error() == QFile::NoError;
}
} // namespace

QString AutostartRegistration::configHome() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString AutostartRegistration::entryPath(const QString& configHome) {
    return QDir(configHome).filePath(QStringLiteral("autostart/") + QLatin1String(kEntryName));
}

bool AutostartRegistration::isEnabled(const QString& configHome) {
    const QFileInfo info(entryPath(configHome));
    if (!info.isFile() || info.isSymLink())
        return false;
    QByteArray content;
    if (!readExisting(info.filePath(), content) || !managedContent(content))
        return false;
    static const QRegularExpression hidden(
        QStringLiteral("(?:^|\\n)\\s*Hidden\\s*=\\s*true\\s*(?:\\r?\\n|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return !hidden.match(QString::fromUtf8(content)).hasMatch();
}

AutostartResult AutostartRegistration::setEnabled(bool enabled, const QString& executable,
                                                  const QString& configHome) {
    if (!QDir::isAbsolutePath(configHome) || configHome.isEmpty())
        return failed();
    const QString path = entryPath(configHome);
    const QFileInfo existing(path);
    if (existing.isSymLink())
        return foreignEntry();
    QByteArray previous;
    if (existing.exists() &&
        (!existing.isFile() || !readExisting(path, previous) || !managedContent(previous)))
        return foreignEntry();
    if (!enabled) {
        if (!existing.exists() || QFile::remove(path))
            return {true, {}};
        return failed();
    }

    const QFileInfo program(executable);
    if (!program.isAbsolute() || !program.isFile() || !program.isExecutable() ||
        executable.contains(QLatin1Char('\n')) || executable.contains(QLatin1Char('\r')) ||
        executable.contains(QChar::Null) || !QDir(configHome).mkpath(QStringLiteral("autostart")))
        return failed();
    const QByteArray content =
        (QStringLiteral("[Desktop Entry]\nType=Application\nName=Snow Shot (KDE)\n"
                        "Terminal=false\nExec=/usr/bin/env ") +
         desktopArgument(executable) + QStringLiteral(" --autostart\nX-SnowShotKDE-Managed=true\n"))
            .toUtf8();
    if (previous == content)
        return {true, {}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit())
        return failed();
    return {true, {}};
}

AutostartResult AutostartRegistration::reconcile(bool enabled, const QString& executable,
                                                 const QString& configHome) {
    if (!QDir::isAbsolutePath(configHome) || configHome.isEmpty())
        return failed();
    const QFileInfo existing(entryPath(configHome));
    if (enabled && (existing.exists() || existing.isSymLink())) {
        QByteArray content;
        if (existing.isSymLink() || !existing.isFile() ||
            !readExisting(existing.filePath(), content) || !managedContent(content))
            return foreignEntry();
        // Respect a startup entry that was disabled or customized in KDE.
        return {true, {}};
    }
    return setEnabled(enabled, executable, configHome);
}

} // namespace snow_shot::platform::linux_desktop
