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

        // The DSP is written and verified, but nothing hosts it yet. Saying so
        // is better than shipping controls that quietly do nothing.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notice.implicitHeight + 20
            radius: 8
            color: Qt.rgba(1, 0.7, 0.1, 0.09)
            border.width: 1
            border.color: Qt.rgba(1, 0.7, 0.1, 0.35)

            RowLayout {
                id: notice
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                HusText {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: HusTheme.Primary.colorWarning
                    text: '压缩器的 DSP 已实现并通过离线验证(增益曲线、拐点、时间常数均对照解析解断言),'
                          + '但尚未接入音频链 —— Equalizer APO 的配置语言无法表达非线性处理,宿主方案待定。'
                          + '下方曲线由真实的 gain computer 计算,不是示意图。'
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 292
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
            Layout.preferredHeight: 236
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
            Layout.preferredHeight: 86
            title: '计划中'
            hint: '需要真实 DSP,和已有效果共用同一套离线验证流程'

            Flow {
                anchors.fill: parent
                spacing: 8

                Repeater {
                    model: ['胆机饱和', '心理声学低音', '激励器 / 清晰度',
                            '立体声扩展', 'Crossfeed', '多频段压缩']
                    delegate: HusTag {
                        required property string modelData
                        text: modelData
                    }
                }
            }
        }
    }
    }
}
