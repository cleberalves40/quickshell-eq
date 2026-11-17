import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Io

Item {
    id: root
    property string currentProfile: "flat"
    property string activeOutId: "local"
    property string activeOutName: "Laptop (Interno)"
    property int preAmpDb: 0
    property int balanceVal: 0
    property bool popupOpen: false
    property bool savingMode: false
    property bool scanningBt: false
    property var profileList: ["flat", "jbl_charge", "bass_boost", "voz"]
    property var btDevices: []

    readonly property var freqLabels: [
        "20", "25", "31", "40", "50", "63", "80", "100", "125", "160",
        "200", "250", "315", "400", "500", "630", "800", "1k", "1.2k", "1.6k",
        "2k", "2.5k", "3.1k", "4k", "5k", "6.3k", "8k", "10k", "12k", "16k", "20k"
    ]
    readonly property var freqValues: [
        "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160",
        "200", "250", "315", "400", "500", "630", "800", "1000", "1250", "1600",
        "2000", "2500", "3150", "4000", "5000", "6300", "8000", "10000", "12500", "16000", "20000"
    ]
    property var bandGains: [
        1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00,
        1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00,
        1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00, 1.00
    ]

    implicitWidth: eqPill.implicitWidth
    implicitHeight: eqPill.implicitHeight
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight

    IpcHandler {
        target: "eq"
        function toggle(): void { root.popupOpen = !root.popupOpen; }
    }

    Process { id: cmdRunner }
    Process { id: boostRunner }
    Process { id: balRunner }

    Process {
        id: listRunner
        command: ["eq-ctl", "list-profiles"]
        stdout: SplitParser {
            onRead: data => {
                var arr = data.trim().split(/\s+/).filter(s => s.length > 0);
                if (arr.length > 0) root.profileList = arr;
            }
        }
    }

    Process {
        id: stateRunner
        command: ["eq-ctl", "get-state"]
        stdout: SplitParser {
            onRead: data => {
                var parts = data.trim().split("|");
                if (parts.length >= 2) {
                    root.currentProfile = parts[0].trim();
                    var vals = parts[1].trim().split(/\s+/).map(Number);
                    if (vals.length === 31) root.bandGains = vals;
                    if (parts.length >= 3 && parts[2].trim() !== "") {
                        root.preAmpDb = parseInt(parts[2].trim()) || 0;
                    }
                    if (parts.length >= 4 && parts[3].trim() !== "") {
                        root.balanceVal = parseInt(parts[3].trim()) || 0;
                    }
                }
            }
        }
    }

    Process {
        id: btDiscoverRunner
        command: ["eq-ctl", "discover-bt"]
        onExited: root.scanningBt = false
        stdout: SplitParser {
            onRead: data => {
                var items = data.trim().split(";").filter(s => s.length > 0);
                var found = [];
                for (var i = 0; i < items.length; i++) {
                    var parts = items[i].split("|");
                    if (parts.length >= 3) {
                        found.push({ mac: parts[0].trim(), name: parts[1].trim(), status: parts[2].trim() });
                    }
                }
                root.btDevices = found;
            }
        }
    }

    Timer {
        id: applyDebounce
        interval: 80
        repeat: false
        onTriggered: {
            var args = ["eq-ctl", "set-custom"];
            for (var i = 0; i < 31; i++) {
                args.push(freqValues[i] + " " + bandGains[i].toFixed(2));
            }
            cmdRunner.command = args;
            cmdRunner.running = true;
        }
    }

    Timer {
        id: syncTimer
        interval: 120
        repeat: false
        onTriggered: {
            stateRunner.running = true;
            listRunner.running = true;
        }
    }

    Component.onCompleted: {
        listRunner.running = true;
        stateRunner.running = true;
    }

    function setBoost(db) {
        preAmpDb = Math.max(-12, Math.min(12, Math.round(db)));
        boostRunner.command = ["eq-ctl", "set-boost", preAmpDb.toString()];
        boostRunner.running = true;
    }

    function boostLabel(db) {
        if (db > 0) return "+" + db + "dB";
        return db + "dB";
    }

    function setBalance(val) {
        balanceVal = Math.max(-10, Math.min(10, Math.round(val)));
        balRunner.command = ["eq-ctl", "set-balance", balanceVal.toString()];
        balRunner.running = true;
    }

    function balLabel(v) {
        if (v === 0) return "C";
        return v < 0 ? "L" + Math.abs(v) : "R" + v;
    }

    function applyProfile(name) {
        currentProfile = name;
        cmdRunner.command = ["eq-ctl", "set-eq", name];
        cmdRunner.running = true;
        syncTimer.restart();
        eqPill.pulse();
    }

    function saveNewProfile(name) {
        var clean = name.trim().replace(/[^a-zA-Z0-9_-]/g, "_").toLowerCase();
        if (clean.length === 0) return;
        var args = ["eq-ctl", "save-profile", clean];
        for (var i = 0; i < 31; i++) {
            args.push(freqValues[i] + " " + bandGains[i].toFixed(2));
        }
        cmdRunner.command = args;
        cmdRunner.running = true;
        currentProfile = clean;
        savingMode = false;
        syncTimer.restart();
        eqPill.pulse();
    }

    function updateBand(index, newVal) {
        var clamped = Math.max(0.10, Math.min(3.00, Math.round(newVal * 10) / 10));
        var copy = bandGains.slice();
        copy[index] = clamped;
        bandGains = copy;
        currentProfile = "custom";
        applyDebounce.restart();
    }

    function selectOutput(id, name) {
        activeOutId = id;
        activeOutName = name;
        if (id === "local") {
            cmdRunner.command = ["eq-ctl", "set-out", "local"];
            cmdRunner.running = true;
        } else {
            cmdRunner.command = ["eq-ctl", "set-out", "bt", id];
            cmdRunner.running = true;
            if (profileList.indexOf("jbl_charge") !== -1) {
                applyProfile("jbl_charge");
            }
        }
        eqPill.pulse();
    }

    Pill {
        id: eqPill
        anchors.fill: parent
        icon: activeOutId !== "local" ? "\uF028" : "\uF1DE"
        text: (activeOutId !== "local" ? activeOutName : "EQ") + " · " + root.currentProfile + (root.preAmpDb !== 0 ? " (" + root.boostLabel(root.preAmpDb) + ")" : "")
        active: root.popupOpen || root.currentProfile !== "flat" || root.preAmpDb !== 0 || root.balanceVal !== 0
        tipRight: true
        tipTitle: "Equalizador 31 Bandas · " + root.activeOutName
        tipText: "Perfil: " + root.currentProfile + "\nGanho: " + root.boostLabel(root.preAmpDb) + "  ·  Balanço: " + root.balLabel(root.balanceVal)
        tipHint: "Clique: abrir mesa de som"
        onClicked: {
            root.popupOpen = !root.popupOpen;
            if (root.popupOpen) {
                listRunner.running = true;
                stateRunner.running = true;
            }
        }
    }

    PopupWindow {
        visible: root.popupOpen
        implicitWidth: 880
        implicitHeight: 455
        color: "transparent"

        anchor {
            item: eqPill
            edges: Edges.Bottom
            gravity: Edges.Bottom
            margins.top: 6
        }

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: Theme.bg
            border.color: "#4C566A"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                // Linha 1: Saídas + Buscar BT
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        Layout.fillWidth: true
                        height: 30
                        radius: 6
                        color: root.activeOutId === "local" ? "#88C0D0" : "#3B4252"

                        Text {
                            anchors.centerIn: parent
                            color: root.activeOutId === "local" ? "#2E3440" : Theme.fg
                            font.family: Theme.font
                            font.bold: true
                            font.pixelSize: 11
                            text: "Laptop / Fones P2"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectOutput("local", "Laptop (Interno)")
                        }
                    }

                    Repeater {
                        model: root.btDevices
                        Rectangle {
                            Layout.fillWidth: true
                            height: 30
                            radius: 6
                            color: root.activeOutId === modelData.mac ? "#88C0D0" : "#3B4252"
                            border.color: "#88C0D0"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                color: root.activeOutId === modelData.mac ? "#2E3440" : "#88C0D0"
                                font.family: Theme.font
                                font.bold: true
                                font.pixelSize: 11
                                text: "󰂯 " + modelData.name
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectOutput(modelData.mac, modelData.name)
                            }
                        }
                    }

                    Rectangle {
                        width: 135
                        height: 30
                        radius: 6
                        color: root.scanningBt ? "#434C5E" : "#3B4252"
                        border.color: "#88C0D0"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            color: "#88C0D0"
                            font.family: Theme.font
                            font.pixelSize: 10
                            font.bold: true
                            text: root.scanningBt ? "Buscando BT..." : "󰂰 Buscar BT Perto"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!root.scanningBt) {
                                    root.scanningBt = true;
                                    btDiscoverRunner.running = true;
                                }
                            }
                        }
                    }
                }

                // Linha 2: Volume Mestre + Ganho (-12 a +12 dB) + Balanceamento L/R
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    // Volume Mestre
                    Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 6
                        color: "#3B4252"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 6

                            Text {
                                text: "Vol"
                                color: Theme.fgDim
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                            }

                            Slider {
                                id: volSlider
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                stepSize: 0.01
                                value: Stats.volume
                                onMoved: Stats.setVolume(value)

                                background: Rectangle {
                                    x: volSlider.leftPadding
                                    y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                                    width: volSlider.availableWidth
                                    height: 4
                                    radius: 2
                                    color: "#4C566A"

                                    Rectangle {
                                        width: volSlider.visualPosition * parent.width
                                        height: parent.height
                                        color: "#88C0D0"
                                        radius: 2
                                    }
                                }

                                handle: Rectangle {
                                    x: volSlider.leftPadding + volSlider.visualPosition * (volSlider.availableWidth - width)
                                    y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                                    width: 12
                                    height: 12
                                    radius: 6
                                    color: Theme.fg
                                }
                            }

                            Text {
                                text: Math.round(Stats.volume * 100) + "%"
                                color: "#88C0D0"
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                                Layout.preferredWidth: 34
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    // Ganho Dinâmico Bipolar (-12 a +12 dB)
                    Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 6
                        color: "#3B4252"
                        border.color: root.preAmpDb > 0 ? "#EBCB8B" : (root.preAmpDb < 0 ? "#88C0D0" : "transparent")
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 6

                            Text {
                                text: "Ganho"
                                color: root.preAmpDb > 0 ? "#EBCB8B" : (root.preAmpDb < 0 ? "#88C0D0" : Theme.fgDim)
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Slider {
                                    id: boostSlider
                                    anchors.fill: parent
                                    from: -12
                                    to: 12
                                    stepSize: 1
                                    value: root.preAmpDb
                                    onMoved: root.setBoost(value)

                                    background: Rectangle {
                                        x: boostSlider.leftPadding
                                        y: boostSlider.topPadding + boostSlider.availableHeight / 2 - height / 2
                                        width: boostSlider.availableWidth
                                        height: 4
                                        radius: 2
                                        color: "#4C566A"

                                        // Marcação central (0 dB)
                                        Rectangle {
                                            x: parent.width / 2 - 1
                                            y: -3
                                            width: 2
                                            height: 10
                                            color: "#81A1C1"
                                        }

                                        Rectangle {
                                            x: Math.min(parent.width / 2, boostSlider.visualPosition * parent.width)
                                            width: Math.abs(boostSlider.visualPosition * parent.width - parent.width / 2)
                                            height: parent.height
                                            color: root.preAmpDb >= 0 ? "#EBCB8B" : "#88C0D0"
                                            radius: 2
                                        }
                                    }

                                    handle: Rectangle {
                                        x: boostSlider.leftPadding + boostSlider.visualPosition * (boostSlider.availableWidth - width)
                                        y: boostSlider.topPadding + boostSlider.availableHeight / 2 - height / 2
                                        width: 12
                                        height: 12
                                        radius: 6
                                        color: Theme.fg
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    propagateComposedEvents: true
                                    onPressed: mouse => { mouse.accepted = false; }
                                    onDoubleClicked: root.setBoost(0)
                                    onWheel: wheel => {
                                        root.setBoost(root.preAmpDb + (wheel.angleDelta.y > 0 ? 1 : -1));
                                    }
                                }
                            }

                            Text {
                                text: root.boostLabel(root.preAmpDb)
                                color: root.preAmpDb > 0 ? "#EBCB8B" : (root.preAmpDb < 0 ? "#88C0D0" : Theme.fgDim)
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                                Layout.preferredWidth: 42
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    // Balanceamento L/R (-10 a +10)
                    Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 6
                        color: "#3B4252"
                        border.color: root.balanceVal !== 0 ? "#A3BE8C" : "transparent"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 6

                            Text {
                                text: "Bal L/R"
                                color: root.balanceVal !== 0 ? "#A3BE8C" : Theme.fgDim
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Slider {
                                    id: balSlider
                                    anchors.fill: parent
                                    from: -10
                                    to: 10
                                    stepSize: 1
                                    value: root.balanceVal
                                    onMoved: root.setBalance(value)

                                    background: Rectangle {
                                        x: balSlider.leftPadding
                                        y: balSlider.topPadding + balSlider.availableHeight / 2 - height / 2
                                        width: balSlider.availableWidth
                                        height: 4
                                        radius: 2
                                        color: "#4C566A"

                                        Rectangle {
                                            x: parent.width / 2 - 1
                                            y: -3
                                            width: 2
                                            height: 10
                                            color: "#81A1C1"
                                        }

                                        Rectangle {
                                            x: Math.min(parent.width / 2, balSlider.visualPosition * parent.width)
                                            width: Math.abs(balSlider.visualPosition * parent.width - parent.width / 2)
                                            height: parent.height
                                            color: "#A3BE8C"
                                            radius: 2
                                        }
                                    }

                                    handle: Rectangle {
                                        x: balSlider.leftPadding + balSlider.visualPosition * (balSlider.availableWidth - width)
                                        y: balSlider.topPadding + balSlider.availableHeight / 2 - height / 2
                                        width: 12
                                        height: 12
                                        radius: 6
                                        color: Theme.fg
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    propagateComposedEvents: true
                                    onPressed: mouse => { mouse.accepted = false; }
                                    onDoubleClicked: root.setBalance(0)
                                    onWheel: wheel => {
                                        root.setBalance(root.balanceVal + (wheel.angleDelta.y > 0 ? 1 : -1));
                                    }
                                }
                            }

                            Text {
                                text: root.balLabel(root.balanceVal)
                                color: root.balanceVal !== 0 ? "#A3BE8C" : Theme.fgDim
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: true
                                Layout.preferredWidth: 28
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }
                }

                // Linha 3: Botões de Pré-Ajustes + Salvar
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    spacing: 6

                    Repeater {
                        model: root.profileList
                        Rectangle {
                            Layout.preferredWidth: profText.implicitWidth + 20
                            Layout.preferredHeight: 28
                            radius: 6
                            color: root.currentProfile === modelData ? "#434C5E" : "#3B4252"
                            border.color: root.currentProfile === modelData ? "#88C0D0" : "transparent"
                            border.width: 1

                            Text {
                                id: profText
                                anchors.centerIn: parent
                                text: modelData
                                color: root.currentProfile === modelData ? Theme.fg : Theme.fgDim
                                font.family: Theme.font
                                font.pixelSize: 11
                                font.bold: root.currentProfile === modelData
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.applyProfile(modelData)
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 28
                        radius: 6
                        color: root.savingMode ? "#88C0D0" : "#3B4252"
                        border.color: "#88C0D0"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: root.savingMode ? "Cancelar" : "+ Salvar"
                            color: root.savingMode ? "#2E3440" : "#88C0D0"
                            font.family: Theme.font
                            font.pixelSize: 10
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.savingMode = !root.savingMode
                        }
                    }
                }

                // Campo de nome ao salvar
                Rectangle {
                    visible: root.savingMode
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: 6
                    color: "#3B4252"
                    border.color: "#88C0D0"
                    border.width: 1

                    TextInput {
                        id: newProfileInput
                        anchors.fill: parent
                        anchors.margins: 6
                        color: Theme.fg
                        font.family: Theme.font
                        font.pixelSize: 11
                        clip: true
                        onAccepted: {
                            root.saveNewProfile(text);
                            text = "";
                        }
                        Text {
                            anchors.fill: parent
                            text: "Digite o nome do perfil + Enter..."
                            color: Theme.fgDim
                            font.family: Theme.font
                            font.pixelSize: 11
                            visible: !newProfileInput.text
                        }
                    }
                }

                // Linha 4: Mesa de 31 Sliders Verticais
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#3B4252"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2

                        Repeater {
                            model: 31

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 3

                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: root.bandGains[index].toFixed(1)
                                    color: root.bandGains[index] !== 1.0 ? "#88C0D0" : Theme.fgDim
                                    font.family: Theme.font
                                    font.pixelSize: 8
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true

                                    Slider {
                                        id: bandSlider
                                        anchors.fill: parent
                                        orientation: Qt.Vertical
                                        from: 0.10
                                        to: 3.00
                                        stepSize: 0.10
                                        value: root.bandGains[index]
                                        onMoved: root.updateBand(index, value)

                                        background: Rectangle {
                                            x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                                            y: bandSlider.topPadding
                                            width: 3
                                            height: bandSlider.availableHeight
                                            radius: 1.5
                                            color: "#4C566A"

                                            Rectangle {
                                                y: parent.height * (1.0 - (1.00 - 0.10) / (3.00 - 0.10))
                                                width: 8
                                                height: 1.5
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                color: "#81A1C1"
                                            }

                                            Rectangle {
                                                y: bandSlider.visualPosition * parent.height
                                                width: parent.width
                                                height: parent.height - y
                                                color: "#88C0D0"
                                                radius: 1.5
                                            }
                                        }

                                        handle: Rectangle {
                                            x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                                            y: bandSlider.topPadding + bandSlider.visualPosition * (bandSlider.availableHeight - height)
                                            width: 10
                                            height: 10
                                            radius: 5
                                            color: Theme.fg
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        propagateComposedEvents: true
                                        onPressed: mouse => { mouse.accepted = false; }
                                        onDoubleClicked: root.updateBand(index, 1.00)
                                        onWheel: wheel => {
                                            var delta = wheel.angleDelta.y > 0 ? 0.10 : -0.10;
                                            root.updateBand(index, root.bandGains[index] + delta);
                                        }
                                    }
                                }

                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: root.freqLabels[index]
                                    color: Theme.fgDim
                                    font.family: Theme.font
                                    font.pixelSize: 8
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
