import QtQuick
import HuskarUI.Basic
import DreamDSP

// Output level for the selected device. Rises instantly, falls slowly -- an
// instantaneous readout at 30 Hz is unreadable flicker.
Item {
    id: root

    property real level: Math.max(0, AppController.peakLevel)
    property real held: 0

    implicitWidth: 72
    implicitHeight: 6
    visible: AppController.metering

    onLevelChanged: {
        if (level > held)
            held = level;
    }

    Timer {
        running: root.visible
        repeat: true
        interval: 60
        onTriggered: root.held = Math.max(root.level, root.held - 0.05)
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)

        Rectangle {
            height: parent.height
            radius: height / 2
            // The scale is deliberately not linear: normal listening levels sit
            // near the top of a linear meter and nothing is distinguishable.
            width: parent.width * Math.min(1, Math.pow(root.held, 0.4))

            color: root.held > 0.97 ? HusTheme.Primary.colorError
                 : root.held > 0.85 ? HusTheme.Primary.colorWarning
                                    : HusTheme.Primary.colorPrimary

            Behavior on width { NumberAnimation { duration: 55 } }
        }
    }

    HusToolTip {
        parent: root
        visible: hover.hovered
        text: AppController.meterAvailable
              ? '输出峰值 ' + Math.round(root.level * 100) + '%'
              : '读不到电平(设备可能处于独占模式)'
    }
    HoverHandler { id: hover }
}
