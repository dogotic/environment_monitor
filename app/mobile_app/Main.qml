import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EnvMonitorApp

ApplicationWindow {
    visible: true
    title: "EnvMonitor WiFi"
    Material.theme: Material.System
    Material.accent: "#0969da"

    BleProvisioner { id: ble }

    // Dot blink animation
    SequentialAnimation {
        id: blinkAnim
        running: ble.state === BleProvisioner.Scanning ||
                 ble.state === BleProvisioner.Connecting
        loops: Animation.Infinite
        NumberAnimation { target: statusDot; property: "opacity"; to: 0.15; duration: 550 }
        NumberAnimation { target: statusDot; property: "opacity"; to: 1.0;  duration: 550 }
        onRunningChanged: if (!running) statusDot.opacity = 1.0
    }

    Flickable {
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: col.implicitHeight + 32
        clip: true

        ColumnLayout {
            id: col
            width: Math.min(parent.width - 32, 480)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 24
            spacing: 12

            // ── Header ──────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    width: 46; height: 46
                    radius: 11
                    color: Material.accentColor
                    // WiFi rings drawn with Canvas
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = "white";
                            ctx.lineCap = "round";
                            var cx = width / 2, cy = height * 0.68;
                            [10, 16, 22].forEach(function(r, i) {
                                ctx.lineWidth = 2.2 - i * 0.3;
                                ctx.beginPath();
                                ctx.arc(cx, cy, r, Math.PI * 1.1, Math.PI * 1.9);
                                ctx.stroke();
                            });
                            ctx.fillStyle = "white";
                            ctx.beginPath();
                            ctx.arc(cx, cy + 4, 2.5, 0, Math.PI * 2);
                            ctx.fill();
                        }
                    }
                }

                ColumnLayout {
                    spacing: 1
                    Label {
                        text: "EnvMonitor"
                        font.pixelSize: 19
                        font.weight: Font.Bold
                    }
                    Label {
                        text: "// WiFi provisioning via BLE"
                        font.pixelSize: 11
                        font.family: "monospace"
                        color: Material.hintTextColor
                    }
                }
            }

            // ── BLE status pill ──────────────────────────────────
            Pane {
                Layout.fillWidth: true
                Material.elevation: 1
                padding: 12

                RowLayout {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        id: statusDot
                        width: 10; height: 10
                        radius: 5
                        color: {
                            switch (ble.state) {
                            case BleProvisioner.Connected:  return "#1a7f37"
                            case BleProvisioner.Scanning:
                            case BleProvisioner.Connecting: return "#9a6700"
                            case BleProvisioner.Error:      return "#cf222e"
                            default:                        return Material.hintTextColor
                            }
                        }
                    }

                    Label {
                        text: ble.statusMessage
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ── Connect button ───────────────────────────────────
            Button {
                Layout.fillWidth: true
                implicitHeight: 48
                visible: ble.state !== BleProvisioner.Connected
                enabled: ble.state === BleProvisioner.Idle ||
                         ble.state === BleProvisioner.Error
                text: ble.state === BleProvisioner.Scanning   ? "Skeniranje..." :
                      ble.state === BleProvisioner.Connecting ? "Spajanje..." :
                                                                "Spoji se na EnvMonitor"
                Material.background: Material.accentColor
                Material.foreground: "white"
                onClicked: ble.startScan()
            }

            // ── Credentials form (visible when connected) ────────
            ColumnLayout {
                visible: ble.state === BleProvisioner.Connected
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: "WiFi mreža (SSID)"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: Material.hintTextColor
                    leftPadding: 2
                }
                TextField {
                    id: ssidField
                    Layout.fillWidth: true
                    placeholderText: "Naziv mreže"
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                }

                Item { height: 6 }

                Label {
                    text: "Lozinka"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    color: Material.hintTextColor
                    leftPadding: 2
                }
                TextField {
                    id: passField
                    Layout.fillWidth: true
                    placeholderText: "WiFi lozinka"
                    echoMode: showPass.checked ? TextInput.Normal : TextInput.Password
                }

                CheckBox {
                    id: showPass
                    text: "Prikaži lozinku"
                    font.pixelSize: 13
                }

                Item { height: 4 }

                Button {
                    Layout.fillWidth: true
                    implicitHeight: 48
                    text: "Pošalji na ESP32"
                    Material.background: Material.accentColor
                    Material.foreground: "white"
                    onClicked: {
                        if (ssidField.text.trim().length === 0) {
                            ssidField.forceActiveFocus()
                            return
                        }
                        ble.sendCredentials(ssidField.text.trim(), passField.text)
                    }
                }

                Button {
                    Layout.fillWidth: true
                    implicitHeight: 42
                    text: "Odspoji BLE"
                    flat: true
                    onClicked: ble.disconnectBle()
                }
            }

            // ── WiFi result banner ───────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: resultLabel.implicitHeight + 24
                visible: ble.wifiResult.length > 0
                radius: 8
                color: ble.wifiResult.startsWith("ok:")
                       ? (Material.theme === Material.Dark ? "#0b2214" : "#dafbe1")
                       : (Material.theme === Material.Dark ? "#2d0806" : "#ffebe9")

                Label {
                    id: resultLabel
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    anchors.margins: 14
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    color: ble.wifiResult.startsWith("ok:") ? "#1a7f37" : "#cf222e"
                    text: ble.wifiResult.startsWith("ok:")
                          ? "✓ WiFi spojen!  IP: " + ble.wifiResult.substring(3)
                          : "✗ Spajanje neuspješno — provjeri SSID i lozinku."
                }
            }
        }
    }
}
