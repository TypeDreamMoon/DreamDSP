pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import HuskarUI.Basic
import DreamDSP

import "components"

HusWindow {
    id: win

    width: 1020
    height: 700
    minimumWidth: 860
    minimumHeight: 580
    title: 'DreamDSP'
    followThemeSwitch: true

    // 0 = graphic sliders, 1 = per-band parameter table, 2 = settings.
    property int eqViewMode: 0

    captionBar.visible: true
    captionBar.height: 34
    captionBar.showThemeButton: true
    captionBar.themeCallback: () => {
        HusTheme.darkMode = HusTheme.isDark ? HusTheme.Light : HusTheme.Dark;
        win.setWindowMode(HusTheme.isDark);
    }

    // Launched by the run-at-login entry (--tray): come up hidden.
    visible: !AppController.startHidden

    Component.onCompleted: {
        HusTheme.darkMode = HusTheme.Dark;
        HusTheme.installThemePrimaryColorBase('#3d7eff');
        win.setWindowMode(true);
        // winId() forces platform-window creation, so this works even when the
        // window starts hidden.
        AppController.attachWindow(win);
    }

    onClosing: (close) => {
        // Flush any pending debounced write, so the last slider move is never
        // lost whichever way we are going.
        AppController.flushNow();
        if (AppController.closeToTray && AppController.trayActive) {
            close.accepted = false;
            win.hide();
        }
    }

    function restoreFromTray() {
        win.show();
        win.raise();
        win.requestActivate();
    }

    // Used by --traymenu to capture the popup, which is a separate window and
    // therefore invisible to a grab of the main one.
    function showTrayMenuAt(gx, gy) {
        trayMenu.popupAt(gx, gy);
    }

    TrayMenu {
        id: trayMenu
        onShowMainWindow: win.restoreFromTray()
    }

    Connections {
        target: AppController
        function onTrayActivated() {
            if (win.visible)
                win.hide();
            else
                win.restoreFromTray();
        }
        function onTrayMenuRequested(gx, gy) {
            trayMenu.popupAt(gx, gy);
        }
        function onToggleWindowRequested() {
            if (win.visible)
                win.hide();
            else
                win.restoreFromTray();
        }
    }

    // HusWindow leaves its own background to the application. Without this the
    // window is fully transparent -- which reads as "white with unreadable
    // pale text" anywhere the compositor is not blurring a backdrop.
    Rectangle {
        anchors.fill: parent
        z: -1
        color: HusTheme.Primary.colorBgBase
        Behavior on color { ColorAnimation { duration: HusTheme.Primary.durationMid } }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: win.captionBar.height + 10
        spacing: 12

        // ---------------------------------------------------------------- header
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            HusSwitch {
                checked: AppController.eqEnabled
                onToggled: AppController.eqEnabled = checked
            }

            HusText {
                text: '均衡器'
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            StatusPill {
                text: AppController.engaged ? '已接管' : '未接管'
                dotColor: AppController.engaged
                          ? HusTheme.Primary.colorSuccess
                          : HusTheme.Primary.colorTextQuaternary
                showDot: true
            }

            Item { Layout.fillWidth: true }

            HusText {
                text: '输出设备'
                font.pixelSize: 12
                opacity: 0.6
            }

            HusSelect {
                id: deviceSelect
                Layout.preferredWidth: 260
                model: AppController.deviceNames.map((n, i) => ({ label: n, value: i }))
                currentIndex: AppController.currentDevice
                onActivated: (index) => AppController.currentDevice = index
            }

            MeterBar {
                Layout.alignment: Qt.AlignVCenter
            }

            HusIconButton {
                iconSource: HusIcon.ReloadOutlined
                text: ''
                onClicked: AppController.refreshDevices()

                HusToolTip {
                    parent: parent
                    visible: parent.hovered
                    text: '重新枚举音频设备'
                }
            }

            HusButton {
                text: AppController.engaged ? '停止接管' : '接管 config.txt'
                type: AppController.engaged ? HusButton.Type_Default : HusButton.Type_Primary
                enabled: AppController.apoFound && AppController.configWritable
                onClicked: AppController.engaged = !AppController.engaged
            }
        }

        // ---------------------------------------------------------- preset row
        PresetBar {
            Layout.fillWidth: true
        }

        // ----------------------------------------------------------- curve card
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

        // ----------------------------------------------------------- bands card
        SectionCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: '均衡'
            hint: win.eqViewMode === 0 ? '拖动滑块调整增益 · ±15 dB'
                : win.eqViewMode === 1 ? '逐段编辑频率 / 增益 / Q / 滤波器类型'
                : win.eqViewMode === 2 ? '耳机校正曲线库'
                                       : ''

            headerRight: HusSegmented {
                options: [{ label: '图形' }, { label: '参数' },
                          { label: 'AutoEQ' }, { label: '设置' }]
                // Do not bind currentIndex: HusSegmented writes it internally on
                // click, which would break the binding anyway.
                Component.onCompleted: currentIndex = win.eqViewMode
                onCurrentIndexChanged: win.eqViewMode = currentIndex
            }

            StackLayout {
                anchors.fill: parent
                currentIndex: win.eqViewMode

            RowLayout {
                spacing: 14

                // preamp lives with the bands: it is just another gain stage
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

                    HusSlider {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillHeight: true
                        Layout.preferredWidth: 26   // see BandStrip.qml -- zero width otherwise
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

                AutoEqPane { }

                SettingsPane { }
            }
        }

        // ---------------------------------------------------------------- footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            StatusPill {
                text: AppController.apoFound
                      ? 'Equalizer APO ' + AppController.apoVersion
                      : '未检测到 Equalizer APO'
                dotColor: AppController.apoFound
                          ? HusTheme.Primary.colorSuccess
                          : HusTheme.Primary.colorError
            }

            HusText {
                Layout.fillWidth: true
                font.pixelSize: 11
                elide: Text.ElideMiddle

                // Errors win, then the most recent action, then just the path.
                readonly property bool isError: AppController.lastError !== ''
                readonly property bool isInfo: !isError && AppController.lastMessage !== ''

                text: isError ? AppController.lastError
                     : isInfo ? AppController.lastMessage
                              : AppController.apoConfigPath
                opacity: (isError || isInfo) ? 0.95 : 0.45
                color: isError ? HusTheme.Primary.colorError
                     : isInfo ? HusTheme.Primary.colorSuccess
                              : HusTheme.Primary.colorTextBase
            }

            HusButton {
                text: '打开配置目录'
                onClicked: AppController.openConfigFolder()
            }

            HusButton {
                text: '全部归零'
                onClicked: AppController.resetAll()
            }
        }
    }
}
