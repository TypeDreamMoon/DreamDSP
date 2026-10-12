import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The label above a group of cards: what the group is for, and how many of its
// effects are switched on.
RowLayout {
    id: root

    property string title: ''
    property string detail: ''

    Layout.fillWidth: true
    Layout.topMargin: 6
    spacing: 10

    HusText {
        text: root.title
        font.pixelSize: Ui.groupTitle
        font.weight: Font.DemiBold
        opacity: 0.85
    }
    HusText {
        text: root.detail
        visible: root.detail !== ''
        font.pixelSize: Ui.caption
        opacity: 0.45
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        implicitHeight: 1
        color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.07)
    }
}
