#include "BleProvisioner.h"
#include <QJsonObject>
#include <QJsonDocument>

const QBluetoothUuid BleProvisioner::SVC_UUID  { QString("12345678-1234-5678-1234-56789abcdef0") };
const QBluetoothUuid BleProvisioner::CRED_UUID { QString("12345678-1234-5678-1234-56789abcdef1") };
const QBluetoothUuid BleProvisioner::STAT_UUID { QString("12345678-1234-5678-1234-56789abcdef2") };

BleProvisioner::BleProvisioner(QObject* parent) : QObject(parent) {}

BleProvisioner::~BleProvisioner() {
    if (m_ctrl) m_ctrl->disconnectFromDevice();
}

void BleProvisioner::setStatus(State s, const QString& msg) {
    if (m_state != s)   { m_state = s; emit stateChanged(); }
    if (m_statusMessage != msg) { m_statusMessage = msg; emit statusMessageChanged(); }
}

// ── Scan ──────────────────────────────────────────────────────────

void BleProvisioner::startScan() {
    // Clean up previous connection
    if (m_svc)   { m_svc->deleteLater();   m_svc  = nullptr; }
    if (m_ctrl)  { m_ctrl->disconnectFromDevice(); m_ctrl->deleteLater(); m_ctrl = nullptr; }
    if (m_scanner) { m_scanner->stop(); m_scanner->deleteLater(); }
    m_wifiResult.clear();
    emit wifiResultChanged();

    setStatus(Scanning, "Skeniram BLE uređaje...");

    m_scanner = new QBluetoothDeviceDiscoveryAgent(this);
    m_scanner->setLowEnergyDiscoveryTimeout(8000);

    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleProvisioner::onDeviceDiscovered);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleProvisioner::onScanFinished);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, [this](QBluetoothDeviceDiscoveryAgent::Error) {
                setStatus(Error, "Scan greška: " + m_scanner->errorString());
            });

    m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleProvisioner::onDeviceDiscovered(const QBluetoothDeviceInfo& info) {
    if (info.name() != "EnvMonitor") return;
    m_scanner->stop();
    setStatus(Connecting, "Pronađen, spajam se...");
    setupController(info);
}

void BleProvisioner::onScanFinished() {
    if (m_state == Scanning)
        setStatus(Error, "Nije pronađen EnvMonitor. Je li uređaj uključen?");
}

// ── Connect / BLE ─────────────────────────────────────────────────

void BleProvisioner::setupController(const QBluetoothDeviceInfo& device) {
    m_ctrl = QLowEnergyController::createCentral(device, this);

    connect(m_ctrl, &QLowEnergyController::connected,
            this, &BleProvisioner::onConnected);
    connect(m_ctrl, &QLowEnergyController::discoveryFinished,
            this, &BleProvisioner::onDiscoveryFinished);
    connect(m_ctrl, &QLowEnergyController::disconnected, this, [this] {
        setStatus(Idle, "BLE veza prekinuta.");
    });
    connect(m_ctrl, &QLowEnergyController::errorOccurred,
            this, &BleProvisioner::onControllerError);

    m_ctrl->connectToDevice();
}

void BleProvisioner::onConnected() {
    setStatus(Connecting, "Spojen, otkrivam servise...");
    m_ctrl->discoverServices();
}

void BleProvisioner::onDiscoveryFinished() {
    m_svc = m_ctrl->createServiceObject(SVC_UUID, this);
    if (!m_svc) {
        setStatus(Error, "EnvMonitor BLE servis nije pronađen.");
        return;
    }

    connect(m_svc, &QLowEnergyService::stateChanged,
            this, &BleProvisioner::onServiceStateChanged);
    connect(m_svc, &QLowEnergyService::characteristicChanged,
            this, &BleProvisioner::onCharacteristicChanged);
    connect(m_svc, &QLowEnergyService::errorOccurred,
            this, [this](QLowEnergyService::ServiceError err) {
                setStatus(Error, "Servis greška: " + QString::number(static_cast<int>(err)));
            });

    m_svc->discoverDetails();
}

void BleProvisioner::onServiceStateChanged(QLowEnergyService::ServiceState s) {
    if (s != QLowEnergyService::RemoteServiceDiscovered) return;

    // Enable notifications on STAT characteristic
    auto stat = m_svc->characteristic(STAT_UUID);
    if (stat.isValid()) {
        auto cccd = stat.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (cccd.isValid())
            m_svc->writeDescriptor(cccd, QByteArray::fromHex("0100"));
    }

    setStatus(Connected, "Spojen na EnvMonitor");
}

void BleProvisioner::onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                              const QByteArray& val)
{
    if (c.uuid() != STAT_UUID) return;
    QString v = QString::fromUtf8(val);

    if (v.startsWith("ok:"))    setStatus(Connected, "WiFi spojen! IP: " + v.mid(3));
    else if (v == "fail")       setStatus(Connected, "Spajanje neuspješno — provjeri SSID/lozinku.");
    else if (v == "connecting") setStatus(Connected, "Spajam na WiFi mrežu...");
    else                        setStatus(Connected, v);

    m_wifiResult = v;
    emit wifiResultChanged();
}

// ── Actions ───────────────────────────────────────────────────────

void BleProvisioner::sendCredentials(const QString& ssid, const QString& pass) {
    if (m_state != Connected || !m_svc) return;

    QJsonObject obj{{ "ssid", ssid }, { "pass", pass }};
    QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    auto cred = m_svc->characteristic(CRED_UUID);
    if (cred.isValid())
        m_svc->writeCharacteristic(cred, json, QLowEnergyService::WriteWithoutResponse);

    setStatus(Connected, "Poslano, čekam odgovor ESP32...");
}

void BleProvisioner::disconnectBle() {
    if (m_ctrl) m_ctrl->disconnectFromDevice();
    setStatus(Idle, "Odspojen.");
}

void BleProvisioner::onControllerError(QLowEnergyController::Error err) {
    switch (err) {
    case QLowEnergyController::UnknownRemoteDeviceError:
        setStatus(Error, "Uređaj nije dostupan."); break;
    case QLowEnergyController::ConnectionError:
        setStatus(Error, "Greška pri spajanju — pokušaj ponovo."); break;
    default:
        setStatus(Error, "BLE greška (" + QString::number(err) + ")."); break;
    }
}
