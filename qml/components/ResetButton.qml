import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic

// Puts one parameter back where it started.
//
// Shared by both row components rather than written twice, because the useful
// part is not the button -- it is the rule about when it is live. The default
// comes from the model, so this cannot offer to reset a value to a number the
// model does not actually consider default; and it goes flat rather than
// disappearing when the value is already there, so the column does not reflow
// every time a slider passes through its own default.
HusIconButton {
    id: control

    // The ParamRow or LimitRow this belongs to. Both expose the same four
    // names, which is the whole interface.
    required property Item row

    Layout.fillWidth: false
    Layout.preferredWidth: 26
    Layout.preferredHeight: 24

    visible: control.row.owner !== null && control.row.prop !== ''
    enabled: !control.row.atDefault

    // Legible in both states. The first version faded the inactive one to 0.18,
    // which on a dark surface is not "subtle" -- it is invisible, and a control
    // nobody can find is a control that does not exist. Inactive is a plain
    // grey glyph; active picks up the accent colour, so a row that has been
    // moved away from its default announces itself.
    opacity: enabled ? 1.0 : 0.38
    colorIcon: enabled ? HusTheme.Primary.colorPrimary
                       : HusTheme.Primary.colorTextBase
    iconSource: HusIcon.ReloadOutlined
    iconSize: 13

    Behavior on opacity { NumberAnimation { duration: HusTheme.Primary.durationFast } }

    onClicked: control.row.owner[control.row.prop] = control.row.defaultValue

    HusToolTip {
        parent: control
        visible: control.hovered && control.enabled
        text: '恢复默认值'
    }
}
