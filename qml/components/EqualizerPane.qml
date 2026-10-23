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

    // The strips are laid out in frequency order, whatever order the bands
    // are stored in: an AutoEQ import lists them by filter, not by frequency,
    // and 105, 181, 6.5k, 9.1k, 2.9k across a row reads as noise.
    property var bandFreqs: []
    function refreshFreqs() {
        const out = [];
        for (let i = 0; i < strips.count; ++i) {
            const it = strips.itemAt(i);
            out.push(it ? it.freq : 0);
        }
        bandFreqs = out;
    }
    function rankOf(i) {
        if (bandFreqs.length !== strips.count || i >= bandFreqs.length)
            return i;
        const f = bandFreqs[i];
        let r = 0;
        for (let j = 0; j < bandFreqs.length; ++j)
            if (bandFreqs[j] < f || (bandFreqs[j] === f && j < i)) ++r;
        return r;
    }

    // Scrolls when the cards do not fit -- the empty graphic card is taller
    // than the loaded one, and the band sliders must keep a usable height
    // whatever else is on the page.
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, page.minHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        HusScrollBar.vertical: HusScrollBar { }

        ColumnLayout {
            id: page
            width: flick.width - 12
            height: flick.contentHeight
            spacing: Ui.gap
            // Preset bar + curve + graphic card + a parametric card tall
            // enough for sliders.
            readonly property real minHeight: 40 + 190 + 12 + graphicCard.implicitHeight + 12 + 260 + 24

            PresetBar { Layout.fillWidth: true }

            SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 190
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
                id: graphicCard
                Layout.fillWidth: true
                // Folded to its header while there is no curve: there is nothing
                // in it to adjust, and the band sliders below need the room.
                // The paste button opens it again for the paste box.
                collapsible: true
                active: AppController.graphicEnabled && AppController.graphicLoaded
                title: '图形均衡'
                hint: AppController.graphicLoaded
                      ? '31 段 1/3 倍频'
                      : '还没有曲线 · 拖入 GraphicEQ.txt / 飞傲调音文件 .xml 都可以'

                // One row while there is no curve. The explanation used to be a
                // box of its own, which pushed the band sliders below the fold
                // at the default window size -- where they cannot be dragged.
                headerRight: RowLayout {
                    spacing: 10

                    HusButton {
                        visible: !AppController.graphicLoaded
                        text: '粘贴曲线'
                        type: HusButton.Type_Primary
                        onClicked: {
                            graphicCard._override = 1;
                            pasteBox.visible = !pasteBox.visible;
                        }
                    }

                    HusSwitch {
                        id: graphicSwitch
                        // Nothing to switch on until a curve is loaded: 31 bands at
                        // 0 dB do nothing, and a switch that is on while doing nothing
                        // is the thing that made this card look broken.
                        enabled: AppController.graphicLoaded || AppController.graphicEnabled
                        checked: AppController.graphicEnabled
                        onToggled: {
                            AppController.graphicEnabled = checked;
                            // A refused request leaves the model where it was; the
                            // binding the click broke has to be put back by hand.
                            checked = Qt.binding(() => AppController.graphicEnabled);
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    opacity: (AppController.graphicEnabled || !AppController.graphicLoaded) ? 1.0 : 0.5

                    Note {
                        visible: Ui.showNotes && !AppController.graphicLoaded
                        text: '这里载入的是别人做好的曲线：AutoEQ 网站的 GraphicEQ.txt 会拟合成 31 段均衡；'
                              + '别人分享的飞傲调音文件(.xml)或 ParametricEQ.txt 会直接变成上面的参数均衡段。'
                              + '在 AutoEQ 页导入过同一副耳机的话,不需要再载入。也可以把文件直接拖到这张卡片上。'
                    }
                    RowLayout {
                        visible: AppController.graphicLoaded
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
                            text: '换一条'
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
                        // Anything: a FiiO XML, an AutoEQ parametric text, a
                        // GraphicEQ curve. loadEqFile decides from the contents.
                        if (drop.hasUrls && drop.urls.length > 0)
                            AppController.loadEqFile(drop.urls[0]);
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: '参数均衡'
                hint: root.bandView === 0 ? '拖动滑块调整增益 · ±15 dB'
                                          : '逐段编辑频率 / 增益 / Q / 滤波器类型'

                // The view selector and the stage's own switch. Every other effect
                // in the application has one of these; this one did not, which is
                // why the parametric equalizer looked like it was always on.
                headerRight: RowLayout {
                    spacing: 10
                    HusSegmented {
                        options: [{ label: '图形' }, { label: '参数' }]
                        Component.onCompleted: currentIndex = root.bandView
                        onCurrentIndexChanged: root.bandView = currentIndex
                    }
                    HusSwitch {
                        checked: AppController.eqEnabled
                        onToggled: AppController.eqEnabled = checked
                    }
                }

                StackLayout {
                    anchors.fill: parent
                    currentIndex: root.bandView

                    RowLayout {
                        spacing: 6

                        // The preamp belongs with the bands: it is just another
                        // gain stage in the same chain.
                        ColumnLayout {
                            Layout.fillHeight: true
                            Layout.preferredWidth: 44
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
                            Layout.rightMargin: 8
                            orientation: Qt.Vertical
                        }

                        // Positioned by hand rather than by the row: a layout
                        // places children in the order they were created, and
                        // these have to be in frequency order instead.
                        Item {
                            id: stripArea
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            readonly property real pitch:
                                strips.count > 0 ? width / strips.count : width

                            Repeater {
                                id: strips
                                model: AppController.bands
                                onCountChanged: Qt.callLater(root.refreshFreqs)

                                delegate: BandStrip {
                                    required property var model
                                    required property int index
                                    readonly property real freq: model.frequency
                                    onFreqChanged: Qt.callLater(root.refreshFreqs)

                                    x: root.rankOf(index) * stripArea.pitch
                                    width: stripArea.pitch
                                    height: stripArea.height
                                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

                                    bandIndex: index
                                    gain: model.gain
                                    label: model.label
                                    range: 15
                                    onGainEdited: (v) => AppController.bands.setGain(index, v)
                                }
                            }
                        }
                    }

                    BandTable { }
                }
            }
            }
    }

}
