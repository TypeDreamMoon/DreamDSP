import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// The equalizer: presets, predicted response with an optional live spectrum,
// and the bands themselves as either sliders or a parameter table.
//
// One card, not two. The third-octave graphic bank that used to sit in a card of
// its own above this one is still a stage in the chain -- it is what a
// point-by-point curve (AutoEQ's GraphicEQ.txt) gets fitted to -- but it is not
// a thing the user picks between. Importing a curve chooses for them, and the
// only control it needs is an amount and an off switch, both of which live in
// the strip that appears when there is a curve.
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

    // Which recent entry is in effect, kept in step by hand (see historySelect).
    function syncHistory() {
        const entries = AppController.eqHistory;
        let found = -1;
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].path === AppController.eqSourcePath) { found = i; break; }
        }
        historySelect.currentIndex = found;
    }

    Component.onCompleted: syncHistory()

    Connections {
        target: AppController
        function onEqSourceChanged() { root.syncHistory() }
        function onEqHistoryChanged() { root.syncHistory() }
    }

    // Scrolls when the cards do not fit -- the paste box and the graphic strip
    // are both taller than what is left of the window, and the band sliders must
    // keep a usable height whatever else is on the page.
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
            // Preset bar + curve + the equalizer card tall enough for sliders,
            // a source line and the graphic strip when one is loaded.
            readonly property real minHeight: 40 + 190 + 12 + 470 + 24

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
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: '参数均衡'
                hint: {
                    const bands = AppController.bands.count;
                    const base = root.bandView === 0 ? '拖动滑块调整增益 · ±15 dB'
                                                     : '逐段编辑频率 / 增益 / Q / 滤波器类型';
                    return AppController.graphicLoaded
                           ? base + ' · 另有一条 31 段图形曲线'
                           : base + ' · ' + bands + ' 段';
                }

                headerRight: RowLayout {
                    spacing: 10

                    // Only while there is no curve: once one is loaded, "换一条"
                    // in the strip below is where this lives.
                    HusButton {
                        visible: !AppController.graphicLoaded
                        text: '粘贴曲线'
                        onClicked: pasteBox.visible = !pasteBox.visible
                    }

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

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    // ------------------------------------------------ source
                    // Where the curve came from, when it came from a file. A
                    // curve typed into the paste box has no path and no row:
                    // there is nothing to reopen, and nothing to remember.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: AppController.eqSourcePath.length > 0
                                 || AppController.eqHistory.length > 0

                        HusText {
                            text: '曲线来源'
                            font.pixelSize: 11
                            opacity: 0.55
                        }

                        // The files imported before, newest first. Picking one
                        // loads it again; the row below shows which one is in
                        // effect now.
                        HusSelect {
                            id: historySelect
                            Layout.preferredWidth: 330
                            model: AppController.eqHistory
                            // Driven imperatively: choosing an entry makes the
                            // control write currentIndex itself, which would
                            // destroy a binding here and leave the box frozen on
                            // whatever was picked first.
                            currentIndex: -1
                            placeholderText: '最近导入的曲线…'
                            onActivated: (index) => AppController.loadEqHistory(index)
                        }

                        HusText {
                            Layout.fillWidth: true
                            text: AppController.eqSourcePath
                            elide: Text.ElideMiddle
                            font.pixelSize: 11
                            opacity: AppController.eqSourceExists ? 0.8 : 0.45
                            color: AppController.eqSourceExists
                                   ? HusTheme.Primary.colorTextBase
                                   : HusTheme.Primary.colorWarning
                        }

                        HusButton {
                            text: '重新载入'
                            enabled: AppController.eqSourceExists
                            onClicked: AppController.reloadEqSource()

                            HusToolTip {
                                parent: parent
                                visible: parent.hovered
                                text: (AppController.eqSourceExists
                                       ? AppController.eqSourcePath
                                       : AppController.eqSourcePath + ' —— 文件不在了')
                                      + '\n按这个路径再读一次,外部改过文件时用'
                            }
                        }

                        HusIconButton {
                            type: HusButton.Type_Text
                            iconSource: HusIcon.DeleteOutlined
                            enabled: AppController.eqHistory.length > 0
                            onClicked: AppController.clearEqHistory()

                            HusToolTip {
                                parent: parent
                                visible: parent.hovered
                                text: '清空最近导入的曲线记录(当前曲线不变)'
                            }
                        }
                    }

                    // -------------------------------------------- graphic bank
                    // Present only when a point-by-point curve was fitted to the
                    // third-octave bank. It is a stage of its own underneath, but
                    // there is nothing to choose here: the import chose it.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: AppController.graphicLoaded

                        HusText {
                            text: '图形曲线'
                            font.pixelSize: 12
                            opacity: 0.7
                        }

                        HusText {
                            text: '31 段 · 与目标偏差 ' + AppController.graphicFitError.toFixed(2) + ' dB'
                            font.pixelSize: 11
                            opacity: 0.5
                        }

                        Item { Layout.fillWidth: true }

                        HusText { text: '强度'; font.pixelSize: 11; opacity: 0.6 }

                        Fader {
                            Layout.preferredWidth: 150
                            min: 0; max: 1; stepSize: 0.01
                            value: AppController.graphicAmount
                            onFirstMoved: AppController.graphicAmount = currentValue
                        }

                        HusText {
                            text: Math.round(AppController.graphicAmount * 100) + '%'
                            font.pixelSize: 11
                            Layout.preferredWidth: 38
                        }

                        HusButton {
                            text: '换一条'
                            onClicked: pasteBox.visible = !pasteBox.visible
                        }

                        HusButton {
                            text: '清除'
                            onClicked: AppController.clearGraphicEq()
                        }

                        HusSwitch {
                            id: graphicSwitch
                            // 31 bands at 0 dB do nothing, so the switch follows
                            // what is loaded rather than the other way round.
                            enabled: AppController.graphicLoaded || AppController.graphicEnabled
                            checked: AppController.graphicEnabled
                            onToggled: {
                                AppController.graphicEnabled = checked;
                                // A refused request leaves the model where it
                                // was; the binding the click broke has to be put
                                // back by hand.
                                checked = Qt.binding(() => AppController.graphicEnabled);
                            }
                        }
                    }

                    // The 31 bands as they came out of the fit, small: the
                    // sliders below are where the curve gets adjusted.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56
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
                                    // Zero sits on the mid-line; +-15 dB fills the row.
                                    readonly property real half: parent.height / 2
                                    readonly property real px:
                                        Math.max(-half, Math.min(half, modelData.gain / 15 * half))
                                    height: Math.max(2, Math.abs(px))
                                    y: px >= 0 ? half - px : half
                                    radius: 2
                                    color: HusTheme.Primary.colorPrimary
                                    opacity: AppController.graphicEnabled ? 0.85 : 0.35
                                }
                            }
                        }
                    }

                    // ------------------------------------------------- paste
                    ColumnLayout {
                        id: pasteBox
                        visible: false
                        Layout.fillWidth: true
                        spacing: 6

                        HusInput {
                            id: curveText
                            Layout.fillWidth: true
                            placeholderText: 'GraphicEQ: 20 -1.5; 25 -1.4; 31.5 -1.3; …'
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
                                text: '粘贴的曲线不记录来源;文件请直接拖到这张卡片上'
                                font.pixelSize: 11
                                opacity: 0.5
                            }
                        }
                    }

                    // ------------------------------------------------- bands
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
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

                // Second child of the card, not of the layout above it: a DropArea
                // anchored inside a ColumnLayout is undefined behaviour, and the
                // card's content area is a plain Item that takes anchors happily.
                // It has to come after the layout -- SectionCard sizes itself
                // from children[0].
                //
                // Anything lands here: a PEQ .xml, an AutoEQ parametric text, a
                // GraphicEQ curve, a .peace preset. loadEqFile decides from the
                // contents, because the names say nothing.
                DropArea {
                    anchors.fill: parent
                    // Below the controls, not on top of them: an item stacked
                    // above another takes the mouse press even when it does not
                    // use it, and a card-sized target that swallowed clicks
                    // would make every slider in this card dead. Drops are
                    // routed to the DropArea whatever its z is.
                    z: -1
                    onDropped: (drop) => {
                        if (drop.hasUrls && drop.urls.length > 0)
                            AppController.loadEqFile(drop.urls[0]);
                    }
                }
            }
        }
    }
}
