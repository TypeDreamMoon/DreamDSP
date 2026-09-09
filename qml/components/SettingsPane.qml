import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

Item {
    id: root

    // Which hotkey action is waiting for a key press, '' when idle.
    property string capturingId: ''
    property var hotkeys: AppController.hotkeyActions()

    Connections {
        target: AppController
        function onHotkeysChanged() { root.hotkeys = AppController.hotkeyActions(); }
    }

    // Grabs the next key press while a binding is being recorded.
    Item {
        id: keyCatcher
        focus: root.capturingId !== ''
        Keys.onPressed: (event) => {
            event.accepted = true;
            if (event.key === Qt.Key_Escape) {
                root.capturingId = '';
                return;
            }
            // Ignore the modifier keys themselves -- they arrive first while the
            // user is still assembling the chord.
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta) {
                return;
            }
            if (AppController.setHotkey(root.capturingId, event.modifiers, event.nativeScanCode))
                root.capturingId = '';
        }
    }

    component SettingRow: RowLayout {
        property string label: ''
        property string hint: ''
        default property alias trailing: trailingArea.data

        Layout.fillWidth: true
        spacing: 12

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true

            HusText {
                text: parent.parent.label
                font.pixelSize: 13
            }
            HusText {
                text: parent.parent.hint
                font.pixelSize: 11
                opacity: 0.5
                visible: text !== ''
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        Item {
            id: trailingArea
            Layout.preferredWidth: childrenRect.width
            Layout.preferredHeight: childrenRect.height
            Layout.alignment: Qt.AlignVCenter
        }
    }

    component SectionTitle: HusText {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.pixelSize: 12
        font.weight: Font.DemiBold
        opacity: 0.75
    }

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
            spacing: 14

            // ------------------------------------------------------- general
            SectionTitle { text: '常规' }

            SettingRow {
                label: '开机自启'
                hint: 'HKCU\\...\\CurrentVersion\\Run,用户级,无需管理员权限'
                HusSwitch {
                    checked: AppController.autostart
                    onToggled: AppController.autostart = checked
                }
            }

            SettingRow {
                label: '关闭窗口时最小化到托盘'
                hint: AppController.trayActive ? '托盘图标已就绪' : '托盘不可用,关闭将直接退出'
                HusSwitch {
                    checked: AppController.closeToTray
                    enabled: AppController.trayActive
                    onToggled: AppController.closeToTray = checked
                }
            }

            SettingRow {
                label: '实时电平表'
                hint: '读取所选设备的输出峰值。独占模式(WASAPI Exclusive)下始终为 0'
                HusSwitch {
                    checked: AppController.metering
                    onToggled: AppController.metering = checked
                }
            }

            SettingRow {
                label: '逐设备独立配置'
                hint: '每个输出设备记住自己的曲线,生成的配置里每段带 Device: 限定。'
                      + '关闭时所有设备共用一条曲线'
                HusSwitch {
                    checked: AppController.perDevice
                    onToggled: AppController.perDevice = checked
                }
            }

            HusDivider { Layout.fillWidth: true }

            // ----------------------------------------------------- system fx
            //
            // Two separate steps, deliberately shown as two: registering the
            // component is machine-wide and needs administrator rights,
            // attaching it to a device does not. Conflating them into one
            // button would mean an elevation prompt every time you switch
            // device.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                SectionTitle { text: '系统音效 (APO)'; Layout.fillWidth: false }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: stateLabel.implicitWidth + 16
                    implicitHeight: 20
                    radius: 10
                    color: AppController.apoAttached
                           ? HusTheme.Primary.colorSuccessBg
                           : (AppController.apoInstalled ? HusTheme.Primary.colorWarningBg
                                                         : HusTheme.Primary.colorFillTertiary)
                    HusText {
                        id: stateLabel
                        anchors.centerIn: parent
                        font.pixelSize: 11
                        // "接入" has to mean the device actually processes.
                        // Holding the post-mix slot on an endpoint whose
                        // enhancements are switched off is a registration
                        // nobody reads, and calling that attached is the lie
                        // that hid a dead install for nineteen days.
                        text: !AppController.apoInstalled ? '未安装'
                              : AppController.apoSysFxDisabled ? '设备已关闭声音增强'
                              : (AppController.apoAttached ? '已接入当前设备' : '已安装 · 未接入')
                        color: AppController.apoAttached
                               ? HusTheme.Primary.colorSuccess
                               : (AppController.apoInstalled ? HusTheme.Primary.colorWarning
                                                             : HusTheme.Primary.colorTextTertiary)
                    }
                }

                Item { Layout.fillWidth: true }
            }

            HusText {
                Layout.fillWidth: true
                text: 'DreamDSP 作为音频处理对象接入 Windows 混音链,对系统里所有声音生效 —— '
                      + '不再依赖 Equalizer APO 执行。'
                font.pixelSize: 11
                opacity: 0.5
                wrapMode: Text.WordWrap
            }

            SettingRow {
                label: '安装音频组件'
                hint: AppController.apoInstalled
                      ? (AppController.apoUpToDate ? '已注册,且是当前版本'
                                                   : '已注册,但文件比程序旧,建议重新安装')
                      : '需要一次管理员授权,之后切换设备都不再需要'
                RowLayout {
                    spacing: 8
                    HusButton {
                        text: AppController.apoInstalled ? '重新安装' : '安装'
                        type: AppController.apoInstalled ? HusButton.Type_Default
                                                         : HusButton.Type_Primary
                        enabled: !AppController.apoBusy
                        onClicked: AppController.installApo()
                    }
                    HusButton {
                        text: '卸载'
                        visible: AppController.apoInstalled
                        enabled: !AppController.apoBusy
                        onClicked: AppController.uninstallApo()
                    }
                }
            }

            SettingRow {
                label: '接入当前输出设备'
                hint: {
                    if (!AppController.apoInstalled)
                        return '先安装音频组件';
                    // Checked before the occupant, because with enhancements
                    // switched off the slot is ours and inert at the same time
                    // -- and warning about displacing ourselves is nonsense.
                    if (AppController.apoSysFxDisabled)
                        return '⚠ 这台设备关闭了「所有声音增强」,打开开关会一并开启它';
                    if (AppController.apoAttached)
                        return '本设备的后混槽位由 DreamDSP 占用';
                    if (AppController.apoSlotOwner !== '')
                        return '⚠ 该槽位当前是「' + AppController.apoSlotOwner
                               + '」,接入会顶替它 —— 移除时会自动还原';
                    return '槽位空闲,可直接接入';
                }
                HusSwitch {
                    checked: AppController.apoAttached
                    enabled: AppController.apoInstalled && !AppController.apoBusy
                    onToggled: AppController.setApoAttached(checked)
                }
            }

            // The one-way channel's blind spot: without a report from the other
            // end, "I moved a slider and nothing happened" has half a dozen
            // indistinguishable causes. This is what tells them apart.
            SettingRow {
                label: '运行状态'
                hint: AppController.apoStatusText
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 10
                    implicitHeight: 10
                    radius: 5
                    color: AppController.apoLive
                           ? (AppController.apoInSync ? HusTheme.Primary.colorSuccess
                                                      : HusTheme.Primary.colorWarning)
                           : HusTheme.Primary.colorTextQuaternary
                }
            }

            SettingRow {
                label: '重启音频服务'
                hint: AppController.apoRestartPending
                      ? '⚠ 有改动尚未生效 —— 音效链只在服务重启时重建'
                      : '全机声音会中断一瞬,正在播放的程序通常能自行恢复'
                HusButton {
                    text: '重启'
                    type: AppController.apoRestartPending ? HusButton.Type_Primary
                                                          : HusButton.Type_Default
                    enabled: !AppController.apoBusy
                    onClicked: AppController.restartAudio()
                }
            }

            HusDivider { Layout.fillWidth: true }

            // ------------------------------------------------------- hotkeys
            SectionTitle { text: '全局热键' }

            HusText {
                Layout.fillWidth: true
                text: '点击右侧按钮后按下组合键,Esc 取消。需要带 Ctrl / Alt / Shift / Win,'
                      + '或使用 F1–F24 与媒体键。'
                font.pixelSize: 11
                opacity: 0.5
                wrapMode: Text.WordWrap
            }

            HusText {
                Layout.fillWidth: true
                text: '⚠ 当有管理员权限的程序处于前台时(如任务管理器),Windows 的 UIPI '
                      + '会拦截发给普通程序的热键 —— 这是系统限制,Peace 也一样。'
                font.pixelSize: 11
                opacity: 0.55
                color: HusTheme.Primary.colorWarning
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: root.hotkeys

                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10

                    HusText {
                        text: modelData.label
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }

                    HusButton {
                        text: root.capturingId === modelData.id
                              ? '按下组合键…'
                              : (modelData.keys !== '' ? modelData.keys : '未设置')
                        type: root.capturingId === modelData.id ? HusButton.Type_Primary
                                                                : HusButton.Type_Default
                        Layout.preferredWidth: 190
                        onClicked: root.capturingId = (root.capturingId === modelData.id)
                                                      ? '' : modelData.id
                    }

                    HusIconButton {
                        iconSource: HusIcon.DeleteOutlined
                        enabled: modelData.keys !== ''
                        onClicked: AppController.clearHotkey(modelData.id)
                    }
                }
            }

            HusDivider { Layout.fillWidth: true }

            // ---------------------------------------------------- locations
            SectionTitle { text: '位置' }

            SettingRow {
                label: 'Equalizer APO'
                hint: AppController.apoFound
                      ? AppController.apoConfigPath
                        + (AppController.configWritable ? '  ·  可写' : '  ·  只读,需要提权')
                      : '未检测到安装'
                HusButton {
                    text: '打开目录'
                    enabled: AppController.apoFound
                    onClicked: AppController.openConfigFolder()
                }
            }

            SettingRow {
                label: '预设目录'
                hint: '你保存的预设(.peace 格式,Peace 也能打开)'
                HusButton {
                    text: '打开目录'
                    onClicked: AppController.openPresetFolder()
                }
            }

            HusDivider { Layout.fillWidth: true }

            // ------------------------------------------------------- updates
            SectionTitle { text: '关于与更新' }

            SettingRow {
                label: '当前版本'
                hint: AppController.updates.status !== ''
                      ? AppController.updates.status
                      : AppController.version
                RowLayout {
                    spacing: 8
                    HusButton {
                        text: '检查更新'
                        enabled: !AppController.updates.busy
                        onClicked: AppController.updates.checkNow()
                    }
                    HusButton {
                        text: '发布页'
                        onClicked: AppController.updates.openReleasePage()
                    }
                }
            }

            SettingRow {
                label: '自动检查更新'
                hint: '启动后与每天各查一次 GitHub 的发布信息。只读取一小段 JSON,不上传任何本机信息。'
                HusSwitch {
                    checked: AppController.updates.automatic
                    onToggled: AppController.updates.automatic = checked
                }
            }

            // Only appears when there is something to do.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: AppController.updates.updateAvailable

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    HusText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        text: '新版本 ' + AppController.updates.latestVersion
                              + (AppController.updates.downloaded
                                 ? ' 已下载并通过 SHA-256 校验'
                                 : (AppController.updates.busy
                                    ? ' 下载中 ' + AppController.updates.downloadPercent + '%'
                                    : ' 可用'))
                    }

                    HusButton {
                        text: '下载'
                        type: HusButton.Type_Primary
                        visible: !AppController.updates.downloaded
                        enabled: !AppController.updates.busy
                        onClicked: AppController.updates.download()
                    }

                    HusButton {
                        text: '安装并重启'
                        type: HusButton.Type_Primary
                        visible: AppController.updates.downloaded
                        onClicked: AppController.updates.installNow()
                    }
                }

                HusText {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    opacity: 0.5
                    // Said plainly rather than left as a surprise: installing
                    // replaces a component that lives inside audiodg.exe, so
                    // the audio service restarts and the machine goes quiet.
                    text: '安装会关闭 DreamDSP 并重启音频服务,全机声音会中断一瞬 —— '
                          + '不会在你没点之前自动进行。'
                }
            }

            HusText {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.bottomMargin: 8
                text: 'DreamDSP ' + AppController.version
                font.pixelSize: 11
                opacity: 0.35
            }
        }
    }
}
