import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

Item {
    id: root

    signal goToPage(int page)

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
            // Moved to its own page. Installing, attaching and the running
            // state used to live here, as four rows that each reported part of
            // the story -- and every one of them read as healthy on the day a
            // driver update moved the device to a composite effect chain and
            // DreamDSP silently stopped being loaded. The 系统音频 page checks
            // all of it, in order, and says which condition failed.
            SettingRow {
                label: '音频组件与诊断'
                hint: !AppController.apoInstalled ? '未安装 —— 在「系统音频」页安装'
                      : AppController.apoAttached ? '已接入当前设备'
                      : '未接入当前设备 —— 在「系统音频」页查看原因'
                HusButton {
                    text: '打开系统音频'
                    type: AppController.apoAttached ? HusButton.Type_Default
                                                    : HusButton.Type_Primary
                    onClicked: root.goToPage(6)
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
                label: '双耳 HRTF 数据'
                hint: 'KEMAR · Bill Gardner / Keith Martin\nMIT Media Laboratory, 1994'
                HusButton {
                    text: '来源与使用许可'
                    onClicked: Qt.openUrlExternally('https://sound.media.mit.edu/resources/KEMAR.html')
                }
            }

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
                              + (AppController.updates.deltaStaged
                                 ? ' 的增量更新已下载并通过 SHA-256 校验'
                                 : (AppController.updates.deltaAvailable
                                    ? ' 可用 · 增量更新只需 '
                                      + AppController.updates.deltaMegabytes.toFixed(1) + ' MB'
                                      + '（不下载整个安装包）'
                                    : (AppController.updates.downloaded
                                       ? ' 已下载并通过 SHA-256 校验'
                                       : (AppController.updates.busy
                                          ? ' 下载中 '
                                            + AppController.updates.downloadPercent + '%'
                                          : ' 可用'))))
                    }

                    HusButton {
                        text: '下载'
                        type: HusButton.Type_Primary
                        visible: !AppController.updates.downloaded
                        enabled: !AppController.updates.busy
                        onClicked: AppController.updates.download()
                    }

                    HusButton {
                        // A file-level update cannot be applied while this copy
                        // has those files mapped, so it is staged and applied
                        // on the next start -- which main() then does by
                        // reopening the window.
                        text: AppController.updates.deltaStaged ? '退出并更新' : '安装并重启'
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
                    // A file-level update swaps the program's own files at
                    // start-up and does not touch the audio service.
                    text: AppController.updates.deltaStaged
                          ? '退出后自动重新打开，新版本就会生效。'
                            + '若音频组件也有更新，到「系统音频」页重装一次即可。'
                          : '安装会关闭 DreamDSP 并重启音频服务,全机声音会中断一瞬 —— '
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
