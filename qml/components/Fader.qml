import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// HusSlider with a usable hit area.
//
// HusSlider has no background of its own and the inner T.Slider has no
// contentItem, so its implicit size on the cross axis is zero -- width when
// vertical, height when horizontal. A zero-sized item receives no mouse
// events, yet still *looks* correct, because the track hardcodes 4 px and the
// handle has its own implicit size and both draw outside the parent's bounds.
// The result is a slider that renders perfectly and cannot be dragged.
//
// Every slider in this project goes through here so the mistake cannot be made
// one control at a time. `--slidertest` additionally fails if any visible
// slider is found with a zero dimension.
HusSlider {
    id: fader

    // Cross-axis size: how thick the control is to the mouse.
    property int thickness: 26

    Layout.preferredWidth: orientation === Qt.Vertical ? thickness : -1
    Layout.preferredHeight: orientation === Qt.Horizontal ? thickness : -1
}
