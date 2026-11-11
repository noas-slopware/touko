// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import codes.noa.touko

Kirigami.Page {
    id: page

    title: root.currentSession ? root.currentSession.title : appName
    padding: 0

    // itemAt() is not something a binding can watch; count is.
    readonly property TerminalItem terminal: {
        const n = views.count
        const view = n > 0 ? views.itemAt(Math.min(root.currentIndex, n - 1)) : null
        return view ? view.terminal : null
    }
    readonly property TerminalSession session: terminal ? terminal.session : null

    background: Rectangle {
        color: page.terminal ? page.terminal.backgroundColor : Kirigami.Theme.backgroundColor
    }

    actions: [
        Kirigami.Action {
            text: qsTr("New Tab")
            icon.name: "tab-new"
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: root.newTab()
        },
        Kirigami.Action {
            text: qsTr("Tabs")
            icon.name: "view-list-symbolic"
            displayHint: Kirigami.DisplayHint.IconOnly
            visible: sessionModel.count > 1
            onTriggered: root.globalDrawer.open()
        },
        Kirigami.Action {
            text: qsTr("Paste")
            icon.name: "edit-paste"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.paste()
        },
        Kirigami.Action {
            text: qsTr("Copy")
            icon.name: "edit-copy"
            enabled: page.session && page.session.hasSelection
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.copy()
        },
        Kirigami.Action {
            text: qsTr("Select All")
            icon.name: "edit-select-all"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.session.selectAll()
        },
        Kirigami.Action {
            text: qsTr("Previous Prompt")
            icon.name: "go-up"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.session.scrollPrompt(-1)
        },
        Kirigami.Action {
            text: qsTr("Bigger Text")
            icon.name: "zoom-in"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: root.settings.fontSize = Math.min(48, root.settings.fontSize + 1)
        },
        Kirigami.Action {
            text: qsTr("Smaller Text")
            icon.name: "zoom-out"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: root.settings.fontSize = Math.max(4, root.settings.fontSize - 1)
        },
        Kirigami.Action {
            text: qsTr("Close Tab")
            icon.name: "tab-close"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: root.closeTab(root.currentIndex, false)
        },
        Kirigami.Action {
            text: qsTr("Settings")
            icon.name: "configure"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: root.pageStack.layers.push(Qt.resolvedUrl("SettingsPage.qml"), { prefs: root.settings })
        }
    ]

    function openContextMenu(x, y) {
        contextMenu.popup(terminal, x, y)
    }

    function copy() {
        if (!session)
            return
        session.copySelection()
        terminal.endSelectionMode()
        root.showPassiveNotification(qsTr("Copied"))
    }

    function paste() {
        if (!session)
            return
        terminal.endSelectionMode()
        if (!session.pasteClipboard(false))
            pastePrompt.open()
        terminal.showKeyboard()
    }

    // The on-screen keyboard: when the compositor does not shrink the
    // window for it, keep the prompt above it ourselves.
    readonly property real keyboardOverlap: {
        const r = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || r.height <= 0)
            return 0
        const top = page.mapFromGlobal(0, r.y).y
        return Math.max(0, Math.min(r.height, page.height - top))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: page.keyboardOverlap
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                id: views
                model: sessionModel

                delegate: Item {
                    id: view
                    required property int index
                    required property TerminalSession session
                    readonly property bool current: index === root.currentIndex
                    property alias terminal: term

                    anchors.fill: parent
                    visible: current

                    TerminalItem {
                        id: term
                        anchors.fill: parent
                        session: view.session
                        focus: view.current
                        fontFamily: root.settings.fontFamily
                        fontSize: root.settings.fontSize
                        lineHeight: root.settings.lineHeight
                        lightTheme: root.settings.lightTheme
                        darkTheme: root.settings.darkTheme
                        dark: root.terminalDark

                        onFontSizeChanged: if (fontSize !== root.settings.fontSize) root.settings.fontSize = fontSize
                        onLinkActivated: url => {
                            linkPrompt.url = url
                            linkPrompt.open()
                        }
                        onContextMenuRequested: (x, y) => {
                            contextMenu.popup(term, x, y)
                        }
                        Component.onCompleted: if (view.current) forceActiveFocus()
                    }

                    onCurrentChanged: if (current) term.forceActiveFocus()

                    Connections {
                        target: view.session
                        function onBell() {
                            if (root.settings.visualBell && view.current)
                                bellFlash.restart()
                        }
                        function onNotify(title, body) {
                            if (!root.active || !view.current)
                                Notifier.notify(title || view.session.title, body)
                        }
                    }

                    // Visual bell: a short flash of the foreground colour.
                    Rectangle {
                        id: flash
                        anchors.fill: parent
                        color: term.foregroundColor
                        opacity: 0
                        NumberAnimation on opacity {
                            id: bellFlash
                            running: false
                            from: 0.18
                            to: 0
                            duration: 180
                        }
                    }

                    // Selection handles, for dragging either end after a long press.
                    Repeater {
                        model: term.selecting && view.session.hasSelection ? 2 : 0
                        delegate: Rectangle {
                            id: handle
                            required property int index
                            readonly property bool isStart: index === 0
                            readonly property rect anchorRect: isStart ? term.selectionStartRect : term.selectionEndRect
                            visible: anchorRect.width > 0
                            width: Kirigami.Units.gridUnit
                            height: width
                            radius: width / 2
                            color: Kirigami.Theme.highlightColor
                            x: isStart ? anchorRect.x - width / 2 : anchorRect.x + anchorRect.width - width / 2
                            y: anchorRect.y + anchorRect.height
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.top
                                width: 2
                                height: term.cellHeight
                                color: Kirigami.Theme.highlightColor
                            }
                            DragHandler {
                                target: null
                                onCentroidChanged: {
                                    if (!active)
                                        return
                                    const p = handle.mapToItem(term, centroid.position.x, centroid.position.y - handle.height - term.cellHeight / 2)
                                    term.moveSelectionEdge(handle.isStart, p.x, p.y)
                                }
                                onActiveChanged: if (!active) contextMenu.popup(term, handle.x, handle.y + handle.height)
                            }
                        }
                    }

                    // Back to the prompt after scrolling up.
                    QQC2.RoundButton {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Kirigami.Units.largeSpacing
                        visible: !view.session.atBottom
                        icon.name: "go-bottom"
                        text: qsTr("Back to the prompt")
                        display: QQC2.AbstractButton.IconOnly
                        focusPolicy: Qt.NoFocus
                        onClicked: view.session.scrollToBottom()
                    }

                    // Scroll position, while not at the bottom.
                    Rectangle {
                        anchors.right: parent.right
                        width: Math.round(Kirigami.Units.smallSpacing * 1.5)
                        radius: width / 2
                        color: term.foregroundColor
                        opacity: 0.35
                        visible: !view.session.atBottom && view.session.scrollSize > 0
                        readonly property real total: Math.max(1, view.session.scrollSize)
                        height: Math.max(Kirigami.Units.gridUnit, parent.height * view.session.rows / total)
                        y: (parent.height - height) * Math.min(1, view.session.scrollPosition / Math.max(1, total - view.session.rows))
                    }

                    // A shell that failed: say so, offer a way out.
                    Kirigami.InlineMessage {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Kirigami.Units.largeSpacing
                        visible: !view.session.running
                        type: Kirigami.MessageType.Warning
                        text: qsTr("The shell exited with code %1.").arg(view.session.exitCode)
                        actions: [
                            Kirigami.Action {
                                text: qsTr("Close Tab")
                                icon.name: "tab-close"
                                onTriggered: root.closeTab(view.index, true)
                            }
                        ]
                    }
                }
            }
        }

        ExtraKeys {
            Layout.fillWidth: true
            visible: root.settings.extraKeys
            terminal: page.terminal
        }
    }

    QQC2.Menu {
        id: contextMenu
        // Back to the terminal, so the keyboard comes back with it.
        onClosed: if (page.terminal) page.terminal.forceActiveFocus()
        QQC2.MenuItem {
            text: qsTr("Copy")
            icon.name: "edit-copy"
            enabled: page.session && page.session.hasSelection
            onTriggered: page.copy()
        }
        QQC2.MenuItem {
            text: qsTr("Paste")
            icon.name: "edit-paste"
            onTriggered: page.paste()
        }
        QQC2.MenuItem {
            text: qsTr("Select All")
            icon.name: "edit-select-all"
            onTriggered: page.session.selectAll()
        }
    }

    Kirigami.PromptDialog {
        id: pastePrompt
        onClosed: if (page.terminal) page.terminal.forceActiveFocus()
        title: qsTr("Paste several lines?")
        subtitle: qsTr("The clipboard has line breaks in it, so the shell may run it right away.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: page.session.pasteClipboard(true)
    }

    Kirigami.PromptDialog {
        id: linkPrompt
        onClosed: if (page.terminal) page.terminal.forceActiveFocus()
        property string url
        title: qsTr("Open this link?")
        subtitle: url
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: Qt.openUrlExternally(url)
    }
}
