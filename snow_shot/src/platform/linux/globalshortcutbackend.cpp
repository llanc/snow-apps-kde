#include "../../presentation/services/globalshortcutbackend_p.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QKeySequence>
#include <QTimer>

#include <optional>
#include <utility>

namespace snow_shot::presentation {
namespace {
const QString kdeAccelService = QStringLiteral("org.kde.kglobalaccel");
const QString path = QStringLiteral("/kglobalaccel");
const QString interfaceName = QStringLiteral("org.kde.KGlobalAccel");
const QString componentInterface = QStringLiteral("org.kde.kglobalaccel.Component");
const QString componentName = QStringLiteral("snow-shot");
constexpr uint setPresentAndNoAutoloading = 0x2U | 0x4U;

QString actionName(int id) {
    return QStringLiteral("_k_session:snow-shot-%1").arg(id);
}

std::optional<int> shortcutKey(const shortcuts::ShortcutBinding& binding) {
    const auto sequence =
        QKeySequence::fromString(binding.portableText, QKeySequence::PortableText);
    if (sequence.count() != 1)
        return std::nullopt;
    const auto combination = sequence[0];
    const auto key = combination.key();
    if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Shift || key == Qt::Key_Meta || key == Qt::Key_AltGr ||
        key == Qt::Key_Super_L || key == Qt::Key_Super_R ||
        combination.keyboardModifiers().testFlag(Qt::GroupSwitchModifier) ||
        combination.toCombined() == -1)
        return std::nullopt;
    return combination.toCombined();
}

class KdeGlobalShortcutBackend final : public QObject, public GlobalShortcutBackend {
    Q_OBJECT

  public:
    KdeGlobalShortcutBackend()
        : m_bus(QDBusConnection::sessionBus()),
          m_interface(kdeAccelService, path, interfaceName, m_bus),
          m_watcher(kdeAccelService, m_bus, QDBusServiceWatcher::WatchForOwnerChange) {
        m_interface.setTimeout(3000);
        connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString&, const QString&, const QString&) {
                    forgetComponent();
                    const QList<int> ids = m_registered.keys();
                    if (m_availabilityChanged)
                        m_availabilityChanged(ids);
                });
        m_bus.connect(kdeAccelService, path, interfaceName,
                      QStringLiteral("yourShortcutGotChanged"), this,
                      SLOT(shortcutChanged(QStringList, QList<int>)));
    }

    ~KdeGlobalShortcutBackend() override {
        const QList<int> ids = m_registered.keys();
        for (int id : ids)
            unregisterShortcut(id);
        forgetComponent();
    }

    void setActivationHandler(ActivationHandler handler) override {
        m_activation = std::move(handler);
    }

    void setAvailabilityChangedHandler(AvailabilityChangedHandler handler) override {
        m_availabilityChanged = std::move(handler);
    }

    GlobalShortcutValidationResult
    validateShortcut(const shortcuts::ShortcutBinding& binding) const override {
        const bool supported = shortcutKey(binding).has_value();
        return {binding.portableText, supported,
                supported ? GlobalShortcutFailureReason::None
                          : GlobalShortcutFailureReason::InvalidShortcut,
                binding};
    }

    GlobalShortcutBackendResult
    registerShortcut(int id, const shortcuts::ShortcutBinding& binding) override {
        const auto key = shortcutKey(binding);
        if (!key || id <= 0 || m_registered.contains(id))
            return {false, GlobalShortcutFailureReason::InvalidShortcut, 0};
        const auto desktops = QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"))
                                  .split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (!desktops.contains(QStringLiteral("KDE"), Qt::CaseInsensitive) ||
            !m_bus.isConnected() || !m_interface.isValid())
            return {false, GlobalShortcutFailureReason::UnsupportedPlatform, 0};
        for (auto existing : std::as_const(m_registered)) {
            if (existing == *key)
                return {false, GlobalShortcutFailureReason::AlreadyInUse, 0};
        }

        const QDBusReply<bool> available =
            m_interface.call(QStringLiteral("isGlobalShortcutAvailable"), *key, componentName);
        if (!available.isValid())
            return {false, GlobalShortcutFailureReason::SystemError, 0};
        if (!available.value())
            return {false, GlobalShortcutFailureReason::AlreadyInUse, 0};

        const QStringList action{componentName, actionName(id), QStringLiteral("Snow Shot"),
                                 binding.portableText};
        const QDBusMessage registration = m_interface.call(QStringLiteral("doRegister"), action);
        if (registration.type() == QDBusMessage::ErrorMessage)
            return {false, GlobalShortcutFailureReason::SystemError, 0};
        const QList<int> keys{*key};
        const QDBusReply<QList<int>> accepted =
            m_interface.call(QStringLiteral("setShortcut"), action, QVariant::fromValue(keys),
                             setPresentAndNoAutoloading);
        if (!accepted.isValid() || accepted.value() != keys || !subscribeComponent()) {
            releaseAction(id);
            return {false,
                    accepted.isValid() && accepted.value() != keys
                        ? GlobalShortcutFailureReason::AlreadyInUse
                        : GlobalShortcutFailureReason::SystemError,
                    0};
        }
        m_registered.insert(id, *key);
        return {true, GlobalShortcutFailureReason::None, 0};
    }

    void unregisterShortcut(int id) override {
        if (m_registered.remove(id) != 0)
            releaseAction(id);
    }

  private slots:
    void shortcutPressed(const QString& component, const QString& action, qlonglong) {
        if (component != componentName)
            return;
        for (auto it = m_registered.cbegin(); it != m_registered.cend(); ++it) {
            if (action == actionName(it.key())) {
                const int id = it.key();
                if (m_activation)
                    m_activation(id);
                return;
            }
        }
    }

    void shortcutChanged(const QStringList& action, const QList<int>& keys) {
        if (action.size() < 2 || action.at(0) != componentName)
            return;
        for (auto it = m_registered.cbegin(); it != m_registered.cend(); ++it) {
            if (action.at(1) == actionName(it.key()) && keys != QList<int>{it.value()}) {
                const int id = it.key();
                QTimer::singleShot(0, this, [this, id] {
                    if (m_registered.contains(id) && m_availabilityChanged)
                        m_availabilityChanged({id});
                });
                return;
            }
        }
    }

  private:
    void forgetComponent() {
        if (m_componentPath.isEmpty())
            return;
        m_bus.disconnect(kdeAccelService, m_componentPath, componentInterface,
                         QStringLiteral("globalShortcutPressed"), this,
                         SLOT(shortcutPressed(QString, QString, qlonglong)));
        m_componentPath.clear();
    }

    bool subscribeComponent() {
        if (!m_componentPath.isEmpty())
            return true;
        const QDBusReply<QDBusObjectPath> component =
            m_interface.call(QStringLiteral("getComponent"), componentName);
        if (!component.isValid())
            return false;
        const QString componentPath = component.value().path();
        if (!m_bus.connect(kdeAccelService, componentPath, componentInterface,
                           QStringLiteral("globalShortcutPressed"), this,
                           SLOT(shortcutPressed(QString, QString, qlonglong))))
            return false;
        m_componentPath = componentPath;
        return true;
    }

    void releaseAction(int id) {
        QDBusMessage message = QDBusMessage::createMethodCall(kdeAccelService, path, interfaceName,
                                                              QStringLiteral("unregister"));
        message.setArguments({componentName, actionName(id)});
        message.setAutoStartService(false);
        m_bus.asyncCall(message);
    }

    QDBusConnection m_bus;
    QDBusInterface m_interface;
    QDBusServiceWatcher m_watcher;
    QString m_componentPath;
    QHash<int, int> m_registered;
    ActivationHandler m_activation;
    AvailabilityChangedHandler m_availabilityChanged;
};
} // namespace

std::unique_ptr<GlobalShortcutBackend> createKdeGlobalShortcutBackend() {
    return std::make_unique<KdeGlobalShortcutBackend>();
}
} // namespace snow_shot::presentation

#include "globalshortcutbackend.moc"
