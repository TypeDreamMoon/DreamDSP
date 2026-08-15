import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The equalizer: presets, predicted response with an optional live spectrum,
// and the bands themselves as either sliders or a parameter table.
Item {
    id: root

    // 0 = graphic sliders, 1 = parameter table
    property int bandView: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        PresetBar { Layout.fillWidth: true }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            title: '频响曲线'
            hint: AppController.spectrumEnabled
                  ? (AppController.spectrumLive ? '预测响应 + 实时频谱' : '实时频谱 · 当前无声音')
                  : '预测响应 · 20 Hz – 20 kHz · 对数刻度'

            headerRight: HusButton {
                text: AppController.spectrumEnabled ? '频谱 开' : '频谱 关'
                type: AppController.spectrumEnabled ? HusButton.Type_Primary
                                                    : HusButton.Type_Default
                onClicked: AppController.spectrumEnabled = !AppController.spectrumEnabled
            }

            ResponseCurveItem {
                anchors.fill: parent
                bands: AppController.bands
                preamp: AppController.preamp
                rangeDb: 15
                // Drawn at the endpoint's rate, so the curve on screen is the
                // filter the endpoint is running rather than a 48 kHz stand-in.
                sampleRate: AppController.deviceRate
                spectrum: AppController.spectrum
                spectrumColor: HusTheme.isDark ? '#63b3ff' : '#2b7fd4'
                curveColor: HusTheme.Primary.colorPrimary
                gridColor: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
                labelColor: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.45) : Qt.rgba(0, 0, 0, 0.45)
                opacity: AppController.eqEnabled ? 1.0 : 0.35

                Behavior on opacity { NumberAnimation { duration: 180 } }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            title: '图形均衡'
            hint: '31 段 1/3 倍频 —— 贴 AutoEQ 的 GraphicEQ 曲线进来'

            headerRight: HusSwitch {
                checked: AppController.graphicEnabled
                onToggled: AppController.graphicEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                opacity: AppController.graphicEnabled ? 1.0 : 0.5

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    HusText { text: '强度'; font.pixelSize: 12; opacity: 0.7 }
                    Fader {
                        Layout.fillWidth: true
                        min: 0; max: 1; stepSize: 0.01
                        value: AppController.graphicAmount
                        onFirstMoved: AppController.graphicAmount = currentValue
                    }
                    HusText {
                        text: Math.round(AppController.graphicAmount * 100) + '%'
                        font.pixelSize: 12
                        Layout.preferredWidth: 42
                    }
                    HusButton {
                        text: '粘贴曲线'
                        onClicked: pasteBox.visible = !pasteBox.visible
                    }
                    HusButton {
                        text: '清除'
                        enabled: AppController.graphicLoaded
                        onClicked: AppController.clearGraphicEq()
                    }
                }

                HusText {
                    visible: AppController.graphicLoaded
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    opacity: 0.55
                    // The gap between what was asked for and what a third-octave
                    // bank can produce. Stated, because it is a real limit and a
                    // silent one would be worse.
                    text: '已载入曲线 · 与目标最大偏差 '
                          + AppController.graphicFitError.toFixed(2) + ' dB'
                }

                ColumnLayout {
                    id: pasteBox
                    visible: false
                    Layout.fillWidth: true
                    spacing: 6

                    HusInput {
                        id: curveText
                        Layout.fillWidth: true
                        placeholderText: 'GraphicEQ: 20 -1.5; 25 -1.4; 31.5 -1.3; ...'
                    }
                    RowLayout {
                        spacing: 8
                        HusButton {
                            text: '载入'
                            type: HusButton.Type_Primary
                            onClicked: {
                                if (AppController.importGraphicEq(curveText.text))
                                    pasteBox.visible = false;
                            }
                        }
                        HusText {
                            text: '也可以把 *GraphicEQ.txt 直接拖到这张卡片上'
                            font.pixelSize: 11
                            opacity: 0.5
                        }
                    }
                }

                // The 31 bands as they came out of the fit.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 74
                    spacing: 2
                    visible: AppController.graphicLoaded

                    Repeater {
                        model: AppController.graphicBands
                        delegate: Item {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.max(3, parent.width - 3)
                                // Zero sits on the mid-line; +-15 dB fills the card.
                                readonly property real half: parent.height / 2
                                readonly property real px:
                                    Math.max(-half, Math.min(half, modelData.gain / 15 * half))
                                height: Math.max(2, Math.abs(px))
                                y: px >= 0 ? half - px : half
                                radius: 2
                                color: HusTheme.Primary.colorPrimary
                                opacity: 0.85
                            }
                        }
                    }
                }
            }

            // Second child of the card, not of the layout above it: a DropArea
            // anchored inside a ColumnLayout is undefined behaviour, and the
            // card's content area is a plain Item that takes anchors happily.
            // It has to come after the layout -- SectionCard sizes itself from
            // children[0].
            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls && drop.urls.length > 0)
                        AppController.loadGraphicEqFile(drop.urls[0]);
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: '均衡'
            hint: root.bandView === 0 ? '拖动滑块调整增益 · ±15 dB'
                                      : '逐段编辑频率 / 增益 / Q / 滤波器类型'

            headerRight: HusSegmented {
                options: [{ label: '图形' }, { label: '参数' }]
                Component.onCompleted: currentIndex = root.bandView
                onCurrentIndexChanged: root.bandView = currentIndex
            }

            StackLayout {
                anchors.fill: parent
                currentIndex: root.bandView

                RowLayout {
                    spacing: 14

                    // The preamp belongs with the bands: it is just another
                    // gain stage in the same chain.
                    ColumnLayout {
                        Layout.fillHeight: true
                        spacing: 6

                        HusText {
                            Layout.alignment: Qt.AlignHCenter
                            text: (AppController.preamp > 0.05 ? '+' : '')
                                  + AppController.preamp.toFixed(1)
                            font.pixelSize: 11
                            opacity: Math.abs(AppController.preamp) < 0.05 ? 0.4 : 0.95
                            color: Math.abs(AppController.preamp) < 0.05
                                   ? HusTheme.Primary.colorTextBase
                                   : HusTheme.Primary.colorWarning
                        }

                        Fader {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillHeight: true
                            orientation: Qt.Vertical
                            min: -30
                            max: 10
                            stepSize: 0.1
                            value: AppController.preamp
                            onFirstMoved: AppController.preamp = currentValue
                            onFirstReleased: AppController.preamp = currentValue
                        }

                        HusText {
                            Layout.alignment: Qt.AlignHCenter
                            text: '前置'
                            font.pixelSize: 10
                            opacity: 0.6
                        }

                        Item { Layout.preferredHeight: 1 }
                    }

                    HusDivider {
                        Layout.fillHeight: true
                        orientation: Qt.Vertical
                    }

                    Repeater {
                        model: AppController.bands

                        delegate: BandStrip {
                            required property var model
                            required property int index

                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            bandIndex: index
                            gain: model.gain
                            label: model.label
                            range: 15
                            onGainEdited: (v) => AppController.bands.setGain(index, v)
                        }
                    }
                }

                BandTable { }
            }
        }
    }
}
