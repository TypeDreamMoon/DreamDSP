import QtQuick
import HuskarUI.Basic
import DreamDSP

// A row in a long list of choices -- headphones, impulse responses -- that
// shows which one is in use.
//
// The list is the only place the choice is visible: pick one, scroll, and
// nothing on the row says it was the one. `current` gives it a tinted
// background, an accent edge and a border, strong enough to find again while
// scrolling at speed.
Rectangle {
    id: root

    property bool current: false
    readonly property bool hovered: hover.hovered

    radius: Ui.smallRadius
    color: root.current
           ? Qt.alpha(HusTheme.Primary.colorPrimary, HusTheme.isDark ? 0.22 : 0.12)
           : root.hovered
             ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
             : 'transparent'
    border.width: root.current ? 1 : 0
    border.color: Qt.alpha(HusTheme.Primary.colorPrimary, 0.55)

    Behavior on color { ColorAnimation { duration: 120 } }

    // The accent edge of a chosen row.
    Rectangle {
        visible: root.current
        anchors.left: parent.left
        anchors.leftMargin: 1
        anchors.verticalCenter: parent.verticalCenter
        width: 3
        height: parent.height - 16
        radius: 1.5
        color: HusTheme.Primary.colorPrimary
    }

    HoverHandler { id: hover }
}
