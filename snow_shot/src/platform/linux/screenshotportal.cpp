#include "screenshotportal.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>

#include <utility>

namespace snow_shot::platform {
namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString screenshotInterface = QStringLiteral("org.freedesktop.portal.Screenshot");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");
} // namespace

ScreenshotPortal::ScreenshotPortal(QDBusConnection connection, QObject* parent)
    : QObject(parent), m_connection(std::move(connection)) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(Result::TimedOut, {}, true); });
}

ScreenshotPortal::~ScreenshotPortal() {
    if (m_pending) {
        // Closing a request tells the portal to dismiss any outstanding dialog.
        closeRequest(m_requestPath);
        disconnectResponse();
    }
}

bool ScreenshotPortal::start(int timeoutMs) {
    if (m_pending || !m_connection.isConnected() || timeoutMs <= 0)
        return false;

    QString sender = m_connection.baseService();
    if (!sender.startsWith(QLatin1Char(':')))
        return false;
    sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    QString token = QUuid::createUuid().toString(QUuid::Id128);
    token.prepend(QStringLiteral("snow_"));
    m_requestPath = portalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
    if (!m_connection.connect(service, m_requestPath, requestInterface, QStringLiteral("Response"),
                              this, SLOT(handleResponse(uint, QVariantMap)))) {
        m_requestPath.clear();
        return false;
    }

    m_pending = true;
    const quint64 generation = ++m_generation;
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("interactive"), false);
    QDBusMessage message = QDBusMessage::createMethodCall(service, portalPath, screenshotInterface,
                                                          QStringLiteral("Screenshot"));
    message << QString() << options;
    auto* watcher = new QDBusPendingCallWatcher(m_connection.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher* finishedCall) {
                const QDBusPendingReply<QDBusObjectPath> reply = *finishedCall;
                finishedCall->deleteLater();
                if (!m_pending || generation != m_generation)
                    return;
                if (reply.isError()) {
                    finish(Result::Failed, {}, true);
                } else if (reply.value().path() != m_requestPath) {
                    closeRequest(reply.value().path());
                    finish(Result::Failed);
                }
            });
    m_timeout.start(timeoutMs);
    return true;
}

void ScreenshotPortal::cancel() {
    if (m_pending)
        finish(Result::Cancelled, {}, true);
}

void ScreenshotPortal::handleResponse(uint response, const QVariantMap& results) {
    if (!m_pending)
        return;
    if (response == 1U) {
        finish(Result::Cancelled);
        return;
    }
    if (response != 0U) {
        finish(Result::Failed);
        return;
    }
    const QUrl uri(results.value(QStringLiteral("uri")).toString());
    if (!uri.isValid() || !uri.isLocalFile() || uri.toLocalFile().isEmpty()) {
        finish(Result::Failed);
        return;
    }
    finish(Result::Success, uri);
}

void ScreenshotPortal::finish(Result result, const QUrl& imageUri, bool closeRequest) {
    if (!m_pending)
        return;
    m_pending = false;
    m_timeout.stop();
    if (closeRequest) {
        this->closeRequest(m_requestPath);
    }
    disconnectResponse();
    m_requestPath.clear();
    emit finished(result, imageUri);
}

void ScreenshotPortal::closeRequest(const QString& requestPath) {
    const auto close = QDBusMessage::createMethodCall(service, requestPath, requestInterface,
                                                      QStringLiteral("Close"));
    m_connection.asyncCall(close);
}

void ScreenshotPortal::disconnectResponse() {
    m_connection.disconnect(service, m_requestPath, requestInterface, QStringLiteral("Response"),
                            this, SLOT(handleResponse(uint, QVariantMap)));
}

} // namespace snow_shot::platform
