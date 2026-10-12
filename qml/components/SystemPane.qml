pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

// 系统音频: whether DreamDSP is actually in the path between the applications
// and the speakers, and if not, which of the conditions failed.
//
// Every failure here is silent -- a driver update that moves the device to a
// composite effect chain, an uninstaller that switches off "all
// enhancements", an attach that has not been followed by an audio service
// restart -- and each one leaves the rest of the interface reading "attached".
// So this page does not trust any cached state: it asks AppController for a
// fresh report every two seconds while it is on screen.
Item {
    id: root

    property var report: ({})
    readonly property var verdict: report.verdict || ({})
    readonly property var device: report.device || ({})
    readonly property var runtime: report.runtime || ({})

    function refresh() { root.report = AppController.systemReport(); }

    function levelColor(level) {
        switch (level) {
        case 'ok':    return HusTheme.Primary.colorSuccess;
        case 'warn':  return HusTheme.Primary.colorWarning;
        case 'error': return HusTheme.Primary.colorError;
        case 'info':  return HusTheme.Primary.colorPrimary;
        default:      return HusTheme.Primary.colorTextTertiary;
        }
    }
    function levelIcon(level) {
        switch (level) {
        case 'ok':    return HusIcon.CheckCircleFilled;
        case 'warn':  return HusIcon.ExclamationCircleFilled;
        case 'error': return HusIcon.CloseCircleFilled;
        case 'off':   return HusIcon.MinusCircleFilled;
        default:      return HusIcon.InfoCircleFilled;
        }
    }

    Timer {
        interval: 2000
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: root.refresh()
    }
    Connections {
        target: AppController
        function onApoStateChanged() { if (root.visible) root.refresh(); }
        function onMasterEnabledChanged() { if (root.visible) root.refresh(); }
    }

    // A labelled chip for one processing object in the chain.
    component ApoChip: Rectangle {
        id: chip
        required property var modelData
        property bool dim: false
        implicitWidth: chipRow.implicitWidth + 16
        implicitHeight: 24
        radius: 6
        opacity: chip.dim ? 0.45 : 1.0
        color: chip.modelData.ours ? Qt.alpha(HusTheme.Primary.colorPrimary, 0.18)
                               : (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05))
        border.width: chip.modelData.ours ? 1 : 0
        border.color: HusTheme.Primary.colorPrimary

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6
            HusText {
                text: chip.modelData.ours ? 'DreamDSP' : chip.modelData.name
                font.pixelSize: 11
                font.weight: chip.modelData.ours ? Font.DemiBold : Font.Normal
                color: chip.modelData.ours ? HusTheme.Primary.colorPrimary : HusTheme.Primary.colorTextBase
            }
            HusText {
                visible: !chip.modelData.ours && chip.modelData.dll !== ''
                text: chip.modelData.dll
                font.pixelSize: 10
                opacity: 0.45
            }
        }

        MouseArea { id: chipHover; anchors.fill: parent; hoverEnabled: true }
        HusToolTip {
            parent: chip
            visible: chipHover.containsMouse
            text: chip.modelData.clsid + (chip.modelData.dllPath ? '\n' + chip.modelData.dllPath : '')
        }
    }

    // A node in the signal path that is not a processing object.
    component PathNode: Rectangle {
        property alias text: nodeText.text
        implicitWidth: nodeText.implicitWidth + 18
        implicitHeight: 24
        radius: 12
        color: 'transparent'
        border.width: 1
        border.color: HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.18)
        HusText { id: nodeText; anchors.centerIn: parent; font.pixelSize: 11; opacity: 0.75 }
    }

    component DownArrow: HusIconText {
        iconSource: HusIcon.ArrowDownOutlined
        iconSize: 11
        opacity: 0.35
        Layout.leftMargin: 14
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: grid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        HusScrollBar.vertical: HusScrollBar { }

        GridLayout {
            id: grid
            width: Math.min(parent.width - 12, 1600)
            x: Math.max(0, (parent.width - width) / 2)
            columns: width >= 900 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12

            // ------------------------------------------------------ verdict
            Rectangle {
                Layout.fillWidth: true
                Layout.columnSpan: grid.columns
                implicitHeight: verdictRow.implicitHeight + 32
                radius: 10
                color: Qt.alpha(root.levelColor(root.verdict.level), 0.10)
                border.width: 1
                border.color: Qt.alpha(root.levelColor(root.verdict.level), 0.40)

                RowLayout {
                    id: verdictRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    HusIconText {
                        iconSource: root.levelIcon(root.verdict.level)
                        iconSize: 28
                        colorIcon: root.levelColor(root.verdict.level)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        HusText {
                            text: root.verdict.title || '正在检查…'
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }
                        HusText {
                            Layout.fillWidth: true
                            text: (root.verdict.detail || '')
                            font.pixelSize: 12
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                        }
                        HusText {
                            text: '设备:' + (root.device.name || '')
                                  + (root.device.composite ? ' · 复合效果链' : ' · 传统槽位')
                            font.pixelSize: 11
                            opacity: 0.45
                        }
                    }

                    HusButton {
                        visible: (root.verdict.action || '') !== ''
                        text: root.verdict.actionText || ''
                        type: HusButton.Type_Primary
                        enabled: !AppController.apoBusy
                        onClicked: { AppController.runSystemAction(root.verdict.action); root.refresh(); }
                    }
                    HusIconButton {
                        iconSource: HusIcon.ReloadOutlined
                        onClicked: root.refresh()
                        HusToolTip { parent: parent; visible: parent.hovered; text: '重新检查' }
                    }
                }
            }

            // ------------------------------------------------------- checks
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '自检'
                hint: '信号到达扬声器之前要满足的每一个条件,按顺序'

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2

                    Repeater {
                        model: root.report.checks || []
                        delegate: RowLayout {
                            id: chk
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10

                            HusIconText {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 3
                                iconSource: root.levelIcon(chk.modelData.state)
                                iconSize: 14
                                colorIcon: root.levelColor(chk.modelData.state)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                HusText { text: chk.modelData.label; font.pixelSize: 12 }
                                HusText {
                                    Layout.fillWidth: true
                                    text: chk.modelData.detail
                                    font.pixelSize: 11
                                    opacity: 0.55
                                    wrapMode: Text.WordWrap
                                }
                            }
                            HusButton {
                                visible: chk.modelData.action !== ''
                                text: chk.modelData.actionText
                                enabled: !AppController.apoBusy
                                onClicked: { AppController.runSystemAction(chk.modelData.action); root.refresh(); }
                            }
                        }
                    }
                }
            }

            // -------------------------------------------------- the chain
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '当前设备的效果链'
                hint: root.device.composite
                      ? '复合效果链 —— Windows 只读这里,旧式槽位被忽略'
                      : '传统槽位'

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    // The signal path, top to bottom. Laid out left to right it
                    // wrapped in a half-width card and left arrows dangling at
                    // the ends of lines, which is the one thing a diagram of a
                    // sequence must not do.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        PathNode { text: '各个应用' }

                        Repeater {
                            model: (root.device.stages || []).filter(s => !s.ignored)
                            delegate: ColumnLayout {
                                id: stageNode
                                required property var modelData
                                readonly property bool postMix: modelData.key === 'EFX' || modelData.key === 'GFX'
                                Layout.fillWidth: true
                                spacing: 4

                                DownArrow {}
                                PathNode { visible: stageNode.postMix; text: '混音' }
                                DownArrow { visible: stageNode.postMix }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    HusText {
                                        Layout.preferredWidth: 40
                                        text: stageNode.modelData.key
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        opacity: 0.7
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Repeater {
                                            model: stageNode.modelData.entries
                                            delegate: ApoChip {}
                                        }
                                        HusText {
                                            visible: stageNode.modelData.entries.length === 0
                                            text: '(空)'
                                            font.pixelSize: 11
                                            opacity: 0.35
                                        }
                                    }
                                    HusText {
                                        text: stageNode.modelData.label
                                        font.pixelSize: 10
                                        opacity: 0.45
                                    }
                                }
                            }
                        }

                        DownArrow {}
                        PathNode { text: root.device.name || '设备' }
                    }

                    // Values that exist but are not read. Shown because "our
                    // CLSID is right there in the registry" is exactly what made
                    // the composite case look healthy.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: (root.device.stages || []).some(s => s.ignored)

                        HusText {
                            text: '被忽略的旧式槽位'
                            font.pixelSize: 11
                            opacity: 0.55
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: (root.device.stages || []).filter(s => s.ignored)
                                delegate: Row {
                                    id: ign
                                    required property var modelData
                                    spacing: 4
                                    HusText {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ign.modelData.key
                                        font.pixelSize: 10
                                        font.strikeout: true
                                        opacity: 0.45
                                    }
                                    Repeater {
                                        model: ign.modelData.entries
                                        delegate: ApoChip { dim: true }
                                    }
                                }
                            }
                        }
                    }

                    HusText {
                        Layout.fillWidth: true
                        visible: !!root.device.sysFxDisabled
                        text: '⚠ 这台设备勾了「禁用所有声音增强」,上面整条链都不会被加载。'
                        color: HusTheme.Primary.colorError
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ----------------------------------------------- all endpoints
            SectionCard {
                Layout.fillWidth: true
                Layout.columnSpan: grid.columns
                Layout.alignment: Qt.AlignTop
                title: '所有输出设备'
                hint: '在哪些设备上接入 DreamDSP —— 改动在重启音频服务后生效'

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Repeater {
                        model: root.report.endpoints || []
                        delegate: Rectangle {
                            id: ep
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 40
                            color: ep.modelData.current
                                   ? (HusTheme.isDark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.03))
                                   : 'transparent'
                            radius: 6

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10

                                HusIconText {
                                    iconSource: HusIcon.SoundOutlined
                                    iconSize: 13
                                    opacity: 0.5
                                }
                                HusText {
                                    Layout.fillWidth: true
                                    text: ep.modelData.name
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                StatusPill {
                                    visible: ep.modelData.isDefault
                                    showDot: false
                                    text: '默认'
                                }
                                HusText {
                                    Layout.preferredWidth: 170
                                    text: ep.modelData.composite
                                          ? '复合效果链' + (ep.modelData.others > 0
                                                            ? ' · 驱动音效 ' + ep.modelData.others : '')
                                          : (ep.modelData.others > 0
                                             ? '传统槽位 · 其他音效 ' + ep.modelData.others : '传统槽位')
                                    font.pixelSize: 11
                                    opacity: 0.5
                                }
                                HusText {
                                    Layout.preferredWidth: 110
                                    horizontalAlignment: Text.AlignRight
                                    font.pixelSize: 11
                                    text: ep.modelData.sysFxDisabled ? '声音增强已关'
                                          : ep.modelData.attached ? '已接入'
                                          : ep.modelData.inert ? '在被忽略的槽位'
                                          : '未接入'
                                    color: ep.modelData.sysFxDisabled || ep.modelData.inert
                                           ? HusTheme.Primary.colorError
                                           : ep.modelData.attached ? HusTheme.Primary.colorSuccess
                                           : HusTheme.Primary.colorTextTertiary
                                }
                                HusSwitch {
                                    checked: ep.modelData.attached && !ep.modelData.sysFxDisabled
                                    enabled: AppController.apoInstalled && !AppController.apoBusy
                                    onToggled: {
                                        AppController.setEndpointAttached(ep.modelData.id, checked);
                                        root.refresh();
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------------ runtime
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '运行反馈'
                hint: '处理对象自己写回来的状态'

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 4

                        component Key: HusText { font.pixelSize: 11; opacity: 0.5 }
                        component Val: HusText { font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Key { text: '音频服务启动于' }
                        Val { text: root.runtime.audiodgStart || '' }
                        Key { text: '最近一次反馈' }
                        Val {
                            text: (root.runtime.statusTime || '')
                                  + (root.runtime.fresh ? ' · 正在运行' : '')
                        }
                        Key { text: '参数代数' }
                        Val {
                            text: '组件 ' + (root.runtime.loadedGeneration ?? '-')
                                  + ' / 程序 ' + (root.runtime.publishedGeneration ?? '-')
                        }
                        Key { text: '载入 / 拒绝' }
                        Val { text: (root.runtime.loads ?? 0) + ' / ' + (root.runtime.rejects ?? 0) }
                        Key { text: '正在运行' }
                        Val { text: root.runtime.running || ''; wrapMode: Text.WordWrap; elide: Text.ElideNone }
                    }

                    Repeater {
                        model: root.runtime.instances || []
                        delegate: HusText {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            font.pixelSize: 11
                            opacity: modelData.streaming ? 0.85 : 0.45
                            wrapMode: Text.WordWrap
                            text: '实例 ' + (index + 1) + ':' + modelData.rate / 1000 + ' kHz · '
                                  + modelData.channels + ' 声道 · ' + modelData.frames + ' 帧'
                                  + (modelData.streaming ? ' · 运行中 ' + Math.round(modelData.seconds) + ' s'
                                                         : ' · 空闲')
                                  + (modelData.passthrough ? ' · 直通' : '')
                        }
                    }
                }
            }

            // ------------------------------------------------- management
            SectionCard {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: '音频组件'
                hint: AppController.apoInstalled
                      ? (AppController.apoUpToDate ? '已安装 · 当前版本' : '已安装 · 比程序旧')
                      : '未安装'

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        spacing: 8
                        HusButton {
                            text: AppController.apoInstalled ? '重新安装' : '安装'
                            type: AppController.apoInstalled && AppController.apoUpToDate
                                  ? HusButton.Type_Default : HusButton.Type_Primary
                            enabled: !AppController.apoBusy
                            onClicked: AppController.installApo()
                        }
                        HusButton {
                            text: '卸载'
                            visible: AppController.apoInstalled
                            enabled: !AppController.apoBusy
                            onClicked: AppController.uninstallApo()
                        }
                        HusButton {
                            text: '重启音频服务'
                            type: AppController.apoRestartPending ? HusButton.Type_Primary
                                                                  : HusButton.Type_Default
                            enabled: !AppController.apoBusy
                            onClicked: AppController.restartAudio()
                        }
                    }
                    HusText {
                        Layout.fillWidth: true
                        text: '安装和重启音频服务需要一次管理员授权;重启时全机声音会中断一两秒。'
                              + '接入或移除设备不需要授权,但要重启音频服务后才生效。'
                        font.pixelSize: 11
                        opacity: 0.5
                        wrapMode: Text.WordWrap
                    }

                    // The fallback, for machines that still have Equalizer APO.
                    RowLayout {
                        visible: AppController.apoFound
                        Layout.fillWidth: true
                        spacing: 8
                        HusText {
                            Layout.fillWidth: true
                            text: 'Equalizer APO ' + AppController.apoVersion
                                  + (AppController.engaged ? ' · 正在由它处理均衡' : ' · 已安装,未使用')
                            font.pixelSize: 12
                        }
                        HusButton {
                            text: AppController.engaged ? '交回 DreamDSP' : '交给 Equalizer APO'
                            enabled: AppController.configWritable
                            onClicked: AppController.engaged = !AppController.engaged
                        }
                    }
                }
            }

            // ---------------------------------------------------------- log
            SectionCard {
                Layout.fillWidth: true
                Layout.columnSpan: grid.columns
                Layout.alignment: Qt.AlignTop
                title: '组件日志'
                hint: 'C:\\ProgramData\\DreamDSP\\apo.log 的最后几行'

                Rectangle {
                    anchors.fill: parent
                    implicitHeight: logText.implicitHeight + 16
                    radius: 6
                    color: HusTheme.isDark ? Qt.rgba(0, 0, 0, 0.25) : Qt.rgba(0, 0, 0, 0.04)

                    HusText {
                        id: logText
                        anchors.fill: parent
                        anchors.margins: 8
                        text: (root.report.log || []).join('\n') || '(空)'
                        font.family: 'Consolas'
                        font.pixelSize: 11
                        opacity: 0.7
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
