// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import codes.noa.touko

// Ghostty's themes, split into light and dark by their background.
Kirigami.ScrollablePage {
    id: picker

    required property var prefs
    required property bool dark

    title: dark ? qsTr("Dark Theme") : qsTr("Light Theme")

    header: QQC2.Control {
        padding: Kirigami.Units.smallSpacing
        contentItem: Kirigami.SearchField {
            id: search
            onTextChanged: list.filter = text.toLowerCase()
        }
    }

    ListView {
        id: list
        property string filter: ""
        readonly property var all: picker.dark ? ThemeList.darkThemes : ThemeList.lightThemes
        model: filter.length ? all.filter(n => n.toLowerCase().indexOf(filter) >= 0) : all
        reuseItems: true

        delegate: QQC2.ItemDelegate {
            id: row
            required property string modelData
            readonly property var colors: ThemeList.preview(modelData)
            width: ListView.view.width
            highlighted: modelData === (picker.dark ? picker.prefs.darkTheme : picker.prefs.lightTheme)
            onClicked: {
                if (picker.dark)
                    picker.prefs.darkTheme = modelData
                else
                    picker.prefs.lightTheme = modelData
                applicationWindow().pageStack.layers.pop()
            }
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Rectangle {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 2
                    radius: Kirigami.Units.cornerRadius
                    color: row.colors.background || "transparent"
                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "$ ls -la"
                            font.family: "monospace"
                            color: row.colors.foreground || Kirigami.Theme.textColor
                        }
                        Row {
                            anchors.horizontalCenter: parent.horizontalCenter
                            Repeater {
                                model: row.colors.palette ? row.colors.palette.slice(1, 7) : []
                                Rectangle {
                                    required property color modelData
                                    width: Kirigami.Units.gridUnit * 0.7
                                    height: Kirigami.Units.gridUnit * 0.35
                                    color: modelData
                                }
                            }
                        }
                    }
                }
                QQC2.Label {
                    text: row.modelData
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }
    }
}
