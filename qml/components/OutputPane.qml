import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The output stage: channel routing, per-channel delay, loudness correction.
//
// These three are what Equalizer APO spells `Copy:`, `Delay:` and
// `LoudnessCorrection:`. They are grouped away from the effects because they
// are not effects -- they describe the speakers rather than the sound.
Item {
    id: root

    readonly property OutputModel out: AppController.output
    readonly property int chanCount: out.channels

    // One labelled slider plus a readout. Local to this pane because the
    // effects page's ParamRow lives in that file and is bound to its models.
    component LimitRow: RowLayout {
        id: lrow
        property string label: ''
        property string unit: ''
        property real lo: 0
        property real hi: 1
        property int dec: 1
        property real value: 0
        signal edited(real v)

        Layout.fillWidth: true
        spacing: 10

        HusText {
            text: lrow.label
            font.pixelSize: 12
            opacity: 0.7
            Layout.preferredWidth: 62
        }
        Fader {
            Layout.fillWidth: true
            min: lrow.lo; max: lrow.hi
            stepSize: lrow.dec === 0 ? 1 : (lrow.dec === 1 ? 0.1 : 0.01)
            value: lrow.value
            onFirstMoved: lrow.edited(currentValue)
        }
        HusText {
            text: lrow.value.toFixed(lrow.dec) + ' ' + lrow.unit
            font.pixelSize: 12
            Layout.preferredWidth: 78
            horizontalAlignment: Text.AlignRight
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        HusScrollBar.vertical: HusScrollBar { }

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 12

            // ------------------------------------------------- loudness
            SectionCard {
                Layout.fillWidth: true
                title: '等响度补偿'
                hint: '音量调低时耳朵会先丢低频 —— 按当前音量补回来'

                headerRight: HusSwitch {
                    checked: root.out.loudnessOn
                    onToggled: root.out.loudnessOn = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    opacity: root.out.loudnessOn ? 1.0 : 0.45

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        HusText {
                            text: root.out.volumeKnown
                                  ? '当前音量 ' + root.out.volumeDb.toFixed(1) + ' dB'
                                  : '读不到系统音量'
                            font.pixelSize: 12
                        }
                        HusText {
                            text: '· 参考 ' + root.out.referenceDb.toFixed(1) + ' dB'
                            font.pixelSize: 12
                            opacity: 0.6
                        }
                        HusButton {
                            text: '以当前音量为参考'
                            enabled: root.out.volumeKnown
                            onClicked: root.out.useCurrentVolumeAsReference()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        HusText { text: '强度'; font.pixelSize: 12; opacity: 0.7 }
                        Fader {
                            Layout.fillWidth: true
                            min: 0; max: 1; stepSize: 0.01
                            value: root.out.loudnessAmount
                            onFirstMoved: root.out.loudnessAmount = currentValue
                        }
                        HusText {
                            text: Math.round(root.out.loudnessAmount * 100) + '%'
                            font.pixelSize: 12
                            Layout.preferredWidth: 42
                        }
                    }

                    // The two shelves it is actually applying. Equalizer APO's
                    // formula is aggressive at large volume differences, and a
                    // number on screen is the difference between a control you
                    // can judge and one you have to guess at.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        HusText {
                            text: '低架 75 Hz  ' + (root.out.lowShelfDb >= 0 ? '+' : '')
                                  + root.out.lowShelfDb.toFixed(1) + ' dB'
                            font.pixelSize: 12
                        }
                        HusText {
                            text: '高架 10 kHz  ' + (root.out.highShelfDb >= 0 ? '+' : '')
                                  + root.out.highShelfDb.toFixed(1) + ' dB'
                            font.pixelSize: 12
                        }
                        HusText {
                            text: '前置 ' + root.out.loudnessPreampDb.toFixed(1) + ' dB'
                            font.pixelSize: 12
                            opacity: 0.7
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ------------------------------------------------- limiter
            SectionCard {
                Layout.fillWidth: true
                title: '限制器'
                hint: '前瞻峰值限制 —— 阈值是保证,不是目标'

                headerRight: HusSwitch {
                    checked: root.out.limiterOn
                    onToggled: root.out.limiterOn = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: root.out.limiterOn ? 1.0 : 0.45

                    LimitRow {
                        label: '输入增益'; unit: 'dB'; lo: -12; hi: 24; dec: 1
                        value: root.out.limiterGain
                        onEdited: (v) => root.out.limiterGain = v
                    }
                    LimitRow {
                        label: '阈值'; unit: 'dBFS'; lo: -30; hi: 0; dec: 1
                        value: root.out.limiterThreshold
                        onEdited: (v) => root.out.limiterThreshold = v
                    }
                    LimitRow {
                        label: '恢复'; unit: 'ms'; lo: 1; hi: 1000; dec: 0
                        value: root.out.limiterRelease
                        onEdited: (v) => root.out.limiterRelease = v
                    }
                    LimitRow {
                        label: '前瞻'; unit: 'ms'; lo: 0; hi: 10; dec: 2
                        value: root.out.limiterLookahead
                        onEdited: (v) => root.out.limiterLookahead = v
                    }

                    HusText {
                        text: '增益加在限制器之前,所以阈值就是真正的天花板 —— '
                              + '往上推到开始限制为止是有意义的操作。前瞻会给整条链路加上同样长的延迟。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ------------------------------------------------- routing
            SectionCard {
                Layout.fillWidth: true
                title: '声道路由'
                hint: '每个输出声道是哪些输入声道的加权和'

                headerRight: HusSwitch {
                    checked: root.out.matrixOn
                    onToggled: root.out.matrixOn = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    opacity: root.out.matrixOn ? 1.0 : 0.45

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: root.out.matrixPresets
                            HusButton {
                                required property var modelData
                                text: modelData.label
                                type: root.out.matrixPreset === modelData.value
                                      ? HusButton.Type_Primary : HusButton.Type_Default
                                onClicked: root.out.applyMatrixPreset(modelData.value)
                            }
                        }
                        HusText {
                            text: '自定义'
                            font.pixelSize: 12
                            opacity: 0.7
                            visible: root.out.matrixPreset === -1
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // The grid, for anything the presets do not cover. Rows are
                    // outputs and columns are inputs, which is the direction the
                    // matrix multiplies in -- labelled, because every other
                    // convention is equally plausible to a reader.
                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.chanCount + 1
                        columnSpacing: 6
                        rowSpacing: 6

                        HusText { text: '输出 \\ 输入'; font.pixelSize: 11; opacity: 0.5 }
                        Repeater {
                            model: root.out.channelNames
                            HusText {
                                required property string modelData
                                text: modelData
                                font.pixelSize: 11
                                opacity: 0.6
                                Layout.preferredWidth: 62
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Repeater {
                            model: root.chanCount
                            delegate: RowLayout {
                                required property int index
                                Layout.column: 0
                                Layout.row: index + 1
                                HusText {
                                    text: root.out.channelNames[parent.index]
                                    font.pixelSize: 11
                                    opacity: 0.6
                                }
                            }
                        }

                        Repeater {
                            model: root.chanCount * root.chanCount
                            delegate: HusInputNumber {
                                required property int index
                                readonly property int outCh: Math.floor(index / root.chanCount)
                                readonly property int inCh: index % root.chanCount

                                Layout.column: inCh + 1
                                Layout.row: outCh + 1
                                Layout.preferredWidth: 78

                                min: -4; max: 4; step: 0.1; precision: 2
                                value: root.out.matrixGain(outCh, inCh)
                                onValueChanged: root.out.setMatrixGain(outCh, inCh, value)
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------- delay
            SectionCard {
                Layout.fillWidth: true
                title: '声道延时'
                hint: '把远的音箱推迟一点,让声音同时到达 —— 343 m/s'

                headerRight: HusSwitch {
                    checked: root.out.delayOn
                    onToggled: root.out.delayOn = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: root.out.delayOn ? 1.0 : 0.45

                    Repeater {
                        model: root.chanCount
                        delegate: RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            spacing: 10

                            HusText {
                                text: root.out.channelNames[index]
                                font.pixelSize: 12
                                Layout.preferredWidth: 34
                            }
                            Fader {
                                Layout.fillWidth: true
                                min: 0; max: 20; stepSize: 0.01
                                value: root.out.delayMs(index)
                                onFirstMoved: root.out.setDelayMs(index, currentValue)
                            }
                            HusText {
                                text: root.out.delayMs(index).toFixed(2) + ' ms'
                                font.pixelSize: 12
                                Layout.preferredWidth: 62
                                horizontalAlignment: Text.AlignRight
                            }
                            HusText {
                                text: root.out.delayCm(index).toFixed(1) + ' cm'
                                font.pixelSize: 12
                                opacity: 0.6
                                Layout.preferredWidth: 66
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    HusText {
                        text: '整条链路会被推迟 ' + root.out.maxDelayMs.toFixed(2)
                              + ' ms —— 这一段是在流建立时报给 Windows 的,之后改动要重新播放才会同步'
                        font.pixelSize: 11
                        opacity: 0.55
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        visible: root.out.maxDelayMs > 0
                    }
                }
            }
        }
    }
}
