import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EnvMonitorApp

ApplicationWindow {
    visible: true
    title: "EnvMonitor"
    Material.theme: Material.System
    Material.accent: "#0969da"

    BleProvisioner { id: ble }

    // Phone's current time, refreshed every second for display
    property date now: new Date()
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: now = new Date()
    }

    // Brojčani Tumbler: prikazuje base + index, s vodećim nulama
    component NumTumbler: Tumbler {
        id: numT
        property int base: 0
        property int digits: 2
        visibleItemCount: 3
        implicitWidth: 52
        implicitHeight: 126
        delegate: Label {
            required property int modelData
            required property int index      // treba ga Tumbler.displacement
            text: {
                var v = String(numT.base + modelData)
                while (v.length < numT.digits) v = "0" + v
                return v
            }
            font.pixelSize: 22
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            opacity: 1.0 - Math.abs(Tumbler.displacement) / (numT.visibleItemCount / 2)
        }
    }

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
                    // Clock face drawn with Canvas
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = "white";
                            ctx.lineCap = "round";
                            var cx = width / 2, cy = height / 2;
                            ctx.lineWidth = 2.2;
                            ctx.beginPath();
                            ctx.arc(cx, cy, 14, 0, Math.PI * 2);
                            ctx.stroke();
                            ctx.beginPath();
                            ctx.moveTo(cx, cy); ctx.lineTo(cx, cy - 9);
                            ctx.moveTo(cx, cy); ctx.lineTo(cx + 6, cy + 3);
                            ctx.stroke();
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
                        text: "// sinkronizacija vremena preko BLE"
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

            // ── Time setting (visible when connected) ────────────
            // Default: vrijeme mobitela. "Ručno podesi" otvara birače
            // datuma i vremena, unaprijed postavljene na vrijeme mobitela.
            ColumnLayout {
                id: timeBox
                visible: ble.state === BleProvisioner.Connected
                Layout.fillWidth: true
                spacing: 4

                readonly property int yearBase: 2024   // FW prihvaća 2024–2099
                property int pickDay: 1                // željeni dan (čuva se kad mjesec ima manje dana)

                function daysIn(y, m) { return new Date(y, m, 0).getDate() }   // m = 1..12

                function loadFromPhone() {
                    var d = new Date()
                    yearT.currentIndex  = d.getFullYear() - yearBase
                    monthT.currentIndex = d.getMonth()
                    pickDay             = d.getDate()
                    dayT.currentIndex   = d.getDate() - 1
                    hourT.currentIndex  = d.getHours()
                    minT.currentIndex   = d.getMinutes()
                }

                // Odabrano lokalno vrijeme mobitela (JS Date)
                function chosenDate() {
                    return new Date(yearT.currentIndex + yearBase, monthT.currentIndex,
                                    dayT.currentIndex + 1,
                                    hourT.currentIndex, minT.currentIndex, 0)
                }

                // ── Prikaz vremena mobitela (default) ────────────
                ColumnLayout {
                    visible: !manualSwitch.checked
                    spacing: 4

                    Label {
                        text: "Vrijeme na mobitelu"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: Material.hintTextColor
                        leftPadding: 2
                    }
                    Label {
                        text: Qt.formatDateTime(now, "HH:mm:ss")
                        font.pixelSize: 40
                        font.weight: Font.Bold
                        leftPadding: 2
                    }
                    Label {
                        text: Qt.formatDateTime(now, "dddd, d.M.yyyy.")
                        font.pixelSize: 14
                        color: Material.hintTextColor
                        leftPadding: 2
                    }
                }

                Switch {
                    id: manualSwitch
                    text: "Ručno podesi datum i vrijeme"
                    onToggled: if (checked) timeBox.loadFromPhone()
                }

                // ── Ručni unos (polazna točka = vrijeme mobitela) ─
                ColumnLayout {
                    visible: manualSwitch.checked
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        text: "Datum"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: Material.hintTextColor
                        leftPadding: 2
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 2

                        NumTumbler {
                            id: dayT
                            base: 1
                            model: timeBox.daysIn(yearT.currentIndex + timeBox.yearBase,
                                                  monthT.currentIndex + 1)
                            // kad se broj dana promijeni (npr. 31.1. → veljača), zadrži
                            // željeni dan ili uzmi zadnji dan u mjesecu
                            onModelChanged: Qt.callLater(function() {
                                dayT.currentIndex = Math.min(timeBox.pickDay, dayT.count) - 1
                            })
                            onMovingChanged: if (!moving) timeBox.pickDay = currentIndex + 1
                        }
                        Label { text: "."; font.pixelSize: 22 }
                        NumTumbler { id: monthT; base: 1; model: 12 }
                        Label { text: "."; font.pixelSize: 22 }
                        NumTumbler {
                            id: yearT
                            base: timeBox.yearBase
                            digits: 4
                            implicitWidth: 76
                            wrap: false
                            model: 2100 - timeBox.yearBase
                        }
                    }

                    Item { height: 4 }

                    Label {
                        text: "Vrijeme"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        color: Material.hintTextColor
                        leftPadding: 2
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 2

                        NumTumbler { id: hourT; model: 24 }
                        Label { text: ":"; font.pixelSize: 22 }
                        NumTumbler { id: minT; model: 60 }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "Bit će postavljeno: " +
                              Qt.formatDateTime(timeBox.chosenDate(), "dddd, d.M.yyyy. HH:mm")
                        font.pixelSize: 13
                        color: Material.hintTextColor
                        wrapMode: Text.WordWrap
                    }

                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Vrati na vrijeme mobitela"
                        flat: true
                        onClicked: timeBox.loadFromPhone()
                    }
                }

                Item { height: 8 }

                Button {
                    Layout.fillWidth: true
                    implicitHeight: 48
                    text: "Postavi vrijeme"
                    Material.background: Material.accentColor
                    Material.foreground: "white"
                    onClicked: {
                        if (manualSwitch.checked)
                            ble.sendEpoch(Math.floor(timeBox.chosenDate().getTime() / 1000))
                        else
                            ble.sendTime()
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

            // ── Sync result banner ───────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: resultLabel.implicitHeight + 24
                visible: ble.syncResult.length > 0
                radius: 8
                color: ble.syncResult.startsWith("ok:")
                       ? (Material.theme === Material.Dark ? "#0b2214" : "#dafbe1")
                       : (Material.theme === Material.Dark ? "#2d0806" : "#ffebe9")

                Label {
                    id: resultLabel
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    anchors.margins: 14
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    color: ble.syncResult.startsWith("ok:") ? "#1a7f37" : "#cf222e"
                    text: ble.syncResult === "ok:rtc"
                          ? "✓ Vrijeme postavljeno i spremljeno u RTC."
                          : ble.syncResult === "ok:local"
                          ? "✓ Vrijeme postavljeno. RTC nije spojen — vrijeme se gubi kad nestane struje."
                          : "✗ Postavljanje vremena nije uspjelo."
                }
            }
        }
    }
}
