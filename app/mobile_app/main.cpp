#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QPermission>
#include "BleProvisioner.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("EnvMonitor");
    app.setOrganizationName("EnvMonitor");

    // Request BLE permissions (Android 12+ / Qt 6.5+)
    QBluetoothPermission btPerm;
    btPerm.setCommunicationModes(QBluetoothPermission::Access);
    if (app.checkPermission(btPerm) == Qt::PermissionStatus::Undetermined)
        app.requestPermission(btPerm, [](const QPermission&) {});

    qmlRegisterType<BleProvisioner>("EnvMonitorApp", 1, 0, "BleProvisioner");

    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [](const QUrl&) { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("EnvMonitorApp", "Main");

    return app.exec();
}
