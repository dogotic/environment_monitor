#pragma once

#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>

class BleProvisioner : public QObject
{
    Q_OBJECT

    Q_PROPERTY(State   state         READ state         NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString wifiResult    READ wifiResult    NOTIFY wifiResultChanged)

public:
    enum State { Idle, Scanning, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit BleProvisioner(QObject* parent = nullptr);
    ~BleProvisioner();

    State   state()         const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    QString wifiResult()    const { return m_wifiResult; }

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void sendCredentials(const QString& ssid, const QString& pass);
    Q_INVOKABLE void disconnectBle();

signals:
    void stateChanged();
    void statusMessageChanged();
    void wifiResultChanged();

private:
    void setStatus(State s, const QString& msg);
    void setupController(const QBluetoothDeviceInfo& device);

    // scan callbacks
    void onDeviceDiscovered(const QBluetoothDeviceInfo& info);
    void onScanFinished();
    // controller callbacks
    void onConnected();
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState s);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c, const QByteArray& val);
    void onControllerError(QLowEnergyController::Error err);

    static const QBluetoothUuid SVC_UUID;
    static const QBluetoothUuid CRED_UUID;
    static const QBluetoothUuid STAT_UUID;

    QBluetoothDeviceDiscoveryAgent* m_scanner = nullptr;
    QLowEnergyController*           m_ctrl    = nullptr;
    QLowEnergyService*              m_svc     = nullptr;

    State   m_state         = Idle;
    QString m_statusMessage = "Nije spojen";
    QString m_wifiResult;
};
