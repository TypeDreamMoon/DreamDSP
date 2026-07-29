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
                        text: !AppController.apoInstalled ? '未安装'
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

            HusText {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.bottomMargin: 8
                text: 'DreamDSP · 均衡由 Equalizer APO 执行,本程序只生成配置'
                font.pixelSize: 11
                opacity: 0.35
            }
        }
    }
}
