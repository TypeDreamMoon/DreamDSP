import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Per-band parameter editing: frequency, gain, Q, filter type, enable.
//
// Hand-rolled from a ListView rather than HusTableView -- the cell/column
// delegate API there is elaborate, and every column here needs a different
// editor anyway.
Item {
    id: root

    // Column widths, shared by the header and every row.
    readonly property int wIndex: 34
    readonly property int wFreq: 108
    readonly property int wGain: 104
    readonly property int wQ: 96
    readonly property int wType: 210
    readonly property int wOn: 52

    readonly property var typeOptions: AppController.bands.filterTypeOptions()

    // HusInputNumber defaults to value.toLocaleString(), which renders 1000 as
    // "1,000" and — in a comma-decimal locale — would turn 1.41 into "1,41".
    // Everything here feeds Equalizer APO, which has its own opinions about
    // commas, so keep the display plain and locale-independent.
    readonly property var plainFormat: (v, locale) => v.toFixed(0)
    readonly property var plainFormat1: (v, locale) => v.toFixed(1)
    readonly property var plainFormat2: (v, locale) => v.toFixed(2)
    readonly property var plainParse: (text, locale) => Number(text)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ header
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            spacing: 8

            component Head: HusText {
                font.pixelSize: 11
                opacity: 0.5
            }

            Head { text: '#';       Layout.preferredWidth: root.wIndex }
            Head { text: '频率 Hz'; Layout.preferredWidth: root.wFreq }
            Head { text: '增益 dB'; Layout.preferredWidth: root.wGain }
            Head { text: 'Q';       Layout.preferredWidth: root.wQ }
            Head { text: '滤波器类型'; Layout.preferredWidth: root.wType }
            Head { text: '启用';    Layout.preferredWidth: root.wOn }
            Item { Layout.fillWidth: true }
        }

        HusDivider { Layout.fillWidth: true }

        // -------------------------------------------------------------- rows
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: AppController.bands
            reuseItems: true

            HusScrollBar.vertical: HusScrollBar { }

            delegate: RowLayout {
                id: row
                required property int index
                required property var model

                width: ListView.view.width
                spacing: 8

                HusText {
                    Layout.preferredWidth: root.wIndex
                    text: (row.index + 1).toString()
                    font.pixelSize: 12
                    opacity: 0.45
                }

                HusInputNumber {
                    Layout.preferredWidth: root.wFreq
                    min: 10
                    max: 24000
                    step: row.model.frequency < 1000 ? 5 : 100
                    precision: 0
                    formatter: root.plainFormat
                    parser: root.plainParse
                    value: row.model.frequency
                    onValueModified: AppController.bands.setFrequency(row.index, value)
                }

                HusInputNumber {
                    Layout.preferredWidth: root.wGain
                    min: -30
                    max: 30
                    step: 0.5
                    precision: 1
                    formatter: root.plainFormat1
                    parser: root.plainParse
                    // LP/HP/BP/NO/AP carry no gain; APO ignores it, so grey it out
                    // rather than letting it look meaningful.
                    enabled: row.model.hasGain
                    opacity: enabled ? 1.0 : 0.35
                    value: row.model.gain
                    onValueModified: AppController.bands.setGain(row.index, value)
                }

                HusInputNumber {
                    Layout.preferredWidth: root.wQ
                    min: 0.1
                    max: 30
                    step: 0.05
                    precision: 2
                    formatter: root.plainFormat2
                    parser: root.plainParse
                    value: row.model.quality
                    onValueModified: AppController.bands.setQ(row.index, value)
                }

                HusSelect {
                    Layout.preferredWidth: root.wType
                    model: root.typeOptions
                    currentIndex: row.model.filterType
                    onActivated: (i) => AppController.bands.setType(row.index, i)
                }

                HusSwitch {
                    Layout.preferredWidth: root.wOn
                    checked: row.model.bandEnabled
                    onToggled: AppController.bands.setBandEnabled(row.index, checked)
                }

                Item { Layout.fillWidth: true }

                HusIconButton {
                    iconSource: HusIcon.PlusOutlined
                    onClicked: AppController.bands.insertBandAfter(row.index)

                    HusToolTip {
                        parent: parent
                        visible: parent.hovered
                        text: '在此频段后插入一段'
                    }
                }

                HusIconButton {
                    iconSource: HusIcon.DeleteOutlined
                    enabled: AppController.bands.count > 1
                    onClicked: AppController.bands.removeBand(row.index)
                }
            }
        }

        // ------------------------------------------------------------ footer
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8

            HusText {
                text: AppController.bands.count + ' / 31 段'
                font.pixelSize: 11
                opacity: 0.5
            }
            Item { Layout.fillWidth: true }
        }
    }
}
