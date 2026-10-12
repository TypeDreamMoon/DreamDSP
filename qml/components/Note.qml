import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// An explanation under a control -- the reasoning, the measurement, the
// source. Hidden unless the header's 说明 switch is on: the text earns its
// place the first time and gets in the way every time after.
HusText {
    Layout.fillWidth: true
    visible: Ui.showNotes
    font.pixelSize: Ui.caption
    lineHeight: 1.25
    opacity: 0.55
    wrapMode: Text.WordWrap
}
