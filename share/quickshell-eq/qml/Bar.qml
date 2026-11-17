import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Hyprland
import Quickshell.Services.SystemTray
import Quickshell.Widgets

PanelWindow {
    id: bar

    anchors {
        top: true
        left: true
        right: true
    }
    implicitHeight: Theme.barHeight
    color: Theme.bg

    readonly property var ptBR: Qt.locale("pt_BR")

    // ── Funções auxiliares ────────────────────────────────────────────────
    function level(v, warn, crit) {
        return v >= crit ? Theme.crit : (v >= warn ? Theme.warn : Theme.fg);
    }

    function batIcon(pct, charging) {
        if (charging) return Theme.icCharge;
        if (pct > 85) return Theme.icBat100;
        if (pct > 60) return Theme.icBat75;
        if (pct > 35) return Theme.icBat50;
        if (pct > 10) return Theme.icBat25;
        return Theme.icBat0;
    }

    function volIcon(vol, muted) {
        if (muted || vol <= 0.001) return Theme.icVolMute;
        return vol < 0.4 ? Theme.icVolLow : Theme.icVolHigh;
    }

    function clip(s, n) {
        s = s || "";
        return s.length > n ? s.slice(0, n - 1) + "…" : s;
    }

    // Janelas de uma área de trabalho: ["classe  título", ...]
    function wsApps(id) {
        const out = [];
        for (const t of Hyprland.toplevels.values) {
            const o = t.lastIpcObject ?? {};
            const wid = t.workspace?.id ?? o.workspace?.id;
            if (wid !== id)
                continue;
            const cls = o.class || "";
            const title = t.title || o.title || "";
            out.push(cls ? cls + "  " + clip(title, 48) : clip(title, 60));
        }
        return out;
    }

    function wsText(id) {
        const apps = wsApps(id);
        if (apps.length === 0)
            return "Nenhuma janela";
        const shown = apps.slice(0, 8).map(a => "• " + a);
        if (apps.length > 8)
            shown.push("… e mais " + (apps.length - 8));
        return shown.join("\n");
    }

    function activeWindowText() {
        const t = Hyprland.activeToplevel;
        if (!t)
            return "";
        const o = t.lastIpcObject ?? {};
        const lines = [];
        if (o.class) lines.push("Classe     " + o.class);
        if (o.pid) lines.push("PID        " + o.pid);
        if (o.workspace) lines.push("Área       " + o.workspace.id);
        if (o.floating !== undefined) lines.push("Flutuante  " + (o.floating ? "sim" : "não"));
        if (o.xwayland !== undefined) lines.push("XWayland   " + (o.xwayland ? "sim" : "não"));
        return lines.join("\n");
    }

    function upText(now) {
        if (!Stats.osBoot)
            return "";
        let s = Math.max(0, Math.floor(now.getTime() / 1000) - Stats.osBoot);
        const d = Math.floor(s / 86400); s -= d * 86400;
        const h = Math.floor(s / 3600); s -= h * 3600;
        const m = Math.floor(s / 60);
        return (d > 0 ? d + "d " : "") + h + "h " + m + "min";
    }

    function isoWeek(d) {
        const t = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
        const day = t.getUTCDay() || 7;
        t.setUTCDate(t.getUTCDate() + 4 - day);
        const y0 = new Date(Date.UTC(t.getUTCFullYear(), 0, 1));
        return Math.ceil(((t - y0) / 86400000 + 1) / 7);
    }

    function dayOfYear(d) {
        return Math.floor((new Date(d.getFullYear(), d.getMonth(), d.getDate()) - new Date(d.getFullYear(), 0, 0)) / 86400000);
    }

    function utcOffset(d) {
        const o = -d.getTimezoneOffset();
        const sign = o >= 0 ? "+" : "-";
        const a = Math.abs(o);
        return "UTC" + sign + String(Math.floor(a / 60)).padStart(2, "0") + ":" + String(a % 60).padStart(2, "0");
    }

    SystemClock {
        id: clock
        precision: SystemClock.Seconds
    }

    Tip {
        parentWindow: bar
    }

    // Pulsos de feedback quando volume, mudo ou carregador mudam
    Connections {
        target: Stats
        function onVolumeChanged() { volPill.pulse(); }
        function onMutedChanged() { volPill.pulse(); }
        function onChargingChanged() { batPill.pulse(); }
    }

    Item {
        id: content
        anchors.fill: parent
        opacity: 0
        transform: Translate { id: enterShift; y: -Theme.barHeight }

        // Entrada: a barra desce do topo com fade
        Component.onCompleted: enterAnim.start()

        ParallelAnimation {
            id: enterAnim
            NumberAnimation { target: content; property: "opacity"; to: 1; duration: Theme.dur(300); easing.type: Easing.OutCubic }
            NumberAnimation { target: enterShift; property: "y"; to: 0; duration: Theme.dur(380); easing.type: Easing.OutCubic }
        }

        // ── Esquerda: logo + workspaces 1-10 + janela ativa ──────────────
        RowLayout {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Pill {
                icon: "\uF120"
                iconColor: Theme.fgDim
                minWidth: 30
                tipTitle: "Terminal suspenso"
                tipText: "Clique: mostrar ou ocultar"
                onClicked: Quickshell.execDetached(["sh", Quickshell.shellPath("scripts/term.sh")])
            }

            Pill {
                icon: Theme.icFreeBSD
                iconColor: Theme.brand
                minWidth: 30
                tipTitle: "FreeBSD " + Stats.osUser
                tipText: "Kernel   " + Stats.osKernel
                       + "\nArquit.  " + Stats.osArch
                       + "\nHost     " + Stats.osHost
                       + (Stats.osBoot ? "\nAtivo    " + bar.upText(clock.date) : "")
            }

            Repeater {
                model: 10
                delegate: Pill {
                    required property int index
                    readonly property int wsId: index + 1
                    readonly property bool focused: Hyprland.focusedWorkspace?.id === wsId
                    readonly property bool occupied: Hyprland.workspaces.values.some(w => w.id === wsId)
                    readonly property int count: bar.wsApps(wsId).length

                    text: wsId
                    minWidth: focused ? 38 : 26
                    active: focused
                    dim: !occupied && !focused
                    tipTitle: "Área de trabalho " + wsId
                            + (count > 0 ? "  ·  " + count + (count === 1 ? " janela" : " janelas") : "  ·  vazia")
                            + (focused ? "  (atual)" : "")
                    tipText: bar.wsText(wsId)
                    tipHint: focused ? "" : "Clique: ir para a área " + wsId
                    onTipOpening: {
                        Hyprland.refreshToplevels();
                        Hyprland.refreshWorkspaces();
                    }
                    onClicked: Hyprland.dispatch("workspace " + wsId)
                }
            }

            TipArea {
                id: titleArea
                Layout.leftMargin: 8
                Layout.maximumWidth: 420
                implicitWidth: titleText.implicitWidth
                implicitHeight: titleText.implicitHeight
                tipTitle: Hyprland.activeToplevel?.title ?? ""
                tipText: bar.activeWindowText()
                onTipOpening: Hyprland.refreshToplevels()

                Text {
                    id: titleText
                    anchors.fill: parent
                    onTextChanged: titleFade.restart()

                    NumberAnimation {
                        id: titleFade
                        target: titleText
                        property: "opacity"
                        from: 0.2
                        to: 1
                        duration: Theme.dur(220)
                        easing.type: Easing.OutCubic
                    }
                    text: Hyprland.activeToplevel?.title ?? ""
                    color: Theme.fgDim
                    elide: Text.ElideRight
                    font.family: Theme.font
                    font.pixelSize: Theme.fontSize
                }
            }
        }

        // ── Centro: relógio ──────────────────────────────────────────────
        TipArea {
            anchors.centerIn: parent
            width: clockText.implicitWidth
            height: clockText.implicitHeight
            tipTitle: bar.ptBR.toString(clock.date, "dddd, d 'de' MMMM 'de' yyyy")
            tipText: "Semana ISO  " + bar.isoWeek(clock.date) + "  ·  dia " + bar.dayOfYear(clock.date)
                   + "\nFuso        " + Qt.formatDateTime(clock.date, "t") + "  (" + bar.utcOffset(clock.date) + ")"
                   + "\nUTC         " + clock.date.toISOString().slice(11, 19)

            Text {
                id: clockText
                anchors.fill: parent
                text: Qt.formatDateTime(clock.date, "ddd dd MMM  HH:mm:ss")
                color: Theme.fg
                font.family: Theme.font
                font.pixelSize: Theme.fontSize
                font.bold: true
            }
        }

        // ── Direita: métricas, volume, bateria, bandeja ──────────────────
        RowLayout {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Pill {
                visible: Stats.wifi !== ""
                icon: Theme.icWifi
                text: Stats.wifi
                tipRight: true
                tipKind: "wifi"
                tipTitle: "Wi-Fi  ·  " + Stats.wifi
                tipText: Stats.d("wifi")
            }
            Pill {
                icon: Theme.icNet
                text: Stats.netIf !== "" ? Stats.netIf + " " + Stats.netIp : "offline"
                fg: Stats.netIf !== "" ? Theme.fg : Theme.crit
                alert: Stats.netIf === ""
                tipRight: true
                tipKind: "net"
                tipTitle: Stats.netIf !== "" ? "Rede  ·  " + Stats.netIf : "Sem conectividade"
                tipText: Stats.d("net")
            }
            Pill {
                icon: Theme.icCpu
                text: Math.round(Stats.cpu) + "%"
                fg: bar.level(Stats.cpu, 60, 85)
                alert: Stats.cpu >= 85
                tipRight: true
                tipKind: "cpu"
                tipTitle: "CPU  ·  " + Math.round(Stats.cpu) + "% de uso"
                tipText: Stats.d("cpu")
            }
            Pill {
                visible: Stats.hasTemp
                icon: Theme.icTemp
                text: Math.round(Stats.temp) + "°C"
                fg: bar.level(Stats.temp, 75, 90)
                alert: Stats.temp >= 90
                tipRight: true
                tipKind: "temp"
                tipTitle: "Temperatura  ·  " + Math.round(Stats.temp) + "°C"
                tipText: Stats.d("temp")
            }
            Pill {
                icon: Theme.icMem
                text: Math.round(Stats.mem) + "%"
                fg: bar.level(Stats.mem, 70, 90)
                alert: Stats.mem >= 90
                tipRight: true
                tipKind: "mem"
                tipTitle: "Memória  ·  " + Math.round(Stats.mem) + "% em uso"
                tipText: Stats.d("mem")
            }
            Pill {
                icon: Theme.icDisk
                text: Math.round(Stats.disk) + "%"
                fg: bar.level(Stats.disk, 80, 92)
                alert: Stats.disk >= 92
                tipRight: true
                tipKind: "disk"
                tipTitle: "Disco  ·  / em " + Math.round(Stats.disk) + "%"
                tipText: Stats.d("disk")
            }
            EqualizerPopup {}
            Pill {
                id: volPill
                icon: bar.volIcon(Stats.volume, Stats.muted)
                text: Stats.muted ? "mudo" : Math.round(Stats.volume * 100) + "%"
                dim: Stats.muted
                tipRight: true
                tipKind: "vol"
                tipTitle: "Áudio  ·  " + (Stats.muted ? "mudo" : Math.round(Stats.volume * 100) + "%")
                tipText: Stats.d("vol")
                tipHint: "Clique: alternar mudo  ·  Roda: ±5%"
                onClicked: Stats.toggleMute()
                onScrolled: delta => Stats.setVolume(Stats.volume + (delta > 0 ? 0.05 : -0.05))
            }
            Pill {
                visible: Stats.hasBattery
                id: batPill
                icon: bar.batIcon(Stats.battery, Stats.charging)
                text: Stats.battery + "%"
                fg: Stats.charging ? Theme.ok : (Stats.battery <= 15 ? Theme.crit : (Stats.battery <= 30 ? Theme.warn : Theme.fg))
                alert: !Stats.charging && Stats.battery <= 15
                tipRight: true
                tipKind: "bat"
                tipTitle: "Bateria  ·  " + Stats.battery + "%" + (Stats.charging ? "  (carregando)" : "")
                tipText: Stats.d("bat")
            }

            Repeater {
                model: SystemTray.items
                delegate: TipArea {
                    id: trayItem
                    required property var modelData

                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

                    tipRight: true
                    tipTitle: modelData.tooltipTitle || modelData.title || modelData.id
                    tipText: modelData.tooltipDescription || ""
                    tipHint: "Clique: abrir  ·  Meio: ação secundária  ·  Direito: menu"

                    onClicked: mouse => {
                        const showMenu = () => {
                            if (!modelData.hasMenu)
                                return;
                            const p = trayItem.mapToItem(content, 0, trayItem.height + 4);
                            modelData.display(bar, p.x, p.y);
                        };
                        if (mouse.button === Qt.LeftButton) {
                            if (modelData.onlyMenu)
                                showMenu();
                            else
                                modelData.activate();
                        } else if (mouse.button === Qt.MiddleButton) {
                            modelData.secondaryActivate();
                        } else {
                            showMenu();
                        }
                    }

                    IconImage {
                        anchors.fill: parent
                        source: trayItem.modelData.icon
                        scale: trayItem.containsMouse ? 1.18 : 1

                        Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
                    }
                }
            }
        }
    }
}
