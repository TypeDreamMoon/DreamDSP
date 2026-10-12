import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Impulse-response browser. APO does the convolution itself, so this only has
// to find files and write one line.
Item {
    id: root

    property var results: []

    function runSearch() {
        results = AppController.searchImpulses(searchField.text, 250);
    }

    function ensureLoaded() {
        if (AppController.impulseCount === 0 && AppController.loadImpulses())
            runSearch();
        else if (AppController.impulseCount > 0 && results.length === 0)
            runSearch();
    }

    onVisibleChanged: if (visible) ensureLoaded()

    Timer {
        id: debounce
        interval: 180
        onTriggered: root.runSearch()
    }

    // Library on the left, the chosen file on the right; stacked when the
    // window is narrow. The page used to be one long column in which the
    // chosen file's curve and sliders pushed the list off the bottom.
    GridLayout {
        id: grid
        anchors.fill: parent
        readonly property bool wide: width >= 860
        columns: wide ? 2 : 1
        columnSpacing: Ui.gap
        rowSpacing: Ui.gap

        // -------------------------------------------------------- the library
        SectionCard {
            Layout.row: grid.wide ? 0 : 1
            Layout.column: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: '脉冲响应库'
            hint: AppController.impulseCount > 0
                  ? (root.results.length < AppController.impulseCount
                     ? '显示 ' + root.results.length + ' / ' + AppController.impulseCount
                     : AppController.impulseCount + ' 个文件')
                  : '还没有扫描'

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    HusInput {
                        id: searchField
                        Layout.fillWidth: true
                        placeholderText: AppController.impulseCount > 0
                                         ? '搜索脉冲响应…'
                                         : '尚未扫描'
                        enabled: AppController.impulseCount > 0
                        onTextChanged: debounce.restart()
                    }

                    HusButton {
                        visible: AppController.impulseCount === 0
                        text: '扫描'
                        type: HusButton.Type_Primary
                        onClicked: root.ensureLoaded()
                    }
                }

                Note {
                    text: '来源:DreamDSP 的 impulses 目录、APO 配置目录,以及 ViPER4Windows 自带的 IR 库。'
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: root.results

                    HusScrollBar.vertical: HusScrollBar { }

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property bool current:
                            AppController.convolutionFile !== ''
                            && row.modelData.name === AppController.convolutionName

                        width: ListView.view.width
                        height: 44
                        radius: 6
                        color: row.current
                               ? Qt.alpha(HusTheme.Primary.colorPrimary, 0.12)
                               : hover.hovered
                                 ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
                                 : 'transparent'

                        HoverHandler { id: hover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                HusText {
                                    text: row.modelData.name
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                HusText {
                                    text: row.modelData.category
                                    font.pixelSize: 10
                                    opacity: 0.45
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            HusText {
                                text: row.modelData.ok
                                      ? (row.modelData.rate / 1000).toFixed(1) + ' kHz · '
                                        + row.modelData.ms + ' ms'
                                      : '未知格式'
                                font.pixelSize: 10
                                opacity: 0.5
                            }

                            // Only a problem when Equalizer APO is doing the work: it
                            // would load the file but produce something other than what
                            // the file describes. DreamDSP converts it instead.
                            HusTag {
                                text: '采样率不符'
                                visible: row.modelData.mismatch && !AppController.nativeConvolution
                                colorText: HusTheme.Primary.colorError
                            }

                            HusTag {
                                visible: row.current
                                text: '使用中'
                                presetColor: 'blue'
                            }

                            // Quiet until the row is pointed at: a primary
                            // button on each of 250 rows is a wall of blue.
                            HusButton {
                                visible: !row.current
                                opacity: hover.hovered ? 1.0 : 0.0
                                text: '应用'
                                type: HusButton.Type_Primary
                                onClicked: AppController.selectImpulse(row.modelData.index)
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------- the chosen one
        SectionCard {
            Layout.row: 0
            Layout.column: grid.wide ? 1 : 0
            Layout.fillWidth: !grid.wide
            Layout.preferredWidth: grid.wide ? Math.min(420, grid.width * 0.42) : -1
            Layout.alignment: Qt.AlignTop
            title: '当前脉冲响应'
            hint: AppController.convolutionFile !== '' ? '' : '未选择'
            border.color: AppController.rateMismatch ? HusTheme.Primary.colorError
                                                     : (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.08)
                                                                        : Qt.rgba(0, 0, 0, 0.07))

            headerRight: HusSwitch {
                enabled: AppController.convolutionFile !== ''
                checked: AppController.convolutionEnabled
                onToggled: AppController.convolutionEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: Ui.rowGap

                HusText {
                    visible: AppController.convolutionFile === ''
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Ui.label
                    opacity: 0.5
                    text: '在脉冲响应库里点「应用」。脉冲响应把一个房间、一副耳机或一台设备的声音特征套到你听的东西上。'
                }

                RowLayout {
                    visible: AppController.convolutionFile !== ''
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        HusText {
                            text: AppController.convolutionName
                            font.pixelSize: Ui.body
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        HusText {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Ui.caption
                            opacity: AppController.rateMismatch ? 0.95 : 0.55
                            color: AppController.rateMismatch ? HusTheme.Primary.colorError
                                                              : HusTheme.Primary.colorTextBase
                            text: {
                                if (AppController.nativeConvolution) {
                                    var same = AppController.convolutionRate === AppController.deviceRate;
                                    return AppController.convolutionRate + ' Hz'
                                           + (same ? ' · 与设备一致'
                                                   : ' → 自动重采样到 ' + AppController.deviceRate + ' Hz')
                                           + ' · 由 DreamDSP 卷积';
                                }
                                if (AppController.rateMismatch) {
                                    return '⚠ 采样率不匹配:脉冲响应 ' + AppController.convolutionRate
                                           + ' Hz,设备 ' + AppController.deviceRate
                                           + ' Hz。Equalizer APO 要求两者一致 —— 安装 DreamDSP 音频组件即可任意采样率。';
                                }
                                return AppController.convolutionRate + ' Hz · 与设备一致 · 由 Equalizer APO 卷积';
                            }
                        }
                    }

                    HusButton {
                        Layout.alignment: Qt.AlignTop
                        text: '移除'
                        type: HusButton.Type_Text
                        onClicked: AppController.clearConvolution()
                    }
                }

                // Several hundred impulse responses whose names are things
                // like "Jazz Club" or "01.Acoustic" cannot be chosen between
                // by name. The curve says in one glance what the name does not.
                ImpulseCurveItem {
                    visible: AppController.impulseCurve.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    curve: AppController.impulseCurve
                    floorDb: AppController.impulseCurveFloor
                    curveColor: HusTheme.Primary.colorPrimary
                    gridColor: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.08)
                                               : Qt.rgba(0, 0, 0, 0.08)
                    labelColor: HusTheme.Primary.colorTextQuaternary
                }

                Note {
                    visible: Ui.showNotes && AppController.impulseCurve.length > 0
                    text: '相对自身峰值,纵轴 0 至 ' + Math.round(AppController.impulseCurveFloor)
                          + ' dB(按曲线自动取范围)。电平由程序自动归一化,这里只看形状。'
                }

                ColumnLayout {
                    visible: AppController.convolutionFile !== ''
                    Layout.fillWidth: true
                    spacing: Ui.rowGap

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        enabled: AppController.nativeConvolution

                        HusText {
                            text: '干湿比'
                            font.pixelSize: 12
                            Layout.preferredWidth: 48
                        }
                        Fader {
                            Layout.fillWidth: true
                            min: 0
                            max: 1
                            stepSize: 0.01
                            value: AppController.convolutionMix
                            onFirstMoved: AppController.convolutionMix = currentValue
                        }
                        HusText {
                            text: Math.round(AppController.convolutionMix * 100) + '%'
                            font.pixelSize: 11
                            opacity: 0.6
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        enabled: AppController.nativeConvolution

                        HusText {
                            text: '增益'
                            font.pixelSize: 12
                            Layout.preferredWidth: 48
                        }
                        Fader {
                            Layout.fillWidth: true
                            min: -24
                            max: 12
                            stepSize: 0.5
                            value: AppController.convolutionTrim
                            onFirstMoved: AppController.convolutionTrim = currentValue
                        }
                        HusText {
                            text: AppController.convolutionTrim.toFixed(1) + ' dB'
                            font.pixelSize: 11
                            opacity: 0.6
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    HusText {
                        Layout.fillWidth: true
                        visible: !AppController.nativeConvolution
                        text: '干湿比与增益需要 DreamDSP 音频组件 —— Equalizer APO 的卷积没有这两个参数。'
                        font.pixelSize: Ui.caption
                        opacity: 0.5
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
