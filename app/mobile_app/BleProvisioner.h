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
    Q_PROPERTY(QString syncResult    READ syncResult    NOTIFY syncResultChanged)

public:
    enum State { Idle, Scanning, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit BleProvisioner(QObject* parent = nullptr);
    ~BleProvisioner();

    State   state()         const { return m_state; }
    QString statusMessage() const { return m_statusMessage; }
    QString syncResult()    const { return m_syncResult; }

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void sendTime();                 // trenutno vrijeme mobitela
    Q_INVOKABLE void sendEpoch(qint64 secs);     // proizvoljno vrijeme (UTC epoch)
    Q_INVOKABLE void disconnectBle();

signals:
    void stateChanged();
    void statusMessageChanged();
    void syncResultChanged();

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
    static const QBluetoothUuid TIME_UUID;
    static const QBluetoothUuid STAT_UUID;

    QBluetoothDeviceDiscoveryAgent* m_scanner = nullptr;
    QLowEnergyController*           m_ctrl    = nullptr;
    QLowEnergyService*              m_svc     = nullptr;

    State   m_state         = Idle;
    QString m_statusMessage = "Nije spojen";
    QString m_syncResult;
};
