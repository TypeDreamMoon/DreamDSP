import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// One equalizer band: gain readout, vertical slider, frequency caption.
ColumnLayout {
    id: root

    property int bandIndex: 0
    property real gain: 0
    property string label: ''
    property real range: 15

    signal gainEdited(real value)

    spacing: 6

    HusText {
        Layout.alignment: Qt.AlignHCenter
        text: (root.gain > 0.05 ? '+' : '') + root.gain.toFixed(1)
        font.pixelSize: 11
        opacity: Math.abs(root.gain) < 0.05 ? 0.4 : 0.95
        color: Math.abs(root.gain) < 0.05
               ? HusTheme.Primary.colorTextBase
               : HusTheme.Primary.colorPrimary
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Fader {
        id: slider
        Layout.alignment: Qt.AlignHCenter
        Layout.fillHeight: true
        orientation: Qt.Vertical
        min: -root.range
        max: root.range
        stepSize: 0.1

        // HusSlider's `value` is write-only from the outside: assigning to it
        // pushes into the inner T.Slider, but dragging never writes back.
        // The dragged result comes out via the read-only `currentValue`, and
        // `firstMoved`/`firstReleased` are the only interaction signals.
        value: root.gain
        onFirstMoved: root.gainEdited(currentValue)
        onFirstReleased: root.gainEdited(currentValue)
    }

    HusText {
        Layout.alignment: Qt.AlignHCenter
        text: root.label
        font.pixelSize: 10
        opacity: 0.6
    }

    // Double-click the caption to zero this band.
    MouseArea {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 34
        Layout.preferredHeight: 1
        onDoubleClicked: root.gainEdited(0)
    }
}
