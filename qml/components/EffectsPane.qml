import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Effects that need real signal processing, i.e. everything APO's linear
// configuration language cannot express.
Item {
    id: root

    CompressorModel { id: comp }
    ReverbModel { id: rev }

    EffectsModel { id: fx }

    OfflineRender {
        id: render
        compressor: comp
        reverb: rev
        effects: fx
    }

    component ParamRow: RowLayout {
        // Referenced by id rather than through `parent`: these children are
        // direct children of the row, so `parent` is already this object and a
        // `parent.parent` chain silently resolves one level too far.
        id: prow

        property string label: ''
        property string unit: ''
        property real value: 0
        property real from: 0
        property real to: 1
        property real step: 0.1
        property int decimals: 1
        property bool autoMode: false
        property bool hasAuto: false
        property real autoValue: 0

        signal edited(real v)
        signal autoToggled(bool on)

        Layout.fillWidth: true
        spacing: 10

        HusText {
            Layout.preferredWidth: 76
            text: prow.label
            font.pixelSize: 12
        }

        Fader {
            Layout.fillWidth: true
            min: prow.from
            max: prow.to
            stepSize: prow.step
            value: prow.value
            enabled: !prow.autoMode
            opacity: enabled ? 1 : 0.4
            onFirstMoved: prow.edited(currentValue)
            onFirstReleased: prow.edited(currentValue)
        }

        HusText {
            Layout.preferredWidth: 78
            horizontalAlignment: Text.AlignRight
            font.pixelSize: 12
            opacity: prow.autoMode ? 0.55 : 0.9
            text: (prow.autoMode ? prow.autoValue : prow.value).toFixed(prow.decimals)
                  + ' ' + prow.unit
        }

        HusButton {
            visible: prow.hasAuto
            text: '自动'
            type: prow.autoMode ? HusButton.Type_Primary : HusButton.Type_Default
            onClicked: prow.autoToggled(!prow.autoMode)
        }
    }

    // The rack only grows, so it scrolls rather than trying to fit.
    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        HusScrollBar.vertical: HusScrollBar { }

    ColumnLayout {
        id: col
        width: parent.width - 12
        spacing: 12

        // These effects have no host: nothing routes system audio through them
        // yet. Rather than leaving switches that quietly do nothing, the page
        // says so plainly and offers the one thing that does work today --
        // running the settings over a file.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notice.implicitHeight + 24
            radius: 10
            color: render.failed ? Qt.rgba(1, 0.35, 0.25, 0.10)
                                 : Qt.rgba(1, 0.7, 0.1, 0.09)
            border.width: 1
            border.color: render.failed ? Qt.rgba(1, 0.35, 0.25, 0.45)
                                        : Qt.rgba(1, 0.7, 0.1, 0.35)

            // Dropping a file anywhere on the banner processes it.
            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls && drop.urls.length > 0)
                        render.renderUrl(drop.urls[0]);
                }
            }

            ColumnLayout {
                id: notice
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    HusTag {
                        text: '尚未接入系统音频'
                        presetColor: '#d48806'
                    }
                    HusText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        opacity: 0.85
                        text: '下面的开关只改参数,不会影响你正在听的声音 —— Equalizer APO 的配置语言'
                              + '无法表达非线性处理,宿主方案待定。DSP 本身已实现并通过离线验证。'
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    HusText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: render.failed ? HusTheme.Primary.colorError
                                             : HusTheme.Primary.colorTextBase
                        opacity: render.status !== '' ? 0.95 : 0.6
                        text: render.status !== ''
                              ? render.status
                              : '把一个 wav 文件拖到这里,用当前设置处理它 —— 这是今天能真正听到效果的方式。'
                    }

                    HusButton {
                        text: '打开输出目录'
                        visible: render.lastOutput !== ''
                        onClicked: render.revealOutput()
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '压缩器'
            hint: '对数域前馈 · 立体声联动 · 软拐点'

            headerRight: HusSwitch {
                checked: comp.enabled
                onToggled: comp.enabled = checked
            }

            RowLayout {
                anchors.fill: parent
                spacing: 16

                TransferCurveItem {
                    Layout.preferredWidth: 260
                    Layout.fillHeight: true
                    model: comp
                    curveColor: HusTheme.Primary.colorPrimary
                    gridColor: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
                    labelColor: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.40) : Qt.rgba(0, 0, 0, 0.40)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 10

                    ParamRow {
                        label: '压缩门限'; unit: 'dB'; from: -60; to: 0; step: 0.5
                        value: comp.threshold
                        onEdited: (v) => comp.threshold = v
                    }
                    ParamRow {
                        label: '压缩比率'; unit: ': 1'; from: 1; to: 20; step: 0.1
                        value: comp.ratio
                        onEdited: (v) => comp.ratio = v
                    }
                    ParamRow {
                        label: '拐点宽度'; unit: 'dB'; from: 0; to: 24; step: 0.5
                        value: comp.knee; hasAuto: true
                        autoMode: comp.autoKnee; autoValue: comp.effectiveKnee
                        onEdited: (v) => comp.knee = v
                        onAutoToggled: (on) => comp.autoKnee = on
                    }
                    ParamRow {
                        label: '压缩时间'; unit: 'ms'; from: 0.1; to: 200; step: 0.1; decimals: 2
                        value: comp.attack; hasAuto: true
                        autoMode: comp.autoAttack; autoValue: comp.attack
                        onEdited: (v) => comp.attack = v
                        onAutoToggled: (on) => comp.autoAttack = on
                    }
                    ParamRow {
                        label: '释放时间'; unit: 'ms'; from: 5; to: 2000; step: 1; decimals: 0
                        value: comp.release; hasAuto: true
                        autoMode: comp.autoRelease; autoValue: comp.release
                        onEdited: (v) => comp.release = v
                        onAutoToggled: (on) => comp.autoRelease = on
                    }
                    ParamRow {
                        label: '增益补偿'; unit: 'dB'; from: -12; to: 24; step: 0.5
                        value: comp.makeup; hasAuto: true
                        autoMode: comp.autoMakeup; autoValue: comp.effectiveMakeup
                        onEdited: (v) => comp.makeup = v
                        onAutoToggled: (on) => comp.autoMakeup = on
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '混响'
            hint: 'Freeverb · 八路并联梳状 + 四级串联全通 · 附加预延迟与输入带宽'

            headerRight: HusSwitch {
                checked: rev.enabled
                onToggled: rev.enabled = checked
            }

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: 22
                rowSpacing: 8

                ParamRow {
                    label: '房间大小'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.roomSize
                    onEdited: (v) => rev.roomSize = v
                }
                ParamRow {
                    label: '预延迟'; unit: 'ms'; from: 0; to: 200; step: 1; decimals: 0
                    value: rev.preDelay
                    onEdited: (v) => rev.preDelay = v
                }
                ParamRow {
                    label: '阻尼系数'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.damping
                    onEdited: (v) => rev.damping = v
                }
                ParamRow {
                    label: '空间密度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.density
                    onEdited: (v) => rev.density = v
                }
                ParamRow {
                    label: '信号带宽'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.bandwidth
                    onEdited: (v) => rev.bandwidth = v
                }
                ParamRow {
                    label: '立体声宽度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.width
                    onEdited: (v) => rev.width = v
                }
                ParamRow {
                    label: '湿混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.wet
                    onEdited: (v) => rev.wet = v
                }
                ParamRow {
                    label: '干混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: rev.dry
                    onEdited: (v) => rev.dry = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '心理声学低音'
            hint: '合成缺失基频的谐波 —— 小喇叭放不出 40 Hz,但耳朵能从 80/120 Hz 推断出它'

            headerRight: HusSwitch {
                checked: fx.bassEnabled
                onToggled: fx.bassEnabled = checked
            }

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: 22
                rowSpacing: 8

                ParamRow {
                    label: '扬声器口径'; unit: 'Hz'; from: 40; to: 250; step: 1; decimals: 0
                    value: fx.bassCutoff
                    onEdited: (v) => fx.bassCutoff = v
                }
                ParamRow {
                    label: '低音水平'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: fx.bassAmount
                    onEdited: (v) => fx.bassAmount = v
                }
                ParamRow {
                    label: '谐波强度'; unit: ''; from: 1; to: 12; step: 0.1; decimals: 1
                    value: fx.bassDrive
                    onEdited: (v) => fx.bassDrive = v
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    HusText {
                        Layout.preferredWidth: 76
                        text: '移除原低频'
                        font.pixelSize: 12
                    }
                    HusSwitch {
                        checked: fx.bassRemoveOriginal
                        onToggled: fx.bassRemoveOriginal = checked
                    }
                    HusText {
                        Layout.fillWidth: true
                        text: '把喇叭放不出的部分滤掉,让振幅留给谐波'
                        font.pixelSize: 11
                        opacity: 0.45
                        elide: Text.ElideRight
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '胆机饱和'
            hint: '非对称软削波 · 2 倍过采样抗混叠 · 偏置产生偶次谐波'

            headerRight: HusSwitch {
                checked: fx.tubeEnabled
                onToggled: fx.tubeEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '驱动'; unit: ''; from: 1; to: 20; step: 0.1; decimals: 1
                    value: fx.tubeDrive
                    onEdited: (v) => fx.tubeDrive = v
                }
                ParamRow {
                    label: '偏置'; unit: ''; from: 0; to: 0.8; step: 0.01; decimals: 2
                    value: fx.tubeBias
                    onEdited: (v) => fx.tubeBias = v
                }
                ParamRow {
                    label: '干湿比'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: fx.tubeMix
                    onEdited: (v) => fx.tubeMix = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '激励器 / 清晰度'
            hint: '只对高频段做谐波激励,再混回原信号 —— 加细节而不是加脏'

            headerRight: HusSwitch {
                checked: fx.exciterEnabled
                onToggled: fx.exciterEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '起始频率'; unit: 'Hz'; from: 1000; to: 12000; step: 50; decimals: 0
                    value: fx.exciterFreq
                    onEdited: (v) => fx.exciterFreq = v
                }
                ParamRow {
                    label: '驱动'; unit: ''; from: 1; to: 15; step: 0.1; decimals: 1
                    value: fx.exciterDrive
                    onEdited: (v) => fx.exciterDrive = v
                }
                ParamRow {
                    label: '混入量'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    value: fx.exciterAmount
                    onEdited: (v) => fx.exciterAmount = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '立体声扩展'
            hint: 'M/S 侧信号增益 · 宽度 1.00 是精确的恒等'

            headerRight: HusSwitch {
                checked: fx.widthEnabled
                onToggled: fx.widthEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '宽度'; unit: ''; from: 0; to: 2; step: 0.01; decimals: 2
                    value: fx.stereoWidth
                    onEdited: (v) => fx.stereoWidth = v
                }
                ParamRow {
                    label: '低频归中'; unit: 'Hz'; from: 0; to: 400; step: 5; decimals: 0
                    value: fx.monoBelow
                    onEdited: (v) => fx.monoBelow = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: 'Crossfeed'
            hint: '耳机用 —— 把每个声道延迟、低通后混一点到另一边,模拟头部遮蔽'

            headerRight: HusSwitch {
                checked: fx.crossfeedEnabled
                onToggled: fx.crossfeedEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '截止频率'; unit: 'Hz'; from: 300; to: 1500; step: 10; decimals: 0
                    value: fx.crossfeedCutoff
                    onEdited: (v) => fx.crossfeedCutoff = v
                }
                ParamRow {
                    label: '串扰量'; unit: 'dB'; from: -18; to: 0; step: 0.5; decimals: 1
                    value: fx.crossfeedLevel
                    onEdited: (v) => fx.crossfeedLevel = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '多频段压缩'
            hint: 'Linkwitz-Riley 4 阶分频 —— 低频不再压掉高频。全部 1:1 时透明'

            headerRight: HusSwitch {
                checked: fx.multibandEnabled
                onToggled: fx.multibandEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '低/中分频'; unit: 'Hz'; from: 60; to: 800; step: 5; decimals: 0
                    value: fx.lowCross
                    onEdited: (v) => fx.lowCross = v
                }
                ParamRow {
                    label: '中/高分频'; unit: 'Hz'; from: 1000; to: 12000; step: 50; decimals: 0
                    value: fx.highCross
                    onEdited: (v) => fx.highCross = v
                }
            }
        }
    }
    }
}
