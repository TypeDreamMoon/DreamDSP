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
