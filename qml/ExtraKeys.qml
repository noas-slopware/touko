// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import codes.noa.touko

// The keys an on-screen keyboard does not have, in one scrollable row.
// None of them take focus: that would hide the keyboard.
QQC2.ToolBar {
    id: bar

    property TerminalItem terminal
    // A double tap locks Ctrl or Alt until the next tap.
    property bool ctrlLocked: false
    property bool altLocked: false

    position: QQC2.ToolBar.Footer
    padding: 0

    readonly property var keys: [
        { label: "Esc", key: "Esc" },
        { label: "Ctrl", mod: "ctrl" },
        { label: "Alt", mod: "alt" },
        { label: "Tab", key: "Tab" },
        { label: "←", key: "Left", repeat: true },
        { label: "↓", key: "Down", repeat: true },
        { label: "↑", key: "Up", repeat: true },
        { label: "→", key: "Right", repeat: true },
        { label: "|", key: "|" },
        { label: "~", key: "~" },
        { label: "/", key: "/" },
        { label: "-", key: "-" },
        { label: "Home", key: "Home" },
        { label: "End", key: "End" },
        { label: "PgUp", key: "PgUp", repeat: true },
        { label: "PgDn", key: "PgDn", repeat: true },
        { label: "Del", key: "Del", repeat: true },
        { label: "F1", key: "F1" }, { label: "F2", key: "F2" }, { label: "F3", key: "F3" },
        { label: "F4", key: "F4" }, { label: "F5", key: "F5" }, { label: "F6", key: "F6" },
        { label: "F7", key: "F7" }, { label: "F8", key: "F8" }, { label: "F9", key: "F9" },
        { label: "F10", key: "F10" }, { label: "F11", key: "F11" }, { label: "F12", key: "F12" }
    ]

    Connections {
        target: bar.terminal
        function onLatchChanged() {
            if (bar.ctrlLocked && !bar.terminal.ctrlLatched)
                bar.terminal.ctrlLatched = true
            if (bar.altLocked && !bar.terminal.altLatched)
                bar.terminal.altLatched = true
        }
    }

    contentItem: ListView {
        id: list
        orientation: ListView.Horizontal
        implicitHeight: Kirigami.Units.gridUnit * 2.25
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: bar.keys
        // About nine keys fill a phone's width; the rest scroll in.
        readonly property real keyWidth: Math.max(Kirigami.Units.gridUnit * 2.2, width / 9)

        delegate: QQC2.ToolButton {
            id: keyButton
            required property var modelData
            readonly property bool isMod: modelData.mod !== undefined
            readonly property bool latched: bar.terminal && (modelData.mod === "ctrl" ? bar.terminal.ctrlLatched
                                                            : modelData.mod === "alt" ? bar.terminal.altLatched : false)
            readonly property bool locked: modelData.mod === "ctrl" ? bar.ctrlLocked
                                         : modelData.mod === "alt" ? bar.altLocked : false
            property double lastTap: 0

            width: list.keyWidth
            height: list.height
            text: modelData.label
            focusPolicy: Qt.NoFocus
            checkable: false
            checked: latched
            autoRepeat: modelData.repeat === true
            font.bold: locked
            font.underline: locked
            Accessible.name: modelData.label
            // No accelerator underlines: these are keys, not menu entries.
            Kirigami.MnemonicData.enabled: false

            onClicked: {
                if (!bar.terminal)
                    return
                if (isMod) {
                    const now = Date.now()
                    const doubleTap = now - lastTap < 350
                    lastTap = now
                    if (modelData.mod === "ctrl") {
                        if (doubleTap) {
                            bar.ctrlLocked = true
                            bar.terminal.ctrlLatched = true
                        } else if (bar.ctrlLocked || bar.terminal.ctrlLatched) {
                            bar.ctrlLocked = false
                            bar.terminal.ctrlLatched = false
                        } else {
                            bar.terminal.ctrlLatched = true
                        }
                    } else {
                        if (doubleTap) {
                            bar.altLocked = true
                            bar.terminal.altLatched = true
                        } else if (bar.altLocked || bar.terminal.altLatched) {
                            bar.altLocked = false
                            bar.terminal.altLatched = false
                        } else {
                            bar.terminal.altLatched = true
                        }
                    }
                    return
                }
                bar.terminal.sendKeyName(modelData.key)
            }
        }
    }
}
