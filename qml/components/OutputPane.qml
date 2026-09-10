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
    // The clipper, the leveller and the night-mode curve live on this page
    // rather than with the effects: they are output conditioning -- what
    // reaches the converter -- not tone.
    readonly property EffectsModel fx: AppController.effects
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

        // See ParamRow in EffectsPane.qml -- same idea, same reason.
        property QtObject owner: null
        property string prop: ''
        readonly property var defaultValue: (lrow.owner && lrow.prop !== '')
                                            ? lrow.owner.defaultOf(lrow.prop)
                                            : undefined
        readonly property real step: lrow.dec === 0 ? 1 : (lrow.dec === 1 ? 0.1 : 0.01)
        readonly property bool atDefault:
            lrow.defaultValue === undefined
            || Math.abs(lrow.value - lrow.defaultValue) <= lrow.step * 0.5

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

        ResetButton {
            row: lrow
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        HusScrollBar.vertical: HusScrollBar { }

        // Two columns when there is room for them.
    //
    // Ten cards in one column is four screens of scrolling on a page where the
    // horizontal half of the window is empty. A GridLayout rather than two
    // hand-balanced ColumnLayouts: the split then follows the window instead of
    // being a guess baked into the file, and the cards that genuinely need the
    // width -- a transfer curve, a pair of side-by-side sliders, an 8x8 matrix
    // -- say so with a column span instead of forcing everything else to be as
    // wide as they are.
        GridLayout {
            id: column
            // Capped, and centred once it is capped. See EffectsPane.
            readonly property real maxWidth: 1600
            width: Math.min(parent.width, maxWidth)
            x: Math.max(0, (parent.width - width) / 2)
            // One column per 440 px, up to three. A fixed two was fine at the
            // default window and wrong on a wide one: the cards stretched to 900 px
            // each, which puts a slider's handle half a screen from the number it
            // sets and leaves the bottom half of the page empty.
            columns: Math.max(1, Math.min(3, Math.floor(width / 440)))
            columnSpacing: 12
            rowSpacing: 12

            // ------------------------------------------------- loudness
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
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
                Layout.alignment: Qt.AlignTop
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
                        owner: root.out; prop: 'limiterGain'
                        value: root.out.limiterGain
                        onEdited: (v) => root.out.limiterGain = v
                    }
                    LimitRow {
                        label: '阈值'; unit: 'dBFS'; lo: -30; hi: 0; dec: 1
                        owner: root.out; prop: 'limiterThreshold'
                        value: root.out.limiterThreshold
                        onEdited: (v) => root.out.limiterThreshold = v
                    }
                    LimitRow {
                        label: '恢复'; unit: 'ms'; lo: 1; hi: 1000; dec: 0
                        owner: root.out; prop: 'limiterRelease'
                        value: root.out.limiterRelease
                        onEdited: (v) => root.out.limiterRelease = v
                    }
                    LimitRow {
                        label: '前瞻'; unit: 'ms'; lo: 0; hi: 10; dec: 2
                        owner: root.out; prop: 'limiterLookahead'
                        value: root.out.limiterLookahead
                        onEdited: (v) => root.out.limiterLookahead = v
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        HusText { text: '真峰值'; Layout.fillWidth: true }
                        HusSwitch {
                            checked: root.out.limiterTruePeak
                            onToggled: root.out.limiterTruePeak = checked
                        }
                    }

                    HusText {
                        text: '增益加在限制器之前,所以阈值就是真正的天花板 —— '
                              + '往上推到开始限制为止是有意义的操作。前瞻会给整条链路加上同样长的延迟。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    HusText {
                        text: '真峰值按 ITU-R BS.1770 用 4 倍过采样测量重建后的波形。'
                              + '关掉它,阈值只约束采样点本身,而 DAC 在采样点之间画出的曲线'
                              + '可以越过每一个采样点 —— 本机实测最多高出 0.90 dB。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ------------------------------------------------- clipper
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '削波器'
                hint: '限制器之前的软削波 —— 把稀疏的峰拿掉,限制器就不必去够它们'

                headerRight: HusSwitch {
                    checked: root.fx.clipperEnabled
                    onToggled: root.fx.clipperEnabled = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: root.fx.clipperEnabled ? 1.0 : 0.45

                    LimitRow {
                        label: '驱动'; unit: 'dB'; lo: 0; hi: 24; dec: 1
                        owner: root.fx; prop: 'clipperDrive'
                        value: root.fx.clipperDrive
                        onEdited: (v) => root.fx.clipperDrive = v
                    }
                    LimitRow {
                        label: '天花板'; unit: 'dBFS'; lo: -24; hi: 0; dec: 1
                        owner: root.fx; prop: 'clipperCeiling'
                        value: root.fx.clipperCeiling
                        onEdited: (v) => root.fx.clipperCeiling = v
                    }
                    LimitRow {
                        label: '软拐点'; unit: 'dB'; lo: 0; hi: 6; dec: 2
                        owner: root.fx; prop: 'clipperKnee'
                        value: root.fx.clipperKnee
                        onEdited: (v) => root.fx.clipperKnee = v
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        HusText { text: '过采样'; Layout.fillWidth: true }
                        HusSwitch {
                            checked: root.fx.clipperOversample
                            onToggled: root.fx.clipperOversample = checked
                        }
                    }

                    HusText {
                        text: '1 dB 的峰值削减下,削波器给持续音带来的调制只有 0.1 dB,'
                              + '前瞻限制器是 17 到 20 dB —— 因为削波只碰到 0.02% 的采样点,'
                              + '每次不到三个采样长,藏在触发它的瞬态的前向掩蔽下面。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    HusText {
                        text: '软拐点是整个机架里最便宜的一个分贝:本机实测把混叠比从 51.5 dB'
                              + '推到 69.0 dB,代价是百分之一分贝的响度。再开过采样到 82.6 dB。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ------------------------------------------------- auto volume
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '自动音量'
                hint: '按 BS.1770 响度把节目音量找平 —— 慢到听不出来为止'

                headerRight: HusSwitch {
                    checked: root.fx.autoGainEnabled
                    onToggled: root.fx.autoGainEnabled = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: root.fx.autoGainEnabled ? 1.0 : 0.45

                    LimitRow {
                        label: '目标'; unit: 'LUFS'; lo: -40; hi: -10; dec: 1
                        owner: root.fx; prop: 'autoGainTarget'
                        value: root.fx.autoGainTarget
                        onEdited: (v) => root.fx.autoGainTarget = v
                    }
                    LimitRow {
                        label: '最大增益'; unit: 'dB'; lo: 0; hi: 25; dec: 1
                        owner: root.fx; prop: 'autoGainMax'
                        value: root.fx.autoGainMax
                        onEdited: (v) => root.fx.autoGainMax = v
                    }
                    LimitRow {
                        label: '速率'; unit: 'dB/s'; lo: 0.25; hi: 20; dec: 2
                        owner: root.fx; prop: 'autoGainRate'
                        value: root.fx.autoGainRate
                        onEdited: (v) => root.fx.autoGainRate = v
                    }
                    LimitRow {
                        label: '静区宽度'; unit: 'dB'; lo: 0; hi: 12; dec: 1
                        owner: root.fx; prop: 'autoGainWindow'
                        value: root.fx.autoGainWindow
                        onEdited: (v) => root.fx.autoGainWindow = v
                    }

                    HusText {
                        text: '速率是这个东西的全部设计。四份互不相干的实现都落在同一个区间:'
                              + 'Orban 的广播 AGC 把 0.5 dB/s 叫做“基本冻结”、2 dB/s 叫做'
                              + '“开阔、自然、不疲劳”;ffmpeg 的 loudnorm 用 0.5023 dB/s;'
                              + 'AC-3 与 AC-4 的释放落在 0.4 到 1.4 dB/s。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    HusText {
                        text: '静区来自 Orban:落在目标附近这么宽的范围内时,增益爬得极慢'
                              + '甚至不动,已经很平的素材就不会被反复微调 —— 那些微调正是'
                              + '人耳听成“喘气”的东西。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ------------------------------------------------- night mode
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '夜间模式'
                hint: '杜比数字的五条动态范围压缩曲线'

                headerRight: HusSwitch {
                    checked: root.fx.nightModeEnabled
                    onToggled: root.fx.nightModeEnabled = checked
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: root.fx.nightModeEnabled ? 1.0 : 0.45

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        HusText { text: '曲线'; Layout.fillWidth: true }
                        HusSelect {
                            Layout.preferredWidth: 180
                            model: [
                                { label: '关', value: 0 },
                                { label: '电影 标准', value: 1 },
                                { label: '电影 轻度', value: 2 },
                                { label: '音乐 标准', value: 3 },
                                { label: '音乐 轻度', value: 4 },
                                { label: '语音', value: 5 }
                            ]
                            currentIndex: root.fx.nightProfile
                            onActivated: (i) => root.fx.nightProfile = i
                        }
                    }
                    LimitRow {
                        label: '提升量'; unit: ''; lo: 0; hi: 1; dec: 2
                        owner: root.fx; prop: 'nightBoost'
                        value: root.fx.nightBoost
                        onEdited: (v) => root.fx.nightBoost = v
                    }
                    LimitRow {
                        label: '压低量'; unit: ''; lo: 0; hi: 1; dec: 2
                        owner: root.fx; prop: 'nightCut'
                        value: root.fx.nightCut
                        onEdited: (v) => root.fx.nightCut = v
                    }
                    LimitRow {
                        label: '参考电平'; unit: 'LUFS'; lo: -40; hi: -10; dec: 1
                        owner: root.fx; prop: 'nightReference'
                        value: root.fx.nightReference
                        onEdited: (v) => root.fx.nightReference = v
                    }

                    HusText {
                        text: '这就是蓝光机和电视上“夜间模式”按下去之后跑的东西。'
                              + '曲线本身不在 AC-3 的标准里 —— ATSC A/52 只规定了增益字怎么传输;'
                              + '曲线发表在杜比的元数据指南和 ETSI TS 103 190-1 表 161 里,'
                              + '这里用的是后者(它还带时间常数,并且修正了前者 Film Light 一行的两处算术错误)。'
                        font.pixelSize: 11
                        opacity: 0.5
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    HusText {
                        text: '提升量和压低量是分开的两个系数,所以“压住大声的、但别把安静的抬起来”'
                              + '是可以表达的 —— MPEG-D DRC 也是这么拆的。'
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
                Layout.alignment: Qt.AlignTop
                Layout.columnSpan: Math.min(2, column.columns)
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
                Layout.alignment: Qt.AlignTop
                Layout.columnSpan: Math.min(2, column.columns)
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
