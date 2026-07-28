import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// Small status chip: coloured dot + text.
Rectangle {
    id: root

    property string text: ''
    property color dotColor: HusTheme.Primary.colorPrimary
    property bool showDot: true

    implicitWidth: row.implicitWidth + 20
    implicitHeight: 24
    radius: height / 2
    color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.04)

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            width: 7; height: 7; radius: 3.5
            color: root.dotColor
            visible: root.showDot
            Layout.alignment: Qt.AlignVCenter

            SequentialAnimation on opacity {
                running: root.showDot
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.35; duration: 1100; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 0.35; to: 1.0; duration: 1100; easing.type: Easing.InOutQuad }
            }
        }

        HusText {
            text: root.text
            font.pixelSize: 11
            opacity: 0.75
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
