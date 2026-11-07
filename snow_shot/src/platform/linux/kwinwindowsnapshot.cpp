#include "kwinwindowsnapshot.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace snow_shot::platform {
namespace {
constexpr int kTimeoutMs = 120;
constexpr int kMaximumWindows = 512;
const QString scriptingPath = QStringLiteral("/Scripting");
const QString scriptingInterface = QStringLiteral("org.kde.kwin.Scripting");

// KWin runs this one-shot script before the portal capture. It sends only rectangles
// and the invocation cursor over the existing user session bus.
const auto scriptSource = QStringLiteral(R"JS(
const [outputName, service, path, limit] = %1;
const outputs = workspace.screens;
if (outputs.length === 1 && outputs[0].name === outputName) {
    const output = outputs[0];
    const area = output.geometry;
    const desktop = workspace.currentDesktopForScreen(output);
    const activity = workspace.currentActivity;
    const cursor = workspace.cursorPos;
    const windows = [];
    const stack = workspace.stackingOrder;
    for (let i = stack.length - 1; i >= 0 && windows.length < limit; --i) {
        const window = stack[i];
        if (window.deleted || window.hidden || window.minimized || window.popupWindow ||
            window.opacity <= 0 || !(window.normalWindow || window.dialog) ||
            (window.desktops.length && !window.desktops.some(d => d.id === desktop.id)) ||
            (window.activities.length && !window.activities.includes(activity))) {
            continue;
        }
        const frame = window.frameGeometry;
        const left = Math.max(area.x, frame.x);
        const top = Math.max(area.y, frame.y);
        const right = Math.min(area.x + area.width, frame.x + frame.width);
        const bottom = Math.min(area.y + area.height, frame.y + frame.height);
        if (right > left && bottom > top) {
            windows.push([(left - area.x) / area.width, (top - area.y) / area.height,
                          (right - left) / area.width, (bottom - top) / area.height]);
        }
    }
    callDBus(service, path, "io.github.llanc.SnowShotKDE.WindowSnapshot", "Submit",
             JSON.stringify([(cursor.x - area.x) / area.width,
                             (cursor.y - area.y) / area.height, windows]));
}
)JS");

class SnapshotReply final : public QDBusVirtualObject {
  public:
    explicit SnapshotReply(QString sender) : m_sender(std::move(sender)) {}

    QString introspect(const QString&) const override {
        return {};
    }

    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override {
        if (message.service() != m_sender ||
            message.interface() != u"io.github.llanc.SnowShotKDE.WindowSnapshot" ||
            message.member() != u"Submit" || message.signature() != u"s") {
            return false;
        }
        payload = message.arguments().first().toString();
        bus.send(message.createReply());
        loop.quit();
        return true;
    }

    QEventLoop loop;
    QString payload;

  private:
    QString m_sender;
};

std::optional<KWinWindowSnapshot> parseSnapshot(const QString& payload) {
    if (payload.isEmpty() || payload.size() > 128 * 1024)
        return std::nullopt;
    const auto snapshot = QJsonDocument::fromJson(payload.toUtf8()).array();
    if (snapshot.size() != 3 || !snapshot[0].isDouble() || !snapshot[1].isDouble() ||
        !snapshot[2].isArray()) {
        return std::nullopt;
    }
    const auto frames = snapshot[2].toArray();
    if (frames.size() > kMaximumWindows)
        return std::nullopt;

    KWinWindowSnapshot result;
    result.cursor = QPointF(snapshot[0].toDouble(), snapshot[1].toDouble());
    result.windows.reserve(frames.size());
    const QRectF unitSquare(0, 0, 1, 1);
    for (const auto& value : frames) {
        const auto frame = value.toArray();
        if (frame.size() != 4)
            continue;
        const QRectF rect(frame[0].toDouble(-1), frame[1].toDouble(-1), frame[2].toDouble(),
                          frame[3].toDouble());
        if (rect.x() >= 0 && rect.y() >= 0 && rect.isValid()) {
            const QRectF clipped = rect.intersected(unitSquare);
            if (!clipped.isEmpty())
                result.windows.push_back(clipped);
        }
    }
    return result;
}
} // namespace

std::optional<KWinWindowSnapshot> snapshotKWinWindows(const QString& outputName) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.baseService().startsWith(QLatin1Char(':')))
        return std::nullopt;
    auto ownerRequest = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    ownerRequest << QStringLiteral("org.kde.KWin");
    const QDBusReply<QString> owner = bus.call(ownerRequest, QDBus::Block, kTimeoutMs);
    if (!owner.isValid() || !owner.value().startsWith(QLatin1Char(':')))
        return std::nullopt;

    auto desktopState = QDBusMessage::createMethodCall(
        owner.value(), QStringLiteral("/KWin"), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    desktopState << QStringLiteral("org.kde.KWin") << QStringLiteral("showingDesktop");
    const QDBusReply<QDBusVariant> showingDesktop =
        bus.call(desktopState, QDBus::Block, kTimeoutMs);
    if (showingDesktop.isValid() && showingDesktop.value().variant().toBool())
        return std::nullopt;

    QTemporaryFile script;
    if (!script.open())
        return std::nullopt;
    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    const QString path = QStringLiteral("/SnowShot/WindowSnapshot/") + token;
    const QString name = QStringLiteral("snow-shot-window-snapshot-") + token;
    const QJsonArray config{outputName, bus.baseService(), path, kMaximumWindows};
    const auto source =
        scriptSource.arg(QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)))
            .toUtf8();
    if (script.write(source) != source.size() || !script.flush())
        return std::nullopt;
    script.close();

    SnapshotReply receiver(owner.value());
    if (!bus.registerVirtualObject(path, &receiver))
        return std::nullopt;
    const auto cleanup = qScopeGuard([&] {
        auto unload = QDBusMessage::createMethodCall(
            owner.value(), scriptingPath, scriptingInterface, QStringLiteral("unloadScript"));
        unload << name;
        bus.asyncCall(unload, kTimeoutMs);
        bus.unregisterObject(path);
    });

    auto load = QDBusMessage::createMethodCall(owner.value(), scriptingPath, scriptingInterface,
                                               QStringLiteral("loadScript"));
    load << script.fileName() << name;
    const QDBusReply<int> id = bus.call(load, QDBus::Block, kTimeoutMs);
    if (!id.isValid() || id.value() < 0)
        return std::nullopt;
    const auto run = QDBusMessage::createMethodCall(
        owner.value(), QStringLiteral("/Scripting/Script%1").arg(id.value()),
        QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    if (bus.call(run, QDBus::Block, kTimeoutMs).type() != QDBusMessage::ReplyMessage)
        return std::nullopt;

    if (receiver.payload.isEmpty()) {
        QTimer::singleShot(kTimeoutMs, &receiver.loop, &QEventLoop::quit);
        receiver.loop.exec();
    }
    return parseSnapshot(receiver.payload);
}
} // namespace snow_shot::platform
