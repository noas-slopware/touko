// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import codes.noa.touko

Kirigami.ScrollablePage {
    id: settingsPage

    required property var prefs

    title: qsTr("Settings")

    Kirigami.FormLayout {
        QQC2.ComboBox {
            id: fontBox
            Kirigami.FormData.label: qsTr("Font:")
            Layout.fillWidth: true
            model: [qsTr("System monospace")].concat(Notifier.monospaceFonts())
            currentIndex: Math.max(0, model.indexOf(settingsPage.prefs.fontFamily))
            onActivated: index => settingsPage.prefs.fontFamily = index === 0 ? "" : model[index]
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: qsTr("Size:")
            from: 40
            to: 480
            stepSize: 5
            value: Math.round(settingsPage.prefs.fontSize * 10)
            textFromValue: (v, locale) => Number(v / 10).toLocaleString(locale, "f", 1)
            valueFromText: (t, locale) => Math.round(Number.fromLocaleString(locale, t) * 10)
            onValueModified: settingsPage.prefs.fontSize = value / 10
        }

        RowLayout {
            Kirigami.FormData.label: qsTr("Line height:")
            QQC2.Slider {
                id: lineSlider
                from: 0.9
                to: 1.6
                stepSize: 0.05
                value: settingsPage.prefs.lineHeight
                onMoved: settingsPage.prefs.lineHeight = value
                Layout.fillWidth: true
            }
            QQC2.Label {
                text: Math.round(lineSlider.value * 100) + "%"
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Colours")
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: qsTr("Scheme:")
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("Follow the system"), value: "system" },
                { text: qsTr("Light"), value: "light" },
                { text: qsTr("Dark"), value: "dark" }
            ]
            Component.onCompleted: currentIndex = indexOfValue(settingsPage.prefs.colorScheme)
            onActivated: settingsPage.prefs.colorScheme = currentValue
        }

        QQC2.Button {
            Kirigami.FormData.label: qsTr("Light theme:")
            text: settingsPage.prefs.lightTheme
            icon.name: "color-management"
            Kirigami.MnemonicData.enabled: false
            onClicked: applicationWindow().pageStack.layers.push(Qt.resolvedUrl("ThemePicker.qml"),
                           { prefs: settingsPage.prefs, dark: false })
        }

        QQC2.Button {
            Kirigami.FormData.label: qsTr("Dark theme:")
            text: settingsPage.prefs.darkTheme
            icon.name: "color-management"
            Kirigami.MnemonicData.enabled: false
            onClicked: applicationWindow().pageStack.layers.push(Qt.resolvedUrl("ThemePicker.qml"),
                           { prefs: settingsPage.prefs, dark: true })
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: qsTr("Behaviour")
        }

        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Extra keys row:")
            checked: settingsPage.prefs.extraKeys
            onToggled: settingsPage.prefs.extraKeys = checked
        }

        QQC2.Switch {
            Kirigami.FormData.label: qsTr("Flash on bell:")
            checked: settingsPage.prefs.visualBell
            onToggled: settingsPage.prefs.visualBell = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
        }

        QQC2.Label {
            Kirigami.FormData.label: qsTr("About:")
            text: qsTr("%1 %2, on libghostty-vt").arg(appName).arg(Qt.application.version)
        }
    }
}
