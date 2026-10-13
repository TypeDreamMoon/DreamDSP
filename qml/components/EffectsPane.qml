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
        property bool logarithmic: false
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
        property var defaultOverride: undefined
        readonly property var defaultValue: (prow.owner && prow.prop !== '')
                                            ? (prow.defaultOverride !== undefined ? prow.defaultOverride
                                               : prow.owner.defaultOf(prow.prop))
                                            : undefined
        readonly property bool atDefault:
            prow.defaultValue === undefined
            || Math.abs(prow.value - prow.defaultValue) <= Math.max(prow.step, 1e-6) * 0.5

        signal edited(real v)
        signal autoToggled(bool on)

        function sliderValue(v) {
            return logarithmic ? Math.log(Math.max(from, v) / from) / Math.log(to / from) : v;
        }
        function parameterValue(v) {
            if (!logarithmic) return v;
            return Math.max(from, Math.min(to, Math.round(from * Math.pow(to / from, v) / step) * step));
        }

        Layout.fillWidth: true
        spacing: 10

        HusText {
            Layout.preferredWidth: 76
            text: prow.label
            font.pixelSize: 12
        }

        Fader {
            Layout.fillWidth: true
            min: prow.logarithmic ? 0 : prow.from
            max: prow.logarithmic ? 1 : prow.to
            stepSize: prow.logarithmic ? 0.001 : prow.step
            value: prow.sliderValue(prow.value)
            enabled: !prow.autoMode
            opacity: enabled ? 1 : 0.4
            onFirstMoved: prow.edited(prow.parameterValue(currentValue))
            onFirstReleased: prow.edited(prow.parameterValue(currentValue))
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
        contentHeight: page.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        HusScrollBar.vertical: HusScrollBar { }

        // Grouped by what an effect is for, each group a masonry flow. Ten
        // effects in one undifferentiated flow made the page a list to read
        // rather than a rack to scan.
        ColumnLayout {
            id: page
            width: parent.width - 12
            spacing: Ui.gap

            Rectangle {
                Layout.fillWidth: true
                // Only while there is something to say: the device is not attached,
                // or a file has just been rendered. Attached and idle, the header's
                // status already says it, and a permanent banner is wallpaper.
                visible: !AppController.apoAttached || render.status !== ''
                implicitHeight: notice.implicitHeight + 24
                radius: 10
                color: render.failed ? Qt.rgba(1, 0.35, 0.25, 0.10)
                                     : Qt.rgba(1, 0.7, 0.1, 0.09)
                border.width: 1
                border.color: render.failed ? Qt.rgba(1, 0.35, 0.25, 0.45)
                                            : Qt.rgba(1, 0.7, 0.1, 0.35)

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
                                    + '还没接入这台设备。到「系统音频」页看原因,通常点一下就能接入。'
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        HusText {
                            Layout.fillWidth: true
                            visible: render.status !== ''
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: render.failed ? HusTheme.Primary.colorError
                                                 : HusTheme.Primary.colorTextBase
                            text: render.status
                        }

                        HusButton {
                            text: '打开输出目录'
                            visible: render.lastOutput !== ''
                            onClicked: render.revealOutput()
                        }
                    }
                }
            }

            GroupHeader { title: '噪声与共振控制' }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    objectName: "gateCard"
                    collapsible: true
                    active: fx.gateEnabled
                    title: '噪声门'
                    hint: '压低停顿底噪 · 平滑开关 · 声道联动'
                    headerRight: HusSwitch {
                        checked: fx.gateEnabled
                        onToggled: fx.gateEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '开门阈值'; unit: 'dB'; from: -80; to: 0; step: 0.5
                            owner: fx; prop: 'gateThreshold'; value: fx.gateThreshold
                            onEdited: (v) => fx.gateThreshold = v
                        }
                        ParamRow {
                            label: '滞回'; unit: 'dB'; from: 0; to: 18; step: 0.5
                            owner: fx; prop: 'gateHysteresis'; value: fx.gateHysteresis
                            onEdited: (v) => fx.gateHysteresis = v
                        }
                        ParamRow {
                            label: '关门衰减'; unit: 'dB'; from: 0; to: 80; step: 1; decimals: 0
                            owner: fx; prop: 'gateRange'; value: fx.gateRange
                            onEdited: (v) => fx.gateRange = v
                        }
                        ParamRow {
                            label: '开门时间'; unit: 'ms'; from: 0.1; to: 50; step: 0.1
                            owner: fx; prop: 'gateAttack'; value: fx.gateAttack
                            onEdited: (v) => fx.gateAttack = v
                        }
                        ParamRow {
                            label: '保持'; unit: 'ms'; from: 0; to: 500; step: 1; decimals: 0
                            owner: fx; prop: 'gateHold'; value: fx.gateHold
                            onEdited: (v) => fx.gateHold = v
                        }
                        ParamRow {
                            label: '关门时间'; unit: 'ms'; from: 10; to: 1000; step: 1; decimals: 0
                            owner: fx; prop: 'gateRelease'; value: fx.gateRelease
                            onEdited: (v) => fx.gateRelease = v
                        }
                        Note { text: '把阈值放在底噪之上、有效声音之下。关门阈值比开门阈值低一个「滞回」值；保持和关门时间能保留句尾与余音。用于播放音频中的停顿底噪，声音播放期间的底噪仍会保留。' }
                    }
                }
                SectionCard {
                    objectName: "dynamicEqCard"
                    collapsible: true
                    active: fx.dynamicEqEnabled
                    title: '动态均衡'
                    hint: '三段动态衰减 · 压低轰鸣、共振与刺耳'
                    headerRight: HusSwitch {
                        checked: fx.dynamicEqEnabled
                        onToggled: fx.dynamicEqEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            HusSelect {
                                id: dynamicEqBandSelect
                                Layout.fillWidth: true
                                clearEnabled: false
                                model: [ { label: '频段 1', value: 0 }, { label: '频段 2', value: 1 }, { label: '频段 3', value: 2 } ]
                                function syncSelection() { currentIndex = fx.dynamicEqBand }
                                Component.onCompleted: Qt.callLater(syncSelection)
                                onModelChanged: Qt.callLater(syncSelection)
                                onActivated: (i) => fx.dynamicEqBand = i
                                Connections {
                                    target: fx
                                    function onChanged() { dynamicEqBandSelect.syncSelection() }
                                }
                            }
                            HusText { text: '启用此段'; font.pixelSize: 12 }
                            HusSwitch {
                                checked: fx.dynamicEqBandEnabled
                                onToggled: fx.dynamicEqBandEnabled = checked
                            }
                        }
                        ParamRow {
                            label: '中心频率'; unit: 'Hz'; from: 40; to: 16000; step: 1; decimals: 0; logarithmic: true
                            owner: fx; prop: 'dynamicEqFrequency'; value: fx.dynamicEqFrequency
                            defaultOverride: fx.dynamicEqDefaultFrequency
                            onEdited: (v) => fx.dynamicEqFrequency = v
                        }
                        ParamRow {
                            label: '带宽 Q'; unit: ''; from: 0.3; to: 8; step: 0.1
                            owner: fx; prop: 'dynamicEqQ'; value: fx.dynamicEqQ
                            onEdited: (v) => fx.dynamicEqQ = v
                        }
                        ParamRow {
                            label: '阈值'; unit: 'dB'; from: -60; to: 0; step: 0.5
                            owner: fx; prop: 'dynamicEqThreshold'; value: fx.dynamicEqThreshold
                            onEdited: (v) => fx.dynamicEqThreshold = v
                        }
                        ParamRow {
                            label: '比例'; unit: ':1'; from: 1; to: 12; step: 0.1
                            owner: fx; prop: 'dynamicEqRatio'; value: fx.dynamicEqRatio
                            onEdited: (v) => fx.dynamicEqRatio = v
                        }
                        ParamRow {
                            label: '最大衰减'; unit: 'dB'; from: 0; to: 18; step: 0.5
                            owner: fx; prop: 'dynamicEqRange'; value: fx.dynamicEqRange
                            onEdited: (v) => fx.dynamicEqRange = v
                        }
                        ParamRow {
                            label: '起音'; unit: 'ms'; from: 0.1; to: 100; step: 0.1
                            owner: fx; prop: 'dynamicEqAttack'; value: fx.dynamicEqAttack
                            onEdited: (v) => fx.dynamicEqAttack = v
                        }
                        ParamRow {
                            label: '释放'; unit: 'ms'; from: 10; to: 1000; step: 1; decimals: 0
                            owner: fx; prop: 'dynamicEqRelease'; value: fx.dynamicEqRelease
                            onEdited: (v) => fx.dynamicEqRelease = v
                        }
                        Note { text: '三段各自保存设置并同时工作，选择频段只切换编辑对象。Q 越大，处理范围越窄；只有该段过强时才衰减，最大衰减设为 0 可试听原声。低采样率下频率会限制在可处理范围内。' }
                    }
                }
            }

            GroupHeader { title: '磁带与人声' }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    objectName: "tapeCard"
                    collapsible: true
                    active: fx.tapeEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '磁带饱和'
                    hint: '磁带风格 · 柔化高频与瞬态 · 4 倍过采样'
                    headerRight: HusSwitch {
                        checked: fx.tapeEnabled
                        onToggled: fx.tapeEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '驱动'; unit: 'dB'; from: 0; to: 24; step: 0.1; decimals: 1
                            owner: fx; prop: 'tapeDrive'
                            value: fx.tapeDrive
                            onEdited: (v) => fx.tapeDrive = v
                        }
                        ParamRow {
                            label: '高频柔化'; unit: 'Hz'; from: 2000; to: 20000; step: 100; decimals: 0
                            owner: fx; prop: 'tapeTone'
                            value: fx.tapeTone
                            onEdited: (v) => fx.tapeTone = v
                        }
                        ParamRow {
                            label: '低频隆起'; unit: 'dB'; from: 0; to: 6; step: 0.1; decimals: 1
                            owner: fx; prop: 'tapeBump'
                            value: fx.tapeBump
                            onEdited: (v) => fx.tapeBump = v
                        }
                        ParamRow {
                            label: '磁化记忆'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'tapeMemory'
                            value: fx.tapeMemory
                            onEdited: (v) => fx.tapeMemory = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'tapeMix'
                            value: fx.tapeMix
                            onEdited: (v) => fx.tapeMix = v
                        }
                        ParamRow {
                            label: '输出'; unit: 'dB'; from: -24; to: 6; step: 0.1; decimals: 1
                            owner: fx; prop: 'tapeOutput'
                            value: fx.tapeOutput
                            onEdited: (v) => fx.tapeOutput = v
                        }
                        Note { text: '算法近似磁带的染色与记忆，不对应某台硬件。混合为 0 时仍经过对齐滤波，关闭模块才是完全直通。' }
                    }
                }
                SectionCard {
                    objectName: "deEsserCard"
                    collapsible: true
                    active: fx.deEsserEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '去齿音'
                    hint: '仅压低过强高频 · 声道联动，保持声像'
                    headerRight: HusSwitch {
                        checked: fx.deEsserEnabled
                        onToggled: fx.deEsserEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '齿音分界'; unit: 'Hz'; from: 2000; to: 12000; step: 100; decimals: 0
                            owner: fx; prop: 'deEsserFrequency'
                            value: fx.deEsserFrequency
                            onEdited: (v) => fx.deEsserFrequency = v
                        }
                        ParamRow {
                            label: '阈值'; unit: 'dB'; from: -60; to: 0; step: 0.5; decimals: 1
                            owner: fx; prop: 'deEsserThreshold'
                            value: fx.deEsserThreshold
                            onEdited: (v) => fx.deEsserThreshold = v
                        }
                        ParamRow {
                            label: '比例'; unit: ':1'; from: 1; to: 12; step: 0.1; decimals: 1
                            owner: fx; prop: 'deEsserRatio'
                            value: fx.deEsserRatio
                            onEdited: (v) => fx.deEsserRatio = v
                        }
                        ParamRow {
                            label: '最大衰减'; unit: 'dB'; from: 0; to: 18; step: 0.5; decimals: 1
                            owner: fx; prop: 'deEsserRange'
                            value: fx.deEsserRange
                            onEdited: (v) => fx.deEsserRange = v
                        }
                        ParamRow {
                            label: '起音'; unit: 'ms'; from: 0.1; to: 20; step: 0.1; decimals: 1
                            owner: fx; prop: 'deEsserAttack'
                            value: fx.deEsserAttack
                            onEdited: (v) => fx.deEsserAttack = v
                        }
                        ParamRow {
                            label: '释放'; unit: 'ms'; from: 10; to: 500; step: 1; decimals: 0
                            owner: fx; prop: 'deEsserRelease'
                            value: fx.deEsserRelease
                            onEdited: (v) => fx.deEsserRelease = v
                        }
                        Note { text: '先把齿音分界设在 5–8 kHz，再降低阈值至齿音柔和。最大衰减限制削减量；这是高频动态处理，不是语音分离。' }
                    }
                }
            }

            GroupHeader { title: '空间与动态建模' }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    collapsible: true
                    active: fx.binauralEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '双耳虚拟音箱'
                    hint: '头部固定 · 音箱环绕与升降 · 双声道耳机'
                    headerRight: HusSwitch {
                        checked: fx.binauralEnabled
                        onToggled: fx.binauralEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            HusSelect {
                                Layout.fillWidth: true
                                model: [
                                    { label: '球头水平 · 旧预设', value: 0 },
                                    { label: 'KEMAR 实测 · 三维', value: 1 }
                                ]
                                currentIndex: fx.binauralModel
                                onActivated: (i) => fx.binauralModel = i
                            }
                            HusButton {
                                text: '影院空间'
                                onClicked: AppController.applyCinemaSpatialPreset()
                            }
                        }
                        BinauralScene {
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            spread: fx.binauralSpread
                            azimuth: fx.binauralRotation
                            elevation: fx.binauralModel === 1 ? fx.binauralElevation : 0
                            accent: HusTheme.Primary.colorPrimary
                            foreground: HusTheme.Primary.colorTextBase
                            guide: HusTheme.isDark ? '#50545c' : '#b5bcc8'
                        }
                        ParamRow {
                            label: '音箱夹角'; unit: '°'; from: 0; to: 180; step: 1; decimals: 0
                            owner: fx; prop: 'binauralSpread'
                            value: fx.binauralSpread
                            onEdited: (v) => fx.binauralSpread = v
                        }
                        ParamRow {
                            label: '环绕方位'; unit: '°'; from: -180; to: 180; step: 1; decimals: 0
                            owner: fx; prop: 'binauralRotation'
                            value: fx.binauralRotation
                            onEdited: (v) => fx.binauralRotation = v
                        }
                        ParamRow {
                            label: '音箱高度'; unit: '°'; from: -40; to: 90; step: 1; decimals: 0
                            owner: fx; prop: 'binauralElevation'
                            value: fx.binauralModel === 1 ? fx.binauralElevation : 0
                            onEdited: (v) => fx.binauralElevation = v
                        }
                        ParamRow {
                            label: '空间混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'binauralMix'
                            value: fx.binauralMix
                            onEdited: (v) => fx.binauralMix = v
                        }
                        ParamRow {
                            label: '输出'; unit: 'dB'; from: -24; to: 6; step: 0.1; decimals: 2
                            owner: fx; prop: 'binauralOutput'
                            value: fx.binauralOutput
                            onEdited: (v) => fx.binauralOutput = v
                        }
                        Note { text: fx.binauralModel === 1
                            ? '0° 朝向屏幕，正角度顺时针转动。高度为相对耳朵的仰角；KEMAR 耳廓线索的上下定位感因人而异。请避免叠加系统空间音效。'
                            : '旧预设保留原听感。调节音箱高度将启用三维实测 HRTF。' }
                        Note { text: '影院空间：前方虚拟音箱配合轻度 FDN 房间混响。适合普通立体声；已做双耳渲染的素材建议直接播放。' }
                    }
                }
                SectionCard {
                    collapsible: true
                    active: fx.neuralEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '神经饱和'
                    hint: 'Synthetic Memory Drive · 支持 8–192 kHz 采样率'
                    headerRight: HusSwitch {
                        checked: fx.neuralEnabled
                        onToggled: fx.neuralEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '驱动'; unit: 'dB'; from: -12; to: 18; step: 0.1; decimals: 2
                            owner: fx; prop: 'neuralDrive'
                            value: fx.neuralDrive
                            onEdited: (v) => fx.neuralDrive = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'neuralMix'
                            value: fx.neuralMix
                            onEdited: (v) => fx.neuralMix = v
                        }
                        ParamRow {
                            label: '输出'; unit: 'dB'; from: -24; to: 6; step: 0.1; decimals: 2
                            owner: fx; prop: 'neuralOutput'
                            value: fx.neuralOutput
                            onEdited: (v) => fx.neuralOutput = v
                        }
                        Note { text: '合成动态失真模型，非实测硬件捕获。支持 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz 等采样率；非 48 kHz 使用内部转换并对齐干湿声，常用采样率约增加 1.3–1.5 ms 延迟。模型的高频带宽仍受 48 kHz 训练采样率限制。' }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: AppController.metering
                            HusText { text: '设备输出峰值'; font.pixelSize: 12 }
                            MeterBar { Layout.fillWidth: true; Layout.preferredHeight: 6 }
                        }
                    }
                }
                SectionCard {
                    collapsible: true
                    active: fx.chorusEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '合唱 Chorus'
                    hint: '连续调制 · 参数平滑 · 反馈限幅'
                    headerRight: HusSwitch {
                        checked: fx.chorusEnabled
                        onToggled: fx.chorusEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '速度'; unit: 'Hz'; from: 0.02; to: 10; step: 0.01; decimals: 2
                            owner: fx; prop: 'chorusRate'
                            value: fx.chorusRate
                            onEdited: (v) => fx.chorusRate = v
                        }
                        ParamRow {
                            label: '深度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'chorusDepth'
                            value: fx.chorusDepth
                            onEdited: (v) => fx.chorusDepth = v
                        }
                        ParamRow {
                            label: '基础延迟'; unit: 'ms'; from: 0.1; to: 30; step: 0.1; decimals: 2
                            owner: fx; prop: 'chorusDelay'
                            value: fx.chorusDelay
                            onEdited: (v) => fx.chorusDelay = v
                        }
                        ParamRow {
                            label: '反馈'; unit: ''; from: -0.85; to: 0.85; step: 0.01; decimals: 2
                            owner: fx; prop: 'chorusFeedback'
                            value: fx.chorusFeedback
                            onEdited: (v) => fx.chorusFeedback = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'chorusMix'
                            value: fx.chorusMix
                            onEdited: (v) => fx.chorusMix = v
                        }
                        ParamRow {
                            label: '立体声相位'; unit: '周'; from: 0; to: 0.5; step: 0.01; decimals: 2
                            owner: fx; prop: 'chorusStereo'
                            value: fx.chorusStereo
                            onEdited: (v) => fx.chorusStereo = v
                        }

                    }
                }
                SectionCard {
                    collapsible: true
                    active: fx.flangerEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '镶边 Flanger'
                    hint: '连续调制 · 参数平滑 · 反馈限幅'
                    headerRight: HusSwitch {
                        checked: fx.flangerEnabled
                        onToggled: fx.flangerEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '速度'; unit: 'Hz'; from: 0.02; to: 10; step: 0.01; decimals: 2
                            owner: fx; prop: 'flangerRate'
                            value: fx.flangerRate
                            onEdited: (v) => fx.flangerRate = v
                        }
                        ParamRow {
                            label: '深度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'flangerDepth'
                            value: fx.flangerDepth
                            onEdited: (v) => fx.flangerDepth = v
                        }
                        ParamRow {
                            label: '基础延迟'; unit: 'ms'; from: 0.1; to: 5; step: 0.1; decimals: 2
                            owner: fx; prop: 'flangerDelay'
                            value: fx.flangerDelay
                            onEdited: (v) => fx.flangerDelay = v
                        }
                        ParamRow {
                            label: '反馈'; unit: ''; from: -0.85; to: 0.85; step: 0.01; decimals: 2
                            owner: fx; prop: 'flangerFeedback'
                            value: fx.flangerFeedback
                            onEdited: (v) => fx.flangerFeedback = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'flangerMix'
                            value: fx.flangerMix
                            onEdited: (v) => fx.flangerMix = v
                        }
                        ParamRow {
                            label: '立体声相位'; unit: '周'; from: 0; to: 0.5; step: 0.01; decimals: 2
                            owner: fx; prop: 'flangerStereo'
                            value: fx.flangerStereo
                            onEdited: (v) => fx.flangerStereo = v
                        }

                    }
                }
                SectionCard {
                    collapsible: true
                    active: fx.phaserEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '移相 Phaser'
                    hint: '连续调制 · 参数平滑 · 反馈限幅'
                    headerRight: HusSwitch {
                        checked: fx.phaserEnabled
                        onToggled: fx.phaserEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '速度'; unit: 'Hz'; from: 0.02; to: 10; step: 0.01; decimals: 2
                            owner: fx; prop: 'phaserRate'
                            value: fx.phaserRate
                            onEdited: (v) => fx.phaserRate = v
                        }
                        ParamRow {
                            label: '深度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'phaserDepth'
                            value: fx.phaserDepth
                            onEdited: (v) => fx.phaserDepth = v
                        }
                        ParamRow {
                            label: '反馈'; unit: ''; from: -0.85; to: 0.85; step: 0.01; decimals: 2
                            owner: fx; prop: 'phaserFeedback'
                            value: fx.phaserFeedback
                            onEdited: (v) => fx.phaserFeedback = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'phaserMix'
                            value: fx.phaserMix
                            onEdited: (v) => fx.phaserMix = v
                        }
                        ParamRow {
                            label: '立体声相位'; unit: '周'; from: 0; to: 0.5; step: 0.01; decimals: 2
                            owner: fx; prop: 'phaserStereo'
                            value: fx.phaserStereo
                            onEdited: (v) => fx.phaserStereo = v
                        }

                    }
                }
                SectionCard {
                    collapsible: true
                    active: fx.fdnEnabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: 'FDN 空间混响'
                    hint: '八路反馈延迟网络 · 独立晚混响，可与短早反射卷积组合'
                    headerRight: HusSwitch {
                        checked: fx.fdnEnabled
                        onToggled: fx.fdnEnabled = checked
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ParamRow {
                            label: '衰减时间'; unit: 's'; from: 0.1; to: 12; step: 0.1; decimals: 2
                            owner: fx; prop: 'fdnDecay'
                            value: fx.fdnDecay
                            onEdited: (v) => fx.fdnDecay = v
                        }
                        ParamRow {
                            label: '高频阻尼'; unit: 'Hz'; from: 200; to: 18000; step: 100; decimals: 0
                            owner: fx; prop: 'fdnDamping'
                            value: fx.fdnDamping
                            onEdited: (v) => fx.fdnDamping = v
                        }
                        ParamRow {
                            label: '预延迟'; unit: 'ms'; from: 0; to: 200; step: 1; decimals: 0
                            owner: fx; prop: 'fdnPreDelay'
                            value: fx.fdnPreDelay
                            onEdited: (v) => fx.fdnPreDelay = v
                        }
                        ParamRow {
                            label: '混合'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'fdnMix'
                            value: fx.fdnMix
                            onEdited: (v) => fx.fdnMix = v
                        }
                        ParamRow {
                            label: '宽度'; unit: ''; from: 0; to: 1; step: 0.01; decimals: 2
                            owner: fx; prop: 'fdnWidth'
                            value: fx.fdnWidth
                            onEdited: (v) => fx.fdnWidth = v
                        }
                        ParamRow {
                            label: '输出'; unit: 'dB'; from: -24; to: 6; step: 0.1; decimals: 2
                            owner: fx; prop: 'fdnOutput'
                            value: fx.fdnOutput
                            onEdited: (v) => fx.fdnOutput = v
                        }

                    }
                }
            }

            GroupHeader {
                title: '动态'
                detail: '控制音量的起伏 · 已开启 ' + [comp.enabled, fx.multibandEnabled, fx.transientEnabled].filter(v => v).length + ' / 3'
            }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    collapsible: true
                    active: comp.enabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: '压缩器'
                    hint: '对数域前馈 · 立体声联动 · 软拐点'

                    headerRight: HusSwitch {
                        checked: comp.enabled
                        onToggled: comp.enabled = checked
                    }

                    // Curve beside the rows when the card is wide, above them when it
                    // is not. The card used to demand two grid columns to fit both
                    // side by side, which is what left a card-sized hole next to it.
                    GridLayout {
                        id: compGrid
                        anchors.fill: parent
                        columns: width >= 640 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 12

                        TransferCurveItem {
                            Layout.preferredWidth: compGrid.columns === 2
                                                   ? Math.max(240, Math.min(360, compGrid.width * 0.32)) : -1
                            Layout.fillWidth: compGrid.columns === 1
                            Layout.preferredHeight: compGrid.columns === 1 ? 190 : -1
                            Layout.fillHeight: compGrid.columns === 2
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
                    collapsible: true
                    active: fx.multibandEnabled
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

                SectionCard {
                    collapsible: true
                    active: fx.transientEnabled
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
                        Note {
                            text: '压缩器的增益是 电平减阈值 的函数,所以同一记鼓点大 10 dB 就会' +
                                  '被区别对待。这里的增益来自同一信号两条包络之差 —— 线性域里' +
                                  '就是一个比值,缩放不变。实测同一段素材相差 20 dB,增益轨迹' +
                                  '完全一致(0.000 dB)。'
                        }
                    }
                }
            }

            GroupHeader {
                title: '低音'
                detail: '小喇叭也能有下潜 · 已开启 ' + [fx.dynBassEnabled, fx.bassEnabled].filter(v => v).length + ' / 2'
            }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    collapsible: true
                    active: fx.dynBassEnabled
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

                        Note {
                            text: '和「虚拟低音」不是一回事:那个合成放不出来的基频,这个抬高本来就有的。'
                        }
                    }
                }

                SectionCard {
                    collapsible: true
                    active: fx.bassEnabled
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
            }

            GroupHeader {
                title: '音色'
                detail: '谐波与染色 · 已开启 ' + [fx.tubeEnabled, fx.exciterEnabled].filter(v => v).length + ' / 2'
            }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    collapsible: true
                    active: fx.tubeEnabled
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
                    collapsible: true
                    active: fx.exciterEnabled
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
                        Note {
                            text: '阈值以下波形整形完全不工作 —— 这是 Aphex 1979 年那份专利里' +
                                  '真正的想法:只让瞬态被削,持续音保持干净。调到 -120 dB 就是' +
                                  '一直工作的静态激励。'
                        }
                    }
                }
            }

            GroupHeader {
                title: '空间'
                detail: '声场与混响 · 已开启 ' + [rev.enabled, fx.widthEnabled, fx.crossfeedEnabled].filter(v => v).length + ' / 3'
            }
            Masonry {
                Layout.fillWidth: true
                SectionCard {
                    collapsible: true
                    active: rev.enabled
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
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
                    collapsible: true
                    active: fx.widthEnabled
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
                        Note {
                            text: '侧信号增益只能缩放已经存在的侧信号:素材本来就是单声道时' +
                                  '它做不了任何事。相位展宽(Zotter–Frank)对中间信号做相移,' +
                                  '这是唯一能把单声道展开的办法 —— 代价是 2 ms 延时,' +
                                  '以及单声道叠加最多 2.4 dB 的凹陷。'
                        }
                    }
                }

                SectionCard {
                    collapsible: true
                    active: fx.crossfeedEnabled
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
            }
        }
    }

    // Drop a wav anywhere on the page to hear the current settings applied to
    // it -- the one way to audition them before the device is attached.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0)
                render.renderUrl(drop.urls[0]);
        }
    }
}
