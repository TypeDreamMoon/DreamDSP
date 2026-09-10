import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// Effects that need real signal processing, i.e. everything APO's linear
// configuration language cannot express.
Item {
    id: root

    // Owned by AppController, not created here. A pane that owns its own
    // models only has them while it is on screen, which is no use to anything
    // that has to save them or push them to the audio engine.
    readonly property CompressorModel comp: AppController.compressor
    readonly property ReverbModel rev: AppController.reverb
    readonly property EffectsModel fx: AppController.effects

    OfflineRender {
        id: render
        compressor: root.comp
        reverb: root.rev
        effects: root.fx
        bands: AppController.bands
        preamp: AppController.preamp
        eqEnabled: AppController.eqEnabled
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
        // Shown instead of a number while the automatic mode is on, for the
        // parameters whose resolved value this process genuinely cannot know.
        property string autoText: ''

        // Which model property this row edits, for the reset button. The
        // default it resets to is read off the model rather than repeated
        // here: a number written twice is a number that will disagree with
        // itself.
        property QtObject owner: null
        property string prop: ''
        readonly property var defaultValue: (prow.owner && prow.prop !== '')
                                            ? prow.owner.defaultOf(prow.prop)
                                            : undefined
        readonly property bool atDefault:
            prow.defaultValue === undefined
            || Math.abs(prow.value - prow.defaultValue) <= Math.max(prow.step, 1e-6) * 0.5

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
            text: (prow.autoMode && prow.autoText !== '')
                  ? prow.autoText
                  : (prow.autoMode ? prow.autoValue : prow.value).toFixed(prow.decimals)
                    + ' ' + prow.unit
        }

        // Before the automatic button rather than after it, so that the reset
        // stays in the same column on every row -- the automatic button only
        // exists on the compressor's rows, and a layout hides what it cannot
        // see rather than reserving space for it.
        ResetButton {
            row: prow
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
        id: col
        // Capped, and centred once it is capped.
        // Nothing on this page reads better at 900 px per column than at 520 --
        // past that a slider's handle ends up half a screen away from the number
        // it sets. On a wide monitor the page becomes a tidy block rather than a
        // stretched one.
        readonly property real maxWidth: 1600
        width: Math.min(parent.width - 12, maxWidth)
        x: Math.max(0, (parent.width - width) / 2)
        // One column per 440 px, up to three. A fixed two was fine at the
        // default window and wrong on a wide one: the cards stretched to 900 px
        // each, which puts a slider's handle half a screen from the number it
        // sets and leaves the bottom half of the page empty.
        columns: Math.max(1, Math.min(3, Math.floor(width / 440)))
        columnSpacing: 12
        rowSpacing: 12

        // These effects have no host: nothing routes system audio through them
        // yet. Rather than leaving switches that quietly do nothing, the page
        // says so plainly and offers the one thing that does work today --
        // running the settings over a file.
        Rectangle {
            Layout.fillWidth: true
            Layout.columnSpan: col.columns
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
                        text: AppController.apoAttached ? '已接入系统音频' : '尚未接入系统音频'
                        presetColor: AppController.apoAttached ? '#389e0d' : '#d48806'
                    }
                    HusText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        opacity: 0.85
                        text: AppController.apoAttached
                              ? '下面的改动会实时作用到系统里所有声音上,约 100 ms 内到位。'
                                + AppController.apoStatusText
                              : '下面的开关只改参数,不会影响你正在听的声音 —— DreamDSP 的音频组件'
                                + '还没接入这台设备。到「设置 → 系统音效」安装并接入即可。'
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
            Layout.alignment: Qt.AlignTop
            Layout.columnSpan: Math.min(2, col.columns)
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
                    // Grows with the card instead of staying at 260 while the
                    // sliders take every pixel the window gains -- a 250 px
                    // curve next to a 1100 px slider is the wrong way round.
                    Layout.preferredWidth: Math.max(240, Math.min(360, parent.width * 0.28))
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
                        owner: comp; prop: 'threshold'
                        value: comp.threshold
                        onEdited: (v) => comp.threshold = v
                    }
                    ParamRow {
                        label: '压缩比率'; unit: ': 1'; from: 1; to: 20; step: 0.1
                        owner: comp; prop: 'ratio'
                        value: comp.ratio
                        onEdited: (v) => comp.ratio = v
                    }
                    ParamRow {
                        label: '拐点宽度'; unit: 'dB'; from: 0; to: 24; step: 0.5
                        owner: comp; prop: 'knee'
                        value: comp.knee; hasAuto: true
                        autoMode: comp.autoKnee; autoValue: comp.effectiveKnee
                        onEdited: (v) => comp.knee = v
                        onAutoToggled: (on) => comp.autoKnee = on
                    }
                    ParamRow {
                        label: '压缩时间'; unit: 'ms'; from: 0.1; to: 200; step: 0.1; decimals: 2
                        owner: comp; prop: 'attack'
                        value: comp.attack; hasAuto: true
                        autoMode: comp.autoAttack; autoText: '随素材自适应'
                        onEdited: (v) => comp.attack = v
                        onAutoToggled: (on) => comp.autoAttack = on
                    }
                    ParamRow {
                        label: '释放时间'; unit: 'ms'; from: 5; to: 2000; step: 1; decimals: 0
                        owner: comp; prop: 'release'
                        value: comp.release; hasAuto: true
                        autoMode: comp.autoRelease; autoText: '随素材自适应'
                        onEdited: (v) => comp.release = v
                        onAutoToggled: (on) => comp.autoRelease = on
                    }
                    ParamRow {
                        label: '增益补偿'; unit: 'dB'; from: -12; to: 24; step: 0.5
                        owner: comp; prop: 'makeup'
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
            Layout.alignment: Qt.AlignTop
            Layout.columnSpan: Math.min(2, col.columns)
            title: '混响'
            hint: 'Freeverb · 八路并联梳状 + 四级串联全通 · 附加预延迟与输入带宽'

            headerRight: HusSwitch {
                checked: rev.enabled
                onToggled: rev.enabled = checked
            }

            GridLayout {
                anchors.fill: parent
                // Two side-by-side sliders need about 560 px between them; below
                // that the fader is squeezed to nothing and the row becomes a
                // label, a number and no way to change it.
                columns: width >= 560 ? 2 : 1
                columnSpacing: 22
                rowSpacing: 8

                ParamRow {
                    label: '房间大小'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'roomSize'
                    value: rev.roomSize
                    onEdited: (v) => rev.roomSize = v
                }
                ParamRow {
                    label: '预延迟'; unit: 'ms'; from: 0; to: 200; step: 1; decimals: 0
                    owner: rev; prop: 'preDelay'
                    value: rev.preDelay
                    onEdited: (v) => rev.preDelay = v
                }
                ParamRow {
                    label: '阻尼系数'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'damping'
                    value: rev.damping
                    onEdited: (v) => rev.damping = v
                }
                ParamRow {
                    label: '空间密度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'density'
                    value: rev.density
                    onEdited: (v) => rev.density = v
                }
                ParamRow {
                    label: '信号带宽'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'bandwidth'
                    value: rev.bandwidth
                    onEdited: (v) => rev.bandwidth = v
                }
                ParamRow {
                    label: '立体声宽度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'width'
                    value: rev.width
                    onEdited: (v) => rev.width = v
                }
                ParamRow {
                    label: '湿混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'wet'
                    value: rev.wet
                    onEdited: (v) => rev.wet = v
                }
                ParamRow {
                    label: '干混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: rev; prop: 'dry'
                    value: rev.dry
                    onEdited: (v) => rev.dry = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            title: '心理声学低音'
            hint: '合成缺失基频的谐波 —— 小喇叭放不出 40 Hz,但耳朵能从 80/120 Hz 推断出它'

            headerRight: HusSwitch {
                checked: fx.bassEnabled
                onToggled: fx.bassEnabled = checked
            }

            GridLayout {
                anchors.fill: parent
                // Two side-by-side sliders need about 560 px between them; below
                // that the fader is squeezed to nothing and the row becomes a
                // label, a number and no way to change it.
                columns: width >= 560 ? 2 : 1
                columnSpacing: 22
                rowSpacing: 8

                ParamRow {
                    label: '扬声器口径'; unit: 'Hz'; from: 40; to: 250; step: 1; decimals: 0
                    owner: fx; prop: 'bassCutoff'
                    value: fx.bassCutoff
                    onEdited: (v) => fx.bassCutoff = v
                }
                ParamRow {
                    label: '低音水平'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'bassAmount'
                    value: fx.bassAmount
                    onEdited: (v) => fx.bassAmount = v
                }
                ParamRow {
                    label: '谐波强度'; unit: ''; from: 1; to: 12; step: 0.1; decimals: 1
                    owner: fx; prop: 'bassDrive'
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
            Layout.alignment: Qt.AlignTop
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
                    owner: fx; prop: 'tubeDrive'
                    value: fx.tubeDrive
                    onEdited: (v) => fx.tubeDrive = v
                }
                ParamRow {
                    label: '偏置'; unit: ''; from: 0; to: 0.8; step: 0.01; decimals: 2
                    owner: fx; prop: 'tubeBias'
                    value: fx.tubeBias
                    onEdited: (v) => fx.tubeBias = v
                }
                ParamRow {
                    label: '干湿比'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'tubeMix'
                    value: fx.tubeMix
                    onEdited: (v) => fx.tubeMix = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            title: '动态低音'
            hint: '按当前还剩多少动态余量来提升低频 —— 安静时给满,响时几乎不给'

            headerRight: HusSwitch {
                checked: fx.dynBassEnabled
                onToggled: fx.dynBassEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '最大提升'; unit: 'dB'; from: 0; to: 24; step: 0.5; decimals: 1
                    owner: fx; prop: 'dynBassMaxGain'
                    value: fx.dynBassMaxGain
                    onEdited: (v) => fx.dynBassMaxGain = v
                }
                ParamRow {
                    label: '分频点'; unit: 'Hz'; from: 30; to: 250; step: 5; decimals: 0
                    owner: fx; prop: 'dynBassCutoff'
                    value: fx.dynBassCutoff
                    onEdited: (v) => fx.dynBassCutoff = v
                }
                ParamRow {
                    label: '恢复'; unit: 'ms'; from: 10; to: 2000; step: 10; decimals: 0
                    owner: fx; prop: 'dynBassRelease'
                    value: fx.dynBassRelease
                    onEdited: (v) => fx.dynBassRelease = v
                }

                HusText {
                    text: '和「虚拟低音」不是一回事:那个合成放不出来的基频,这个抬高本来就有的。'
                    font.pixelSize: 11
                    opacity: 0.5
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
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
                    owner: fx; prop: 'exciterFreq'
                    value: fx.exciterFreq
                    onEdited: (v) => fx.exciterFreq = v
                }
                ParamRow {
                    label: '驱动'; unit: ''; from: 1; to: 15; step: 0.1; decimals: 1
                    owner: fx; prop: 'exciterDrive'
                    value: fx.exciterDrive
                    onEdited: (v) => fx.exciterDrive = v
                }
                ParamRow {
                    label: '混入量'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'exciterAmount'
                    value: fx.exciterAmount
                    onEdited: (v) => fx.exciterAmount = v
                }
                ParamRow {
                    label: '触发阈值'; unit: 'dB'; from: -120; to: -12; step: 1; decimals: 0
                    owner: fx; prop: 'exciterThreshold'
                    value: fx.exciterThreshold
                    onEdited: (v) => fx.exciterThreshold = v
                }
                Text {
                    text: '阈值以下波形整形完全不工作 —— 这是 Aphex 1979 年那份专利里' +
                          '真正的想法:只让瞬态被削,持续音保持干净。调到 -120 dB 就是' +
                          '一直工作的静态激励。'
                    color: HusTheme.Primary.colorTextSecondary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
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
                    owner: fx; prop: 'stereoWidth'
                    value: fx.stereoWidth
                    onEdited: (v) => fx.stereoWidth = v
                }
                ParamRow {
                    label: '低频归中'; unit: 'Hz'; from: 0; to: 400; step: 5; decimals: 0
                    owner: fx; prop: 'monoBelow'
                    value: fx.monoBelow
                    onEdited: (v) => fx.monoBelow = v
                }
                ParamRow {
                    label: '相位展宽'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'widthPhase'
                    value: fx.widthPhase
                    onEdited: (v) => fx.widthPhase = v
                }
                Text {
                    text: '侧信号增益只能缩放已经存在的侧信号:素材本来就是单声道时' +
                          '它做不了任何事。相位展宽(Zotter–Frank)对中间信号做相移,' +
                          '这是唯一能把单声道展开的办法 —— 代价是 2 ms 延时,' +
                          '以及单声道叠加最多 2.4 dB 的凹陷。'
                    color: HusTheme.Primary.colorTextSecondary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            title: '瞬态整形'
            hint: '独立调整起音与延音 —— 没有阈值,轻敲和重击得到同样的处理'

            headerRight: HusSwitch {
                checked: fx.transientEnabled
                onToggled: fx.transientEnabled = checked
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ParamRow {
                    label: '起音'; unit: ''; from: -1; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'transientAttack'
                    value: fx.transientAttack
                    onEdited: (v) => fx.transientAttack = v
                }
                ParamRow {
                    label: '延音'; unit: ''; from: -1; to: 1; step: 0.01; decimals: 2
                    owner: fx; prop: 'transientSustain'
                    value: fx.transientSustain
                    onEdited: (v) => fx.transientSustain = v
                }
                ParamRow {
                    label: '起音时间'; unit: 'ms'; from: 1; to: 500; step: 1; decimals: 0
                    owner: fx; prop: 'transientAttackMs'
                    value: fx.transientAttackMs
                    onEdited: (v) => fx.transientAttackMs = v
                }
                ParamRow {
                    label: '延音时间'; unit: 'ms'; from: 1; to: 5000; step: 10; decimals: 0
                    owner: fx; prop: 'transientReleaseMs'
                    value: fx.transientReleaseMs
                    onEdited: (v) => fx.transientReleaseMs = v
                }
                Text {
                    text: '压缩器的增益是 电平减阈值 的函数,所以同一记鼓点大 10 dB 就会' +
                          '被区别对待。这里的增益来自同一信号两条包络之差 —— 线性域里' +
                          '就是一个比值,缩放不变。实测同一段素材相差 20 dB,增益轨迹' +
                          '完全一致(0.000 dB)。'
                    color: HusTheme.Primary.colorTextSecondary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
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
                    owner: fx; prop: 'crossfeedCutoff'
                    value: fx.crossfeedCutoff
                    onEdited: (v) => fx.crossfeedCutoff = v
                }
                ParamRow {
                    label: '串扰量'; unit: 'dB'; from: -18; to: 0; step: 0.5; decimals: 1
                    owner: fx; prop: 'crossfeedLevel'
                    value: fx.crossfeedLevel
                    onEdited: (v) => fx.crossfeedLevel = v
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
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
                    owner: fx; prop: 'lowCross'
                    value: fx.lowCross
                    onEdited: (v) => fx.lowCross = v
                }
                ParamRow {
                    label: '中/高分频'; unit: 'Hz'; from: 1000; to: 12000; step: 50; decimals: 0
                    owner: fx; prop: 'highCross'
                    value: fx.highCross
                    onEdited: (v) => fx.highCross = v
                }
            }
        }
    }
    }
}
