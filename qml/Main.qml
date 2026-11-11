// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtCore
import org.kde.kirigami as Kirigami
import codes.noa.touko

Kirigami.ApplicationWindow {
    id: root

    title: currentSession ? currentSession.title : appName
    width: Kirigami.Units.gridUnit * 24
    height: Kirigami.Units.gridUnit * 40
    minimumWidth: Kirigami.Units.gridUnit * 10
    minimumHeight: Kirigami.Units.gridUnit * 10

    property int currentIndex: 0
    readonly property TerminalSession currentSession: sessionModel.count > 0
        ? sessionModel.sessionAt(Math.min(currentIndex, sessionModel.count - 1)) : null

    // Terminal colours follow the system unless the user picked a side.
    readonly property bool systemDark: Kirigami.Theme.backgroundColor.hslLightness < 0.5
    readonly property bool terminalDark: prefs.colorScheme === "dark"
        || (prefs.colorScheme === "system" && systemDark)

    Settings {
        id: prefs
        property string fontFamily: ""
        property real fontSize: 11
        property real lineHeight: 1.0
        property string lightTheme: "GitHub Light Default"
        property string darkTheme: "Breeze"
        property string colorScheme: "system"
        property bool extraKeys: false
        property bool visualBell: true
    }

    SessionModel {
        id: sessionModel
        onSessionFinished: session => {
            // A shell that exits cleanly closes its tab; a failure stays up
            // so its last words can be read.
            if (session.exitCode === 0)
                root.closeTab(sessionModel.indexOf(session), true)
        }
    }

    function newTab() {
        const wd = currentSession ? currentSession.workingDirectory : ""
        sessionModel.newSession(wd)
        currentIndex = sessionModel.count - 1
    }

    function closeTab(index, force) {
        const s = sessionModel.sessionAt(index)
        if (!s)
            return
        if (!force && s.running && s.busy) {
            closePrompt.index = index
            closePrompt.open()
            return
        }
        sessionModel.closeSession(index)
        if (sessionModel.count === 0) {
            Qt.quit()
            return
        }
        if (currentIndex >= sessionModel.count)
            currentIndex = sessionModel.count - 1
    }

    Component.onCompleted: {
        sessionModel.newSessionWithCommand(startup.command, startup.workdir)
        if (startup.testPage)
            testTimer.start()
    }

    // Screenshots of the other screens, for testing without a phone.
    Timer {
        id: testTimer
        interval: 2000
        onTriggered: {
            switch (startup.testPage) {
            case "settings":
                root.pageStack.layers.push(Qt.resolvedUrl("SettingsPage.qml"), { prefs: prefs })
                break
            case "light-themes":
            case "dark-themes":
                root.pageStack.layers.push(Qt.resolvedUrl("ThemePicker.qml"),
                                           { prefs: prefs, dark: startup.testPage === "dark-themes" })
                break
            case "tabs":
                sessionModel.newSession("")
                sessionModel.newSession("/tmp")
                drawer.open()
                break
            case "select":
                terminalPage.terminal.selectWordAtPoint(terminalPage.terminal.cellWidth * 2, terminalPage.terminal.cellHeight * 0.5)
                terminalPage.openContextMenu(terminalPage.terminal.cellWidth * 2, terminalPage.terminal.cellHeight * 2)
                break
            }
        }
    }

    pageStack.initialPage: TerminalPage {
        id: terminalPage
    }
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.ShowBackButton
    // One slim bar on top, also on phones: the bottom action bar would take
    // rows away from the terminal.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar

    globalDrawer: Kirigami.GlobalDrawer {
        id: drawer
        handleVisible: false
        modal: true
        title: appName
        titleIcon: "utilities-terminal"

        topContent: [
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: sessionModel
                    delegate: QQC2.ItemDelegate {
                        id: tabDelegate
                        required property int index
                        required property string title
                        required property bool running
                        Layout.fillWidth: true
                        highlighted: index === root.currentIndex
                        onClicked: {
                            root.currentIndex = index
                            drawer.close()
                        }
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Icon {
                                source: tabDelegate.running ? "utilities-terminal" : "process-stop"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                            }
                            QQC2.Label {
                                text: tabDelegate.title
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            QQC2.ToolButton {
                                icon.name: "tab-close"
                                display: QQC2.AbstractButton.IconOnly
                                text: qsTr("Close")
                                onClicked: root.closeTab(tabDelegate.index, false)
                            }
                        }
                    }
                }
                Kirigami.Separator {
                    Layout.fillWidth: true
                }
            }
        ]

        actions: [
            Kirigami.Action {
                text: qsTr("New Tab")
                icon.name: "tab-new"
                onTriggered: root.newTab()
            },
            Kirigami.Action {
                text: qsTr("Settings")
                icon.name: "configure"
                onTriggered: root.pageStack.layers.push(Qt.resolvedUrl("SettingsPage.qml"), { prefs: prefs })
            }
        ]
    }

    Kirigami.PromptDialog {
        id: closePrompt
        property int index: -1
        title: qsTr("Close this tab?")
        subtitle: qsTr("Something is still running in it.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.closeTab(index, true)
    }

    Shortcut {
        sequences: ["Ctrl+Shift+T"]
        onActivated: root.newTab()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+W"]
        onActivated: root.closeTab(root.currentIndex, false)
    }
    Shortcut {
        sequences: ["Ctrl+PgDown"]
        onActivated: root.currentIndex = (root.currentIndex + 1) % sessionModel.count
    }
    Shortcut {
        sequences: ["Ctrl+PgUp"]
        onActivated: root.currentIndex = (root.currentIndex + sessionModel.count - 1) % sessionModel.count
    }

    property alias sessions: sessionModel
    property alias settings: prefs
}
