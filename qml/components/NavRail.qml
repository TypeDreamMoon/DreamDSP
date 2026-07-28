import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

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

            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData

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

        Item { Layout.fillHeight: true }
    }
}
