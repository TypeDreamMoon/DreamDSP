import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Left-hand navigation.
//
// Hand-rolled rather than HusMenu: this is forty lines of Rectangle and the
// library's components have a habit of surprising API (write-only values,
// unbindable currentIndex, zero implicit width), which is a poor trade for
// something this simple.
Rectangle {
    id: root

    property var items: []          // [{ label, icon }]
    property int currentIndex: 0

    implicitWidth: 168
    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.02)

    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        Repeater {
            model: root.items

            // A section label above the first entry of each group. Eight
            // entries in one undivided list read as eight peers; they are not
            // -- four shape the sound, two are tools, two are about the
            // machine.
            delegate: ColumnLayout {
                id: entry
                required property int index
                required property var modelData
                Layout.fillWidth: true
                spacing: 4

                readonly property bool firstOfSection:
                    (entry.modelData.section || '') !== ''
                    && (entry.index === 0
                        || root.items[entry.index - 1].section !== entry.modelData.section)

                HusText {
                    visible: entry.firstOfSection
                    Layout.topMargin: entry.index === 0 ? 4 : 14
                    Layout.leftMargin: 12
                    Layout.bottomMargin: 2
                    text: entry.modelData.section || ''
                    font.pixelSize: 11
                    opacity: 0.4
                }

            Rectangle {
                id: row
                readonly property int index: entry.index
                readonly property var modelData: entry.modelData

                readonly property bool active: root.currentIndex === row.index

                Layout.fillWidth: true
                implicitHeight: 38
                radius: 8
                color: row.active
                       ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.07))
                       : (hover.hovered ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05)
                                                           : Qt.rgba(0, 0, 0, 0.035))
                                        : 'transparent')

                Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationFast } }

                // Accent bar on the selected entry.
                Rectangle {
                    width: 3
                    height: row.active ? 18 : 0
                    radius: 1.5
                    anchors.left: parent.left
                    anchors.leftMargin: 1
                    anchors.verticalCenter: parent.verticalCenter
                    color: HusTheme.Primary.colorPrimary
                    Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 9

                    HusIconText {
                        iconSource: row.modelData.icon
                        iconSize: 15
                        colorIcon: row.active ? HusTheme.Primary.colorPrimary
                                              : HusTheme.Primary.colorTextBase
                        opacity: row.active ? 1.0 : 0.6
                    }

                    HusText {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        font.pixelSize: 13
                        font.weight: row.active ? Font.DemiBold : Font.Normal
                        color: row.active ? HusTheme.Primary.colorPrimary
                                          : HusTheme.Primary.colorTextBase
                        opacity: row.active ? 1.0 : 0.75
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        width: 6; height: 6; radius: 3
                        visible: row.modelData.dot === true
                        color: HusTheme.Primary.colorSuccess
                    }
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.currentIndex = row.index }
            }
            }
        }

        Item { Layout.fillHeight: true }

        HusText {
            Layout.leftMargin: 12
            Layout.bottomMargin: 4
            // The release number only: a dev build's "+0.2d09349-dirty" ran
            // out of the rail and under the status bar. Settings has the rest.
            text: 'v' + AppController.version.split('+')[0]
            font.pixelSize: 11
            opacity: 0.3
        }
    }
}
